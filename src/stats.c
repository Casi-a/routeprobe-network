/*
 * stats.c
 * PingResult 배열을 packet loss, RTT 분포, jitter, percentile 통계로 집계한다.
 */
#include "stats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* RTT 값을 오름차순으로 정렬하기 위한 qsort 비교 함수다. */
static int compare_double(const void *a, const void *b)
{
    double lhs = *(const double *)a;
    double rhs = *(const double *)b;
    return (lhs > rhs) - (lhs < rhs);
}

/* 최종 결과 배열에서 모든 Ping 요약 지표를 계산한다. */
void compute_ping_stats(const PingResult *results, int count, PingStats *stats)
{
    double *rtts = NULL;
    double sum = 0.0;
    int received = 0;

    memset(stats, 0, sizeof(*stats));
    stats->transmitted = count;
    if (count <= 0) {
        return;
    }

    rtts = calloc((size_t)count, sizeof(*rtts));
    if (rtts == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (results[i].status == PING_STATUS_SUCCESS) {
            double rtt = results[i].rtt_ms;
            if (received == 0 || rtt < stats->min_ms) {
                stats->min_ms = rtt;
            }
            if (received == 0 || rtt > stats->max_ms) {
                stats->max_ms = rtt;
            }
            rtts[received++] = rtt;
            sum += rtt;
        }
    }

    stats->received = received;
    stats->loss_percent = ((double)(count - received) / (double)count) * 100.0;
    if (received == 0) {
        free(rtts);
        return;
    }

    stats->has_rtt = true;
    stats->avg_ms = sum / (double)received;

    double variance_sum = 0.0;
    for (int i = 0; i < received; i++) {
        double diff = rtts[i] - stats->avg_ms;
        variance_sum += diff * diff;
    }
    stats->stddev_ms = sqrt(variance_sum / (double)received);

    // 압축된 RTT 배열은 타임아웃을 의도적으로 제외한다. jitter는 연속된
    // 성공 응답 사이에서만 측정한다.
    if (received >= 2) {
        double jitter_sum = 0.0;
        for (int i = 1; i < received; i++) {
            jitter_sum += fabs(rtts[i] - rtts[i - 1]);
        }
        stats->jitter_ms = jitter_sum / (double)(received - 1);
        stats->has_jitter = true;
    }

    qsort(rtts, (size_t)received, sizeof(*rtts), compare_double);
    int rank = (int)ceil(0.95 * (double)received);
    if (rank < 1) {
        rank = 1;
    }
    stats->p95_ms = rtts[rank - 1];
    free(rtts);
}
