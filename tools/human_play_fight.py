#!/usr/bin/env python3
"""经 USB 调试通道,按真人手速在卡上(或桌面模拟器的 --port 串口上)和电脑闯关,记下大招、晕、输赢。
  tools/human_play_fight.py PORT OUT_DIR RUNS [--kid] [--stages N] [--link] [--shell DIR]
先把卡停在库巴格斗里(出招表 / 首页都行,首页光标在「和电脑打」上);脚本只发 KEYU / KEYD / KEYO / KEYW、STAT、SNAP。
--link:两人联机(对面是 Mac 的 link_peer.py fight 或另一张卡):先把卡停在「正在找对方」或选人页,打 RUNS 场
(打完一场按 ● 再来一场,最后一场停在结束页)。
- 手:src/fight_bot.c 的「真人模型」(和 test_balance 调数值用的同一份):大人 fight_human_adult(反应 0.65 ± 0.15 秒、
  一秒 5 下),--kid 用 fight_human_kid(反应 0.9 ± 0.25 秒、两下隔 0.286 秒、按错 12%)。脚本把它编成共享库,
  每读一次状态行(FT,字段见 src/fight_app.c 的 fight_status)就填一份「看得见的」交给它,按过去了多少毫秒 10 ms 一步走,
  它要按的键马上发出去。读状态行的来回(USB 约 20–50 ms)算在过去的时间里:人看到的本来就是稍早一点的画面。
- 状态行里没有招式、还剩多久、对方在不在保护里。保护照看得见的猜:对方晕了就在保护里,醒后站了 0.3 秒或者动了就没了。
  招式看不出是拳还是重击,所以大人的「读习惯」关掉(不然只看得见防,以为对方老在防、一直抡重击;在模拟器上这样打输给了
  「简单」);跟着关掉的还有防住拳以后反打、看见太极架势先不打(这两样要招式 / 还剩多久)。
- 和电脑打,一次闯关(RUNS 次):选人页上一次选的人往下挪一个(第一次就用光标上的人),打到输一关或者赢到第 N 关(默认 1),回首页。
- 记在 OUT_DIR/log.jsonl:每次大招(谁放的)、每次晕(谁、多久醒、狂按了几下)、每回合、每关的输赢和比分、一关多长、打中几下;
  第一次见到的对打、大招特写、对方晕、自己晕、结束页各截一张图。
--shell:外壳仓库目录(或环境变量 COOPA_SHELL),截图用它的 tools/granny_devlink.py。要 pyserial。"""
import argparse
import ctypes
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import time

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PG_TITLE, PG_LINK, PG_GAME, PG_HELP = range(4)
GP_WAIT, GP_PICK, GP_COUNT, GP_PLAY, GP_END, GP_OVER = range(6)
FS_IDLE, FS_MOVE, FS_GUARD, FS_HITSTUN, FS_BLOCKSTUN, FS_DIZZY, FS_SUPER_IN, FS_SUPER_OUT, FS_STANCE, FS_KO = range(10)
FM_UNKNOWN = 7  # fight_move_t 的 FM_COUNT:当「看不出是什么招」
FK_KEYS = "UDOW"  # fight_key_t → 调试通道的键(W = 长按 ▼)
KEYS = ["page", "phase", "mode", "char", "peer_char", "round", "me", "peer", "hp", "peer_hp", "g", "peer_g",
        "st", "peer_st", "t", "peer_t", "chain", "stage"]
NAMES = ["库巴", "爸爸", "奶奶", "爷爷"]
WAKE_EAT_MS = 300  # fight_sim.h FIGHT_WAKE_EAT
OVER_DEAF_S, PICK_DEAF_S, NAV_S = 1.3, 0.7, 0.8  # 结束页前 1 秒、选人页前 0.4 秒不收键;翻页之间等一下
FIGHT_STEP = 10
STATUS = re.compile(rb"^[A-Z]{2,4}( -?\d+)+$")  # 别的游戏 / 系统首页的状态行(日志行不是这样)


class Seen(ctypes.Structure):  # fight_seen_t
    _fields_ = [("st", ctypes.c_uint8 * 2), ("move", ctypes.c_uint8 * 2), ("t", ctypes.c_uint16 * 2),
                ("left", ctypes.c_uint16 * 2), ("hp", ctypes.c_int16 * 2), ("gauge", ctypes.c_uint16 * 2),
                ("chain_open", ctypes.c_bool), ("protect_peer", ctypes.c_bool)]


RND = ctypes.CFUNCTYPE(ctypes.c_uint32)


