/*
 * csv_output.h
 * Ping/Trace 모드의 CSV 이스케이프, 헤더, 행 출력 인터페이스를 제공한다.
 */
#ifndef ROUTEPROBE_CSV_OUTPUT_H
#define ROUTEPROBE_CSV_OUTPUT_H

#include "common.h"

#include <stdio.h>

/* CSV 특수 문자가 있는 필드만 따옴표로 감싸 한 필드를 출력한다. */
void csv_escape(FILE *fp, const char *value);

/* Ping CSV 스키마 헤더를 출력한다. */
int write_ping_csv_header(FILE *fp);

/* 현재 로컬 timestamp를 포함해 Ping 결과 행 하나를 출력한다. */
int write_ping_csv_row(FILE *fp, const RouteProbeConfig *config, const PingResult *result);

/* Trace CSV 스키마 헤더를 출력한다. */
int write_trace_csv_header(FILE *fp);

/* 현재 로컬 timestamp를 포함해 Trace 결과 행 하나를 출력한다. */
int write_trace_csv_row(FILE *fp, const RouteProbeConfig *config, const TraceResult *result);

/* MTR hop 누적 통계 스키마 헤더를 출력한다. */
int write_mtr_csv_header(FILE *fp);

/* 현재 로컬 timestamp를 포함해 MTR hop 누적 통계 행 하나를 출력한다. */
int write_mtr_csv_row(FILE *fp, const RouteProbeConfig *config, const MtrHopStats *stats);

#endif
