/*
 * baseline.c
 * MTR 결과를 기준선 파일로 저장하고 현재 측정값과 비교해 경로 품질 회귀
 * 리포트를 생성한다.
 */
#include "baseline.h"

#include "timeutil.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BASELINE_MAGIC "routeprobe-baseline-v1"
#define NO_REPLY_REGRESSION_THRESHOLD 5.0
#define AVG_REGRESSION_THRESHOLD_MS 20.0
#define BASELINE_HOP_HEADER_V1 "hop\tremote_ip\tsent\treceived\tloss_percent\tlast_ms\tavg_ms\tbest_ms\tworst_ms\tjitter_ms\tstatus"
#define BASELINE_HOP_HEADER_V2 "hop\tremote_ip\tsent\treceived\tno_reply_percent\tlast_ms\tavg_ms\tbest_ms\tworst_ms\tjitter_ms\tstatus"

typedef enum {
    MTR_DIAGNOSIS_NORMAL,
    MTR_DIAGNOSIS_CAUTION,
    MTR_DIAGNOSIS_POOR
} MtrDiagnosisLevel;

typedef struct {
    MtrDiagnosisLevel level;
    int score;
    int cycles;
    const MtrHopStats *destination;
    const MtrHopStats *baseline_destination;
    bool destination_missing;
    bool has_rtt_delta;
    double rtt_delta_ms;
    double rtt_delta_percent;
    bool has_jitter_delta;
    double jitter_delta_ms;
} MtrQualityDiagnosis;

/* TraceStatus 값을 기준선 파일에 안정적으로 저장할 문자열로 바꾼다. */
static const char *trace_status_name(TraceStatus status)
{
    switch (status) {
    case TRACE_STATUS_HOP:
        return "hop";
    case TRACE_STATUS_DESTINATION:
        return "destination";
    case TRACE_STATUS_TIMEOUT:
        return "timeout";
    case TRACE_STATUS_ERROR:
        return "error";
    case TRACE_STATUS_UNKNOWN:
        return "unknown";
    }
    return "unknown";
}

/* 기준선 파일의 status 문자열을 내부 enum으로 복원한다. */
static TraceStatus parse_trace_status(const char *text)
{
    if (strcmp(text, "hop") == 0) {
        return TRACE_STATUS_HOP;
    }
    if (strcmp(text, "destination") == 0) {
        return TRACE_STATUS_DESTINATION;
    }
    if (strcmp(text, "timeout") == 0) {
        return TRACE_STATUS_TIMEOUT;
    }
    if (strcmp(text, "error") == 0) {
        return TRACE_STATUS_ERROR;
    }
    return TRACE_STATUS_UNKNOWN;
}

/* Markdown 리포트에서 사용할 상태 값을 한국어 라벨로 변환한다. */
static const char *trace_status_korean(TraceStatus status)
{
    switch (status) {
    case TRACE_STATUS_HOP:
        return "중간 hop";
    case TRACE_STATUS_DESTINATION:
        return "목적지";
    case TRACE_STATUS_TIMEOUT:
        return "무응답";
    case TRACE_STATUS_ERROR:
        return "오류";
    case TRACE_STATUS_UNKNOWN:
        return "알 수 없음";
    }
    return "알 수 없음";
}

/* fgets 결과의 줄바꿈을 제거해 tab 파싱을 단순하게 만든다. */
static void strip_newline(char *line)
{
    line[strcspn(line, "\r\n")] = '\0';
}

/* hop index가 파일/배열 경계 안에 있는지 확인한다. */
static bool valid_hop(int hop)
{
    return hop >= 1 && hop <= ROUTEPROBE_BASELINE_MAX_HOPS;
}

