# C-trace-ping

`traceping`은 C로 작성된 Linux CLI 네트워크 진단 도구이다. raw ICMP Echo Request를 직접 보내 Ping, Trace, MTR 스타일 hop 품질 측정, 기준선 저장/비교, Markdown 장애 분석 리포트 생성을 수행한다.

## 주요 기능

- Ping 모드: RTT, packet loss, jitter, p95, 품질 등급 출력
- Trace 모드: TTL 증가 기반 경로 추적
- MTR 모드: hop별 ICMP 무응답률, RTT 분포, jitter 누적
- 기준선 저장/비교: 정상 상태와 현재 MTR 결과 비교
- Markdown 리포트: 현재 MTR 표, 기준선 표, 회귀 분석 결과 저장
- CSV 출력: Ping, Trace, MTR 결과 저장
- 도메인 또는 IPv4 주소 대상 지원

## 요구사항

- Linux
- C11 컴파일러 (`cc`, `gcc` 등)
- `make`
- raw ICMP 권한

raw ICMP 소켓은 일반적으로 `sudo` 또는 `CAP_NET_RAW` 권한이 필요하다.

```sh
sudo ./traceping 8.8.8.8
```

또는:

```sh
sudo setcap cap_net_raw+ep ./traceping
./traceping 8.8.8.8
```

## 빌드

```sh
make
```

실행 파일:

```text
./traceping
```

빌드 산출물 정리:

```sh
make clean
```

## 기본 사용법

```sh
./traceping <target> [options]
```

`target`은 IPv4 주소 또는 IPv4로 해석 가능한 도메인 이름이다.

예시:

```sh
sudo ./traceping 8.8.8.8
sudo ./traceping google.com --count 10
sudo ./traceping google.com --c 10
sudo ./traceping google.com --interval 500 --timeout 1500
sudo ./traceping google.com --graph
sudo ./traceping google.com --output ping.csv
sudo ./traceping google.com --trace
sudo ./traceping google.com --tr
sudo ./traceping google.com --trace --max-hop 20 --output trace.csv
sudo ./traceping google.com --trace --trace-attempts 5
sudo ./traceping google.com --trace --show-timeouts
sudo ./traceping google.com --mtr --count 10
sudo ./traceping google.com --mt --c 10 --mh 20 --output mtr.csv
sudo ./traceping google.com --mt --c 10 --baseline-save office-google.tsv
sudo ./traceping google.com --mt --c 10 --baseline-compare office-google.tsv --report incident.md
```

## 옵션

| 옵션 | 설명 | 기본값 | 범위 |
| --- | --- | ---: | --- |
| `--count <n>`, `--c <n>` | Ping 패킷 수 또는 MTR cycle 수 | `4` | `1..100000` |
| `--interval <ms>`, `--i <ms>` | Ping 패킷 또는 MTR cycle 사이 대기 시간 | `1000` | `10..60000` |
| `--timeout <ms>`, `--to <ms>` | 각 probe 응답 대기 시간 | `1000` | `1..60000` |
| `--trace`, `--tr` | TTL 기반 Trace 모드 실행 | off | |
| `--mtr`, `--mt` | MTR 스타일 hop 품질 모드 실행 | off | |
| `--max-hop <n>`, `--mh <n>` | Trace/MTR 최대 hop | `30` | `1..255` |
| `--trace-attempts <n>`, `--ta <n>` | Trace hop당 재시도 횟수 | `3` | `1..10` |
| `--show-timeouts`, `--st` | Trace 모드에서 무응답 hop 출력 | off | |
| `--output <file>`, `--o <file>` | CSV 파일 저장 | 없음 | |
| `--baseline-save <file>`, `--bs <file>` | MTR 결과를 기준선으로 저장 | 없음 | |
| `--baseline-compare <file>`, `--bc <file>` | MTR 결과를 기준선과 비교 | 없음 | |
| `--report <file>`, `--rp <file>` | MTR 장애 분석 리포트 저장 | 없음 | |
| `--graph`, `--g` | Ping RTT 그래프 출력 | off | |
| `--help`, `--h` | 사용법 출력 | | |
| `--version`, `--v` | 버전 출력 | | |

잘못된 옵션, 누락된 옵션 값, 범위를 벗어난 숫자, target 누락, target 중복은 사용법 오류로 처리된다. 기준선과 리포트 옵션은 `--mtr`와 함께 사용해야 하며, `--trace`와 `--mtr`는 동시에 사용할 수 없다.

## Ping 모드

Ping 모드는 기본 실행 모드이다.

