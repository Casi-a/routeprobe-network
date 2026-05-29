/*
 * quality.h
 * 계산된 Ping 통계를 기반으로 네트워크 품질 등급을 평가한다.
 */
#ifndef ROUTEPROBE_QUALITY_H
#define ROUTEPROBE_QUALITY_H

#include "common.h"

typedef enum {
    QUALITY_GOOD = 0,
    QUALITY_NORMAL = 1,
    QUALITY_POOR = 2
} QualityLevel;

/* 손실률, 평균 RTT, jitter가 암시하는 등급 중 가장 나쁜 등급을 반환한다. */
QualityLevel evaluate_quality(const PingStats *stats);

/* 품질 enum을 사용자에게 보여줄 대문자 라벨로 변환한다. */
const char *quality_to_string(QualityLevel quality);

#endif
