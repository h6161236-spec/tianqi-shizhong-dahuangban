#!/usr/bin/env python3
"""Export firmware's real U8g2 glyph pixels for the offline browser preview.

Uses the vendored C++ decoder itself; no downloaded fonts or Python packages.
Requires MSVC Build Tools on Windows, or c++/g++/clang++ on Linux/macOS.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "firmware/lib/U8g2_for_TFT_eSPI/src"

STUB = r"""
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
class Print { public: virtual ~Print() = default; virtual size_t write(uint8_t) = 0; };
class TFT_eSPI {
 public:
  static const int W = 640, H = 96;
  std::vector<uint16_t> pixels = std::vector<uint16_t>(W * H, 0);
  void clear() { std::fill(pixels.begin(), pixels.end(), 0); }
  void drawFastHLine(int x,int y,int n,uint16_t c) {
    for(int i=0;i<n;++i) if(x+i>=0 && x+i<W && y>=0 && y<H) pixels[y*W+x+i]=c;
  }
  void drawFastVLine(int x,int y,int n,uint16_t c) {
    for(int i=0;i<n;++i) if(x>=0 && x<W && y+i>=0 && y+i<H) pixels[(y+i)*W+x]=c;
  }
};
"""

EXPORTER = r"""
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "U8g2_for_TFT_eSPI.cpp"
#include "u8g2_fonts.c"

struct Glyph { int advance, x, y, w, h; std::vector<unsigned> rows; };

static std::vector<unsigned> encodings(U8g2_for_TFT_eSPI& face) {
  std::vector<unsigned> out;
  const uint8_t* p = face.u8g2.font + 23;
  while (p[1] != 0) { out.push_back(p[0]); p += p[1]; }
  if (face.u8g2.font_info.start_pos_unicode != 0) {
    p = face.u8g2.font + 23 + face.u8g2.font_info.start_pos_unicode;
    p += (unsigned(p[0]) << 8) | p[1]; // Skip the Unicode lookup table.
    while (p[0] != 0 || p[1] != 0) {
      out.push_back((unsigned(p[0]) << 8) | p[1]); p += p[2];
    }
  }
  return out;
}

static void verify(U8g2_for_TFT_eSPI& face, TFT_eSPI& screen,
                   const std::map<unsigned,Glyph>& glyphs, const char* sample) {
  screen.clear();
  const int actualAdvance = face.drawUTF8(24, 48, sample);
  const auto expectedPixels = screen.pixels;
  screen.clear();
  int advance = 0, width = 0, lastAdvance = 0, lastWidth = 0, lastX = 0;
  face.utf8_state = 0;
  for (const unsigned char* p = (const unsigned char*)sample; *p; ++p) {
    const unsigned cp = face.utf8_next(*p);
    if (cp >= 0xfffe) continue;
    const auto it = glyphs.find(cp);
    lastAdvance = 0;
    if (it == glyphs.end()) continue;
    const Glyph& g = it->second;
    for (int y=0; y<g.h; ++y) for (int x=0; x<g.w; ++x)
      if (g.rows[y] & (1u << x)) screen.drawFastHLine(24+advance+g.x+x,48+g.y+y,1,1);
    advance += g.advance;
    width += g.advance; lastAdvance = g.advance; lastWidth = g.w; lastX = g.x;
  }
  if (lastWidth) width += lastWidth + lastX - lastAdvance;
  if (screen.pixels != expectedPixels || advance != actualAdvance || width != face.getUTF8Width(sample)) {
    std::fprintf(stderr,"Glyph composition check failed for %s\n",sample); std::exit(2);
  }
}