/* 기준선 stream header와 hop 행을 순서대로 기록한다. */
int write_mtr_baseline_stream(FILE *fp, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count)
{
    char timestamp[64] = "";

    format_timestamp(timestamp, sizeof(timestamp));
    if (fprintf(fp, "%s\n", BASELINE_MAGIC) < 0 ||
        fprintf(fp, "target\t%s\n", config->target) < 0 ||
        fprintf(fp, "resolved_ip\t%s\n", resolved->ip) < 0 ||
        fprintf(fp, "captured_at\t%s\n", timestamp) < 0 ||
        fprintf(fp, "hop_count\t%d\n", count) < 0 ||
        fprintf(fp, "%s\n", BASELINE_HOP_HEADER_V2) < 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (stats[i].sent == 0) {
            continue;
        }
        if (fprintf(fp, "%d\t%s\t%d\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%s\n",
                    stats[i].hop,
                    stats[i].remote_ip,
                    stats[i].sent,
                    stats[i].received,
                    stats[i].no_reply_percent,
                    stats[i].last_ms,
                    stats[i].avg_ms,
                    stats[i].best_ms,
                    stats[i].worst_ms,
                    stats[i].jitter_ms,
                    trace_status_name(stats[i].status)) < 0) {
            return -1;
        }
    }
    return ferror(fp) ? -1 : 0;
}

/* key-value metadata 한 줄을 기준선 구조체에 반영한다. */
static int parse_metadata_line(char *line, MtrBaseline *baseline)
{
    char *key = strtok(line, "\t");
    char *value = strtok(NULL, "\t");

    if (key == NULL || value == NULL) {
        return 0;
    }
    if (strcmp(key, "target") == 0) {
        snprintf(baseline->target, sizeof(baseline->target), "%s", value);
    } else if (strcmp(key, "resolved_ip") == 0) {
        snprintf(baseline->resolved_ip, sizeof(baseline->resolved_ip), "%s", value);
    } else if (strcmp(key, "captured_at") == 0) {
        snprintf(baseline->captured_at, sizeof(baseline->captured_at), "%s", value);
    } else if (strcmp(key, "hop_count") == 0) {
        baseline->hop_count = atoi(value);
        if (baseline->hop_count < 0 || baseline->hop_count > ROUTEPROBE_BASELINE_MAX_HOPS) {
            return -1;
        }
    }
    return 0;
}

/* strtok가 버리는 빈 remote_ip 필드를 보존하며 tab 필드를 나눈다. */
static size_t split_tab_fields(char *line, char **fields, size_t field_count)
{
    size_t count = 0;
    char *cursor = line;

    while (count < field_count) {
        char *tab;

        fields[count++] = cursor;
        tab = strchr(cursor, '\t');
        if (tab == NULL) {
            break;
        }
        *tab = '\0';
        cursor = tab + 1;
    }
    return count;
}

/* tab으로 구분된 hop 통계 행 하나를 기준선 배열에 복원한다. */
static int parse_hop_line(char *line, MtrBaseline *baseline)
{
    char *fields[11];

    if (split_tab_fields(line, fields, 11) < 11) {
        return -1;
    }

    int hop = atoi(fields[0]);
    if (!valid_hop(hop)) {
        return -1;
    }

    MtrHopStats *stats = &baseline->hops[hop - 1];
    memset(stats, 0, sizeof(*stats));
    stats->hop = hop;
    snprintf(stats->remote_ip, sizeof(stats->remote_ip), "%s", fields[1]);
    stats->sent = atoi(fields[2]);
    stats->received = atoi(fields[3]);
    stats->no_reply_percent = strtod(fields[4], NULL);
    stats->last_ms = strtod(fields[5], NULL);
    stats->avg_ms = strtod(fields[6], NULL);
    stats->best_ms = strtod(fields[7], NULL);
    stats->worst_ms = strtod(fields[8], NULL);
    stats->jitter_ms = strtod(fields[9], NULL);
    stats->status = parse_trace_status(fields[10]);
    stats->has_rtt = stats->received > 0;
    stats->has_jitter = stats->received > 1;
    if (baseline->hop_count < hop) {
        baseline->hop_count = hop;
    }
    return 0;
}

/* MTR 기준선 파일을 읽어 비교 가능한 snapshot으로 복원한다. */
int read_mtr_baseline_stream(FILE *fp, MtrBaseline *baseline)
{
    char line[512];
    bool reading_hops = false;

    memset(baseline, 0, sizeof(*baseline));
    if (fgets(line, sizeof(line), fp) == NULL) {
        return -1;
    }
    strip_newline(line);
    if (strcmp(line, BASELINE_MAGIC) != 0) {
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        strip_newline(line);
        if (line[0] == '\0') {
            continue;
        }
        if (strcmp(line, BASELINE_HOP_HEADER_V1) == 0 || strcmp(line, BASELINE_HOP_HEADER_V2) == 0) {
            reading_hops = true;
            continue;
        }
        if (reading_hops) {
            if (parse_hop_line(line, baseline) != 0) {
                return -1;
            }
        } else if (parse_metadata_line(line, baseline) != 0) {
            return -1;
        }
    }
    return ferror(fp) ? -1 : 0;
}

