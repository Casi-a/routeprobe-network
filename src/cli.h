/*
 * cli.h
 * traceping의 CLI 파싱과 사용법 출력 공개 인터페이스를 제공한다.
 */
#ifndef TRACEPING_CLI_H
#define TRACEPING_CLI_H

#include "common.h"

/* argv 값을 적용하기 전에 TracePingConfig의 기본값을 채운다. */
void config_init_defaults(TracePingConfig *config);

/* argv를 설정으로 변환하고 잘못된 입력은 오류 메시지와 함께 사용법 오류로 반환한다. */
int parse_cli(int argc, char **argv, TracePingConfig *config, char *error, size_t error_size);

/* CLI 옵션 표에서 생성한 지원 옵션 목록을 출력한다. */
void print_usage(const char *program);

/* 빌드에 포함된 traceping 버전 문자열을 출력한다. */
void print_version(void);

#endif
