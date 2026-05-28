/*
 * ping.h
 * Ping 모드 실행 entrypoint를 제공한다.
 */
#ifndef TRACEPING_PING_H
#define TRACEPING_PING_H

#include "common.h"

/* 대상 해석, Echo Request 전송, 출력, 선택적 CSV 기록까지 Ping 흐름을 수행한다. */
int run_ping_mode(const TracePingConfig *config);

#endif