/* current 배열에서 hop 번호에 해당하는 측정값을 찾는다. */
static const MtrHopStats *find_current_hop(const MtrHopStats *current, int current_count, int hop)
{
    for (int i = 0; i < current_count; i++) {
        if (current[i].hop == hop && current[i].sent > 0) {
            return &current[i];
        }
    }
    return NULL;
}

/* 최종 목적지 hop은 end-to-end 품질 판정의 기준점으로 사용한다. */
static const MtrHopStats *find_destination_hop(const MtrHopStats *stats, int count)
{
    for (int i = 0; i < count; i++) {
        if (stats[i].sent > 0 && stats[i].status == TRACE_STATUS_DESTINATION) {
            return &stats[i];
        }
    }
    return NULL;
}

/* 측정 신뢰도 표시에 사용할 cycle 수를 hop 통계에서 추정한다. */
static int observed_cycles(const MtrHopStats *stats, int count)
{
    int cycles = 0;

    for (int i = 0; i < count; i++) {
        if (stats[i].sent > cycles) {
            cycles = stats[i].sent;
        }
    }
    return cycles;
}

/* 점수 합계와 목적지 미도달 여부를 사용자용 진단 등급으로 변환한다. */
static MtrDiagnosisLevel diagnosis_level_from_score(int score, bool destination_missing)
{
    if (destination_missing || score >= 5) {
        return MTR_DIAGNOSIS_POOR;
    }
    if (score >= 2) {
        return MTR_DIAGNOSIS_CAUTION;
    }
    return MTR_DIAGNOSIS_NORMAL;
}

/* 한국어 리포트에 표시할 진단 등급 라벨을 반환한다. */
static const char *diagnosis_level_korean(MtrDiagnosisLevel level)
{
    switch (level) {
    case MTR_DIAGNOSIS_NORMAL:
        return "정상";
    case MTR_DIAGNOSIS_CAUTION:
        return "주의";
    case MTR_DIAGNOSIS_POOR:
        return "나쁨";
    }
    return "알 수 없음";
}

/* 측정 cycle 수를 기반으로 진단 신뢰도 라벨을 반환한다. */
static const char *confidence_label(int cycles)
{
    if (cycles < 3) {
        return "참고용";
    }
    if (cycles < 10) {
        return "낮음";
    }
    return "보통";
}

