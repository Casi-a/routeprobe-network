# Makefile
# traceping 실행 파일과 테스트 바이너리를 빌드하고 검증 대상을 제공한다.

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -MMD -MP
LDFLAGS ?=
LDLIBS ?= -lm

BIN := traceping
TEST_BIN := tests/test_traceping

SRC := \
	src/baseline.c \
	src/cli.c \
	src/csv_output.c \
	src/icmp.c \
	src/mtr.c \
	src/ping.c \
	src/quality.c \
	src/resolver.c \
	src/runtime.c \
	src/stats.c \
	src/terminal_output.c \
	src/timeutil.c \
	src/trace.c

OBJ := $(SRC:.c=.o)
MAIN_OBJ := src/main.o
TEST_OBJ := tests/test_traceping.o
DEP := $(OBJ:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

.PHONY: all test integration-test verify clean

all: $(BIN)

$(BIN): $(MAIN_OBJ) $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(TEST_OBJ) $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

integration-test: $(BIN)
	./tests/run_integration.sh

verify: test integration-test

clean:
	rm -f $(BIN) $(TEST_BIN) $(MAIN_OBJ) $(OBJ) $(TEST_OBJ) $(DEP) src/*.o src/*.d tests/*.d

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c -o $@ $<

-include $(DEP)
