/*
 * trace.c
 * TTL 기반 Trace 모드를 구현한다. hop별 retry, 선택적 timeout 표시,
 * 선택적 CSV row 기록을 담당한다.
 */
#include "trace.h"

#include "csv_output.h"
#include "icmp.h"
#include "runtime.h"
#include "terminal_output.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* 목적지 도달 또는 최대 hop 소진까지 설정된 TTL 순회를 수행한다. */
int run_trace_mode(const TracePingConfig *config)
{
    TracePingRuntime runtime;
    uint16_t ident = (uint16_t)(getpid() & 0xffff);
    bool reached = false;
    int rc;

    rc = runtime_open(config, write_trace_csv_header, &runtime);
    if (rc != TRACEPING_OK) {
        return rc;
    }

    printf("Trace route to %s (%s), %d hops max\n\n", config->target, runtime.resolved.ip, config->max_hop);
    for (int hop = 1; hop <= config->max_hop; hop++) {
        TraceResult result;
        memset(&result, 0, sizeof(result));
        result.hop = hop;

        if (setsockopt(runtime.sockfd, IPPROTO_IP, IP_TTL, &hop, sizeof(hop)) != 0) {
            result.status = TRACE_STATUS_ERROR;
            snprintf(result.error, sizeof(result.error), "failed to set TTL: %s", strerror(errno));
        } else {
            // 일부 라우터는 TTL 만료 ICMP 응답을 rate-limit하거나 drop한다.
            // 타임아웃 표시/생략을 결정하기 전에 같은 hop을 재시도한다.
            for (int attempt = 1; attempt <= config->trace_attempts; attempt++) {
                uint16_t seq = (uint16_t)(((hop - 1) * config->trace_attempts) + attempt);

                memset(&result, 0, sizeof(result));
                result.hop = hop;
                if (send_icmp_echo(runtime.sockfd, &runtime.resolved.addr, ident, seq, result.error, sizeof(result.error)) != 0) {
                    result.status = TRACE_STATUS_ERROR;
                    break;
                }
                receive_trace_response(runtime.sockfd, ident, seq, config->timeout_ms, &result);
                if (result.status != TRACE_STATUS_TIMEOUT) {
                    break;
                }
            }
        }

        // 기본 출력은 응답한 라우터에 집중하기 위해 타임아웃 hop을 생략한다.
        // --show-timeouts는 원시 traceroute에 가까운 행을 복원한다.
        if (result.status == TRACE_STATUS_TIMEOUT && !config->show_timeouts) {
            continue;
        }

        print_trace_result(&result);
        if (runtime.csv != NULL && write_trace_csv_row(runtime.csv, config, &result) != 0) {
            fprintf(stderr, "failed to write CSV row\n");
            runtime_close(&runtime);
            return TRACEPING_ERR_IO;
        }

        if (result.destination_reached) {
            reached = true;
            break;
        }
    }

    if (reached) {
        printf("\nTrace complete.\n");
    } else {
        printf("\nTrace stopped: max hop reached.\n");
    }

    runtime_close(&runtime);
    return TRACEPING_OK;
}
