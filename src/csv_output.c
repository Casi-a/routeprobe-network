/*
 * csv_output.c
 * Ping/Trace CSV 헤더와 행을 출력하며 대상/오류 문구의 CSV 이스케이프
 * 규칙을 보존한다.
 */
#include "csv_output.h"

#include "timeutil.h"

#include <stdio.h>

/* RFC 4180 스타일 따옴표 규칙에 맞춰 CSV 필드 하나를 이스케이프한다. */
void csv_escape(FILE *fp, const char *value)
{
    bool quote = false;
    const char *p;

    if (value == NULL) {
        return;
    }
    for (p = value; *p != '\0'; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            quote = true;
            break;
        }
    }

    // 필요한 필드만 따옴표로 감싸 사람이 읽기 쉬운 CSV를 유지하면서
    // 쉼표, 따옴표, 포함된 줄바꿈은 보존한다.
    if (!quote) {
        fputs(value, fp);
        return;
    }

    fputc('"', fp);
    for (p = value; *p != '\0'; p++) {
        if (*p == '"') {
            fputc('"', fp);
        }
        fputc(*p, fp);
    }
    fputc('"', fp);
}

/* 내부 Ping 상태 값을 안정적인 CSV 라벨로 변환한다. */
static const char *ping_status_string(PingStatus status)
{
    switch (status) {
    case PING_STATUS_SUCCESS:
        return "success";
    case PING_STATUS_TIMEOUT:
        return "timeout";
    case PING_STATUS_ERROR:
        return "error";
    }
    return "unknown";
}

/* 내부 Trace 상태 값을 안정적인 CSV 라벨로 변환한다. */
static const char *trace_status_string(TraceStatus status)
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

/* Ping CSV 열 이름을 출력한다. */
int write_ping_csv_header(FILE *fp)
{
    return fprintf(fp, "seq,timestamp,target,remote_ip,rtt_ms,status,ttl,error\n") < 0 ? -1 : 0;
}

/* 성공 응답이 없으면 RTT/TTL을 비워 두고 Ping CSV 행 하나를 출력한다. */
int write_ping_csv_row(FILE *fp, const RouteProbeConfig *config, const PingResult *result)
{
    char timestamp[64] = "";
    format_timestamp(timestamp, sizeof(timestamp));

    fprintf(fp, "%d,", result->seq);
    csv_escape(fp, timestamp);
    fputc(',', fp);
    csv_escape(fp, config->target);
    fputc(',', fp);
    csv_escape(fp, result->remote_ip);
    fputc(',', fp);
    if (result->status == PING_STATUS_SUCCESS) {
        fprintf(fp, "%.1f", result->rtt_ms);
    }
    fputc(',', fp);
    fputs(ping_status_string(result->status), fp);
    fputc(',', fp);
    if (result->status == PING_STATUS_SUCCESS) {
        fprintf(fp, "%d", result->ttl);
    }
    fputc(',', fp);
    csv_escape(fp, result->error);
    fputc('\n', fp);
    return ferror(fp) ? -1 : 0;
}

/* Trace CSV 열 이름을 출력한다. */
int write_trace_csv_header(FILE *fp)
{
    return fprintf(fp, "hop,timestamp,target,remote_ip,rtt_ms,status,error\n") < 0 ? -1 : 0;
}

/* 타임아웃/오류 행에서는 RTT를 비워 두고 Trace CSV 행 하나를 출력한다. */
int write_trace_csv_row(FILE *fp, const RouteProbeConfig *config, const TraceResult *result)
{
    char timestamp[64] = "";
    format_timestamp(timestamp, sizeof(timestamp));

    fprintf(fp, "%d,", result->hop);
    csv_escape(fp, timestamp);
    fputc(',', fp);
    csv_escape(fp, config->target);
    fputc(',', fp);
    csv_escape(fp, result->remote_ip);
    fputc(',', fp);
    if (result->status == TRACE_STATUS_HOP || result->status == TRACE_STATUS_DESTINATION) {
        fprintf(fp, "%.1f", result->rtt_ms);
    }
    fputc(',', fp);
    fputs(trace_status_string(result->status), fp);
    fputc(',', fp);
    csv_escape(fp, result->error);
    fputc('\n', fp);
    return ferror(fp) ? -1 : 0;
}

/* MTR 누적 통계 CSV 열 이름을 출력한다. */
int write_mtr_csv_header(FILE *fp)
{
    return fprintf(fp, "hop,timestamp,target,remote_ip,no_reply_percent,sent,received,last_ms,avg_ms,best_ms,worst_ms,jitter_ms,status,error\n") < 0 ? -1 : 0;
}

/* RTT가 없는 hop은 RTT 계열 값을 비워 두고 MTR 누적 통계 행 하나를 출력한다. */
int write_mtr_csv_row(FILE *fp, const RouteProbeConfig *config, const MtrHopStats *stats)
{
    char timestamp[64] = "";
    format_timestamp(timestamp, sizeof(timestamp));

    fprintf(fp, "%d,", stats->hop);
    csv_escape(fp, timestamp);
    fputc(',', fp);
    csv_escape(fp, config->target);
    fputc(',', fp);
    csv_escape(fp, stats->remote_ip);
    fprintf(fp, ",%.1f,%d,%d,", stats->no_reply_percent, stats->sent, stats->received);
    if (stats->has_rtt) {
        fprintf(fp, "%.1f,%.1f,%.1f,%.1f,", stats->last_ms, stats->avg_ms, stats->best_ms, stats->worst_ms);
    } else {
        fputs(",,,,", fp);
    }
    if (stats->has_jitter) {
        fprintf(fp, "%.1f", stats->jitter_ms);
    }
    fputc(',', fp);
    fputs(trace_status_string(stats->status), fp);
    fputc(',', fp);
    csv_escape(fp, stats->error);
    fputc('\n', fp);
    return ferror(fp) ? -1 : 0;
}
