#!/usr/bin/env python3
"""一个桌面模拟器(外壳的 tools/sim,step 时钟)装上库巴格斗,和电脑闯关。按键全用 press / down / up(真的按键组件)。
第一次进游戏先看出招表 → 首页「和电脑打」→ 选库巴 → 开打:
  1. 先测按键(Review Focus 1、Task 2 留下的):按着 ▼ 过 0.5 秒长按,盾撑过 600 ms(状态行里看);
     按着 ▼ 按 ●(● 没反应)、按着 ● 按 ▼ 再松开 ▼(冒出一个新的 ●):不崩,最多多一拳,不出大招;
  2. 攒气:站着挨打(挨打涨气最多),攒满一格;
  3. 打:对方 idle / 在出招就 ●,在防就 ▲;● 打中(连招窗口开着)有气就 ▼ ●(大招),没气就再 ●;晕了狂按。
直到第 1 关赢过、大招放过(大招特写、对方晕都截图)。中途不开蓝牙(状态行的模式 = 1)。
  tests/sim_cpu.py --shell 外壳仓库目录(或环境变量 COOPA_SHELL) [--font-dir DIR] [--keep] [--shots DIR]
--font-dir:按本游戏文案生成的字库(没给就用外壳提交的字库,游戏里的生字截图上是空白,不影响判定)。"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PG_TITLE, PG_LINK, PG_GAME, PG_HELP = range(4)
GP_WAIT, GP_PICK, GP_COUNT, GP_PLAY, GP_END, GP_OVER = range(6)
FS_IDLE, FS_MOVE, FS_GUARD, FS_HITSTUN, FS_BLOCKSTUN, FS_DIZZY, FS_SUPER_IN, FS_SUPER_OUT, FS_STANCE, FS_KO = range(10)
KEYS = ["page", "phase", "mode", "char", "peer_char", "round", "me", "peer", "hp", "peer_hp", "g", "peer_g",
        "st", "peer_st", "t", "peer_t", "chain", "stage"]
GUARD_MS = [600, 600, 800, 600]  # 不长按的盾(fight_moves.c 的 guard_ms,下标 = 角色)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", default=os.environ.get("COOPA_SHELL"))
    ap.add_argument("--font-dir")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--shots", help="把各个画面截图存到这个目录")
    ap.add_argument("--char", type=int, default=0, help="选哪个人(0 库巴、1 爸爸、2 奶奶、3 爷爷)")
    ap.add_argument("--sim-args", default="", help="原样交给模拟器的参数,比如 \"--text-check --corners\"")
    a = ap.parse_args()
    if not a.shell:
        ap.error("要给外壳仓库目录:--shell DIR 或环境变量 COOPA_SHELL")
    shell = os.path.abspath(a.shell)
    sys.path.insert(0, os.path.join(shell, "tools", "sim"))
    from simctl import Sim  # noqa: E402
    root = tempfile.mkdtemp(prefix="fight-cpu.")
    catalog = os.path.join(root, "catalog.toml")
    subprocess.run([sys.executable, os.path.join(GAME, "tests", "make_catalog.py"), shell, catalog], check=True,
                   stdout=subprocess.DEVNULL)
    sock = os.path.join(root, "ctl.sock")
    proc = subprocess.Popen(
        [os.path.join(shell, "tools/sim/run.sh"), "--headless", "--wipe", "--clock", "step", "--seed", "5",
         "--catalog", catalog, "--games", "12", *(["--font-dir", a.font_dir] if a.font_dir else []), "--ctl", sock,
         "--state", os.path.join(root, "state"), "--out", os.path.join(root, "out"), *a.sim_args.split()],
        stdout=open(os.path.join(root, "stdout.log"), "w"), stderr=subprocess.STDOUT)
    end = time.time() + 600
    while not os.path.exists(sock):
        if proc.poll() is not None or time.time() > end:
            sys.exit(f"模拟器没起来,看 {root}/stdout.log")
        time.sleep(0.2)
    c = Sim(sock)
    shots = set()

    def stat():
        f = c.cmd("stat").split()
        return dict(zip(KEYS, map(int, f[2:]))) if len(f) == len(KEYS) + 2 and f[:2] == ["ok", "FT"] else None

    def wait(ms):
        c.ok(f"wait {ms}")

    def press(k, ms=50):  # 按下 ms 再松开(模拟时间跟着走)
        c.ok(f"press {k} {ms}")

    def shot(name):
        if a.shots and name not in shots:
            shots.add(name)
            os.makedirs(a.shots, exist_ok=True)
            c.ok(f"shot {os.path.join(os.path.abspath(a.shots), name)}.png")

    def watch(ms, pred):
        """往前走 ms(每 10 ms 看一次状态行),pred 成立就返回那一刻的状态。"""
        for _ in range(ms // 10):
            s = stat()
            if pred(s):
                return s
            wait(10)
        return None

    def calm(s):
        return s["phase"] == GP_PLAY and s["st"] == FS_IDLE and s["peer_st"] in (FS_IDLE, FS_GUARD)

    def test_long_guard():
        """按着 ▼:0.5 秒的长按把盾撑到 guard_long_ms,状态行里盾过了 600 ms 还在。对方砸开了就重来。"""
        for _ in range(12):
            if not watch(3000, calm):
                continue
            c.ok("down down")
            wait(1100)  # 0.5 秒来长按,盾撑到 1200
            s = stat()
            c.ok("up down")
            if s["st"] == FS_GUARD and s["t"] > GUARD_MS[a.char] + 100:
                print(f"  按着 ▼:盾撑了 {s['t']} ms(不长按 {GUARD_MS[a.char]})", flush=True)
                return True
            wait(800)
        return False

    def run_states(ms):
        """往前走 ms,记下每 10 ms 自己的状态;挨打、晕了(对方打过来)返回 None。"""
        out = []
        for _ in range(ms // 10):
            x = stat()
            assert x["page"] == PG_GAME and x["st"] != FS_SUPER_OUT, x  # 不崩、不冒出大招
            if x["st"] in (FS_HITSTUN, FS_DIZZY, FS_SUPER_IN):
                return None
            out.append((x["st"], x["t"]))
            wait(10)
        return out

    def moves_in(states):
        """出了几下招(进 FS_MOVE、或者 FS_MOVE 的 t 重新从小数起)。"""
        n, last = 0, None
        for st, t in states:
            if st == FS_MOVE and (last is None or t < last):
                n += 1
            last = t if st == FS_MOVE else None
        return n

    def overlap_down_ok():
        """按着 ▼ 按 ●:● 没反应(▼ 压过 ●)。返回 True = 测完;None = 被对方打断了。"""
        c.ok("down down")
        wait(80)
        c.ok("down ok")
        st = run_states(80)
        c.ok("up ok")
        st = st and st + (run_states(80) or [])
        c.ok("up down")
        st = st and st + (run_states(300) or [])
        if not st:
            return None
        assert moves_in(st) == 0, st
        print("  按着 ▼ 按 ●:● 没反应", flush=True)
        return True

    def overlap_ok_down():
        """按着 ● 按 ▼ 再松开 ▼:冒出一个新的 ●(幻影)。不崩、最多多一拳、不出大招。返回 True / None(被打断)。"""
        c.ok("down ok")  # 按着 ●(出一拳)
        st = run_states(80)
        c.ok("down down")  # 再按 ▼:● 算松开
        st = st and st + (run_states(80) or [])
        c.ok("up down")  # 松开 ▼:还按着的 ● 又算一次按下
        st = st and st + (run_states(60) or [])
        c.ok("up ok")
        st = st and st + (run_states(800) or [])
        if not st:
            return None
        n = moves_in(st)
        assert 1 <= n <= 2, f"出了 {n} 下:{st}"
        print(f"  按着 ● 按 ▼ 再松开 ▼:出了 {n} 下(第二下是幻影 ●),没崩、没大招", flush=True)
        return True

    def attack(s):
        """一下:对方在防就 ▲,不然 ●;● 打中了接 ▼ ●(有气)或 ●。返回放没放出大招。"""
        if s["peer_st"] == FS_GUARD:
            press("up")
            return False
        press("ok")
        hit = watch(500, lambda x: x["chain"] == 1)
        if not hit:
            return False
        if hit["g"] >= 1000:
            press("down", 40)
            wait(30)
            press("ok", 40)
            return bool(watch(300, lambda x: x["st"] == FS_SUPER_OUT))
        press("ok")
        return False

    try:
        wait(3000)  # 开机
        end = time.time() + 120
        while not stat():  # 开机菜单光标在游戏上:按一次 ●,等游戏出来(别多按)
            assert time.time() < end, "进不了游戏"
            press("ok")
            for _ in range(20):
                if stat():
                    break
                wait(100)
        wait(500)
        s = stat()
        assert s["page"] == PG_HELP, s  # 第一次进游戏:出招表
        shot("1-help")
        press("ok")
        wait(300)
        assert stat()["page"] == PG_TITLE
        shot("2-title")
        press("ok")  # 和电脑打
        wait(700)  # 刚进选人页 0.4 秒不收键
        s = stat()
        assert s["page"] == PG_GAME and s["phase"] == GP_PICK and s["mode"] == 1, s
        shot("3-pick")
        for _ in range(a.char):  # 新存档光标在库巴
            press("down")
            wait(200)
        press("ok")
        assert watch(5000, lambda x: x["phase"] == GP_COUNT), "没开始倒数"
        wait(1200)
        shot("4-count")
        assert watch(5000, lambda x: x["phase"] == GP_PLAY), "没开打"
        assert test_long_guard(), "按着 ▼ 盾没撑住"
        pending = [overlap_down_ok, overlap_ok_down]  # 两个键叠着按:在对方被大招打晕的时候测(他打不过来)
        beat1, supers, dizzy_seen, matches = False, 0, False, 0
        budget = 3000  # 模拟时间上限:约 3000 次 × 平均几十 ms
        while budget > 0:
            budget -= 1
            s = stat()
            assert s["mode"] == 1, s
            if s["phase"] == GP_OVER:
                wait(1300)  # 结束页前 1 秒不收键
                s = stat()
                matches += 1
                won = s["me"] == 2
                beat1 |= won and s["stage"] == 1
                print(f"  第 {s['stage']} 关 对 {s['peer_char']}:{s['me']}:{s['peer']} {'赢' if won else '输'}", flush=True)
                shot("9-over")
                if (beat1 and supers and not pending) or matches >= 8:
                    break
                press("ok")  # 下一关 / 再打一次
                wait(800)
                continue
            if s["phase"] == GP_END:
                wait(1000)
                shot("10-round-end")
            if s["phase"] != GP_PLAY:
                wait(100)
                continue
            if s["peer_st"] == FS_DIZZY and supers and not dizzy_seen:
                dizzy_seen = True
                wait(200)  # 砸下来的白闪过去
                shot("7-dizzy")
                continue
            if s["st"] == FS_DIZZY:  # 晕了:狂按
                shot("8-me-dizzy")
                press("ok", 40)
                wait(100)
                continue
            if s["st"] not in (FS_IDLE, FS_GUARD):
                wait(20)
                continue
            if pending and s["peer_st"] == FS_DIZZY and s["st"] == FS_IDLE:
                if pending[0]():
                    pending.pop(0)
                continue
            if (not supers or pending) and s["g"] < 1000:  # 攒气:站着挨打
                shot("5-fight")
                wait(50)
                continue
            if s["g"] >= 1000:
                shot("5-super-hint")
            if attack(s):
                supers += 1
                wait(800)  # 道具砸下来、分身出来以后
                shot("6-super")
                assert watch(1500, lambda x: x["st"] != FS_SUPER_OUT), "特写没完"
            wait(30)
        assert beat1, "第 1 关没赢"
        assert supers, "没放出大招"
        assert dizzy_seen, "没看到对方被大招打晕"
        assert not pending, "两个键叠着按没测完"
        print(f"第 1 关赢了;{matches} 场,大招 {supers} 次")
        print("sim_cpu: ok")
        return 0
    finally:
        proc.terminate()
        proc.wait(10)
        if a.keep:
            print("留着:", root)
        else:
            shutil.rmtree(root)


if __name__ == "__main__":
    sys.exit(main())