class Bot(ctypes.Structure):  # fight_bot_t
    _fields_ = [("cfg", ctypes.c_void_p), ("rnd", RND), ("super_try", ctypes.c_uint32), ("super_ok", ctypes.c_uint32),
                ("pad", ctypes.c_uint32 * 24)]


def load_bot():
    """把 src/fight_bot.c 编成共享库;先核对 fight_seen_t / fight_bot_t 和这里的一样大。
    返回 (lib, fight_bot_cfg_t 多大, reads_habits 在第几个字节)。"""
    tmp, src = tempfile.gettempdir(), os.path.join(GAME, "src")
    probe = os.path.join(tmp, f"coopa_fight_human_size.{os.getpid()}")  # 带进程号:同时开两个(两张卡)不打架
    subprocess.run(["cc", "-x", "c", "-I" + src, "-o", probe, "-"], check=True,
                   input=b'#include <stdio.h>\n#include "fight_bot.h"\n'
                         b'#include <stddef.h>\nint main(void){printf("%zu %zu %zu %zu",sizeof(fight_seen_t),sizeof(fight_bot_t),'
                         b'sizeof(fight_bot_cfg_t),offsetof(fight_bot_cfg_t,reads_habits));}')
    *sizes, cfg_size, habits_at = map(int, subprocess.run([probe], capture_output=True, check=True).stdout.split())
    sizes = tuple(sizes)
    if sizes != (ctypes.sizeof(Seen), ctypes.sizeof(Bot)):
        sys.exit(f"fight_seen_t / fight_bot_t 变了:C {sizes},脚本 {(ctypes.sizeof(Seen), ctypes.sizeof(Bot))}")
    os.remove(probe)
    out = os.path.join(tmp, f"coopa_fight_human.{os.getpid()}.dylib")
    subprocess.run(["cc", "-std=c11", "-O2", "-shared", "-fPIC", "-I" + src, os.path.join(src, "fight_bot.c"), "-o", out],
                   check=True)
    lib = ctypes.CDLL(out)
    os.remove(out)  # 已经载进来了
    lib.fight_bot_step.restype = ctypes.c_int
    lib.fight_bot_step.argtypes = [ctypes.POINTER(Bot), ctypes.POINTER(Seen), ctypes.c_uint32]
    lib.fight_bot_init.argtypes = [ctypes.POINTER(Bot), ctypes.c_void_p, RND]
    return lib, cfg_size, habits_at


def parse_ft(line):
    """一行状态行 → dict;不是 FT 行返回 None。"""
    f = line.split()
    if len(f) != len(KEYS) + 1 or f[0] != b"FT":
        return None
    return dict(zip(KEYS, map(int, f[1:])))


class Eyes:
    """把一串状态行变成 fight_seen_t。招式填「看不出」、还剩多久填 0(不读习惯时脑子用不着);对方的保护照看得见的猜。"""

    def __init__(self):
        self.protect = False

    def see(self, s, v):
        st, t = (s["st"], s["peer_st"]), (s["t"], s["peer_t"])
        for i in range(2):
            v.st[i], v.move[i], v.t[i], v.left[i] = st[i], FM_UNKNOWN, t[i], 0
        if st[1] == FS_DIZZY:
            self.protect = True
        elif (st[1] == FS_IDLE and t[1] >= WAKE_EAT_MS) or st[1] in (FS_MOVE, FS_GUARD, FS_STANCE):
            self.protect = False
        v.hp[0], v.hp[1], v.gauge[0], v.gauge[1] = s["hp"], s["peer_hp"], s["g"], s["peer_g"]
        v.chain_open, v.protect_peer = bool(s["chain"]), self.protect


