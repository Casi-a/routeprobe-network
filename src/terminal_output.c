/*
 * terminal_output.c
 * ping packet, trace hop, summary stats, 품질, 선택적 ASCII graph를
 * terminal에 사용자용 형식으로 출력한다.
 */
#include "terminal_output.h"

#include <math.h>
#include <stdio.h>

/* 터미널 가독성을 위해 RTT를 상한이 있는 압축 그래프 폭으로 변환한다. */
int graph_bar_length(double rtt_ms)
{
    int length = (int)ceil((rtt_ms / 200.0) * 40.0);
    if (length < 1) {
        length = 1;
    }
    if (length > 40) {
        length = 40;
    }
    return length;
}

/* 성공한 Ping RTT에 대한 ASCII 그래프 조각을 출력한다. */
static void print_graph(double rtt_ms)
{
    int length = graph_bar_length(rtt_ms);
    for (int i = 0; i < length; i++) {
        putchar('#');
    }
}

/* 선택적 그래프 장식을 포함해 Ping 결과 하나를 출력한다. */
void print_ping_result(const PingResult *result, bool graph_enabled)
{
    if (result->status == PING_STATUS_SUCCESS) {
        printf("[%d] reply from %s: time=%.1f ms ttl=%d", result->seq, result->remote_ip, result->rtt_ms, result->ttl);
        if (graph_enabled) {
            printf(" | ");
            print_graph(result->rtt_ms);
        }
        putchar('\n');
        return;
    }

    if (result->status == PING_STATUS_TIMEOUT) {
        printf("[%d] timeout", result->seq);
        if (graph_enabled) {
            printf(" | x");
        }
        putchar('\n');
        return;
    }

    printf("[%d] error: %s\n", result->seq, result->error[0] ? result->error : "unknown error");
}

/* 패킷 수, 손실률, RTT 분포 통계를 출력한다. */
void print_ping_stats(const PingStats *stats)
{
    printf("\n--- ping statistics ---\n");
    printf("%d packets transmitted, %d received, %.1f%% packet loss\n",
           stats->transmitted, stats->received, stats->loss_percent);
    if (!stats->has_rtt) {
        printf("rtt min/avg/max = N/A\n");
        printf("stddev = N/A\n");
        printf("jitter = N/A\n");
        printf("p95 = N/A\n");
        return;
    }

    printf("rtt min/avg/max = %.1f/%.1f/%.1f ms\n", stats->min_ms, stats->avg_ms, stats->max_ms);
    printf("stddev = %.1f ms\n", stats->stddev_ms);
    if (stats->has_jitter) {
        printf("jitter = %.1f ms\n", stats->jitter_ms);
    } else {
        printf("jitter = N/A\n");
    }
    printf("p95 = %.1f ms\n", stats->p95_ms);
}

/* 전체 품질 등급과 지표별 판단 근거를 출력한다. */
void print_quality(const PingStats *stats, QualityLevel quality)
{
    printf("\nNetwork quality: %s\n", quality_to_string(quality));
    printf("Reason:\n");
    printf("- Packet loss: %.1f%%\n", stats->loss_percent);
    if (stats->has_rtt) {
        printf("- Average RTT: %.1f ms\n", stats->avg_ms);
    } else {
        printf("- Average RTT: N/A\n");
    }
    if (stats->has_jitter) {
        printf("- Jitter: %.1f ms\n", stats->jitter_ms);
    } else {
        printf("- Jitter: N/A\n");
    }
}

/* 타임아웃이 숨겨진 경우에도 hop 번호를 보존하며 Trace 행 하나를 출력한다. */
void print_trace_result(const TraceResult *result)
{
    if (result->status == TRACE_STATUS_HOP || result->status == TRACE_STATUS_DESTINATION) {
        printf("%-3d %-15s %.1f ms\n", result->hop, result->remote_ip, result->rtt_ms);
    } else if (result->status == TRACE_STATUS_TIMEOUT) {
        printf("%-3d %-15s timeout\n", result->hop, "*");
    } else if (result->status == TRACE_STATUS_UNKNOWN) {
        printf("%-3d %-15s unknown\n", result->hop, result->remote_ip[0] ? result->remote_ip : "*");
    } else {
        printf("%-3d %-15s error: %s\n", result->hop, "*", result->error[0] ? result->error : "unknown error");
    }
}

/* MTR 모드의 대상 정보와 cycle 단위 진행 표시 줄을 출력한다. */
void print_mtr_title(const RouteProbeConfig *config, const ResolvedTarget *resolved)
{
    printf("MTR %s (%s): %d cycles, %d hops max\n", config->target, resolved->ip, config->count, config->max_hop);
    printf("Progress: ");
    fflush(stdout);
}

/* cycle이 끝났음을 ASCII 네모 하나로 표시한다. */
void print_mtr_progress_tick(void)
{
    printf("[]");
    fflush(stdout);
}

/* 진행 표시 줄을 닫고 최종 표와 시각적으로 분리한다. */
void print_mtr_progress_done(void)
{
    printf(" done\n\n");
}

/* MTR 결과 표의 열 이름을 출력한다. */
void print_mtr_table_header(void)
{
    printf("%-3s %-15s %6s %5s %5s %7s %7s %7s %7s %7s %s\n",
           "Hop", "Host", "NoRep%", "Sent", "Recv", "Last", "Avg", "Best", "Worst", "Jitter", "Status");
}

/* RTT가 없는 hop은 빈 값 대신 '-'를 표시해 ICMP 무응답 hop을 빠르게 구분한다. */
void print_mtr_hop_stats(const MtrHopStats *stats)
{
    const char *host = stats->remote_ip[0] ? stats->remote_ip : "*";

    if (stats->has_rtt) {
        printf("%-3d %-15s %5.1f%% %5d %5d %7.1f %7.1f %7.1f %7.1f ",
               stats->hop, host, stats->no_reply_percent, stats->sent, stats->received,
               stats->last_ms, stats->avg_ms, stats->best_ms, stats->worst_ms);
        if (stats->has_jitter) {
            printf("%7.1f ", stats->jitter_ms);
        } else {
            printf("%7s ", "-");
        }
    } else {
        printf("%-3d %-15s %5.1f%% %5d %5d %7s %7s %7s %7s %7s ",
               stats->hop, host, stats->no_reply_percent, stats->sent, stats->received,
               "-", "-", "-", "-", "-");
    }

    if (stats->status == TRACE_STATUS_DESTINATION) {
        printf("destination\n");
    } else if (stats->status == TRACE_STATUS_HOP) {
        printf("hop\n");
    } else if (stats->status == TRACE_STATUS_TIMEOUT) {
        printf("timeout\n");
    } else if (stats->status == TRACE_STATUS_ERROR) {
        printf("error");
        if (stats->error[0]) {
            printf(": %s", stats->error);
        }
        putchar('\n');
    } else {
        printf("unknown\n");
    }
}
