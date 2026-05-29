/*
 * mtr.c
 * Trace probe를 여러 cycle 반복해 hop별 ICMP 무응답률, RTT 분포, jitter를 집계하는
 * MTR 스타일 모드를 구현한다.
 */
#include "mtr.h"

#include "baseline.h"
#include "csv_output.h"
#include "icmp.h"
#include "runtime.h"
#include "terminal_output.h"
#include "timeutil.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 성공 응답이 RTT 통계에 포함될 수 있는 상태인지 확인한다. */
static bool trace_result_has_rtt(const TraceResult *result)
{
    return result->status == TRACE_STATUS_HOP || result->status == TRACE_STATUS_DESTINATION;
}

/* hop 통계 구조체를 지정한 hop 번호의 빈 상태로 초기화한다. */
void mtr_hop_stats_init(MtrHopStats *stats, int hop)
{
    memset(stats, 0, sizeof(*stats));
    stats->hop = hop;
    stats->status = TRACE_STATUS_UNKNOWN;
}

/* 성공 응답 하나의 RTT를 누적 통계에 반영한다. */
static void record_success_rtt(MtrHopStats *stats, const TraceResult *result)
{
    stats->received += 1;
    stats->last_ms = result->rtt_ms;
    stats->total_ms += result->rtt_ms;
    stats->avg_ms = stats->total_ms / stats->received;

    if (!stats->has_rtt || result->rtt_ms < stats->best_ms) {
        stats->best_ms = result->rtt_ms;
    }
    if (!stats->has_rtt || result->rtt_ms > stats->worst_ms) {
        stats->worst_ms = result->rtt_ms;
    }

    if (stats->has_previous_ms) {
        double delta = result->rtt_ms - stats->previous_ms;
        if (delta < 0.0) {
            delta = -delta;
        }
        stats->jitter_total_ms += delta;
        stats->jitter_ms = stats->jitter_total_ms / (stats->received - 1);
        stats->has_jitter = true;
    }
    stats->previous_ms = result->rtt_ms;
    stats->has_previous_ms = true;
    stats->has_rtt = true;
}

/* TraceResult 하나를 hop 누적 통계에 반영한다. */
void mtr_hop_stats_record(MtrHopStats *stats, const TraceResult *result)
{
    stats->sent += 1;
    snprintf(stats->error, sizeof(stats->error), "%s", result->error);

    if (trace_result_has_rtt(result)) {
        record_success_rtt(stats, result);
        snprintf(stats->remote_ip, sizeof(stats->remote_ip), "%s", result->remote_ip);
        stats->status = result->status == TRACE_STATUS_DESTINATION ? TRACE_STATUS_DESTINATION : TRACE_STATUS_HOP;
    } else if (!stats->has_rtt && stats->status != TRACE_STATUS_DESTINATION) {
        stats->status = result->status;
    }

    stats->no_reply_percent = stats->sent == 0 ? 0.0 : ((double)(stats->sent - stats->received) / stats->sent) * 100.0;
}

/* 현재 hop에 TTL을 적용하고 probe 하나를 전송한 뒤 응답을 받는다. */
static void probe_mtr_hop(const RouteProbeRuntime *runtime, const RouteProbeConfig *config, uint16_t ident, uint16_t seq, int hop, TraceResult *result)
{
    memset(result, 0, sizeof(*result));
    result->hop = hop;

    if (setsockopt(runtime->sockfd, IPPROTO_IP, IP_TTL, &hop, sizeof(hop)) != 0) {
        result->status = TRACE_STATUS_ERROR;
        snprintf(result->error, sizeof(result->error), "failed to set TTL: %s", strerror(errno));
        return;
    }

    if (send_icmp_echo(runtime->sockfd, &runtime->resolved.addr, ident, seq, result->error, sizeof(result->error)) != 0) {
        result->status = TRACE_STATUS_ERROR;
        return;
    }
    receive_trace_response(runtime->sockfd, ident, seq, config->timeout_ms, result);
}

/* CSV가 요청된 경우 최종 hop 통계를 한 번에 기록한다. */
static int write_mtr_csv(FILE *csv, const RouteProbeConfig *config, const MtrHopStats *stats, int count)
{
    for (int i = 0; i < count; i++) {
        if (stats[i].sent == 0) {
            continue;
        }
        if (write_mtr_csv_row(csv, config, &stats[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

/* TTL sweep을 여러 cycle 반복하며 hop별 ICMP 무응답률과 RTT 분포를 출력한다. */
int run_mtr_mode(const RouteProbeConfig *config)
{
    RouteProbeRuntime runtime;
    MtrHopStats *stats;
    uint16_t ident = (uint16_t)(getpid() & 0xffff);
    uint32_t seq = 1;
    int rc;

    rc = runtime_open(config, write_mtr_csv_header, &runtime);
    if (rc != ROUTEPROBE_OK) {
        return rc;
    }

    stats = calloc((size_t)config->max_hop, sizeof(*stats));
    if (stats == NULL) {
        fprintf(stderr, "failed to allocate MTR stats buffer\n");
        runtime_close(&runtime);
        return ROUTEPROBE_ERR_GENERAL;
    }
    for (int hop = 1; hop <= config->max_hop; hop++) {
        mtr_hop_stats_init(&stats[hop - 1], hop);
    }

    print_mtr_title(config, &runtime.resolved);
    for (int cycle = 1; cycle <= config->count; cycle++) {
        bool reached = false;

        for (int hop = 1; hop <= config->max_hop; hop++) {
            TraceResult result;

            probe_mtr_hop(&runtime, config, ident, (uint16_t)(seq++ & 0xffffU), hop, &result);
            mtr_hop_stats_record(&stats[hop - 1], &result);
            if (result.destination_reached) {
                reached = true;
                break;
            }
        }

        print_mtr_progress_tick();
        if (cycle < config->count) {
            sleep_ms(config->interval_ms);
        }
        (void)reached;
    }

    print_mtr_progress_done();
    print_mtr_table_header();
    for (int i = 0; i < config->max_hop; i++) {
        if (stats[i].sent == 0) {
            continue;
        }
        print_mtr_hop_stats(&stats[i]);
    }

    if (runtime.csv != NULL && write_mtr_csv(runtime.csv, config, stats, config->max_hop) != 0) {
        fprintf(stderr, "failed to write MTR CSV row\n");
        free(stats);
        runtime_close(&runtime);
        return ROUTEPROBE_ERR_IO;
    }

    if (config->baseline_save_path != NULL) {
        rc = save_mtr_baseline(config->baseline_save_path, config, &runtime.resolved, stats, config->max_hop);
        if (rc != ROUTEPROBE_OK) {
            free(stats);
            runtime_close(&runtime);
            return rc;
        }
    }
    if (config->baseline_compare_path != NULL) {
        rc = compare_mtr_baseline(config->baseline_compare_path, stats, config->max_hop);
        if (rc != ROUTEPROBE_OK) {
            free(stats);
            runtime_close(&runtime);
            return rc;
        }
    }
    if (config->report_path != NULL) {
        rc = write_mtr_report(config->report_path, config, &runtime.resolved, stats, config->max_hop, config->baseline_compare_path);
        if (rc != ROUTEPROBE_OK) {
            free(stats);
            runtime_close(&runtime);
            return rc;
        }
    }

    free(stats);
    runtime_close(&runtime);
    return ROUTEPROBE_OK;
}
