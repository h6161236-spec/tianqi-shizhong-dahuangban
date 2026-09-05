#include "text.h"

#include <U8g2_for_TFT_eSPI.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"

namespace {

U8g2_for_TFT_eSPI u8f;

/** 跑马灯循环时两遍文字之间的空隙,免得首尾贴在一起看不出断点 */
constexpr int MARQUEE_GAP = 44;

const uint8_t* const FONTS[TXT_FONT_N] = {
    u8g2_font_wqy12_t_gb2312,   // CN12
    u8g2_font_wqy16_t_gb2312,   // CN16
    u8g2_font_t0_14b_tn,        // NUM14
    u8g2_font_t0_22b_tn,        // NUM22
};

int8_t g_ascent[TXT_FONT_N];
int8_t g_descent[TXT_FONT_N];

/** 当前已经 setFont 过的字号。setFont 要重新解析字体头,能省就省。 */
uint8_t g_cur = 0xFF;

inline void use(TxtFont f) {
  if (g_cur != (uint8_t)f) {
    u8f.setFont(FONTS[f]);
    // U8g2_for_TFT_eSPI resets font mode to opaque on every setFont().
    // Re-apply transparent mode so glyphs do not acquire black rectangles
    // when pages switch between Chinese and numeric fonts.
    u8f.setFontMode(1);
    g_cur = (uint8_t)f;
  }
}

/** UTF-8 首字节 -> 这个字符占几个字节。非法字节按 1 跳过,保证不死循环。 */
inline size_t charBytes(uint8_t c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

inline void copyStr(char* out, size_t outSize, const char* src, size_t n) {
  if (!outSize) return;
  if (n >= outSize) n = outSize - 1;
  memcpy(out, src, n);
  out[n] = 0;
}

}  // namespace

namespace Txt {

void init(TFT_eSPI& screen) {
  u8f.begin(screen);
  u8f.setFontMode(1);       // 透明背景:不擦底,叠在卡片/渐变上才好看
  u8f.setFontDirection(0);

  // 把四个字号的度量值读出来缓存。顺带把字体符号引用住,让链接器带上。
  for (uint8_t i = 0; i < TXT_FONT_N; ++i) {
    u8f.setFont(FONTS[i]);
    g_ascent[i] = u8f.getFontAscent();
    g_descent[i] = u8f.getFontDescent();   // 通常是负数
  }
  g_cur = 0xFF;
}

void target(TFT_eSPI& t) {
  // begin() 只换 tft 指针,不动字体/字色状态,所以 g_cur 仍然有效
  u8f.begin(t);
}

int width(TxtFont f, const char* s) {
  if (!s || !*s) return 0;
  use(f);
  return (int)u8f.getUTF8Width(s);
}

int lineH(TxtFont f) { return (int)g_ascent[f] - (int)g_descent[f]; }
int ascent(TxtFont f) { return (int)g_ascent[f]; }

int draw(int x, int y, TxtFont f, uint16_t color, const char* s) {
  if (!s || !*s) return x;
  use(f);
  u8f.setForegroundColor(color);
  return x + (int)u8f.drawUTF8((int16_t)x, (int16_t)y, s);
}

int drawTop(int x, int topY, TxtFont f, uint16_t color, const char* s) {
  return draw(x, topY + g_ascent[f], f, color, s);
}

int drawCenter(int cx, int y, TxtFont f, uint16_t color, const char* s) {
  if (!s || !*s) return cx;
  use(f);
  const int w = (int)u8f.getUTF8Width(s);
  return draw(cx - w / 2, y, f, color, s);
}

int drawRight(int rx, int y, TxtFont f, uint16_t color, const char* s) {
  if (!s || !*s) return rx;
  use(f);
  const int w = (int)u8f.getUTF8Width(s);
  return draw(rx - w, y, f, color, s);
}

void clip(TxtFont f, const char* src, int maxPx, char* out, size_t outSize) {
  if (!outSize) return;
  if (!src) { out[0] = 0; return; }

  use(f);
  if ((int)u8f.getUTF8Width(src) <= maxPx) {
    copyStr(out, outSize, src, strlen(src));
    return;
  }

  static const char kEllipsis[] = "...";
  const int budget = maxPx - (int)u8f.getUTF8Width(kEllipsis);
  if (budget <= 0) { out[0] = 0; return; }

  // 逐个 UTF-8 字符累加宽度,直到再加一个就超预算
  size_t keep = 0;
  int w = 0;
  for (size_t i = 0; src[i];) {
    const size_t n = charBytes((uint8_t)src[i]);
    char one[5];
    memcpy(one, src + i, n);
    one[n] = 0;
    const int cw = (int)u8f.getUTF8Width(one);
    if (w + cw > budget) break;
    w += cw;
    i += n;
    keep = i;
  }

  copyStr(out, outSize, src, keep);
  // 追加省略号(空间不够就算了,截断本身已经表达了"还有更多")
  const size_t used = strlen(out);
  if (used + sizeof(kEllipsis) <= outSize) {
    memcpy(out + used, kEllipsis, sizeof(kEllipsis));
  }
}

int marquee(int y, int w, TxtFont f, uint16_t color, const char* s, int offset,
            uint8_t align) {
  if (!s || !*s) return 0;
  use(f);
  u8f.setForegroundColor(color);

  const int tw = (int)u8f.getUTF8Width(s);

  // 装得下就不滚,按对齐方式摆好
  if (tw <= w) {
    const int x = (align == 1) ? (w - tw) / 2 : (align == 2) ? (w - tw) : 0;
    u8f.drawUTF8((int16_t)x, (int16_t)y, s);
    return 0;
  }

  const int span = tw + MARQUEE_GAP;
  int off = offset % span;
  if (off < 0) off += span;

  // 第一遍:画到负坐标,Sprite 会精确裁掉左边超出的部分
  u8f.drawUTF8((int16_t)(-off), (int16_t)y, s);
  // 第二遍:尾巴滚进可视区时接上,形成无缝循环
  if (span - off < w) u8f.drawUTF8((int16_t)(span - off), (int16_t)y, s);

  return off;
}

void fadeEdges(TFT_eSprite& spr, int w, int h, int fadePx, uint16_t bg) {
  if (fadePx <= 0) return;
  if (fadePx * 2 > w) fadePx = w / 2;

  for (int i = 0; i < fadePx; ++i) {
    // i=0 在最边缘,完全变成底色;i=fadePx-1 几乎不变
    const uint8_t keep = (uint8_t)((i + 1) * 255 / (fadePx + 1));
    const int xl = i;
    const int xr = w - 1 - i;
    for (int y = 0; y < h; ++y) {
      spr.drawPixel(xl, y, lerp565(bg, spr.readPixel(xl, y), keep));
      spr.drawPixel(xr, y, lerp565(bg, spr.readPixel(xr, y), keep));
    }
  }
}

int utf8Len(const char* s) {
  if (!s) return 0;
  int n = 0;
  for (size_t i = 0; s[i]; i += charBytes((uint8_t)s[i])) ++n;
  return n;
}

void utf8Head(const char* src, int n, char* out, size_t outSize) {
  if (!outSize) return;
  if (!src || n <= 0) { out[0] = 0; return; }
  size_t i = 0;
  for (int k = 0; k < n && src[i]; ++k) i += charBytes((uint8_t)src[i]);
  copyStr(out, outSize, src, i);
}

}  // namespace Txt
