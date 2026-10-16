// tests/test_lines.c —— 16 句语音都能用外壳的解码器完整解出来,样本数对得上,每句不超过 2.5 秒。
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
    assert(n == l->samples && n > 16000 / 2 && n <= 16000 * 5 / 2);
}

int main(void) {
    const kit_line_t *sets[4] = { fight_lines_pick, fight_lines_special, fight_lines_super, fight_lines_win };
    for (int k = 0; k < 4; k++)
        for (int f = 0; f < 4; f++) check(&sets[k][f]);
    puts("test_lines: ok");
    return 0;
}
