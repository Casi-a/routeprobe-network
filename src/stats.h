/*
 * stats.h
 * Ping 결과 배열에서 packet loss와 RTT 기반 통계를 계산하는 인터페이스다.
 */
#ifndef ROUTEPROBE_STATS_H
#define ROUTEPROBE_STATS_H

#include "common.h"

/* 고정된 PingResult 배열에서 요약 통계를 계산한다. */
void compute_ping_stats(const PingResult *results, int count, PingStats *stats);

#endif
