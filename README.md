# TracePing

`traceping`은 C로 만든 Linux용 네트워크 진단 CLI이다. Ping, Trace, MTR 스타일 측정을 지원하고, 정상 기준선과 현재 상태를 비교해 한국어 Markdown 리포트를 생성한다.

## 핵심 기능

- **Ping**: RTT, packet loss, jitter, p95, 품질 등급 확인
- **Trace**: TTL 기반 경로 추적
- **MTR**: 여러 cycle 동안 hop별 RTT, jitter, ICMP 무응답률 누적
- **Baseline**: 정상 상태 MTR 결과 저장 후 현재 상태와 비교
- **Report**: 목적지 도달성, RTT, jitter 중심의 장애 분석 리포트 생성
- **CSV**: Ping, Trace, MTR 결과 저장
- **Target**: IPv4 주소와 도메인 이름 지원

## 빌드

```sh
make
```

raw ICMP 소켓을 사용하므로 실행에는 보통 `sudo` 또는 `CAP_NET_RAW` 권한이 필요하다.

```sh
sudo ./traceping 8.8.8.8
```

## 사용 예시

기본 Ping:

```sh
sudo ./traceping 8.8.8.8
sudo ./traceping google.com --c 10 --i 500 --to 1500
```

Ping RTT 그래프:

```sh
sudo ./traceping 8.8.8.8 --graph
```

Trace:

```sh
sudo ./traceping google.com --trace
sudo ./traceping google.com --tr --mh 20 --st
```

MTR:

```sh
sudo ./traceping google.com --mtr --count 10
sudo ./traceping google.com --mt --c 10 --mh 30
```

CSV 저장:

```sh
sudo ./traceping google.com --mt --c 10 --output mtr.csv
```

기준선 저장:

```sh
sudo ./traceping google.com --mt --c 10 --baseline-save office-google.tsv
```

기준선 비교 및 리포트 생성:

```sh
sudo ./traceping google.com --mt --c 10 \
  --baseline-compare office-google.tsv \
  --report incident.md
```

## MTR 진단 방식

MTR 모드는 hop별 ICMP 무응답률과 RTT를 보여주지만, 중간 hop의 ICMP 무응답을 바로 packet loss로 단정하지 않는다. 라우터 정책, 필터링, rate limit 때문에 중간 hop이 응답하지 않을 수 있기 때문이다.

TracePing의 리포트는 다음을 우선해서 진단한다.

1. 목적지에 도달했는가
2. 목적지 ICMP 무응답률이 낮은가
3. 목적지 평균 RTT가 안정적인가
4. 목적지 jitter가 안정적인가
5. 기준선 대비 목적지 품질이 악화됐는가

그래서 중간 hop 변화가 있어도 목적지 RTT와 jitter가 안정적이면 정상으로 판단하고, 반대로 packet loss가 없어도 RTT와 jitter가 크게 나빠지면 품질 저하로 판단한다.

## 주요 옵션

| 옵션 | 설명 |
| --- | --- |
| `--count`, `--c` | Ping 패킷 수 또는 MTR cycle 수 |
| `--interval`, `--i` | packet/cycle 사이 대기 시간 |
| `--timeout`, `--to` | probe 응답 대기 시간 |
| `--trace`, `--tr` | Trace 모드 |
| `--mtr`, `--mt` | MTR 모드 |
| `--max-hop`, `--mh` | Trace/MTR 최대 hop |
| `--show-timeouts`, `--st` | Trace timeout hop 표시 |
| `--output`, `--o` | CSV 저장 |
| `--baseline-save`, `--bs` | MTR 기준선 저장 |
| `--baseline-compare`, `--bc` | MTR 기준선 비교 |
| `--report`, `--rp` | Markdown 리포트 생성 |
| `--graph`, `--g` | Ping RTT 그래프 표시 |