/* 목적지 도달성, RTT, jitter와 기준선 대비 변화를 점수화한다. */
static MtrQualityDiagnosis evaluate_mtr_quality(const MtrBaseline *baseline, const MtrHopStats *current, int current_count)
{
    MtrQualityDiagnosis diagnosis;
    int score = 0;

    memset(&diagnosis, 0, sizeof(diagnosis));
    diagnosis.cycles = observed_cycles(current, current_count);
    diagnosis.destination = find_destination_hop(current, current_count);
    diagnosis.destination_missing = diagnosis.destination == NULL;
    if (baseline != NULL) {
        diagnosis.baseline_destination = find_destination_hop(baseline->hops, baseline->hop_count);
    }

    if (diagnosis.destination_missing) {
        diagnosis.score = 4;
        diagnosis.level = MTR_DIAGNOSIS_POOR;
        return diagnosis;
    }

    if (diagnosis.destination->no_reply_percent > 5.0) {
        score += 4;
    } else if (diagnosis.destination->no_reply_percent > 1.0) {
        score += 2;
    }

    if (!diagnosis.destination->has_rtt) {
        score += 4;
    } else if (diagnosis.destination->avg_ms > 180.0) {
        score += 3;
    } else if (diagnosis.destination->avg_ms > 80.0) {
        score += 1;
    }

    if (diagnosis.destination->has_jitter) {
        if (diagnosis.destination->jitter_ms > 30.0) {
            score += 3;
        } else if (diagnosis.destination->jitter_ms > 10.0) {
            score += 1;
        }
    }

    if (diagnosis.baseline_destination != NULL && diagnosis.baseline_destination->has_rtt && diagnosis.destination->has_rtt) {
        diagnosis.has_rtt_delta = true;
        diagnosis.rtt_delta_ms = diagnosis.destination->avg_ms - diagnosis.baseline_destination->avg_ms;
        if (diagnosis.baseline_destination->avg_ms > 0.0) {
            diagnosis.rtt_delta_percent = (diagnosis.rtt_delta_ms / diagnosis.baseline_destination->avg_ms) * 100.0;
        }
        if (diagnosis.rtt_delta_ms >= 80.0 && diagnosis.rtt_delta_percent >= 100.0) {
            score += 3;
        } else if (diagnosis.rtt_delta_ms >= 30.0 && diagnosis.rtt_delta_percent >= 50.0) {
            score += 1;
        }
    }

    if (diagnosis.baseline_destination != NULL && diagnosis.baseline_destination->has_jitter && diagnosis.destination->has_jitter) {
        diagnosis.has_jitter_delta = true;
        diagnosis.jitter_delta_ms = diagnosis.destination->jitter_ms - diagnosis.baseline_destination->jitter_ms;
        if (diagnosis.jitter_delta_ms >= 40.0 && diagnosis.destination->jitter_ms > 30.0) {
            score += 3;
        } else if (diagnosis.jitter_delta_ms >= 20.0) {
            score += 1;
        }
    }

    diagnosis.score = score;
    diagnosis.level = diagnosis_level_from_score(score, diagnosis.destination_missing);
    return diagnosis;
}

/* 품질 진단 핵심 근거를 Markdown/list 형식으로 출력한다. */
static int print_quality_diagnosis_body(FILE *fp, const MtrQualityDiagnosis *diagnosis)
{
    fprintf(fp, "- 최종 진단: %s\n", diagnosis_level_korean(diagnosis->level));
    fprintf(fp, "- 신뢰도: %s", confidence_label(diagnosis->cycles));
    if (diagnosis->cycles < 10) {
        fprintf(fp, " (측정 cycle %d회)", diagnosis->cycles);
    }
    fprintf(fp, "\n");

    fprintf(fp, "\n핵심 근거:\n");
    if (diagnosis->destination_missing) {
        fprintf(fp, "- 목적지 hop에 도달하지 못했습니다.\n");
        fprintf(fp, "- 중간 hop 정보보다 목적지 미도달이 우선 진단 사유입니다.\n");
    } else {
        const MtrHopStats *destination = diagnosis->destination;

        fprintf(fp, "- 목적지 hop %d에 도달했습니다.\n", destination->hop);
        fprintf(fp, "- 목적지 ICMP 무응답률은 %.1f%%입니다.\n", destination->no_reply_percent);
        if (destination->has_rtt) {
            fprintf(fp, "- 목적지 평균 RTT는 %.1f ms입니다.\n", destination->avg_ms);
        }
        if (destination->has_jitter) {
            fprintf(fp, "- 목적지 jitter는 %.1f ms입니다.\n", destination->jitter_ms);
        } else {
            fprintf(fp, "- 목적지 jitter는 측정 횟수가 부족해 계산하지 않았습니다.\n");
        }
        if (diagnosis->has_rtt_delta) {
            fprintf(fp, "- 기준선 대비 목적지 평균 RTT 변화는 %.1f ms입니다.\n", diagnosis->rtt_delta_ms);
        }
        if (diagnosis->has_jitter_delta) {
            fprintf(fp, "- 기준선 대비 목적지 jitter 변화는 %.1f ms입니다.\n", diagnosis->jitter_delta_ms);
        }
        if (diagnosis->level == MTR_DIAGNOSIS_NORMAL) {
            fprintf(fp, "- 목적지 기준 RTT와 jitter가 안정적이므로 중간 hop 변경은 장애 근거로 보지 않습니다.\n");
        } else {
            fprintf(fp, "- 목적지 기준 품질 저하가 관찰됩니다. 특정 hop 원인 추정은 downstream 전파 여부를 함께 봐야 합니다.\n");
        }
    }
    return ferror(fp) ? -1 : 0;
}

