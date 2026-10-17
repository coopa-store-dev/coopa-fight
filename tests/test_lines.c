// tests/test_lines.c —— 全部语音(四个角色 8 种 + 裁判)都能用外壳的解码器完整解出来,样本数对得上,每句 0.2–2.5 秒。
// 要外壳的 main/kit_voice_dsp.c:make -C tests COOPA_SHELL=外壳目录(没给就跳过)。
#include "fight_lines.h"
#include "kit_voice_dsp.h"

#include <assert.h>
#include <stdio.h>

static void check(const kit_line_t *l) {
    vdsp_adpcm_t d;
    vdsp_adpcm_init(&d, l->adpcm, l->samples);
    int16_t buf[256];
    uint32_t n = 0;
    size_t got;
    while ((got = vdsp_adpcm_read(&d, buf, 256)) > 0) n += (uint32_t)got;
    assert(n == l->samples && n > 16000 / 5 && n <= 16000 * 5 / 2);
}

int main(void) {
    const kit_line_t *sets[] = { fight_lines_pick,  fight_lines_special, fight_lines_super, fight_lines_win,
                                 fight_lines_heavy, fight_lines_hurt,    fight_lines_down,  fight_lines_parry };
    for (size_t k = 0; k < sizeof sets / sizeof sets[0]; k++)
        for (int f = 0; f < 4; f++) check(&sets[k][f]);
    for (int c = 0; c < FCALL_COUNT; c++) check(&fight_lines_call[c]);
    puts("test_lines: ok");
    return 0;
}
