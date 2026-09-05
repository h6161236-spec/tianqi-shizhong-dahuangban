#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
校验固件配色表 == 网页预览配色表。

theme.h 的 TH::* / PK::* 是手抄 preview/ui.html 的 RGB / SPR_PAL 的,
抄错一个十六进制位在屏上就是另一个颜色。这个脚本把两边解析出来直接比。

用法: python firmware/tools/check_theme.py
退出码 0 = 一致,1 = 有差异。
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI = os.path.join(ROOT, "preview", "ui.html")
THEME = os.path.join(ROOT, "firmware", "src", "theme.h")


def read(path):
    with io.open(path, encoding="utf-8") as f:
        return f.read()


def preview_theme(src):
    """预览 const RGB = { BG:0xF8F6EF, ... }"""
    m = re.search(r"const\s+RGB\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("preview: 找不到 RGB 调色板")
    return {k: int(v, 16)
            for k, v in re.findall(r"(\w+)\s*:\s*0x([0-9A-Fa-f]{6})", m.group(1))}


def preview_sprite(src):
    """预览 const SPR_PAL = { Y: '#F8D030', ... }"""
    m = re.search(r"const\s+SPR_PAL\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("preview: 找不到 SPR_PAL")
    return {k: int(v, 16)
            for k, v in re.findall(r"(\w+)\s*:\s*'#([0-9A-Fa-f]{6})'", m.group(1))}


def firmware_ns(src, ns):
    """firmware namespace 里的 constexpr uint16_t NAME = hex565(0x......);"""
    m = re.search(r"namespace\s+" + ns + r"\s*\{(.*?)\n\}\s*//\s*namespace\s+" + ns,
                  src, re.S)
    if not m:
        raise SystemExit("firmware: 找不到 namespace %s" % ns)
    return {k: int(v, 16)
            for k, v in re.findall(
                r"constexpr\s+uint16_t\s+(\w+)\s*=\s*hex565\(0x([0-9A-Fa-f]{6})\)",
                m.group(1))}


def cmp(label, fw, pv):
    bad = 0
    for k in sorted(pv):
        if k not in fw:
            print("[FAIL] %s.%s 预览有(0x%06X),固件缺" % (label, k, pv[k]))
            bad += 1
        elif fw[k] != pv[k]:
            print("[FAIL] %s.%s 固件 0x%06X != 预览 0x%06X" % (label, k, fw[k], pv[k]))
            bad += 1
    if not bad:
        print("[ OK ] %s 全部 %d 色与预览一致" % (label, len(pv)))
    return bad


def main():
    ui, th = read(UI), read(THEME)
    bad = 0
    bad += cmp("TH", firmware_ns(th, "TH"), preview_theme(ui))
    bad += cmp("PK", firmware_ns(th, "PK"), preview_sprite(ui))
    if bad:
        print("\n%d 处配色与预览不一致" % bad)
        return 1
    print("\n配色表与预览完全一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
