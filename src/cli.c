/*
 * cli.c
 * 명령행 옵션을 RouteProbeConfig로 변환하고 사용법/버전 출력을 만든다.
 */
#include "cli.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPTION_BOOL,
    OPTION_INT,
    OPTION_STRING
} OptionType;

typedef struct {
    const char *long_name;
    const char *short_name;
    const char *value_name;
    const char *description;
    OptionType type;
    size_t offset;
    int min;
    int max;
    int default_value;
    bool exits_early;
} OptionSpec;

#define BOOL_OPTION(long_name_, short_name_, field_, description_, exits_early_) \
    {long_name_, short_name_, NULL, description_, OPTION_BOOL, offsetof(RouteProbeConfig, field_), 0, 0, 0, exits_early_}

#define INT_OPTION(long_name_, short_name_, value_name_, field_, description_, default_, min_, max_) \
    {long_name_, short_name_, value_name_, description_, OPTION_INT, offsetof(RouteProbeConfig, field_), min_, max_, default_, false}

#define STRING_OPTION(long_name_, short_name_, value_name_, field_, description_) \
    {long_name_, short_name_, value_name_, description_, OPTION_STRING, offsetof(RouteProbeConfig, field_), 0, 0, 0, false}

static const OptionSpec OPTIONS[] = {
    // 파싱과 도움말 출력이 같은 표를 보게 해서 별칭 불일치를 막는다.
    INT_OPTION("--count", "--c", "<n>", count, "Ping packets or MTR cycles", 4, 1, 100000),
    INT_OPTION("--interval", "--i", "<ms>", interval_ms, "Interval between packets/cycles", 1000, 10, 60000),
    INT_OPTION("--timeout", "--to", "<ms>", timeout_ms, "Reply timeout per packet", 1000, 1, 60000),
    BOOL_OPTION("--trace", "--tr", trace, "Run TTL-based trace mode", false),
    BOOL_OPTION("--mtr", "--mt", mtr, "Run MTR-style hop quality mode", false),
    INT_OPTION("--max-hop", "--mh", "<n>", max_hop, "Maximum trace/MTR hops", 30, 1, 255),
    INT_OPTION("--trace-attempts", "--ta", "<n>", trace_attempts, "Probe attempts per trace hop", 3, 1, 10),
    BOOL_OPTION("--show-timeouts", "--st", show_timeouts, "Show trace hops that do not reply", false),
    STRING_OPTION("--output", "--o", "<file>", output_path, "Write CSV output"),
    STRING_OPTION("--baseline-save", "--bs", "<file>", baseline_save_path, "Save MTR baseline snapshot"),
    STRING_OPTION("--baseline-compare", "--bc", "<file>", baseline_compare_path, "Compare MTR result with baseline"),
    STRING_OPTION("--report", "--rp", "<file>", report_path, "Write MTR incident report"),
    BOOL_OPTION("--graph", "--g", graph_enabled, "Enable ping RTT graph", false),
    BOOL_OPTION("--help", "--h", help, "Show this help", true),
    BOOL_OPTION("--version", "--v", version, "Show version", true),
};

/* 명령행 override를 적용하기 전에 내장 기본값을 채운다. */
void config_init_defaults(RouteProbeConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->count = 4;
    config->interval_ms = 1000;
    config->timeout_ms = 1000;
    config->max_hop = 30;
    config->trace_attempts = 3;
}

/* 십진수 정수를 파싱하고 옵션별 포함 범위를 강제한다. */
static bool parse_int_range(const char *text, int min, int max, int *out)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < min || value > max) {
        return false;
    }
    *out = (int)value;
    return true;
}

/* argv 인덱스를 값 위치로 이동하고 값이 없으면 사용법 오류를 만든다. */
static int require_value(int argc, char **argv, int *index, const char *option, const char **value, char *error, size_t error_size)
{
    if (*index + 1 >= argc) {
        snprintf(error, error_size, "%s requires a value", option);
        return ROUTEPROBE_ERR_USAGE;
    }
    *index += 1;
    *value = argv[*index];
    return ROUTEPROBE_OK;
}

/* 긴 옵션 이름 또는 모호하지 않은 짧은 별칭과 일치하는지 확인한다. */
static bool option_matches(const OptionSpec *option, const char *arg)
{
    return strcmp(arg, option->long_name) == 0 || strcmp(arg, option->short_name) == 0;
}

