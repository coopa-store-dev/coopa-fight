# tests/Makefile —— 宿主测试(纯 C 的部分)。make -C tests:全部编译(AddressSanitizer + UBSan)并运行。
CC ?= cc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
          -I../src -I.
OUT := .build
HDRS := $(wildcard ../src/*.h) $(wildcard *.h)
TESTS := test_moves test_sim

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	@echo "host tests: PASS"

$(OUT):
	mkdir -p $@

$(OUT)/test_moves: test_moves.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_sim: test_sim.c ../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

.PHONY: all
