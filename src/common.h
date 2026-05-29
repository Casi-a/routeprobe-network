/*
 * common.h
 * CLI 파싱, probe, 출력, 테스트에서 함께 사용하는 상수, 종료 코드,
 * 설정값, 결과 자료구조를 정의한다.
 */
#ifndef ROUTEPROBE_COMMON_H
#define ROUTEPROBE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>

#define ROUTEPROBE_VERSION "0.1.0"
#define ROUTEPROBE_MAX_ERROR 128
#define ROUTEPROBE_MAX_TARGET 256

typedef enum {
    ROUTEPROBE_OK = 0,
    ROUTEPROBE_ERR_GENERAL = 1,
    ROUTEPROBE_ERR_USAGE = 2,
    ROUTEPROBE_ERR_SOCKET = 3,
    ROUTEPROBE_ERR_DNS = 4,
    ROUTEPROBE_ERR_IO = 5
} RouteProbeExitCode;

typedef struct {
    const char *target;
    int count;
    int interval_ms;
    int timeout_ms;
    bool trace;
    bool mtr;
    int max_hop;
    int trace_attempts;
    bool show_timeouts;
    const char *output_path;
    const char *baseline_save_path;
    const char *baseline_compare_path;
    const char *report_path;
    bool graph_enabled;
    bool help;
    bool version;
} RouteProbeConfig;

typedef enum {
    PING_STATUS_SUCCESS,
    PING_STATUS_TIMEOUT,
    PING_STATUS_ERROR
} PingStatus;

typedef enum {
    TRACE_STATUS_TIMEOUT,
    TRACE_STATUS_ERROR,
    TRACE_STATUS_UNKNOWN,
    TRACE_STATUS_HOP,
    TRACE_STATUS_DESTINATION
} TraceStatus;

typedef struct {
    int seq;
    char remote_ip[INET_ADDRSTRLEN];
    double rtt_ms;
    int ttl;
    PingStatus status;
    char error[ROUTEPROBE_MAX_ERROR];
} PingResult;

typedef struct {
    int hop;
    char remote_ip[INET_ADDRSTRLEN];
    double rtt_ms;
    TraceStatus status;
    bool destination_reached;
    char error[ROUTEPROBE_MAX_ERROR];
} TraceResult;

typedef struct {
    int transmitted;
    int received;
    double loss_percent;
    double min_ms;
    double avg_ms;
    double max_ms;
    double stddev_ms;
    double jitter_ms;
    double p95_ms;
    bool has_rtt;
    bool has_jitter;
} PingStats;

typedef struct {
    int hop;
    char remote_ip[INET_ADDRSTRLEN];
    int sent;
    int received;
    double no_reply_percent;
    double last_ms;
    double best_ms;
    double avg_ms;
    double worst_ms;
    double jitter_ms;
    bool has_rtt;
    bool has_jitter;
    TraceStatus status;
    char error[ROUTEPROBE_MAX_ERROR];
    double total_ms;
    double previous_ms;
    bool has_previous_ms;
    double jitter_total_ms;
} MtrHopStats;

typedef struct {
    char ip[INET_ADDRSTRLEN];
    struct sockaddr_in addr;
} ResolvedTarget;

#endif
