/*
 * terminal_output.h
 * Ping 결과, Trace row, 통계, 품질, 선택적 RTT graph의 terminal 출력을 담당한다.
 */
#ifndef ROUTEPROBE_TERMINAL_OUTPUT_H
#define ROUTEPROBE_TERMINAL_OUTPUT_H

#include "common.h"
#include "quality.h"

/* Ping 그래프에 사용할 RTT 기반 ASCII 막대 길이를 제한된 폭으로 변환한다. */
int graph_bar_length(double rtt_ms);

/* Ping 결과 하나를 사용자용 터미널 형식으로 출력한다. */
void print_ping_result(const PingResult *result, bool graph_enabled);

/* Ping 실행 종료 후 집계 통계를 출력한다. */
void print_ping_stats(const PingStats *stats);

/* 품질 등급과 등급 판단에 사용된 지표 이유를 출력한다. */
void print_quality(const PingStats *stats, QualityLevel quality);

/* Trace hop, 목적지, 타임아웃, 오류 행 하나를 출력한다. */
void print_trace_result(const TraceResult *result);

/* MTR 실행 시작 정보와 진행 표시 줄을 출력한다. */
void print_mtr_title(const RouteProbeConfig *config, const ResolvedTarget *resolved);

/* MTR cycle 하나가 끝날 때마다 진행 네모를 출력한다. */
void print_mtr_progress_tick(void);

/* MTR 진행 표시 줄을 닫고 결과 표 앞의 여백을 출력한다. */
void print_mtr_progress_done(void);

/* MTR 결과 표의 열 헤더를 출력한다. */
void print_mtr_table_header(void);

/* MTR hop 누적 통계 행 하나를 출력한다. */
void print_mtr_hop_stats(const MtrHopStats *stats);

#endif