/* 두 hop의 주소 변화, ICMP 무응답률 증가, 평균 RTT 증가를 참고 신호로 출력한다. */
static int print_hop_change(FILE *fp, const MtrHopStats *old_hop, const MtrHopStats *new_hop, int *changes)
{
    if (old_hop->remote_ip[0] != '\0' && new_hop->remote_ip[0] != '\0' && strcmp(old_hop->remote_ip, new_hop->remote_ip) != 0) {
        fprintf(fp, "- hop %d 주소 변경: %s -> %s\n", old_hop->hop, old_hop->remote_ip, new_hop->remote_ip);
        *changes += 1;
    }
    if (new_hop->no_reply_percent - old_hop->no_reply_percent >= NO_REPLY_REGRESSION_THRESHOLD) {
        fprintf(fp, "- hop %d ICMP 무응답률 증가: %.1f%% -> %.1f%%\n", old_hop->hop, old_hop->no_reply_percent, new_hop->no_reply_percent);
        *changes += 1;
    }
    if (old_hop->has_rtt && new_hop->has_rtt && new_hop->avg_ms - old_hop->avg_ms >= AVG_REGRESSION_THRESHOLD_MS) {
        fprintf(fp, "- hop %d 평균 RTT 증가: %.1f ms -> %.1f ms\n", old_hop->hop, old_hop->avg_ms, new_hop->avg_ms);
        *changes += 1;
    }
    return ferror(fp) ? -1 : 0;
}

/* 기준선과 현재 MTR의 변경 사항을 진단과 분리된 참고 신호로 출력한다. */
static int print_baseline_change_list(FILE *fp, const MtrBaseline *baseline, const MtrHopStats *current, int current_count, int *changes)
{
    for (int hop = 1; hop <= baseline->hop_count; hop++) {
        const MtrHopStats *old_hop = &baseline->hops[hop - 1];
        const MtrHopStats *new_hop;

        if (old_hop->sent == 0) {
            continue;
        }
        new_hop = find_current_hop(current, current_count, hop);
        if (new_hop == NULL) {
            fprintf(fp, "- hop %d 현재 결과에서 누락: %s\n", hop, old_hop->remote_ip[0] ? old_hop->remote_ip : "*");
            *changes += 1;
            continue;
        }
        if (print_hop_change(fp, old_hop, new_hop, changes) != 0) {
            return -1;
        }
    }

    for (int i = 0; i < current_count; i++) {
        if (current[i].sent == 0 || !valid_hop(current[i].hop)) {
            continue;
        }
        if (current[i].hop > baseline->hop_count || baseline->hops[current[i].hop - 1].sent == 0) {
            fprintf(fp, "- hop %d 새로 감지: %s\n", current[i].hop, current[i].remote_ip[0] ? current[i].remote_ip : "*");
            *changes += 1;
        }
    }

    if (*changes == 0) {
        fprintf(fp, "- 유의미한 회귀가 감지되지 않았습니다.\n");
    }
    return ferror(fp) ? -1 : 0;
}

/* 경로 품질 진단과 기준선 대비 참고 신호를 지정한 stream에 출력한다. */
int print_mtr_baseline_comparison(FILE *fp, const MtrBaseline *baseline, const MtrHopStats *current, int current_count)
{
    int changes = 0;
    MtrQualityDiagnosis diagnosis = evaluate_mtr_quality(baseline, current, current_count);

    fprintf(fp, "경로 품질 진단\n\n");
    if (print_quality_diagnosis_body(fp, &diagnosis) != 0) {
        return -1;
    }

    fprintf(fp, "\n기준선 비교\n\n");
    fprintf(fp, "- 기준선: %s (%s)\n", baseline->target[0] ? baseline->target : "알 수 없음", baseline->captured_at[0] ? baseline->captured_at : "시간 알 수 없음");
    fprintf(fp, "\n변경 사항:\n");
    if (print_baseline_change_list(fp, baseline, current, current_count, &changes) != 0) {
        return -1;
    }

    fprintf(fp, "\n해석:\n");
    if (changes > 0 && diagnosis.level == MTR_DIAGNOSIS_NORMAL) {
        fprintf(fp, "- 변경 사항은 관찰되지만 목적지 품질 저하로 전파되지 않았습니다.\n");
        fprintf(fp, "- 중간 hop의 ICMP 무응답은 라우터 정책, 필터링, rate limit 때문에 발생할 수 있으므로 장애 근거가 아닌 참고 신호로 해석합니다.\n");
    } else if (diagnosis.level == MTR_DIAGNOSIS_NORMAL) {
        fprintf(fp, "- 현재 경로 품질은 저장된 기준선과 큰 차이가 없습니다.\n");
    } else {
        fprintf(fp, "- 목적지 품질 저하가 함께 관찰됩니다. 중간 hop 변경은 downstream 전파 여부를 확인하는 단서로만 사용합니다.\n");
    }
    return ferror(fp) ? -1 : 0;
}

