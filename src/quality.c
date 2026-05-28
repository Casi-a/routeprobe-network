/*
 * quality.c
 * Ping 통계를 GOOD/NORMAL/POOR 네트워크 품질 label로 변환한다.
 */
#include "quality.h"

/* 두 품질 등급 중 더 나쁜 등급을 반환한다. */
static QualityLevel max_quality(QualityLevel a, QualityLevel b)
{
    return a > b ? a : b;
}

/* 패킷 손실률, 평균 RTT, jitter를 각각 평가한 뒤 최악 등급을 반환한다. */
QualityLevel evaluate_quality(const PingStats *stats)
{
    QualityLevel overall = QUALITY_GOOD;
    QualityLevel loss;
    QualityLevel avg;
    QualityLevel jitter;

    if (!stats->has_rtt) {
        return QUALITY_POOR;
    }

    if (stats->loss_percent == 0.0) {
        loss = QUALITY_GOOD;
    } else if (stats->loss_percent <= 5.0) {
        loss = QUALITY_NORMAL;
    } else {
        loss = QUALITY_POOR;
    }

    if (stats->avg_ms <= 50.0) {
        avg = QUALITY_GOOD;
    } else if (stats->avg_ms <= 150.0) {
        avg = QUALITY_NORMAL;
    } else {
        avg = QUALITY_POOR;
    }

    if (!stats->has_jitter || stats->jitter_ms <= 10.0) {
        jitter = QUALITY_GOOD;
    } else if (stats->jitter_ms <= 30.0) {
        jitter = QUALITY_NORMAL;
    } else {
        jitter = QUALITY_POOR;
    }

    overall = max_quality(overall, loss);
    overall = max_quality(overall, avg);
    overall = max_quality(overall, jitter);
    return overall;
}

/* QualityLevel을 요약에 출력할 사용자용 라벨로 변환한다. */
const char *quality_to_string(QualityLevel quality)
{
    switch (quality) {
    case QUALITY_GOOD:
        return "GOOD";
    case QUALITY_NORMAL:
        return "NORMAL";
    case QUALITY_POOR:
        return "POOR";
    }
    return "UNKNOWN";
}
