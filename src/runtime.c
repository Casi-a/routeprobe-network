/*
 * runtime.c
 * Ping/Trace 모드가 공유하는 target 해석, raw socket 생성, 선택적 CSV 준비,
 * cleanup을 한 곳에 모은다.
 */
#include "runtime.h"

#include "icmp.h"
#include "resolver.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* runtime_close가 기대하는 sentinel 값으로 runtime 자원을 초기화한다. */
void runtime_init_empty(TracePingRuntime *runtime)
{
    // 일부 자원만 열린 runtime도 항상 runtime_close에 넘길 수 있어야 한다.
    runtime->sockfd = -1;
    runtime->csv = NULL;
}

/* 모드가 probe를 보내기 전에 필요한 모든 자원을 준비한다. */
int runtime_open(const TracePingConfig *config, CsvHeaderWriter write_header, TracePingRuntime *runtime)
{
    char error[TRACEPING_MAX_ERROR] = "";
    int rc;

    runtime_init_empty(runtime);

    rc = resolve_target_ipv4(config->target, &runtime->resolved, error, sizeof(error));
    if (rc != TRACEPING_OK) {
        fprintf(stderr, "%s\n", error);
        return rc;
    }

    runtime->sockfd = open_icmp_socket(error, sizeof(error));
    if (runtime->sockfd < 0) {
        fprintf(stderr, "%s\n", error);
        runtime_close(runtime);
        return TRACEPING_ERR_SOCKET;
    }

    if (config->output_path != NULL) {
        runtime->csv = fopen(config->output_path, "w");
        if (runtime->csv == NULL) {
            fprintf(stderr, "failed to open %s: %s\n", config->output_path, strerror(errno));
            runtime_close(runtime);
            return TRACEPING_ERR_IO;
        }
        if (write_header(runtime->csv) != 0) {
            fprintf(stderr, "failed to write CSV header\n");
            runtime_close(runtime);
            return TRACEPING_ERR_IO;
        }
    }

    return TRACEPING_OK;
}

/* CSV와 소켓 자원이 열려 있으면 해제한다. */
void runtime_close(TracePingRuntime *runtime)
{
    if (runtime->csv != NULL) {
        fclose(runtime->csv);
        runtime->csv = NULL;
    }
    if (runtime->sockfd >= 0) {
        close(runtime->sockfd);
        runtime->sockfd = -1;
    }
}