/* MTR 기준선 파일을 저장한다. */
int save_mtr_baseline(const char *path, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "failed to open baseline file %s: %s\n", path, strerror(errno));
        return ROUTEPROBE_ERR_IO;
    }
    if (write_mtr_baseline_stream(fp, config, resolved, stats, count) != 0) {
        fprintf(stderr, "failed to write baseline file %s\n", path);
        fclose(fp);
        return ROUTEPROBE_ERR_IO;
    }
    fclose(fp);
    printf("\n기준선 저장 완료: %s\n", path);
    return ROUTEPROBE_OK;
}

/* MTR 기준선 파일을 읽고 현재 측정값과 비교 결과를 출력한다. */
int compare_mtr_baseline(const char *path, const MtrHopStats *current, int current_count)
{
    MtrBaseline baseline;
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "failed to open baseline file %s: %s\n", path, strerror(errno));
        return ROUTEPROBE_ERR_IO;
    }
    if (read_mtr_baseline_stream(fp, &baseline) != 0) {
        fprintf(stderr, "failed to read baseline file %s\n", path);
        fclose(fp);
        return ROUTEPROBE_ERR_IO;
    }
    fclose(fp);
    putchar('\n');
    return print_mtr_baseline_comparison(stdout, &baseline, current, current_count) == 0 ? ROUTEPROBE_OK : ROUTEPROBE_ERR_IO;
}

/* Markdown 표에 MTR hop 통계 전체를 기록한다. */
static int write_report_mtr_table(FILE *fp, const MtrHopStats *stats, int count)
{
    fprintf(fp, "| Hop | 주소 | ICMP 무응답률(%%) | 전송 | 수신 | 최근 RTT | 평균 RTT | 최소 RTT | 최대 RTT | Jitter | 상태 |\n");
    fprintf(fp, "| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |\n");
    for (int i = 0; i < count; i++) {
        if (stats[i].sent == 0) {
            continue;
        }
        fprintf(fp, "| %d | %s | %.1f | %d | %d | ",
                stats[i].hop,
                stats[i].remote_ip[0] ? stats[i].remote_ip : "*",
                stats[i].no_reply_percent,
                stats[i].sent,
                stats[i].received);
        if (stats[i].has_rtt) {
            fprintf(fp, "%.1f | %.1f | %.1f | %.1f | ", stats[i].last_ms, stats[i].avg_ms, stats[i].best_ms, stats[i].worst_ms);
        } else {
            fprintf(fp, " |  |  |  | ");
        }
        if (stats[i].has_jitter) {
            fprintf(fp, "%.1f | ", stats[i].jitter_ms);
        } else {
            fprintf(fp, " | ");
        }
        fprintf(fp, "%s |\n", trace_status_korean(stats[i].status));
    }
    return ferror(fp) ? -1 : 0;
}

