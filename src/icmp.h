/*
 * icmp.h
 * raw ICMP 소켓, 패킷 생성, checksum, 응답 수신 API를 제공한다.
 */
#ifndef ROUTEPROBE_ICMP_H
#define ROUTEPROBE_ICMP_H

#include "common.h"

#include <stddef.h>
#include <stdint.h>

/* 패킷 버퍼 전체에 대한 ICMP one's-complement checksum을 계산한다. */
uint16_t icmp_checksum(const void *data, size_t length);

/* Linux raw ICMP 소켓을 열며, 호출자는 root 또는 CAP_NET_RAW 권한이 필요하다. */
int open_icmp_socket(char *error, size_t error_size);

/* ident/seq와 timestamp payload를 담은 ICMP Echo Request 하나를 보낸다. */
int send_icmp_echo(int sockfd, const struct sockaddr_in *addr, uint16_t ident, uint16_t seq, char *error, size_t error_size);

/* 현재 요청과 일치하는 Echo Reply를 기다려 PingResult로 변환한다. */
int receive_ping_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, PingResult *ping);

/* 현재 요청과 일치하는 Trace hop 또는 목적지 응답을 TraceResult로 변환한다. */
int receive_trace_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, TraceResult *trace);

/* NULL이 아닌 결과 포인터에 따라 ping/trace 수신 함수로 위임하는 호환 진입점이다. */
int receive_icmp_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, PingResult *ping, TraceResult *trace);

#endif
