/*
 * runtime.h
 * target, raw socket, 선택적 CSV 출력이 필요한 모드의 공통 runtime 자원
 * 준비와 정리를 담당한다.
 */
#ifndef TRACEPING_RUNTIME_H
#define TRACEPING_RUNTIME_H

#include "common.h"

#include <stdio.h>

typedef int (*CsvHeaderWriter)(FILE *fp);

typedef struct {
    ResolvedTarget resolved;
    int sockfd;
    FILE *csv;
} TracePingRuntime;

/* runtime_close가 안전하게 처리할 수 있는 sentinel 상태로 초기화한다. */
void runtime_init_empty(TracePingRuntime *runtime);

/* 대상 해석, raw 소켓 열기, 선택적 CSV 헤더 출력을 수행한다. */
int runtime_open(const TracePingConfig *config, CsvHeaderWriter write_header, TracePingRuntime *runtime);

/* runtime_open 중 성공적으로 열린 자원만 닫는다. */
void runtime_close(TracePingRuntime *runtime);

#endif
