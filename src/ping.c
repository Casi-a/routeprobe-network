/*
 * ping.c
 * Ping 모드를 구현한다. 반복 ICMP Echo Request, packet별 출력, 요약 통계,
 * 품질 평가, 선택적 CSV row 기록을 담당한다.
 */
#include "ping.h"

#include "csv_output.h"
#include "icmp.h"
#include "quality.h"
#include "runtime.h"
#include "stats.h"
#include "terminal_output.h"
#include "timeutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* 해석된 IPv4 대상 하나에 대해 설정된 Ping 흐름을 수행한다. */
int run_ping_mode(const TracePingConfig *config)
{
    PingResult *results;
    TracePingRuntime runtime;
    uint16_t ident = (uint16_t)(getpid() & 0xffff);
    int rc;

    rc = runtime_open(config, write_ping_csv_header, &runtime);
    if (rc != TRACEPING_OK) {
        return rc;
    }

    results = calloc((size_t)config->count, sizeof(*results));
    if (results == NULL) {
        fprintf(stderr, "failed to allocate ping result buffer\n");
        runtime_close(&runtime);
        return TRACEPING_ERR_GENERAL;
    }

    printf("PING %s (%s): %d packets\n", config->target, runtime.resolved.ip, config->count);
    for (int i = 0; i < config->count; i++) {
        PingResult *result = &results[i];
        result->seq = i + 1;

        if (send_icmp_echo(runtime.sockfd, &runtime.resolved.addr, ident, (uint16_t)result->seq, result->error, sizeof(result->error)) != 0) {
            result->status = PING_STATUS_ERROR;
        } else {
            receive_ping_response(runtime.sockfd, ident, (uint16_t)result->seq, config->timeout_ms, result);
        }

        print_ping_result(result, config->graph_enabled);
        if (runtime.csv != NULL && write_ping_csv_row(runtime.csv, config, result) != 0) {
            fprintf(stderr, "failed to write CSV row\n");
            free(results);
            runtime_close(&runtime);
            return TRACEPING_ERR_IO;
        }

        if (i + 1 < config->count) {
            sleep_ms(config->interval_ms);
        }
    }

    PingStats stats;
    compute_ping_stats(results, config->count, &stats);
    print_ping_stats(&stats);
    print_quality(&stats, evaluate_quality(&stats));

    free(results);
    runtime_close(&runtime);
    return TRACEPING_OK;
}