static void exportFont(const char* name, const uint8_t* font, bool first) {
  TFT_eSPI screen;
  U8g2_for_TFT_eSPI face;
  face.begin(screen); face.setFont(font); face.setFontMode(1); face.setForegroundColor(1);
  std::map<unsigned,Glyph> glyphs;
  for (unsigned cp: encodings(face)) {
    screen.clear();
    Glyph g;
    g.advance = face.drawGlyph(24, 48, uint16_t(cp));
    g.w = face.u8g2.font_decode.glyph_width; g.h = face.u8g2.font_decode.glyph_height;
    g.x = face.u8g2.font_decode.target_x-24; g.y = face.u8g2.font_decode.target_y-48;
    if (g.w > 31) { std::fprintf(stderr,"Glyph too wide\n"); std::exit(2); }
    for (int y=0; y<g.h; ++y) {
      unsigned row=0;
      for (int x=0; x<g.w; ++x)
        if (screen.pixels[(48+g.y+y)*TFT_eSPI::W+24+g.x+x]) row |= 1u << x;
      g.rows.push_back(row);
    }
    glyphs.emplace(cp, g);
  }
  verify(face,screen,glyphs,"0123456789 12:34 -45.6");
  if (name[0]=='C') {
    verify(face,screen,glyphs,u8"歌手 晴天 周杰伦");
    verify(face,screen,glyphs,u8"为你写诗 / Wi-Fi / Agjp 空格 ");
    verify(face,screen,glyphs,u8"中文ABC。，！？（测试）");
  }
  std::printf("%s\"%s\":{\"ascent\":%d,\"descent\":%d,\"glyphs\":{\n",
              first?"":",\n",name,int(face.getFontAscent()),int(face.getFontDescent()));
  bool firstGlyph=true;
  for (const auto& pair:glyphs) {
    const Glyph& g=pair.second;
    std::printf("%s\"%u\":[%d,%d,%d,%d,%d",firstGlyph?"":",\n",pair.first,g.advance,g.x,g.y,g.w,g.h);
    for (unsigned row:g.rows) std::printf(",%u",row);
    std::printf("]"); firstGlyph=false;
  }
  std::printf("\n}}");
  std::fprintf(stderr,"%s: %zu glyphs, ascent %d, descent %d; raster/metrics checks passed\n",
               name,glyphs.size(),int(face.getFontAscent()),int(face.getFontDescent()));
}

int main() {
  std::printf("window.PIKA_FONTS = {\n");
  exportFont("CN12",u8g2_font_wqy12_t_gb2312,true);
  exportFont("CN16",u8g2_font_wqy16_t_gb2312,false);
  exportFont("NUM14",u8g2_font_t0_14b_tn,false);
  exportFont("NUM22",u8g2_font_t0_22b_tn,false);
  std::printf("\n};\n");
}
"""


def compile_exporter(build: Path) -> Path:
    source = build / "export.cpp"
    source.write_text(EXPORTER, encoding="utf-8")
    (build / "TFT_eSPI.h").write_text(STUB, encoding="utf-8")
    executable = build / ("export.exe" if os.name == "nt" else "export")
    if os.name == "nt":
        vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output([
            str(vswhere), "-latest", "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath",
        ], text=True).strip()
        if not installation:
            raise RuntimeError("MSVC C++ Build Tools are required to export fonts")
        vcvars = Path(installation) / "VC/Auxiliary/Build/vcvars64.bat"
        script = build / "compile.cmd"
        # Controlled local paths; percent escaping also supports % in a workspace path.
        def batch_path(path: Path) -> str:
            return '"' + str(path).replace("%", "%%") + '"'
        script.write_text(
            "@echo off\ncall " + batch_path(vcvars) + " >nul\n"
            "if errorlevel 1 exit /b %errorlevel%\n"
            "cl /nologo /utf-8 /EHsc /std:c++14 /O2 /I" + batch_path(build) +
            " /I" + batch_path(VENDOR) + " " + batch_path(source) +
            " /Fe:" + batch_path(executable) + "\n", encoding="utf-8")
        subprocess.run(["cmd.exe", "/d", "/c", str(script)], cwd=build, check=True)
    else:
        compiler = next((shutil.which(c) for c in ("c++", "g++", "clang++") if shutil.which(c)), None)
        if not compiler:
            raise RuntimeError("A C++ compiler is required to export fonts")
        subprocess.run([compiler, "-std=c++14", "-O2", "-I", str(build), "-I", str(VENDOR),
                        str(source), "-o", str(executable)], cwd=build, check=True)
    return executable


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "preview/fonts.js")
    parser.add_argument("--check", action="store_true", help="Verify committed output without changing it")
    args = parser.parse_args()
    digest = hashlib.sha256()
    for name in ("u8g2_fonts.c", "U8g2_for_TFT_eSPI.cpp", "U8g2_for_TFT_eSPI.h"):
        digest.update((VENDOR / name).read_bytes())
    header = ("// Generated by firmware/tools/export_preview_fonts.py; do not edit.\n"
              "// Original font/decoder licenses: firmware/lib/U8g2_for_TFT_eSPI/LICENSE\n"
              "// [advance, xOffset, topOffsetFromBaseline, width, height, ...rowMasksLSBLeft]\n"
              "// Vendored source SHA-256: " + digest.hexdigest() + "\n")
    with tempfile.TemporaryDirectory(prefix="pika-font-export-") as temporary:
        executable = compile_exporter(Path(temporary))
        result = subprocess.check_output([str(executable)])
    data = header.encode("utf-8") + result.replace(b"\r\n", b"\n")
    if args.check:
        if not args.output.exists() or args.output.read_bytes() != data:
            raise SystemExit("Preview font data is stale; run export_preview_fonts.py")
        print(f"Verified {args.output} ({len(data):,} bytes)")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(f"Wrote {args.output} ({len(data):,} bytes)")


if __name__ == "__main__":
    main()
