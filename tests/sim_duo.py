#!/usr/bin/env python3
"""两个桌面模拟器(外壳的 tools/sim,真时钟,假链路走本机 UDP)装上库巴格斗,联机打一场。按键全用 press(真的按键组件)。
两边先过出招表、选「两人联机」→ 连上、选人(x 库巴、y 爷爷)→ 开打:
  1. x 打、y 站着挨打攒气 → y 气满一格:x 站着,y ● 打中马上 ▼ ●(大招);
  2. 反过来 x 攒气、x 放大招;
  3. 两边乱打到一方赢 2 回合;中途停住 y 3 秒(断线)再放开,这一回合作废重开、比分留着。
一直查:两边同一阶段同一回合时比分互为镜像;最后两边都到结束页、比分一样;两边都放过大招(两张卡上都放了特写)。
  tests/sim_duo.py --shell 外壳仓库目录(或环境变量 COOPA_SHELL) [--font-dir DIR] [--keep]"""
import argparse
import os
import random
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


class Card:
    def __init__(self, shell, root, name, seed, catalog, fonts):
        sys.path.insert(0, os.path.join(shell, "tools", "sim"))
        from simctl import Sim  # noqa: E402
        d = os.path.join(root, name)
        os.makedirs(d)
        self.name = name
        self.supers_seen = 0   # 这张卡上看到的大招特写(谁放的都算)
        self.supers_done = 0   # 这张卡的人放的大招
        sock = os.path.join(d, "ctl.sock")
        self.proc = subprocess.Popen(
            [os.path.join(shell, "tools/sim/run.sh"), "--headless", "--wipe", "--seed", str(seed), "--catalog", catalog,
             "--games", "13", *(["--font-dir", fonts] if fonts else []), "--ctl", sock,
             "--state", os.path.join(d, "state"), "--out", os.path.join(d, "out")],
            stdout=open(os.path.join(d, "stdout.log"), "w"), stderr=subprocess.STDOUT)
        end = time.time() + 600
        while not os.path.exists(sock):
            if self.proc.poll() is not None or time.time() > end:
                sys.exit(f"模拟器 {name} 没起来,看 {d}/stdout.log")
            time.sleep(0.2)
        self.ctl = Sim(sock)
        self.last_st = None

    def stat(self):
        f = self.ctl.cmd("stat").split()
        if len(f) != len(KEYS) + 2 or f[:2] != ["ok", "FT"]:
            return None
        s = dict(zip(KEYS, map(int, f[2:])))
        if s["phase"] == GP_PLAY:  # 特写开始(两边看到的状态都是一人 SUPER_OUT 一人 SUPER_IN)
            sup = FS_SUPER_OUT in (s["st"], s["peer_st"])
            if sup and not self.last_st:
                self.supers_seen += 1
                self.supers_done += s["st"] == FS_SUPER_OUT
            self.last_st = sup
        return s

    def press(self, k, ms=50):
        self.ctl.ok(f"press {k} {ms}")

    def enter_game(self):
        """开机菜单光标在游戏上:● 进游戏;第一次先是出招表,● 看过;再 ▼ 到「两人联机」、●。"""
        end = time.time() + 60
        while self.stat() is None:
            if time.time() > end:
                sys.exit(f"{self.name} 进不了游戏")
            self.press("ok")
            time.sleep(1.5)
        assert self.stat()["page"] == PG_HELP
        self.press("ok")
        time.sleep(0.3)
        assert self.stat()["page"] == PG_TITLE
        self.press("down")
        time.sleep(0.15)
        self.press("ok")

    def try_super(self):
        """● 打中(连招窗口开了)马上 ▼ ●。返回放没放出来。"""
        self.press("ok")
        end = time.time() + 0.5
        while time.time() < end:
            s = self.stat()
            if s["chain"]:
                self.press("down", 40)
                time.sleep(0.03)
                self.press("ok", 40)
                end = time.time() + 0.5
                while time.time() < end:
                    if self.stat()["st"] == FS_SUPER_OUT:
                        return True
                    time.sleep(0.02)
                return False
            time.sleep(0.01)
        return False

    def close(self):
        self.proc.terminate()
        self.proc.wait(10)


