#!/usr/bin/env python3
"""生成库巴格斗四个角色的专属语音(选人、小招、大招、赢了),编进游戏里。

用微软神经网络语音合成(edge-tts 包,要联网),转成 16 kHz 单声道,去掉小喇叭放不出的低频、
裁掉前后静音、把峰值调到 16000,存成 IMA ADPCM(ffmpeg adpcm_ima_wav,块 1024 字节,trellis 搜索),
格式和 Coopa OS 内置台词一样,用 sfx_play_line 播放。

台词规矩:只说自己,或者对着「你」说,不点对方是谁(对面可能是任何一个家人)。

用法:brew install ffmpeg;python3 -m venv ~/.venvs/coopa-tts && ~/.venvs/coopa-tts/bin/pip install edge-tts
      ~/.venvs/coopa-tts/bin/python tools/gen_lines.py [--all]
写 src/fight_lines.h、src/fight_lines.c(都是生成的,改台词改这里)。
名字和台词都没变的句子保留原来的音频(合成服务两次输出不完全一样);--all 全部重新合成。
选人和赢了的 8 句和库巴传炸弹一样:本仓库没有时,从旁边的 coopa-bomb 里按名字、台词拷过来,字节不变。
"""

import math
import re
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
H = ROOT / "src" / "fight_lines.h"
C = ROOT / "src" / "fight_lines.c"
BOMB = ROOT.parent / "coopa-bomb" / "src"  # 选人、赢了的现成数据

# (声音, 音调, 语速)
GRANNY = ("zh-CN-shaanxi-XiaoniNeural", "-10Hz", "+0%")   # 陕西话
GRANDPA = ("zh-CN-YunjianNeural", "-15Hz", "-10%")
DAD = ("zh-CN-YunjianNeural", "+0Hz", "+5%")
KID = ("zh-CN-YunxiaNeural", "+0Hz", "+5%")               # 小男孩

FIGHTERS = [("KID", KID), ("DAD", DAD), ("GRANNY", GRANNY), ("GRANDPA", GRANDPA)]  # 顺序 = fight_char_t
KINDS = ["PICK", "SPECIAL", "SUPER", "WIN"]  # 公开数组 fight_lines_pick / _special / _super / _win
TEXT = {
    "KID": ["库巴来啦！", "弹弓，发射！", "影分身乱打！", "耶！我赢啦！"],
    "DAD": ["放马过来吧！", "看我肩撞！", "铁笼大锤！", "谁也别想赢过我！"],
    "GRANNY": ["看奶奶的厉害！", "锅盖飞盘！", "念叨风暴！", "服不服？"],
    "GRANDPA": ["爷爷来陪你耍。", "太极推手！", "太极，排山倒海！", "姜还是老的辣！"],
}

HIGHPASS_HZ = 120
TARGET_PEAK = 16000
SAMPLE_RATE = 16000
MAX_SECONDS = 2.5
MAX_KB = 180
TRIM_LEVEL = 300
PAD = SAMPLE_RATE * 30 // 1000
ADPCM_BLOCK = 1024
ADPCM_BLOCK_SAMPLES = 1 + (ADPCM_BLOCK - 4) * 2


def synth(text: str, tmp: Path, voice: tuple[str, str, str]) -> list[int]:
    mp3, wav = tmp / "line.mp3", tmp / "line.wav"
    name, pitch, rate = voice
    subprocess.run([sys.executable, "-m", "edge_tts", "--voice", name, f"--pitch={pitch}", f"--rate={rate}",
                    "--text", text, "--write-media", str(mp3)], check=True, stderr=subprocess.DEVNULL)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(mp3), "-ac", "1", "-ar", str(SAMPLE_RATE),
                    "-sample_fmt", "s16", str(wav)], check=True)
    with wave.open(str(wav)) as w:
        n = w.getnframes()
        samples = list(struct.unpack(f"<{n}h", w.readframes(n)))
    if n < SAMPLE_RATE // 10:
        sys.exit(f"语音合成几乎没出声:{text!r}(声音 {name} 可用吗?)")
    return samples


def process(samples: list[int]) -> list[int]:
    rc = 1.0 / (2 * math.pi * HIGHPASS_HZ)
    a = rc / (rc + 1.0 / SAMPLE_RATE)
    out, px, py = [], 0.0, 0.0
    for x in samples:
        py = a * (py + x - px)
        px = x
        out.append(py)
    loud = [i for i, s in enumerate(out) if abs(s) >= TRIM_LEVEL]
    clip = out[max(loud[0] - PAD, 0):min(loud[-1] + PAD + 1, len(out))]
    gain = TARGET_PEAK / max(abs(s) for s in clip)
    return [max(-32768, min(32767, round(s * gain))) for s in clip]


