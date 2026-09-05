#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
逐字符校验固件精灵点阵 == 网页预览点阵。

固件 src/pika.cpp 里的 BODY/TAIL/LEGS/ARMS 必须和 preview/ui.html 里的
PK_BODY/PK_TAIL/PK_LEGS/PK_ARMS 完全一致 —— 手抄 20 行点阵很容易错一个
字符,肉眼看不出来,但上屏就是缺一块。这个脚本把两边都解析成字符串列表
直接比。

用法: python firmware/tools/check_sprite.py
退出码 0 = 一致,1 = 有差异。
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI = os.path.join(ROOT, "preview", "ui.html")
PIKA = os.path.join(ROOT, "firmware", "src", "pika.cpp")


def read(path):
    with io.open(path, encoding="utf-8") as f:
        return f.read()


def js_rows(src, name):
    """取 const NAME = [ '...', '...' ]; 里的字符串(允许嵌套一层数组)。"""
    m = re.search(r"const\s+" + name + r"\s*=\s*\[(.*?)\n\];", src, re.S)
    if not m:
        raise SystemExit("preview: 找不到 %s" % name)
    body = m.group(1)
    # 嵌套数组(LEGS/ARMS 是 4 帧):按 [ ... ] 分组
    groups = re.findall(r"\[(.*?)\]", body, re.S)
    if groups:
        return [re.findall(r"'([^']*)'", g) for g in groups]
    return re.findall(r"'([^']*)'", body)


def cpp_rows(src, name):
    """取 const char NAME[...] PROGMEM = { "...", ... }; 里的字符串。"""
    m = re.search(
        r"const\s+char\s+" + name + r"\s*\[[^=]*PROGMEM\s*=\s*\{(.*?)\n\};",
        src, re.S)
    if not m:
        raise SystemExit("firmware: 找不到 %s" % name)
    body = m.group(1)
    groups = re.findall(r"\{(.*?)\}", body, re.S)
    if groups:
        return [re.findall(r'"([^"]*)"', g) for g in groups]
    return re.findall(r'"([^"]*)"', body)


def flat(x):
    """把 [[a,b],[c]] 拍平成带帧号的 (label, row) 列表。"""
    if x and isinstance(x[0], list):
        out = []
        for i, frame in enumerate(x):
            for j, row in enumerate(frame):
                out.append(("f%d.r%d" % (i, j), row))
        return out
    return [("r%d" % j, row) for j, row in enumerate(x)]


def main():
    ui, pk = read(UI), read(PIKA)
    pairs = [
        ("BODY", "PK_BODY"),
        ("TAIL", "PK_TAIL"),
        ("LEGS", "PK_LEGS"),
        ("ARMS", "PK_ARMS"),
    ]
    bad = 0
    for cname, jname in pairs:
        a = flat(cpp_rows(pk, cname))
        b = flat(js_rows(ui, jname))
        if len(a) != len(b):
            print("[FAIL] %s: 行数 固件 %d != 预览 %d" % (cname, len(a), len(b)))
            bad += 1
            continue
        diffs = [(lab, x, y) for (lab, x), (_, y) in zip(a, b) if x != y]
        if diffs:
            bad += 1
            print("[FAIL] %s: %d 行不一致" % (cname, len(diffs)))
            for lab, x, y in diffs:
                print("   %s" % lab)
                print("     固件 %r" % x)
                print("     预览 %r" % y)
        else:
            print("[ OK ] %s == %s (%d 行)" % (cname, jname, len(a)))
    if bad:
        print("\n%d 组点阵与预览不一致" % bad)
        return 1
    print("\n全部点阵与预览逐字符一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
