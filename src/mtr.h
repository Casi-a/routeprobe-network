/*
 * mtr.h
 * hop별 품질 통계를 누적하는 MTR 스타일 모드의 공개 인터페이스를 제공한다.
 */
#ifndef TRACEPING_MTR_H
#define TRACEPING_MTR_H

#include "common.h"

/* hop 통계 구조체를 지정한 hop 번호의 빈 상태로 초기화한다. */
void mtr_hop_stats_init(MtrHopStats *stats, int hop);

/* TraceResult 하나를 hop 누적 통계에 반영한다. */
void mtr_hop_stats_record(MtrHopStats *stats, const TraceResult *result);

/* TTL sweep을 여러 cycle 반복하며 hop별 ICMP 무응답률과 RTT 분포를 출력한다. */
int run_mtr_mode(const TracePingConfig *config);

#endif