def wait(pred, secs, what):
    end = time.time() + secs
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.1)
    sys.exit(f"等不到:{what}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shell", default=os.environ.get("COOPA_SHELL"))
    ap.add_argument("--font-dir")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not a.shell:
        ap.error("要给外壳仓库目录:--shell DIR 或环境变量 COOPA_SHELL")
    shell = os.path.abspath(a.shell)
    root = tempfile.mkdtemp(prefix="fight-duo.")
    catalog = os.path.join(root, "catalog.toml")
    subprocess.run([sys.executable, os.path.join(GAME, "tests", "make_catalog.py"), shell, catalog], check=True,
                   stdout=subprocess.DEVNULL)
    x, y = Card(shell, root, "x", 1, catalog, a.font_dir), Card(shell, root, "y", 2, catalog, a.font_dir)
    try:
        time.sleep(3)  # 开机
        for c in (x, y):
            c.enter_game()
        wait(lambda: x.stat()["phase"] == GP_PICK and y.stat()["phase"] == GP_PICK, 60, "两边进选人页")
        time.sleep(0.6)  # 刚进选人页 0.4 秒不收键
        x.press("ok")  # 库巴(新存档光标在库巴)
        for _ in range(3):
            y.press("down")  # 爷爷(同一个键连按,中间要松开一会儿)
            time.sleep(0.15)
        y.press("ok")
        wait(lambda: x.stat()["phase"] in (GP_COUNT, GP_PLAY), 60, "开打")
        sx, sy = x.stat(), y.stat()
        assert sx["char"] == 0 and sx["peer_char"] == 3 and sy["char"] == 3 and sy["peer_char"] == 0, (sx, sy)
        # 谁打、谁放大招:(攻, 挨打的) 攒气 → 挨打的放大招;两轮以后乱打
        plan = [("farm", x, y), ("super", y, x), ("farm", y, x), ("super", x, y), ("free", None, None)]
        stage, paused, lost_seen, checks = 0, False, False, 0
        end = time.time() + 900
        while time.time() < end:
            sx, sy = x.stat(), y.stat()
            if sx["phase"] == GP_OVER and sy["phase"] == GP_OVER:
                break
            # 两边同一阶段、同一回合才比(一边刚 KO、'O' 还在路上时比分本来就差一拍)
            if sx["phase"] == sy["phase"] and sx["phase"] in (GP_COUNT, GP_PLAY) and sx["round"] == sy["round"]:
                assert sx["me"] == sy["peer"] and sx["peer"] == sy["me"], (sx, sy)
                checks += 1
            if sx["phase"] != GP_PLAY or sy["phase"] != GP_PLAY:
                time.sleep(0.05)
                continue
            kind, att, vic = plan[stage]
            if kind == "free" and not paused and sx["round"] >= 2:
                y.ctl.ok("pause")  # 中途停住 y 3 秒:x 那边 1.5 秒没音信算断线
                t0 = time.time()
                while time.time() - t0 < 3:
                    lost_seen |= x.stat()["page"] == PG_LINK
                    time.sleep(0.1)
                y.ctl.ok("resume")
                assert lost_seen, "x 没发现断线"
                wait(lambda: x.stat()["page"] == PG_GAME and y.stat()["page"] == PG_GAME and
                     x.stat()["phase"] in (GP_COUNT, GP_PLAY), 30, "断线后接着打")
                paused = True
                print(f"  第 {sx['round']} 回合中途停住 y 3 秒:x 看到断线,连回来重开这一回合", flush=True)
                continue
            if kind == "farm":  # 攻的一方一直 ●,挨打的站着
                if att.stat()["st"] == FS_IDLE:
                    att.press("ok")
                if vic.stat()["g"] >= 1000:
                    stage += 1
                    print(f"  {vic.name} 挨打攒满一格气", flush=True)
                time.sleep(0.08)
            elif kind == "super":
                s = att.stat()
                if s["st"] == FS_IDLE and vic.stat()["st"] in (FS_IDLE, FS_GUARD) and att.try_super():
                    print(f"  {att.name} 放出大招", flush=True)
                    t0 = time.time()
                    while time.time() - t0 < 1.8:  # 特写 1.5 秒:两张卡都看着(数特写)
                        x.stat()
                        y.stat()
                        time.sleep(0.05)
                    stage += 1
                time.sleep(0.05)
            else:
                for c, s in ((x, sx), (y, sy)):
                    if s["st"] == FS_DIZZY:
                        c.press("ok", 40)
                    elif s["st"] == FS_IDLE and random.random() < 0.5:
                        c.press(random.choice(["ok", "ok", "up", "down"]))
                time.sleep(0.05)
        sx, sy = x.stat(), y.stat()
        assert sx["phase"] == sy["phase"] == GP_OVER, (sx, sy)
        assert sx["me"] == sy["peer"] and sx["peer"] == sy["me"] and max(sx["me"], sx["peer"]) == 2, (sx, sy)
        assert paused, "没测到断线"
        assert x.supers_done and y.supers_done, ("两边都要放过大招", x.supers_done, y.supers_done)
        assert x.supers_seen >= 2 and y.supers_seen >= 2, ("两张卡都要放过两次特写", x.supers_seen, y.supers_seen)
        print(f"一场打完 {sx['me']}:{sx['peer']}(两边一样),比分对过 {checks} 次;大招 x {x.supers_done} 次、"
              f"y {y.supers_done} 次,特写 x 卡 {x.supers_seen} 次、y 卡 {y.supers_seen} 次;中途断线续上")
        print("sim_duo: ok")
        return 0
    finally:
        x.close()
        y.close()
        if a.keep:
            print("留着:", root)
        else:
            shutil.rmtree(root)


if __name__ == "__main__":
    sys.exit(main())