/* 현재 MTR 측정값과 선택적 기준선 비교를 Markdown 리포트로 저장한다. */
int write_mtr_report(const char *path, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count, const char *baseline_path)
{
    char timestamp[64] = "";
    MtrBaseline baseline;
    MtrBaseline *loaded_baseline = NULL;
    bool baseline_open_failed = false;
    bool baseline_parse_failed = false;
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        fprintf(stderr, "failed to open report file %s: %s\n", path, strerror(errno));
        return ROUTEPROBE_ERR_IO;
    }

    if (baseline_path != NULL) {
        FILE *baseline_fp = fopen(baseline_path, "r");
        if (baseline_fp == NULL) {
            baseline_open_failed = true;
        } else if (read_mtr_baseline_stream(baseline_fp, &baseline) != 0) {
            baseline_parse_failed = true;
            fclose(baseline_fp);
        } else {
            loaded_baseline = &baseline;
            fclose(baseline_fp);
        }
    }

    format_timestamp(timestamp, sizeof(timestamp));
    fprintf(fp, "# RouteProbe 장애 분석 리포트\n\n");
    fprintf(fp, "- 생성 시각: `%s`\n", timestamp);
    fprintf(fp, "- 대상: `%s`\n", config->target);
    fprintf(fp, "- 해석된 IP: `%s`\n", resolved->ip);
    fprintf(fp, "- 실행 모드: `mtr`\n");
    fprintf(fp, "- 측정 cycle: `%d`\n", config->count);
    fprintf(fp, "- 최대 hop: `%d`\n", config->max_hop);
    fprintf(fp, "- 응답 대기 시간(ms): `%d`\n", config->timeout_ms);

    fprintf(fp, "\n## 품질 진단\n\n");
    MtrQualityDiagnosis diagnosis = evaluate_mtr_quality(loaded_baseline, stats, count);
    if (print_quality_diagnosis_body(fp, &diagnosis) != 0) {
        fclose(fp);
        return ROUTEPROBE_ERR_IO;
    }

    if (baseline_path != NULL) {
        fprintf(fp, "\n## 기준선 비교\n\n");
        if (baseline_open_failed) {
            fprintf(fp, "기준선 파일을 열 수 없습니다: `%s`\n", baseline_path);
        } else if (baseline_parse_failed) {
            fprintf(fp, "기준선 파일을 해석할 수 없습니다: `%s`\n", baseline_path);
        } else if (loaded_baseline != NULL) {
            int changes = 0;

            fprintf(fp, "- 기준선: %s (%s)\n", loaded_baseline->target[0] ? loaded_baseline->target : "알 수 없음", loaded_baseline->captured_at[0] ? loaded_baseline->captured_at : "시간 알 수 없음");
            fprintf(fp, "\n변경 사항:\n");
            if (print_baseline_change_list(fp, loaded_baseline, stats, count, &changes) != 0) {
                fclose(fp);
                return ROUTEPROBE_ERR_IO;
            }

            fprintf(fp, "\n해석:\n");
            if (changes > 0 && diagnosis.level == MTR_DIAGNOSIS_NORMAL) {
                fprintf(fp, "- 변경 사항은 관찰되지만 목적지 품질 저하로 전파되지 않았습니다.\n");
                fprintf(fp, "- 중간 hop의 ICMP 무응답은 라우터 정책, 필터링, rate limit 때문에 발생할 수 있으므로 장애 근거가 아닌 참고 신호로 해석합니다.\n");
            } else if (diagnosis.level == MTR_DIAGNOSIS_NORMAL) {
                fprintf(fp, "- 현재 경로 품질은 저장된 기준선과 큰 차이가 없습니다.\n");
            } else {
                fprintf(fp, "- 목적지 품질 저하가 함께 관찰됩니다. 중간 hop 변경은 downstream 전파 여부를 확인하는 단서로만 사용합니다.\n");
            }
        }
    }

    fprintf(fp, "\n## 참고 자료\n\n");
    if (loaded_baseline != NULL) {
        fprintf(fp, "### 기준선 MTR\n\n");
        fprintf(fp, "- 기준선 대상: `%s`\n", loaded_baseline->target[0] ? loaded_baseline->target : "알 수 없음");
        fprintf(fp, "- 기준선 해석 IP: `%s`\n", loaded_baseline->resolved_ip[0] ? loaded_baseline->resolved_ip : "알 수 없음");
        fprintf(fp, "- 기준선 생성 시각: `%s`\n\n", loaded_baseline->captured_at[0] ? loaded_baseline->captured_at : "알 수 없음");
        if (write_report_mtr_table(fp, loaded_baseline->hops, loaded_baseline->hop_count) != 0) {
            fclose(fp);
            return ROUTEPROBE_ERR_IO;
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "### 현재 MTR\n\n");
    if (write_report_mtr_table(fp, stats, count) != 0) {
        fclose(fp);
        return ROUTEPROBE_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return ROUTEPROBE_ERR_IO;
    }
    printf("\n리포트 저장 완료: %s\n", path);
    return ROUTEPROBE_OK;
}
