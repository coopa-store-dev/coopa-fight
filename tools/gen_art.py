#!/usr/bin/env python3
"""把 art/*.txt 的字符画转成游戏块里的常量图片(src/fight_art.h、src/fight_art.c,都是生成的,改图改 art/)。

为什么不用外壳的 kit_img:那是开机后解码到堆里(每张 16×16 占 1064 字节),和蓝牙放不到一起(设计 §3.3、§9)。
这里每张图是 LV_COLOR_FORMAT_RGB565A8 的 static const 数组(先 w*h 个 RGB565、再 w*h 个透明度),
放在 .rodata 里,LVGL 直接从游戏块(flash)读,不进堆。人物另生成一份水平翻转的(朝左)。

字符画:art/<文件>.txt 里一段一张图,「== 名字」开头,下面每行一样宽;「#」开头的行是注释。
调色板字符和颜色照外壳的精灵调色板(下面 PALETTE 是一份写死的拷贝,不在运行时依赖外壳);'.' 是透明。

用法:python3 tools/gen_art.py(没有参数;同样的字符画永远生成同样的文件)
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "art"
H = ROOT / "src" / "fight_art.h"
C = ROOT / "src" / "fight_art.c"

# 外壳调色板的拷贝(main/kit_art.c 的 PALETTE):字符 → 0xRRGGBB,全都不透明。
PALETTE = {
    "k": 0x2B1B17,  # 描边
    "w": 0xFFFFFF,
    "s": 0xF5C9A0,  # 皮肤
    "n": 0xD9A066,  # 鼻子 / 擀面杖
    "h": 0xC9C9D6,  # 白发
    "H": 0x9A9AAE,  # 白发阴影
    "g": 0x4A4A5A,  # 眼镜框
    "p": 0xE86A92,  # 开衫
    "P": 0xB84870,  # 开衫阴影
    "r": 0xD83A3A,
    "y": 0xF2C230,
    "o": 0xE8902A,
    "G": 0x3FA34D,  # 龟壳绿
    "D": 0x2A7035,  # 深绿
    "c": 0xFFF1C8,  # 奶油色
    "b": 0x8B5A2B,  # 木头
    "B": 0x4A7BD0,  # 拖鞋蓝
    "K": 0x1E1A1E,  # 黑发
    "v": 0xB9A6E8,  # 奶奶的淡紫遮阳帽
    "V": 0x8E78C8,  # 遮阳帽帽檐
    "d": 0x3A3A48,  # 奶奶的黑衣服
    "x": 0x262630,  # 黑衣服阴影
    "f": 0xF29C8C,  # 腮红
    "m": 0x8FE3C3,  # 薄荷绿 T 恤
    "M": 0x5FBF9C,  # T 恤上的小图案
    "q": 0x5B6E86,  # 灰蓝运动裤
    "z": 0x7E2433,  # 校服酒红西装
    "Z": 0x5C1824,  # 西装阴影
    "t": 0xB8333F,  # 格子领带红
    "T": 0x6E5A5E,  # 格子领带灰
    "j": 0x3C3F52,  # 深色校裤
    "e": 0xD8C8A8,  # 爷爷的米色 T 恤
    "E": 0xB8A888,  # T 恤阴影
    "J": 0x3A5A9A,  # 牛仔裤
    "N": 0x2A3050,  # 深蓝鞋
    "a": 0xEDEDE6,  # 厨房奶奶的白围裙
    "F": 0xD8404A,  # 广场舞奶奶的红花衣
    "l": 0xF29CC8,  # 粉扇子
    "R": 0xE86A55,  # 暴怒奶奶的红脸
}

# 顺序 = fight_char_t(fight_moves.h)
CHARS = [("kid", "库巴"), ("dad", "爸爸"), ("granny", "奶奶"), ("grandpa", "爷爷")]
# 顺序 = fight_pose_t
POSES = [
    ("IDLE", "站着(拳头举在胸前)"), ("JAB", "轻拳(手伸出去)"), ("WINDUP", "抡重击(手往后拉)"),
    ("HEAVY", "重击(砸出去)"), ("GUARD", "举盾(双臂交叉挡在前面)"), ("HIT", "挨打(往后仰)"),
    ("DIZZY", "晕(歪着、眼睛半闭)"), ("KO", "倒下(躺着)"), ("SPECIAL", "小招"), ("WIN", "赢了(双手举起)"),
]
# 顺序 = fight_fx_t
FX = [
    ("STAR", "晕的星星"), ("SPARK", "打中的火花"), ("SHIELD", "挡住的盾"), ("PELLET", "库巴的弹珠"),
    ("LID", "奶奶的锅盖"), ("CAGE", "爸爸大招的铁笼"), ("TAIJI", "爷爷的太极"), ("ROLLER", "奶奶大招的擀面杖"),
    ("HAMMER", "爸爸大招的大锤"),
]
POSE_W = POSE_H = 16


def parse(path: Path) -> dict[str, list[str]]:
    """一个 .txt → {名字: 行};行宽不一、字符不在调色板里、名字重复都直接报错退出。"""
    arts: dict[str, list[str]] = {}
    name = None
    for no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("=="):
            name = line[2:].strip()
            if name in arts:
                sys.exit(f"{path.name}:{no}: 「{name}」重复了")
            arts[name] = []
            continue
        if name is None:
            sys.exit(f"{path.name}:{no}: 图要以「== 名字」开头")
        bad = [ch for ch in line if ch != "." and ch not in PALETTE]
        if bad:
            sys.exit(f"{path.name}:{no}: 调色板里没有「{bad[0]}」")
        if arts[name] and len(line) != len(arts[name][0]):
            sys.exit(f"{path.name}:{no}: 「{name}」这行宽 {len(line)},上面是 {len(arts[name][0])}")
        arts[name].append(line)
    return arts


def need(arts: dict[str, list[str]], names: list[str], where: str, size: tuple[int, int] | None) -> None:
    if list(arts) != names:
        sys.exit(f"{where}:图要正好是 {names}(按这个顺序),现在是 {list(arts)}")
    for n, rows in arts.items():
        if not rows:
            sys.exit(f"{where}「{n}」:是空的")
        if size and (len(rows[0]), len(rows)) != size:
            sys.exit(f"{where}「{n}」:要 {size[0]}×{size[1]},现在是 {len(rows[0])}×{len(rows)}")


def rgb565a8(rows: list[str]) -> bytes:
    """先 w*h 个 RGB565(小端,和 LVGL 内存里一样),再 w*h 个透明度(0 / 255)。透明处颜色也填 0。"""
    color, alpha = bytearray(), bytearray()
    for row in rows:
        for ch in row:
            if ch == ".":
                color += b"\x00\x00"
                alpha.append(0)
                continue
            v = PALETTE[ch]
            r, g, b = v >> 16, (v >> 8) & 0xFF, v & 0xFF
            c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            color += bytes((c & 0xFF, c >> 8))
            alpha.append(0xFF)
    return bytes(color + alpha)


def array(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}[] __attribute__((aligned(4))) = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02x}," for b in data[i:i + 16]))
    lines.append("};")
    return "\n".join(lines)


def dsc(name: str, rows: list[str]) -> str:
    w, h = len(rows[0]), len(rows)
    return (f"{{ .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .w = {w}, .h = {h}, "
            f".stride = {w * 2} }}, .data_size = {w * h * 3}, .data = {name} }}")


def header() -> str:
    pose = ", ".join(f"FA_{p}" + (" = 0" if i == 0 else "") for i, (p, _) in enumerate(POSES))
    fx = ", ".join(f"FX_{f}" + (" = 0" if i == 0 else "") for i, (f, _) in enumerate(FX))
    who = "、".join(f"{i} {n}" for i, (_, n) in enumerate(CHARS))
    notes = "\n".join(f"//   FA_{p}:{t}" for p, t in POSES) + "\n" + "\n".join(f"//   FX_{f}:{t}" for f, t in FX)
    return f"""// src/fight_art.h —— 人物、特效、热血头像的像素画(tools/gen_art.py 从 art/*.txt 生成,勿手改)。
