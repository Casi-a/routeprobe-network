/*
 * timeutil.c
 * RTT용 monotonic 경과 시간 helper와 CSV timestamp용 local wall-clock
 * formatting을 제공한다.
 */
#include "timeutil.h"

#include <stdio.h>

/* 두 monotonic timestamp 차이를 밀리초 단위로 변환한다. */
double monotonic_diff_ms(struct timespec start, struct timespec end)
{
    time_t sec = end.tv_sec - start.tv_sec;
    long nsec = end.tv_nsec - start.tv_nsec;
    return ((double)sec * 1000.0) + ((double)nsec / 1000000.0);
}

/* signal로 중단되면 재개하면서 밀리초 간격만큼 sleep한다. */
void sleep_ms(int ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&req, &req) == -1) {
    }
}

/* CSV timestamp 형식으로 현재 로컬 시간을 포맷한다. */
int format_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm tm_now;

    if (localtime_r(&now, &tm_now) == NULL) {
        return -1;
    }
    if (strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &tm_now) == 0) {
        return -1;
    }
    return 0;
}