/* 파싱과 도움말 출력이 같은 기준 데이터를 쓰도록 옵션 메타데이터를 찾는다. */
static const OptionSpec *find_option(const char *arg)
{
    size_t count = sizeof(OPTIONS) / sizeof(OPTIONS[0]);

    for (size_t i = 0; i < count; i++) {
        if (option_matches(&OPTIONS[i], arg)) {
            return &OPTIONS[i];
        }
    }
    return NULL;
}

/* 값 검증을 포함해 옵션 하나를 RouteProbeConfig에 반영한다. */
static int apply_option(const OptionSpec *option, int argc, char **argv, int *index, RouteProbeConfig *config, char *error, size_t error_size)
{
    char *base = (char *)config;
    const char *value = NULL;

    if (option->type == OPTION_BOOL) {
        bool *field = (bool *)(base + option->offset);
        *field = true;
        return ROUTEPROBE_OK;
    }

    int rc = require_value(argc, argv, index, option->long_name, &value, error, error_size);
    if (rc != ROUTEPROBE_OK) {
        return rc;
    }

    if (option->type == OPTION_STRING) {
        const char **field = (const char **)(base + option->offset);
        *field = value;
        return ROUTEPROBE_OK;
    }

    int parsed;
    if (!parse_int_range(value, option->min, option->max, &parsed)) {
        snprintf(error, error_size, "%s must be an integer in range %d..%d", option->long_name, option->min, option->max);
        return ROUTEPROBE_ERR_USAGE;
    }
    int *field = (int *)(base + option->offset);
    *field = parsed;
    return ROUTEPROBE_OK;
}

/* argv를 설정으로 변환하며 도움말/버전 요청은 대상 검증 전에 종료한다. */
int parse_cli(int argc, char **argv, RouteProbeConfig *config, char *error, size_t error_size)
{
    config_init_defaults(config);

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const OptionSpec *option = find_option(arg);

        if (option != NULL) {
            int rc = apply_option(option, argc, argv, &i, config, error, error_size);
            if (rc != ROUTEPROBE_OK) {
                return rc;
            }
            if (option->exits_early) {
                return ROUTEPROBE_OK;
            }
            continue;
        }

        if (strncmp(arg, "--", 2) == 0) {
            snprintf(error, error_size, "unknown option: %s", arg);
            return ROUTEPROBE_ERR_USAGE;
        }
        if (config->target != NULL) {
            snprintf(error, error_size, "multiple targets specified");
            return ROUTEPROBE_ERR_USAGE;
        }
        config->target = arg;
    }

    if (config->target == NULL) {
        snprintf(error, error_size, "target is required");
        return ROUTEPROBE_ERR_USAGE;
    }
    if (config->trace && config->mtr) {
        snprintf(error, error_size, "--trace and --mtr cannot be used together");
        return ROUTEPROBE_ERR_USAGE;
    }
    if (!config->mtr && (config->baseline_save_path != NULL || config->baseline_compare_path != NULL || config->report_path != NULL)) {
        snprintf(error, error_size, "baseline and report options require --mtr");
        return ROUTEPROBE_ERR_USAGE;
    }
    return ROUTEPROBE_OK;
}

/* 별칭과 기본값이 어긋나지 않도록 옵션 표에서 사용법 줄을 출력한다. */
void print_usage(const char *program)
{
    printf("Usage: %s <target> [options]\n", program);
    printf("\nOptions:\n");

    size_t count = sizeof(OPTIONS) / sizeof(OPTIONS[0]);
    for (size_t i = 0; i < count; i++) {
        const OptionSpec *option = &OPTIONS[i];
        char names[48];

        if (option->value_name != NULL) {
            snprintf(names, sizeof(names), "%s, %s %s", option->long_name, option->short_name, option->value_name);
        } else {
            snprintf(names, sizeof(names), "%s, %s", option->long_name, option->short_name);
        }

        if (option->type == OPTION_INT) {
            printf("  %-31s %s (default: %d)\n", names, option->description, option->default_value);
        } else {
            printf("  %-31s %s\n", names, option->description);
        }
    }
}

/* --version과 --v에서 사용하는 프로그램 버전을 출력한다. */
void print_version(void)
{
    printf("routeprobe %s\n", ROUTEPROBE_VERSION);
}
