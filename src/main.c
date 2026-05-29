/*
 * main.c
 * CLI 인자를 파싱하고 Ping, Trace, MTR 모드로 dispatch하는 프로그램 entrypoint다.
 */
#include "cli.h"
#include "common.h"
#include "mtr.h"
#include "ping.h"
#include "trace.h"

#include <stdio.h>

/* 검증된 설정을 선택된 실행 모드로 전달한다. */
int main(int argc, char **argv)
{
    RouteProbeConfig config;
    char error[ROUTEPROBE_MAX_ERROR] = "";
    int rc = parse_cli(argc, argv, &config, error, sizeof(error));

    if (rc != ROUTEPROBE_OK) {
        fprintf(stderr, "error: %s\n\n", error);
        print_usage(argv[0]);
        return rc;
    }
    if (config.help) {
        print_usage(argv[0]);
        return ROUTEPROBE_OK;
    }
    if (config.version) {
        print_version();
        return ROUTEPROBE_OK;
    }

    if (config.trace) {
        return run_trace_mode(&config);
    }
    if (config.mtr) {
        return run_mtr_mode(&config);
    }
    return run_ping_mode(&config);
}
