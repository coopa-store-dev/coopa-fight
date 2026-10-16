# tests/Makefile —— 宿主测试(纯 C 的部分)。make -C tests:全部编译(AddressSanitizer + UBSan)并运行。
CC ?= cc
CFLAGS := -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
          -I../src -I.
OUT := .build
HDRS := $(wildcard ../src/*.h) $(wildcard *.h)
TESTS := test_moves test_sim test_super test_game test_link test_bot test_local test_balance test_save test_art test_peer_glue

# 语音解码测试要外壳的解码器:make -C tests COOPA_SHELL=外壳目录(没给就跳过)
ifneq ($(COOPA_SHELL),)
TESTS += test_lines
endif

all: $(addprefix $(OUT)/,$(TESTS))
	@for t in $(TESTS); do $(OUT)/$$t || exit 1; done
	@echo "host tests: PASS"

$(OUT):
	mkdir -p $@

$(OUT)/test_moves: test_moves.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_sim: test_sim.c ../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_super: test_super.c ../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

GAME := duo.c ../src/fight_game.c ../src/fight_msg.c ../src/fight_sim.c ../src/fight_moves.c

$(OUT)/test_game: test_game.c $(GAME) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_link: test_link.c $(GAME) $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_bot: test_bot.c ../src/fight_bot.c ../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_local: test_local.c ../src/fight_local.c ../src/fight_bot.c ../src/fight_game.c ../src/fight_msg.c \
		../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# Mac 冒充对手的 C 接口:peer_glue.c 自己包含了其余的规则源文件(link_peer.py 只编 fight_game.c 和它)
$(OUT)/test_peer_glue: test_peer_glue.c peer_glue.c duo.c ../src/fight_game.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

$(OUT)/test_save: test_save.c ../src/fight_save.c $(HDRS) | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# 常量图:宿主上没有 LVGL,用 stub/lvgl.h(只有 lv_image_dsc_t)
$(OUT)/test_art: test_art.c ../src/fight_art.c $(HDRS) stub/lvgl.h | $(OUT)
	$(CC) $(CFLAGS) -Istub $(filter %.c,$^) -o $@

$(OUT)/test_lines: test_lines.c ../src/fight_lines.c $(COOPA_SHELL)/main/kit_voice_dsp.c | $(OUT)
	$(CC) $(CFLAGS) -I$(COOPA_SHELL)/main $(filter %.c,$^) -o $@

# 数值统计跑上万场:开优化、不开 sanitizer(规则在别的测试里已经在 ASan 下跑过)。场数:make -C tests BALANCE_N=1000
$(OUT)/test_balance: test_balance.c ../src/fight_bot.c ../src/fight_sim.c ../src/fight_moves.c $(HDRS) | $(OUT)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -I../src -I. $(filter %.c,$^) -o $@

.PHONY: all