// 全是 LV_COLOR_FORMAT_RGB565A8 的常量图,放在游戏块(flash)里,LVGL 直接读,不进堆;画面上 lv_image_set_scale 放大。
// 人物 {POSE_W}×{POSE_H},原图朝右,facing_left 给水平翻转的那份。下标 ch = fight_char_t({who})。
{notes}
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum {{ {pose}, FA_POSES }} fight_pose_t;
typedef enum {{ {fx}, FX_COUNT }} fight_fx_t;

const lv_image_dsc_t *fight_art_pose(uint8_t ch, uint8_t pose, bool facing_left);  // 越界返回库巴站立
const lv_image_dsc_t *fight_art_fx(uint8_t fx);    // 越界返回 NULL
const lv_image_dsc_t *fight_art_hot(uint8_t ch);   // 特写大头像(传炸弹的热血头像);越界返回库巴
"""


def source(chars: list[dict[str, list[str]]], fx: dict[str, list[str]], hot: dict[str, list[str]]) -> str:
    out = ["// src/fight_art.c —— tools/gen_art.py 从 art/*.txt 生成,勿手改。见 fight_art.h。",
           '#include "fight_art.h"', "", "#include <stddef.h>", ""]
    pose_dsc = []
    for (cid, _), arts in zip(CHARS, chars):
        row = []
        for p, _ in POSES:
            rows = arts[p.lower()]
            base = f"{cid.upper()}_{p}"
            out += [array(base + "_R", rgb565a8(rows)), array(base + "_L", rgb565a8([r[::-1] for r in rows])), ""]
            row.append(f"        {{ {dsc(base + '_R', rows)},\n          {dsc(base + '_L', [r[::-1] for r in rows])} }},")
        pose_dsc.append("    {\n" + "\n".join(row) + "\n    },")
    for f, _ in FX:
        out += [array(f"FX_{f}_IMG", rgb565a8(fx[f.lower()])), ""]
    for cid, _ in CHARS:
        out += [array(f"HOT_{cid.upper()}", rgb565a8(hot[cid])), ""]
    out.append(f"static const lv_image_dsc_t POSE[{len(CHARS)}][FA_POSES][2] = {{  // [ch][pose][朝左]")
    out += pose_dsc + ["};", ""]
    out.append("static const lv_image_dsc_t FXS[FX_COUNT] = {")
    out += [f"    {dsc(f'FX_{f}_IMG', fx[f.lower()])}," for f, _ in FX] + ["};", ""]
    out.append(f"static const lv_image_dsc_t HOT[{len(CHARS)}] = {{")
    out += [f"    {dsc(f'HOT_{c.upper()}', hot[c])}," for c, _ in CHARS] + ["};", ""]
    out += [
        "const lv_image_dsc_t *fight_art_pose(uint8_t ch, uint8_t pose, bool facing_left) {",
        f"    if (ch >= {len(CHARS)} || pose >= FA_POSES) {{",
        "        ch = 0;",
        "        pose = FA_IDLE;",
        "    }",
        "    return &POSE[ch][pose][facing_left ? 1 : 0];",
        "}",
        "",
        "const lv_image_dsc_t *fight_art_fx(uint8_t fx) { return fx < FX_COUNT ? &FXS[fx] : NULL; }",
        "",
        f"const lv_image_dsc_t *fight_art_hot(uint8_t ch) {{ return &HOT[ch < {len(CHARS)} ? ch : 0]; }}",
        "",
    ]
    return "\n".join(out)


def main() -> None:
    names = [p.lower() for p, _ in POSES]
    chars = []
    for cid, _ in CHARS:
        arts = parse(ART / f"{cid}.txt")
        need(arts, names, f"art/{cid}.txt", (POSE_W, POSE_H))
        chars.append(arts)
    fx = parse(ART / "fx.txt")
    need(fx, [f.lower() for f, _ in FX], "art/fx.txt", None)
    hot = parse(ART / "hot.txt")
    need(hot, [c for c, _ in CHARS], "art/hot.txt", (16, 16))
    H.write_text(header(), encoding="utf-8")
    C.write_text(source(chars, fx, hot), encoding="utf-8")
    print(f"写了 {H.relative_to(ROOT)}、{C.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
