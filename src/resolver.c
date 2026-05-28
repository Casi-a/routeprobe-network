/*
 * resolver.c
 * 숫자 IPv4 주소와 domain name을 raw ICMP sendto에 사용할 sockaddr_in
 * target으로 해석한다.
 */
#include "resolver.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

/* 대상을 첫 번째 사용 가능한 IPv4 addrinfo 결과로 해석한다. */
int resolve_target_ipv4(const char *target, ResolvedTarget *resolved, char *error, size_t error_size)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *current;
    int rc;

    memset(resolved, 0, sizeof(*resolved));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;

    rc = getaddrinfo(target, NULL, &hints, &result);
    if (rc != 0) {
        snprintf(error, error_size, "failed to resolve %s: %s", target, gai_strerror(rc));
        return TRACEPING_ERR_DNS;
    }

    // 첫 addrinfo가 항상 사용 가능하다고 가정하지 않고 방어적으로 순회한다.
    for (current = result; current != NULL; current = current->ai_next) {
        if (current->ai_family != AF_INET || current->ai_addrlen < sizeof(resolved->addr)) {
            continue;
        }

        memcpy(&resolved->addr, current->ai_addr, sizeof(resolved->addr));
        resolved->addr.sin_port = 0;
        if (inet_ntop(AF_INET, &resolved->addr.sin_addr, resolved->ip, sizeof(resolved->ip)) == NULL) {
            snprintf(error, error_size, "failed to format resolved IPv4 address");
            freeaddrinfo(result);
            return TRACEPING_ERR_DNS;
        }

        freeaddrinfo(result);
        return TRACEPING_OK;
    }

    snprintf(error, error_size, "failed to resolve %s: no IPv4 address found", target);
    freeaddrinfo(result);
    return TRACEPING_ERR_DNS;
}