class Card:
    """调试通道:STAT 读状态行,KEYx 按键,SNAP 截图(granny_devlink.snap)。"""

    def __init__(self, port, out, devlink):
        import serial  # pyserial(ESP-IDF 的 Python 环境里有)
        self.port, self.out, self.devlink, self.shots = serial.Serial(port, 115200, timeout=0.02), out, devlink, set()
        self.snapped = False  # 刚截过图(截图要一会儿,人这段时间没看着)

    def stat(self):
        """FT 行 → dict;别的状态行(系统首页、别的游戏)→ {"other": 那一行};2 秒没回 → None。"""
        self.port.reset_input_buffer()
        self.port.write(b"STAT")
        buf, end = b"", time.time() + 2
        while time.time() < end:
            buf += self.port.read(256)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                s = parse_ft(line)
                if s:
                    return s
                if STATUS.match(line):
                    return {"other": line.decode()}
        return None

    def key(self, k):
        self.port.write(b"KEY" + k.encode())

    def snap(self, tag, wait=0.0):
        """每种画面截一张(第一次见到的;先等 wait 秒);返回截了没有。"""
        if tag in self.shots:
            return False
        self.shots.add(tag)
        time.sleep(wait)
        self.devlink.snap(self.port, os.path.join(self.out, tag + ".png"))
        self.snapped = True
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("out")
    ap.add_argument("runs", type=int)
    ap.add_argument("--kid", action="store_true", help="小孩的手(fight_human_kid)")
    ap.add_argument("--stages", type=int, default=1, choices=(1, 2, 3), help="一次闯关打到第几关")
    ap.add_argument("--link", action="store_true", help="两人联机(不是和电脑打)")
    ap.add_argument("--shell", default=os.environ.get("COOPA_SHELL"))
    a = ap.parse_args()
    if not a.shell:
        ap.error("要给外壳仓库目录:--shell DIR 或环境变量 COOPA_SHELL")
    sys.path.insert(0, os.path.join(os.path.abspath(a.shell), "tools"))
    import granny_devlink  # noqa: E402
    os.makedirs(a.out, exist_ok=True)
    lib, cfg_size, habits_at = load_bot()
    rnd = RND(lambda: random.getrandbits(32))
    hand = ctypes.c_char.in_dll(lib, "fight_human_kid" if a.kid else "fight_human_adult")
    cfg = ctypes.create_string_buffer(ctypes.string_at(ctypes.addressof(hand), cfg_size), cfg_size)  # 拷一份再改
    cfg[habits_at] = 0  # 不读习惯(见最上面)
    card = Card(a.port, a.out, granny_devlink)
    log = open(os.path.join(a.out, "log.jsonl"), "a")

    def emit(**kw):
        kw["t"] = round(time.time(), 2)
        log.write(json.dumps(kw, ensure_ascii=False) + "\n")
        log.flush()

    emit(ev="start", hand="kid" if a.kid else "adult", runs=a.runs, stages=a.stages, link=a.link)
    bot, seen, eyes = Bot(), Seen(), Eyes()
    run, prev_char, picked_run = 0, None, -1
    nav_at = over_at = pick_at = 0.0  # 上一次翻页按键 / 看到结束页 / 看到选人页的时刻
    last, acc, prev = time.monotonic(), 0, None
    m = {}  # 这一关的统计
    dizzy = [None, None]  # 正在晕:[开始的时刻, 狂按了几下, 是不是挨了大招]
    while run < a.runs:
        s = card.stat()
        now = time.monotonic()
        if s is None:
            sys.exit("调试通道 2 秒没回状态行")
        if "other" in s:
            sys.exit(f"不在库巴格斗里(状态行 {s['other']}):先进库巴格斗(首页光标在「和电脑打」上)再跑")
        if s["page"] == PG_GAME and s["mode"] != (0 if a.link else 1):
            sys.exit("在两人联机里:先回库巴格斗首页" if not a.link else "在和电脑打:先回首页进「两人联机」")
        if s["page"] == PG_LINK and not a.link:
            sys.exit("进了「两人联机」:首页光标不在「和电脑打」上,挪好再跑")
        if s["page"] in (PG_HELP, PG_TITLE) and a.link:
            sys.exit("在库巴格斗首页:--link 要先进「两人联机」")
        ph = s["phase"] if s["page"] == PG_GAME else None
        if ph != GP_PLAY:
            last, acc = now, 0
        if s["page"] == PG_LINK:
            pass  # 联机:在找对方 / 对方走开了,等着
        elif s["page"] in (PG_HELP, PG_TITLE) and now - nav_at > NAV_S:
            card.key("O")  # 出招表:看过了;首页:和电脑打
            nav_at = now
        elif ph == GP_PICK:
            if not pick_at:
                pick_at = now
            # 状态行看不出选没选上(「再来一场」时上一场的人还留着):每 2 秒按一次 ●,选上了再按不算数
            if now - pick_at > PICK_DEAF_S and now - nav_at > 2:
                if picked_run != run:
                    if prev_char is not None:  # 往下挪到下一个人(光标在上一次选的人上;第 5 格是「回首页」,跳过)
                        for _ in range(1 if prev_char < 3 else 2):
                            card.key("D")
                            time.sleep(0.3)
                    picked_run = run
                card.key("O")
                nav_at = now
        elif ph in (GP_COUNT, GP_PLAY) and not m:  # 新的一关(脚本半路开始的话从这一回合算起)
            pick_at = 0.0
            m = {"run": run, "me": NAMES[s["char"]], "foe": NAMES[s["peer_char"]], "stage": s["stage"], "start": now,
                 "hits": 0, "hurt": 0, "supers": 0, "supers_in": 0, "keys": 0, "polls": 0}
            bot = Bot()
            lib.fight_bot_init(ctypes.byref(bot), ctypes.addressof(cfg), rnd)
            eyes = Eyes()
            print(f"第 {run + 1} 次 {m['me']} 第 {m['stage']} 关 对 {m['foe']}", flush=True)
        elif ph == GP_PLAY:
            m["polls"] += 1
            eyes.see(s, seen)
            ms = int((now - last) * 1000)
            last += ms / 1000
            acc += ms
            while acc >= FIGHT_STEP:  # 过了几个 10 ms 就走几步
                acc -= FIGHT_STEP
                k = lib.fight_bot_step(ctypes.byref(bot), ctypes.byref(seen), FIGHT_STEP)
                if k >= 0:
                    card.key(FK_KEYS[k])
                    m["keys"] += 1
                    if dizzy[0] and k != 3:
                        dizzy[0][1] += 1
            if prev and prev.get("phase") == GP_PLAY:
                watch(s, prev, m, dizzy, emit, card)
            if now - m["start"] > 8:
                card.snap("fight")
        elif ph == GP_END and prev and prev.get("phase") == GP_PLAY:
            ko = s["hp"] <= 0 or s["peer_hp"] <= 0
            emit(ev="round", round=s["round"], score=f'{s["me"]}:{s["peer"]}', ko=ko, hp=[s["hp"], s["peer_hp"]])
            dizzy = [None, None]
        elif ph == GP_OVER and not m and not a.link and now - nav_at > 2:  # 脚本开始时就停在结束页:回首页
            card.key("D")
            nav_at = now
        elif ph == GP_OVER and m:
            if not over_at:
                over_at = now
            if now - over_at < OVER_DEAF_S:
                prev = s
                continue
            won = s["me"] >= 2
            m.update(ev="match", score=f'{s["me"]}:{s["peer"]}', won=won, secs=round(now - OVER_DEAF_S - m.pop("start"), 1),
                     super_try=bot.super_try, super_ok=bot.super_ok)
            emit(**m)
            print(f"  {m['score']} {'赢' if won else '输'};{m['secs']} 秒,打中 {m['hits']} 下、挨 {m['hurt']} 下,"
                  f"大招 放 {m['supers']} 挨 {m['supers_in']};按了 {m['keys']} 下,读了 {m['polls']} 次状态行", flush=True)
            card.snap("over")
            prev_char = s["char"]
            if a.link:
                emit(ev="run", run=run, me=NAMES[s["char"]], won=won)
                run += 1
                if run < a.runs:
                    card.key("O")  # 再来一场
            elif won and s["stage"] < a.stages:
                card.key("O")  # 下一关(自动选同一个人)
            else:
                card.key("D")  # 回首页,这次闯关完了
                emit(ev="run", run=run, me=NAMES[s["char"]], reached=s["stage"], cleared=won)
                run += 1
            m, over_at, nav_at = {}, 0.0, now
        if card.snapped:  # 截图花的时间不算:人那会儿没在看
            card.snapped, last, acc = False, time.monotonic(), 0
        prev = s
    print(f"打完 {a.runs} 次;记录在 {os.path.join(a.out, 'log.jsonl')}")


