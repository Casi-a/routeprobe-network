/*
 * icmp.c
 * ICMP Echo 패킷 생성, raw 소켓 송수신, Ping/Trace 응답 matching,
 * RTT 계산을 담당한다.
 */
#include "icmp.h"

#include "timeutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ICMP_PAYLOAD_SIZE 56

typedef struct {
    unsigned char buffer[1500];
    ssize_t length;
    struct sockaddr_in from;
    const struct iphdr *ip;
    const struct icmphdr *icmp;
    int ip_header_len;
    char remote_ip[INET_ADDRSTRLEN];
} IcmpPacket;

/* ICMP 패킷 버퍼에 대한 one's-complement checksum을 계산한다. */
uint16_t icmp_checksum(const void *data, size_t length)
{
    const uint16_t *words = data;
    uint32_t sum = 0;

    while (length > 1) {
        sum += *words++;
        length -= 2;
    }
    if (length == 1) {
        uint16_t last = 0;
        memcpy(&last, words, 1);
        sum += last;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffU) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

/* raw ICMP 소켓을 열고 권한 오류에는 실행 방법 힌트를 포함한다. */
int open_icmp_socket(char *error, size_t error_size)
{
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        snprintf(error, error_size, "failed to create raw ICMP socket: %s. Try sudo or CAP_NET_RAW.", strerror(errno));
        return -1;
    }
    return sockfd;
}