def adpcm(pcm: list[int], tmp: Path) -> bytes:
    src, dst = tmp / "pcm.wav", tmp / "adpcm.wav"
    with wave.open(str(src), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(src), "-c:a", "adpcm_ima_wav",
                    "-block_size", str(ADPCM_BLOCK), "-trellis", "16", str(dst)], check=True)
    raw = dst.read_bytes()
    k = raw.index(b"data")
    data = raw[k + 8:k + 8 + struct.unpack("<I", raw[k + 4:k + 8])[0]]
    full, rest = divmod(len(pcm), ADPCM_BLOCK_SAMPLES)
    size = full * ADPCM_BLOCK + (4 + rest // 2 if rest else 0)
    assert len(data) >= size
    return data[:size]


def load(h: Path, c: Path, prefix: str) -> dict[str, tuple[str, bytes, int]]:
    """名字 → (台词, ADPCM, 样本数):h、c 里已经生成过的。"""
    if not h.exists() or not c.exists():
        return {}
    texts = dict(re.findall(r"^// (\w+): (.*)$", h.read_text(), re.M))
    src = c.read_text()
    out = {}
    for arr, body in re.findall(rf"static const uint8_t ({prefix}\w+)\[\] = \{{([^}}]*)\}};", src):
        n = re.search(rf'\{{ {arr}, (\d+), "(\w+)" \}}', src)
        name = n.group(2) if n else ""
        if name in texts and n:
            out[name] = (texts[name], bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", body)), int(n.group(1)))
    return out


def existing() -> dict[str, tuple[str, bytes, int]]:
    """本仓库已有的优先;没有的选人、赢了从 coopa-bomb 拷(名字和台词都对得上才用)。"""
    out = load(BOMB / "bomb_lines.h", BOMB / "bomb_lines.c", "L_")
    out = {k: v for k, v in out.items() if k.startswith(("PICK_", "WIN_"))}
    out.update(load(H, C, "fight_"))
    return out


def main() -> int:
    redo = "--all" in sys.argv
    old = existing()
    lines = []  # (名字, 台词, ADPCM, 样本数)
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        for kind_i, kind in enumerate(KINDS):
            for fighter, voice in FIGHTERS:
                name, text = f"{kind}_{fighter}", TEXT[fighter][kind_i]
                if not redo and name in old and old[name][0] == text:
                    lines.append((name, text, old[name][1], old[name][2]))
                    continue
                pcm = process(synth(text, tmp, voice))
                lines.append((name, text, adpcm(pcm, tmp), len(pcm)))
                print(f"  {name}: {text}  {len(pcm) / SAMPLE_RATE:.2f} 秒", flush=True)
    h = ["// src/fight_lines.h —— 四个角色的专属语音(tools/gen_lines.py 生成,勿手改)。下标 = fight_char_t。",
         "// 格式同 Coopa OS 内置台词(16 kHz IMA ADPCM),用 sfx_play_line 播放。台词:", ""]
    h += [f"// {name}: {text}" for name, text, _, _ in lines]
    h += ["", "#pragma once", "", '#include "kit_lines.h"', "",
          "extern const kit_line_t fight_lines_pick[4], fight_lines_special[4], fight_lines_super[4], fight_lines_win[4];", ""]
    H.write_text("\n".join(h))
    c = ["// src/fight_lines.c —— tools/gen_lines.py 生成,勿手改。", '#include "fight_lines.h"', ""]
    for name, _, data, _ in lines:
        c.append(f"static const uint8_t fight_{name.lower()}[] = {{")
        for i in range(0, len(data), 16):
            c.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
        c.append("};")
    c.append("")
    for kind in KINDS:
        items = [f'{{ fight_{kind.lower()}_{f.lower()}, {n}, "{kind}_{f}" }}' for f, _ in FIGHTERS
                 for name, _, _, n in lines if name == f"{kind}_{f}"]
        c.append(f"const kit_line_t fight_lines_{kind.lower()}[4] = {{")
        c += [f"    {it}," for it in items]
        c.append("};")
    c.append("")
    C.write_text("\n".join(c))
    total = sum(len(x[2]) for x in lines)
    longest = max(x[3] for x in lines) / SAMPLE_RATE
    print(f"16 句,共 {total / 1024:.0f} KB(上限 {MAX_KB}),最长 {longest:.2f} 秒(上限 {MAX_SECONDS})")
    for name, _, _, n in lines:
        print(f"  {name}: {n / SAMPLE_RATE:.2f} 秒")
    if total > MAX_KB * 1024 or longest > MAX_SECONDS:
        print("超限:调台词或语速")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
