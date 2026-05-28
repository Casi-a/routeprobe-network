/*
 * timeutil.h
 * monotonic 시간 계산, millisecond sleep, local timestamp formatting helper다.
 */
#ifndef TRACEPING_TIMEUTIL_H
#define TRACEPING_TIMEUTIL_H

#include <stddef.h>
#include <time.h>

/* 두 monotonic timestamp 사이의 경과 시간을 밀리초로 반환한다. */
double monotonic_diff_ms(struct timespec start, struct timespec end);

/* signal로 중단되면 남은 시간만큼 재시도하며 밀리초 단위로 sleep한다. */
void sleep_ms(int ms);

/* CSV 행에 사용할 현재 로컬 시간 문자열을 만든다. */
int format_timestamp(char *buffer, size_t size);

#endif