/* timestamp payload를 포함한 ICMP Echo Request 하나를 전송한다. */
int send_icmp_echo(int sockfd, const struct sockaddr_in *addr, uint16_t ident, uint16_t seq, char *error, size_t error_size)
{
    unsigned char packet[sizeof(struct icmphdr) + ICMP_PAYLOAD_SIZE];
    struct icmphdr *icmp = (struct icmphdr *)packet;
    struct timespec now;

    memset(packet, 0, sizeof(packet));
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = htons(ident);
    icmp->un.echo.sequence = htons(seq);

    clock_gettime(CLOCK_MONOTONIC, &now);
    // 시스템 시각 변경에 영향받지 않도록 monotonic 전송 시각을 payload에 넣는다.
    memcpy(packet + sizeof(*icmp), &now, sizeof(now));
    for (size_t i = sizeof(now); i < ICMP_PAYLOAD_SIZE; i++) {
        packet[sizeof(*icmp) + i] = (unsigned char)(i & 0xffU);
    }

    icmp->checksum = 0;
    icmp->checksum = icmp_checksum(packet, sizeof(packet));

    if (sendto(sockfd, packet, sizeof(packet), 0, (const struct sockaddr *)addr, sizeof(*addr)) < 0) {
        snprintf(error, error_size, "sendto failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/* 전체 타임아웃 구간에서 이미 소비한 시간을 제외한 남은 대기 시간을 계산한다. */
static int remaining_timeout_ms(struct timespec start, int timeout_ms)
{
    struct timespec now;
    double elapsed;

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = monotonic_diff_ms(start, now);
    if (elapsed >= timeout_ms) {
        return 0;
    }
    return timeout_ms - (int)elapsed;
}

/* Time Exceeded payload에 포함된 원본 Echo Request가 현재 probe인지 확인한다. */
static bool parse_original_echo(const unsigned char *data, ssize_t len, uint16_t ident, uint16_t seq)
{
    const struct iphdr *inner_ip;
    const struct icmphdr *inner_icmp;
    int inner_ihl;

    if (len < (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
        return false;
    }
    inner_ip = (const struct iphdr *)data;
    inner_ihl = inner_ip->ihl * 4;
    if (inner_ihl < (int)sizeof(struct iphdr) || len < inner_ihl + (int)sizeof(struct icmphdr)) {
        return false;
    }
    // Time Exceeded 응답은 원본 IPv4 패킷을 인용한다. 포함된 Echo 헤더를
    // 맞춰 보아야 다른 traceroute 트래픽을 잘못 받아들이지 않는다.
    inner_icmp = (const struct icmphdr *)(data + inner_ihl);
    return inner_icmp->type == ICMP_ECHO &&
           ntohs(inner_icmp->un.echo.id) == ident &&
           ntohs(inner_icmp->un.echo.sequence) == seq;
}

/* Ping 응답 대기 시간이 끝났음을 결과에 기록한다. */
static void set_ping_timeout(PingResult *ping)
{
    ping->status = PING_STATUS_TIMEOUT;
    snprintf(ping->error, sizeof(ping->error), "timeout");
}

/* Trace 응답 대기 시간이 끝났음을 결과에 기록한다. */
static void set_trace_timeout(TraceResult *trace)
{
    trace->status = TRACE_STATUS_TIMEOUT;
    snprintf(trace->error, sizeof(trace->error), "timeout");
}

/* Ping 수신 중 발생한 시스템 호출 오류를 결과에 기록한다. */
static void set_ping_error(PingResult *ping, const char *context)
{
    ping->status = PING_STATUS_ERROR;
    snprintf(ping->error, sizeof(ping->error), "%s failed: %s", context, strerror(errno));
}

/* Trace 수신 중 발생한 시스템 호출 오류를 결과에 기록한다. */
static void set_trace_error(TraceResult *trace, const char *context)
{
    trace->status = TRACE_STATUS_ERROR;
    snprintf(trace->error, sizeof(trace->error), "%s failed: %s", context, strerror(errno));
}

/* 남은 타임아웃 안에서 raw 소켓이 읽기 가능해질 때까지 기다린다. */
static int wait_for_readable_socket(int sockfd, int remain_ms)
{
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);
    tv.tv_sec = remain_ms / 1000;
    tv.tv_usec = (remain_ms % 1000) * 1000;

    for (;;) {
        int selected = select(sockfd + 1, &readfds, NULL, NULL, &tv);
        if (selected < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        return selected;
    }
}

/* EINTR를 흡수하며 raw 소켓에서 패킷 하나를 읽는다. */
static int recv_icmp_packet(int sockfd, IcmpPacket *packet)
{
    socklen_t from_len = sizeof(packet->from);

    memset(packet, 0, sizeof(*packet));
    for (;;) {
        packet->length = recvfrom(sockfd, packet->buffer, sizeof(packet->buffer), 0, (struct sockaddr *)&packet->from, &from_len);
        if (packet->length < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        return 0;
    }
}

/* Linux raw ICMP 수신 버퍼에서 바깥쪽 IPv4 헤더와 ICMP 헤더를 분리한다. */
static bool parse_outer_ipv4_icmp(IcmpPacket *packet)
{
    if (packet->length < (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
        return false;
    }

    // Linux raw ICMP 소켓은 ICMP message 앞에 바깥쪽 IPv4 헤더를 함께 준다.
    // 따라서 ICMP offset은 IPv4 헤더 길이에서 계산해야 한다.
    packet->ip = (const struct iphdr *)packet->buffer;
    packet->ip_header_len = packet->ip->ihl * 4;
    if (packet->ip_header_len < (int)sizeof(struct iphdr) ||
        packet->length < packet->ip_header_len + (int)sizeof(struct icmphdr)) {
        return false;
    }

    packet->icmp = (const struct icmphdr *)(packet->buffer + packet->ip_header_len);
    if (inet_ntop(AF_INET, &packet->from.sin_addr, packet->remote_ip, sizeof(packet->remote_ip)) == NULL) {
        packet->remote_ip[0] = '\0';
    }
    return true;
}

/* Echo Reply의 identifier와 sequence가 현재 요청과 일치하는지 확인한다. */
static bool is_matching_echo_reply(const IcmpPacket *packet, uint16_t ident, uint16_t seq)
{
    return packet->icmp->type == ICMP_ECHOREPLY &&
           ntohs(packet->icmp->un.echo.id) == ident &&
           ntohs(packet->icmp->un.echo.sequence) == seq;
}

/* TTL 만료 응답이 현재 Trace probe에 대한 응답인지 확인한다. */
static bool is_matching_time_exceeded(const IcmpPacket *packet, uint16_t ident, uint16_t seq)
{
    const unsigned char *inner;
    ssize_t inner_len;

    if (packet->icmp->type != ICMP_TIME_EXCEEDED) {
        return false;
    }
    inner = (const unsigned char *)(packet->icmp + 1);
    inner_len = packet->length - packet->ip_header_len - (ssize_t)sizeof(*packet->icmp);
    return parse_original_echo(inner, inner_len, ident, seq);
}

/* Echo Reply payload에 담긴 전송 시각으로 RTT를 계산한다. */
static double echo_reply_rtt_ms(const IcmpPacket *packet)
{
    const unsigned char *payload = (const unsigned char *)(packet->icmp + 1);
    ssize_t payload_len = packet->length - packet->ip_header_len - (ssize_t)sizeof(*packet->icmp);
    struct timespec sent_at;
    struct timespec now;

    if (payload_len < (ssize_t)sizeof(sent_at)) {
        return 0.0;
    }
    memcpy(&sent_at, payload, sizeof(sent_at));
    clock_gettime(CLOCK_MONOTONIC, &now);
    return monotonic_diff_ms(sent_at, now);
}

/* 타임아웃 구간 안에서 현재 ident/seq에 대응되는 ICMP 패킷만 골라낸다. */
static int receive_matching_packet(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, struct timespec wait_start, IcmpPacket *packet, bool trace_mode, bool *time_exceeded)
{
    for (;;) {
        int remain = remaining_timeout_ms(wait_start, timeout_ms);
        if (remain <= 0) {
            return 0;
        }

        int selected = wait_for_readable_socket(sockfd, remain);
        if (selected < 0) {
            return -1;
        }
        if (selected == 0) {
            continue;
        }

        if (recv_icmp_packet(sockfd, packet) != 0) {
            return -2;
        }
        // raw 소켓은 다른 probe나 다른 프로세스의 패킷도 받을 수 있다.
        // 현재 요청의 identifier/sequence가 맞을 때까지 계속 기다린다.
        if (!parse_outer_ipv4_icmp(packet)) {
            continue;
        }
        if (is_matching_echo_reply(packet, ident, seq)) {
            *time_exceeded = false;
            return 1;
        }
        if (trace_mode && is_matching_time_exceeded(packet, ident, seq)) {
            *time_exceeded = true;
            return 1;
        }
    }
}

/* Ping 모드에서 사용할 Echo Reply를 기다리고 PingResult로 변환한다. */
int receive_ping_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, PingResult *ping)
{
    struct timespec wait_start;
    IcmpPacket packet;
    bool time_exceeded = false;

    clock_gettime(CLOCK_MONOTONIC, &wait_start);
    int rc = receive_matching_packet(sockfd, ident, seq, timeout_ms, wait_start, &packet, false, &time_exceeded);
    if (rc == 0) {
        set_ping_timeout(ping);
        return 0;
    }
    if (rc < 0) {
        set_ping_error(ping, rc == -1 ? "select" : "recvfrom");
        return -1;
    }

    ping->status = PING_STATUS_SUCCESS;
    ping->rtt_ms = echo_reply_rtt_ms(&packet);
    ping->ttl = packet.ip->ttl;
    snprintf(ping->remote_ip, sizeof(ping->remote_ip), "%s", packet.remote_ip);
    return 0;
}

/* Trace 모드에서 hop 또는 destination 응답을 기다리고 TraceResult로 변환한다. */
int receive_trace_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, TraceResult *trace)
{
    struct timespec wait_start;
    IcmpPacket packet;
    bool time_exceeded = false;

    clock_gettime(CLOCK_MONOTONIC, &wait_start);
    int rc = receive_matching_packet(sockfd, ident, seq, timeout_ms, wait_start, &packet, true, &time_exceeded);
    if (rc == 0) {
        set_trace_timeout(trace);
        return 0;
    }
    if (rc < 0) {
        set_trace_error(trace, rc == -1 ? "select" : "recvfrom");
        return -1;
    }

    if (time_exceeded) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        trace->status = TRACE_STATUS_HOP;
        trace->rtt_ms = monotonic_diff_ms(wait_start, now);
        snprintf(trace->remote_ip, sizeof(trace->remote_ip), "%s", packet.remote_ip);
        return 0;
    }

    trace->status = TRACE_STATUS_DESTINATION;
    trace->destination_reached = true;
    trace->rtt_ms = echo_reply_rtt_ms(&packet);
    snprintf(trace->remote_ip, sizeof(trace->remote_ip), "%s", packet.remote_ip);
    return 0;
}

/* 이전 호출 형태를 유지하기 위해 결과 포인터 종류에 따라 수신 함수를 위임한다. */
int receive_icmp_response(int sockfd, uint16_t ident, uint16_t seq, int timeout_ms, PingResult *ping, TraceResult *trace)
{
    if (ping != NULL) {
        return receive_ping_response(sockfd, ident, seq, timeout_ms, ping);
    }
    if (trace != NULL) {
        return receive_trace_response(sockfd, ident, seq, timeout_ms, trace);
    }
    return TRACEPING_ERR_GENERAL;
}