1. target을 IPv4로 해석한다.
2. raw ICMP 소켓을 연다.
3. sequence 번호별 ICMP Echo Request를 보낸다.
4. `--timeout` 안에 일치하는 Echo Reply를 기다린다.
5. 각 결과를 출력한다.
6. `--interval`만큼 대기한다.
7. 전체 통계와 품질 등급을 출력한다.

예시:

```text
PING 8.8.8.8 (8.8.8.8): 4 packets
[1] reply from 8.8.8.8: time=23.4 ms ttl=117
[2] timeout
```

그래프는 기본적으로 꺼져 있으며 `--graph` 또는 `--g`로 켠다.

```sh
sudo ./traceping 8.8.8.8 --graph
```

그래프는 ASCII `#` 문자를 사용하며, timeout은 `x`로 표시한다.

## Ping 통계와 품질 등급

Ping 종료 후 다음 항목을 출력한다.

- 전송 패킷 수
- 수신 패킷 수
- packet loss
- RTT min/avg/max
- 표준편차
- jitter
- p95
- 품질 등급 (`GOOD`, `NORMAL`, `POOR`)

예시:

```text
--- ping statistics ---
4 packets transmitted, 4 received, 0.0% packet loss
rtt min/avg/max = 22.1/24.0/27.4 ms
stddev = 2.0 ms
jitter = 2.5 ms
p95 = 27.4 ms

Network quality: GOOD
Reason:
- Packet loss: 0.0%
- Average RTT: 24.0 ms
- Jitter: 2.5 ms
```

품질 등급은 packet loss, 평균 RTT, jitter 중 가장 나쁜 등급을 전체 등급으로 사용한다.

| 지표 | GOOD | NORMAL | POOR |
| --- | ---: | ---: | ---: |
| Packet loss | `0%` | `> 0%` and `<= 5%` | `> 5%` |
| Average RTT | `<= 50ms` | `> 50ms` and `<= 150ms` | `> 150ms` |
| Jitter | `<= 10ms` 또는 없음 | `> 10ms` and `<= 30ms` | `> 30ms` |

## Trace 모드

Trace 모드는 TTL 값을 1부터 증가시키며 목적지까지의 경로를 추적한다.

```sh
sudo ./traceping 8.8.8.8 --trace
```

동작:

1. 현재 hop 번호를 `IP_TTL`로 설정한다.
2. ICMP Echo Request를 보낸다.
3. `ICMP_TIME_EXCEEDED`는 중간 hop으로 처리한다.
4. `ICMP_ECHOREPLY`는 목적지 도달로 처리한다.
5. timeout이면 같은 hop을 `--trace-attempts`만큼 재시도한다.
6. 목적지 도달 또는 `--max-hop` 도달 시 종료한다.

기본적으로 무응답 중간 hop은 출력하지 않는다. 원시 trace에 가깝게 보려면 `--show-timeouts`를 사용한다.

```text
Trace route to 8.8.8.8 (8.8.8.8), 30 hops max

1   192.168.0.1     1.2 ms
2   10.20.0.1       5.8 ms
4   8.8.8.8         24.6 ms

Trace complete.
```

## MTR 모드

MTR 모드는 TTL sweep을 여러 cycle 반복해 hop별 품질을 누적한다.

```sh
sudo ./traceping 8.8.8.8 --mtr --count 10
```

장시간 실행 중에는 cycle이 끝날 때마다 ASCII 네모 `[]`를 출력한다.

```text
MTR 8.8.8.8 (8.8.8.8): 10 cycles, 30 hops max
Progress: [][][][][][][][][][] done
```

MTR 표의 `NoRep%`는 ICMP 무응답률이다. 특히 중간 hop의 ICMP 무응답은 라우터 정책, 필터링, rate limit 때문에 발생할 수 있으므로 확정적인 패킷 손실로 단정하지 않는다. downstream hop과 destination 결과를 함께 봐야 한다.

예시:

```text
Hop Host             NoRep% Sent  Recv    Last     Avg    Best   Worst  Jitter Status
1   192.168.0.1       0.0%    10    10     1.2     1.4     1.0     2.1     0.3 hop
2   10.20.0.1        10.0%    10     9     5.8     6.1     5.4     8.0     0.7 hop
3   *               100.0%    10     0       -       -       -       -       - timeout
4   8.8.8.8           0.0%    10    10    24.6    25.0    23.8    29.1     1.2 destination
```

## 기준선 비교와 리포트

정상 상태를 기준선으로 저장한다.

```sh
sudo ./traceping google.com --mt --c 10 --baseline-save office-google.tsv
```

이후 현재 경로와 비교한다.

```sh
sudo ./traceping google.com --mt --c 10 --baseline-compare office-google.tsv
```

비교 항목:

- hop 주소 변경
- 현재 결과에서 사라진 hop
- 새로 나타난 hop
- ICMP 무응답률 5%p 이상 증가
- 평균 RTT 20ms 이상 증가

Markdown 장애 분석 리포트도 생성할 수 있다.

```sh
sudo ./traceping google.com --mt --c 10 --baseline-compare office-google.tsv --report incident.md
```

리포트에는 다음이 포함된다.

- 실행 메타데이터
- 목적지 도달성, RTT, jitter 중심의 품질 진단
- 기준선 대비 변경 사항
- 변경 사항 해석
- 마지막 참고 자료 섹션의 기준선 MTR 표
- 마지막 참고 자료 섹션의 현재 MTR 표
- 중간 hop ICMP 무응답 해석 주의 문구

품질 진단은 중간 hop의 ICMP 무응답이나 경로 변경만으로 장애를 단정하지 않는다. 목적지에 안정적으로 도달하고 목적지 기준 RTT와 jitter가 안정적이면 전체 품질은 정상으로 판정한다.

리포트는 한국어로 작성된다.

## CSV 출력

`--output <file>`을 사용하면 CSV 파일을 저장한다. 기존 파일은 덮어쓴다.

Ping CSV:

```csv
seq,timestamp,target,remote_ip,rtt_ms,status,ttl,error
```

Trace CSV:

```csv
hop,timestamp,target,remote_ip,rtt_ms,status,error
```

MTR CSV:

```csv
hop,timestamp,target,remote_ip,no_reply_percent,sent,received,last_ms,avg_ms,best_ms,worst_ms,jitter_ms,status,error
```

CSV 규칙:

- `timestamp`는 local time `%Y-%m-%dT%H:%M:%S%z` 형식이다.
- RTT 계열 값은 소수점 첫째 자리까지 기록한다.
- 값이 없으면 빈 필드로 기록한다.
- 쉼표, 따옴표, 줄바꿈이 포함된 필드는 CSV 규칙에 따라 따옴표 처리한다.

## 프로그램 구조

```text
src/
  main.c               진입점, Ping/Trace/MTR 모드 선택
  cli.c/.h             명령행 파싱, 기본값, help, version
  common.h             공통 상수, 종료 코드, 설정, 결과 구조체
  resolver.c/.h        getaddrinfo 기반 IPv4 해석
  icmp.c/.h            raw socket, ICMP checksum, 송수신
  runtime.c/.h         target/socket/CSV 준비와 정리
  ping.c/.h            Ping 모드 실행 흐름
  trace.c/.h           Trace 모드 TTL 처리
  mtr.c/.h             MTR 모드 hop 품질 누적
  baseline.c/.h        기준선 저장/비교, Markdown 리포트
  stats.c/.h           packet loss, RTT 통계, jitter, p95
  quality.c/.h         GOOD/NORMAL/POOR 품질 판정
  terminal_output.c/.h 터미널 출력과 그래프
  csv_output.c/.h      CSV escaping, header, row
  timeutil.c/.h        monotonic time, sleep, timestamp

tests/
  test_traceping.c     단위 테스트
  run_integration.sh   sudo 권한 loopback 통합 테스트
```

## 테스트

단위 테스트:

```sh
make test
```

sudo raw ICMP 통합 테스트까지 포함한 전체 검증:

```sh
TRACEPING_TEST_PASSWORD=... make verify
```

통합 테스트는 비밀번호를 `TRACEPING_TEST_PASSWORD` 환경변수에서만 읽는다. 비밀번호를 저장소에 저장하지 않는다.

검증 항목:

- Ping loopback
- 도메인 대상 Ping
- Trace loopback
- MTR loopback
- MTR CSV
- 기준선 저장
- 기준선 비교
- Markdown 리포트 생성

## 종료 코드

| 코드 | 이름 | 의미 |
| ---: | --- | --- |
| `0` | `TRACEPING_OK` | 정상 종료 |
| `1` | `TRACEPING_ERR_GENERAL` | 일반 실행 오류 |
| `2` | `TRACEPING_ERR_USAGE` | CLI 사용법 오류 |
| `3` | `TRACEPING_ERR_SOCKET` | raw socket 또는 권한 오류 |
| `4` | `TRACEPING_ERR_DNS` | DNS 또는 IPv4 해석 오류 |
| `5` | `TRACEPING_ERR_IO` | 파일 I/O 오류 |

개별 packet timeout은 프로그램 실패로 처리하지 않는다.

## 제한사항

- IPv4만 지원
- Linux raw socket 기반
- TCP/UDP probe 미지원
- IPv6 미지원
- 방화벽, 라우팅, ICMP 필터링, 라우터 rate limit에 따라 Trace/MTR 결과가 달라질 수 있음
