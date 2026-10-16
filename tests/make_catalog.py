#!/usr/bin/env python3
"""生成一份带库巴格斗(12 号,local:本仓库)的商店目录,给外壳的模拟器 / 卡上构建用(照 coopa-bomb)。
  tests/make_catalog.py SHELL OUT.toml
SHELL = 外壳仓库目录;目录里原有的游戏照抄。"""
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
shell, out = Path(sys.argv[1]).resolve(), Path(sys.argv[2])
src = (shell / "store" / "catalog.toml").read_text(encoding="utf-8")
src = src.replace('icons = ["icons/', f'icons = ["{shell}/store/icons/').replace('", "icons/', f'", "{shell}/store/icons/')
src = src.replace('repo = "local:', f'repo = "local:{shell}/')
out.write_text(src + f'''
[[game]]
no = 12
id = "fight"
author = "库巴老爸"
blurb = "三个键的格斗:轻拳打中马上接大招"
repo = "local:{GAME}"
commit = ""
price = 10
starter = false
listed = true
stars = [2, 3, 4]
size_kb = 400
tags = []
icons = []
''', encoding="utf-8")
print(out)
