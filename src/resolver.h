/*
 * resolver.h
 * 숫자 IPv4 주소와 domain name을 IPv4 target으로 해석하는 인터페이스다.
 */
#ifndef ROUTEPROBE_RESOLVER_H
#define ROUTEPROBE_RESOLVER_H

#include "common.h"

/* 대상을 sockaddr_in과 출력 가능한 IPv4 문자열로 해석한다. */
int resolve_target_ipv4(const char *target, ResolvedTarget *resolved, char *error, size_t error_size);

#endif
