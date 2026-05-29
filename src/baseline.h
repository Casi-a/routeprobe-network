/*
 * baseline.h
 * MTR 기준선 저장, 기준선 비교, 장애 리포트 생성 인터페이스를 제공한다.
 */
#ifndef ROUTEPROBE_BASELINE_H
#define ROUTEPROBE_BASELINE_H

#include "common.h"

#include <stdio.h>

#define ROUTEPROBE_BASELINE_MAX_HOPS 255

typedef struct {
    char target[ROUTEPROBE_MAX_TARGET];
    char resolved_ip[INET_ADDRSTRLEN];
    char captured_at[64];
    int hop_count;
    MtrHopStats hops[ROUTEPROBE_BASELINE_MAX_HOPS];
} MtrBaseline;

/* MTR 기준선 내용을 열린 stream에 기록한다. */
int write_mtr_baseline_stream(FILE *fp, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count);

/* MTR 기준선 파일을 읽어 비교 가능한 snapshot으로 복원한다. */
int read_mtr_baseline_stream(FILE *fp, MtrBaseline *baseline);

/* 경로 품질 회귀 비교 결과를 지정한 stream에 출력한다. */
int print_mtr_baseline_comparison(FILE *fp, const MtrBaseline *baseline, const MtrHopStats *current, int current_count);

/* MTR 기준선 파일을 저장한다. */
int save_mtr_baseline(const char *path, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count);

/* MTR 기준선 파일을 읽고 현재 측정값과 비교 결과를 출력한다. */
int compare_mtr_baseline(const char *path, const MtrHopStats *current, int current_count);

/* 현재 MTR 측정값과 선택적 기준선 비교를 Markdown 리포트로 저장한다. */
int write_mtr_report(const char *path, const RouteProbeConfig *config, const ResolvedTarget *resolved, const MtrHopStats *stats, int count, const char *baseline_path);

#endif