def watch(s, prev, m, dizzy, emit, card):
    """和上一行状态行比:打中 / 挨打、大招(谁放的)、晕(谁、多久醒、狂按几下)。"""
    m["hits"] += s["peer_hp"] < prev["peer_hp"]
    m["hurt"] += s["hp"] < prev["hp"]
    for i, (st, pst) in enumerate(((s["st"], prev["st"]), (s["peer_st"], prev["peer_st"]))):
        who = "me" if i == 0 else "foe"
        if st == FS_SUPER_OUT and pst != FS_SUPER_OUT:
            m["supers" if i == 0 else "supers_in"] += 1
            emit(ev="super", by=who, char=m["me"] if i == 0 else m["foe"])
            card.snap("super-" + (m["me"] if i == 0 else m["foe"]), 0.6)  # 特写里头像、招式名出来了(这时按键本来就不算)
        if st == FS_DIZZY and pst != FS_DIZZY:
            dizzy[i] = [time.monotonic(), 0, pst == FS_SUPER_IN]
            if i == 1 and dizzy[1][2]:
                card.snap("foe-dizzy-super")
            if i == 0:
                card.snap("me-dizzy")
        elif st != FS_DIZZY and pst == FS_DIZZY and dizzy[i]:
            start, presses, by_super = dizzy[i]
            emit(ev="dizzy", who=who, secs=round(time.monotonic() - start, 2), super=by_super,
                 **({"presses": presses} if i == 0 else {}))
            dizzy[i] = None


if __name__ == "__main__":
    main()
