/*
 * trace.h
 * TTL 기반 Trace 모드 실행 entrypoint를 제공한다.
 */
#ifndef TRACEPING_TRACE_H
#define TRACEPING_TRACE_H

#include "common.h"

/* 목적지 도달 또는 최대 hop 소진까지 TTL 값을 증가시키며 probe한다. */
int run_trace_mode(const TracePingConfig *config);

#endif
