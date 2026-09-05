#include "gfx.h"

#include <math.h>

#include "text.h"   // 只为 Txt::target():push 前后要把 u8g2 绑回正确的 target
#include "theme.h"

namespace {

TFT_eSPI* g_tft = nullptr;
TFT_eSprite* g_big = nullptr;
TFT_eSprite* g_strip = nullptr;
TFT_eSprite* g_bar = nullptr;

/**
 * TFT_eSPI 的抗锯齿图元约定:bg_color 传这个哨兵值时,它会 readPixel 取
 * 真实底色来混合,而不是拿一个固定色硬混。
 * 七段数字的笔画在拐角必然互相压,只有用哨兵才不会出现接缝脏边。
 * 代价是每像素多一次读 —— 在 Sprite 上是内存读,可忽略。
 */
constexpr uint32_t BG_READ = 0x00FFFFFF;

// ---------------------------------------------------------------------
// 七段数字几何
//
//      --- a ---
//     |         |
//     f         b
//     |         |
//      --- g ---
//     |         |
//     e         c
//     |         |
//      --- d ---
// ---------------------------------------------------------------------
enum Seg : uint8_t { SA = 1 << 0, SB = 1 << 1, SC = 1 << 2, SD = 1 << 3,
                     SE = 1 << 4, SF = 1 << 5, SG = 1 << 6 };

const uint8_t DIGIT_SEG[10] = {
    /*0*/ SA | SB | SC | SD | SE | SF,
    /*1*/ SB | SC,
    /*2*/ SA | SB | SG | SE | SD,
    /*3*/ SA | SB | SG | SC | SD,
    /*4*/ SF | SG | SB | SC,
    /*5*/ SA | SF | SG | SC | SD,
    /*6*/ SA | SF | SG | SE | SC | SD,
    /*7*/ SA | SB | SC,
    /*8*/ SA | SB | SC | SD | SE | SF | SG,
    /*9*/ SA | SB | SC | SD | SF | SG,
};

/** 竖笔画切成几段来做渐变。段之间靠圆头重叠,看不出接缝。 */
constexpr int VSEG_STEPS = 6;

/**
 * 画一根有竖向渐变的粗线。
 * 横笔画整条同色(高度就那么点,渐变看不出来),竖笔画切段渐变。
 * @param spanY0/spanY1 渐变映射的参考范围(整个数字块的上下边),
 *                      这样同一个数字里所有笔画的渐变是连续的
 */
void stroke(TFT_eSPI& t, float x0, float y0, float x1, float y1, float wd,
            uint16_t c0, uint16_t c1, int spanY0, int spanY1, uint32_t bg) {
  const float spanH = (float)(spanY1 - spanY0);
  auto colAt = [&](float y) -> uint16_t {
    if (spanH <= 0.0f) return c0;
    float k = (y - (float)spanY0) / spanH;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return lerp565(c0, c1, (uint8_t)(k * 255.0f));
  };

  // 横笔画(或很短的笔画):一条搞定
  if (fabsf(y1 - y0) < wd) {
    t.drawWideLine(x0, y0, x1, y1, wd, colAt((y0 + y1) * 0.5f), bg);
    return;
  }

  // 竖笔画:切成 VSEG_STEPS 段,每段取自己位置的颜色
  for (int i = 0; i < VSEG_STEPS; ++i) {
    const float t0 = (float)i / VSEG_STEPS;
    const float t1 = (float)(i + 1) / VSEG_STEPS;
    const float ax = x0 + (x1 - x0) * t0, ay = y0 + (y1 - y0) * t0;
    const float bx = x0 + (x1 - x0) * t1, by = y0 + (y1 - y0) * t1;
    t.drawWideLine(ax, ay, bx, by, wd, colAt((ay + by) * 0.5f), bg);
  }
}

/** 画一个七段数字。glowCol != 0 时先垫一层更粗的暗色做外发光。 */
void drawDigit(TFT_eSPI& t, int x, int y, int w, int h, uint8_t mask,
               uint16_t c0, uint16_t c1, int spanY0, int spanY1, uint16_t bg,
               uint16_t glowCol) {
  const float sw = h * 0.135f;          // 笔画粗细
  const float in = sw * 0.62f;          // 内缩,保证笔画完全在 w×h 里
  const float L = x + in, R = x + w - in;
  const float T = y + in, M = y + h * 0.5f, B = y + h - in;

  struct Line { float x0, y0, x1, y1; uint8_t bit; };
  const Line lines[7] = {
      {L, T, R, T, SA},   // 上横
      {R, T, R, M, SB},   // 右上竖
      {R, M, R, B, SC},   // 右下竖
      {L, B, R, B, SD},   // 下横
      {L, M, L, B, SE},   // 左下竖
      {L, T, L, M, SF},   // 左上竖
      {L, M, R, M, SG},   // 中横
  };

  // 发光层:先整层画完,免得亮笔画被后面的光晕糊掉。
  // 这层不能用 BG_READ —— 否则重叠处光晕会累加,越叠越亮。
  if (glowCol) {
    for (const auto& ln : lines) {
      if (mask & ln.bit)
        t.drawWideLine(ln.x0, ln.y0, ln.x1, ln.y1, sw * 1.85f, glowCol, bg);
    }
  }

  for (const auto& ln : lines) {
    if (mask & ln.bit)
      stroke(t, ln.x0, ln.y0, ln.x1, ln.y1, sw, c0, c1, spanY0, spanY1,
             BG_READ);
  }
}

/** 冒号:两个圆点。宽度按 digitW/3 算。 */
void drawColon(TFT_eSPI& t, int x, int y, int w, int h, uint16_t c0,
               uint16_t c1, int spanY0, int spanY1, uint16_t bg,
               uint16_t glowCol) {
  const float r = h * 0.068f;
  const float cx = x + w * 0.5f;
  const float y0 = y + h * 0.32f, y1 = y + h * 0.70f;
  const float spanH = (float)(spanY1 - spanY0);
  auto colAt = [&](float yy) -> uint16_t {
    if (spanH <= 0.0f) return c0;
    float k = (yy - (float)spanY0) / spanH;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return lerp565(c0, c1, (uint8_t)(k * 255.0f));
  };

  if (glowCol) {
    t.drawSpot(cx, y0, r * 1.9f, glowCol, bg);
    t.drawSpot(cx, y1, r * 1.9f, glowCol, bg);
  }
  t.drawSpot(cx, y0, r, colAt(y0), BG_READ);
  t.drawSpot(cx, y1, r, colAt(y1), BG_READ);
}

/** float → int 四舍五入。抗锯齿图元里 int32_t 参数用它,别让 C++ 直接截断。 */
inline int32_t ri(float v) { return (int32_t)(v + (v < 0 ? -0.5f : 0.5f)); }

/** 圆角矩形在第 row 行(相对顶边)的水平内缩量,用于逐行渐变填充 */
inline int roundInset(int row, int h, int r) {
  int d = -1;
  if (row < r) d = r - 1 - row;
  else if (row >= h - r) d = row - (h - r);
  if (d < 0) return 0;
  const float k = sqrtf((float)(r * r - d * d));
  const int inset = r - (int)(k + 0.5f);
  return inset < 0 ? 0 : inset;
}

}  // namespace

namespace Gfx {

bool init(TFT_eSPI& screen_) {
  g_tft = &screen_;

  static TFT_eSprite sBig(&screen_);
  static TFT_eSprite sStrip(&screen_);
  static TFT_eSprite sBar(&screen_);
  g_big = &sBig;
  g_strip = &sStrip;
  g_bar = &sBar;

  sBig.setColorDepth(16);
  sStrip.setColorDepth(16);
  sBar.setColorDepth(16);

  // 先分配大的。堆此刻还是整块的,越早分配越不容易失败。
  if (!sBig.createSprite(GFX_BIG_W, GFX_BIG_H)) return false;
  if (!sStrip.createSprite(GFX_STRIP_W, GFX_STRIP_H)) return false;
  if (!sBar.createSprite(GFX_BAR_W, GFX_BAR_H)) return false;
  return true;
}

TFT_eSPI& screen() { return *g_tft; }
TFT_eSprite& big() { return *g_big; }
TFT_eSprite& strip() { return *g_strip; }
TFT_eSprite& bar() { return *g_bar; }

void beginBig(uint16_t bg) { g_big->fillSprite(bg); Txt::target(*g_big); }
void beginStrip(uint16_t bg) { g_strip->fillSprite(bg); Txt::target(*g_strip); }
void beginBar(uint16_t bg) { g_bar->fillSprite(bg); Txt::target(*g_bar); }

void pushBig(int x, int y) { g_big->pushSprite(x, y); Txt::target(*g_tft); }
void pushStrip(int x, int y) { g_strip->pushSprite(x, y); Txt::target(*g_tft); }
void pushBar(int x, int y) { g_bar->pushSprite(x, y); Txt::target(*g_tft); }

void pushBigTransparent(int x, int y, uint16_t transparentColor) {
  g_big->pushSprite(x, y, transparentColor);
  Txt::target(*g_tft);
}

void pushStripTransparent(int x, int y, uint16_t transparentColor) {
  g_strip->pushSprite(x, y, transparentColor);
  Txt::target(*g_tft);
}

void pushStripH(int x, int y, int h) {
  if (h >= GFX_STRIP_H) { pushStrip(x, y); return; }
  // Sprite 内存是连续的 RGB565,整块宽度一致,所以取前 h 行直接就是
  // 前 w*h 个像素,不用处理跨距。Sprite 内部按字节交换存,推之前要开 swap。
  const bool sw = g_tft->getSwapBytes();
  g_tft->setSwapBytes(true);
  g_tft->pushImage(x, y, GFX_STRIP_W, h, (uint16_t*)g_strip->getPointer());
  g_tft->setSwapBytes(sw);
  Txt::target(*g_tft);
}

void pushBigRegion(int x, int y, int w, int h, int srcX, int srcY) {
  // big 的行跨距是 GFX_BIG_W,子区域跨行不连续,只能逐行推。
  // 每行一次 pushImage,行高 1,调用开销换正确性;时钟页 ~48 行,够快。
  if (w <= 0 || h <= 0) return;
  if (srcX == 0 && w == GFX_BIG_W) {
    // 整行宽特例:连续,一次推完
    const bool sw = g_tft->getSwapBytes();
    g_tft->setSwapBytes(true);
    g_tft->pushImage(x, y, w, h, (uint16_t*)g_big->getPointer() +
                                       (size_t)srcY * GFX_BIG_W);
    g_tft->setSwapBytes(sw);
  } else {
    const bool sw = g_tft->getSwapBytes();
    g_tft->setSwapBytes(true);
    for (int row = 0; row < h; ++row) {
      g_tft->pushImage(x, y + row, w, 1,
                       (uint16_t*)g_big->getPointer() +
                           (size_t)(srcY + row) * GFX_BIG_W + srcX);
    }
    g_tft->setSwapBytes(sw);
  }
  Txt::target(*g_tft);
}

// ---------------------------------------------------------------------
// 卡片
// ---------------------------------------------------------------------

void card(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t fill,
          uint16_t bg) {
  t.fillSmoothRoundRect(x, y, w, h, r, fill, bg);
}

void cardRim(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t fill,
             uint16_t rim, uint16_t bg) {
  t.fillSmoothRoundRect(x, y, w, h, r, fill, bg);
  // drawSmoothRoundRect 的签名是 (x, y, r_outer, r_inner, w, h, fg, bg)
  t.drawSmoothRoundRect(x, y, r, r - 1, w, h, rim, bg);
}

void cardGrad(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t c0,
              uint16_t c1, uint16_t bg) {
  if (h <= 0 || w <= 0) return;
  // 逐行画,行内是纯色、行间渐变;两端按圆角内缩
  for (int row = 0; row < h; ++row) {
    const uint8_t k = (uint8_t)(row * 255 / (h - 1 > 0 ? h - 1 : 1));
    const uint16_t c = lerp565(c0, c1, k);
    const int ins = roundInset(row, h, r);
    const int lw = w - ins * 2;
    if (lw > 0) t.drawFastHLine(x + ins, y + row, lw, c);
  }
  // 圆角的阶梯边缘用一圈抗锯齿描边盖掉
  t.drawSmoothRoundRect(x, y, r, r - 1, w, h, lerp565(c0, c1, 128), bg);
}

void divider(TFT_eSPI& t, int x, int y, int w, uint16_t c, uint16_t bg) {
  const int fade = w / 4 > 24 ? 24 : w / 4;
  for (int i = 0; i < w; ++i) {
    uint8_t a = 255;
    if (i < fade) a = (uint8_t)(i * 255 / fade);
    else if (i >= w - fade) a = (uint8_t)((w - 1 - i) * 255 / fade);
    t.drawPixel(x + i, y, lerp565(bg, c, a));
  }
}

void dots(TFT_eSPI& t, int cx, int y, int n, int cur, uint16_t on, uint16_t off,
          uint16_t bg) {
  constexpr int STEP = 14;
  const int x0 = cx - (n - 1) * STEP / 2;
  for (int i = 0; i < n; ++i) {
    const bool hit = (i == cur);
    // 当前页画成小横条,其他画点 —— 比单纯变色更好认
    if (hit) t.fillSmoothRoundRect(x0 + i * STEP - 5, y - 2, 11, 4, 2, on, bg);
    else t.fillSmoothCircle(x0 + i * STEP, y, 2, off, bg);
  }
}

void progress(TFT_eSPI& t, int x, int y, int w, int h, float frac,
              uint16_t track, uint16_t c0, uint16_t c1, uint16_t bg) {
  if (w <= 0 || h <= 0) return;
  const int r = h / 2;
  t.fillSmoothRoundRect(x, y, w, h, r, track, bg);
  if (frac <= 0.0f) return;
  if (frac > 1.0f) frac = 1.0f;

  int fw = (int)(w * frac + 0.5f);
  if (fw < h) fw = h;                 // 太短时至少保持一个圆头,不然是个方块
  // 已完成段:逐列横向渐变
  for (int i = 0; i < fw; ++i) {
    const uint8_t k = (uint8_t)(i * 255 / (w - 1 > 0 ? w - 1 : 1));
    const uint16_t c = lerp565(c0, c1, k);
    // 用 roundInset 的思路做左右圆头
    int top = 0, bot = h;
    if (i < r) {
      const int d = r - 1 - i;
      const int cut = r - (int)(sqrtf((float)(r * r - d * d)) + 0.5f);
      top = cut; bot = h - cut;
    } else if (i >= fw - r) {
      const int d = i - (fw - r);
      const int cut = r - (int)(sqrtf((float)(r * r - d * d)) + 0.5f);
      top = cut; bot = h - cut;
    }
    if (bot > top) t.drawFastVLine(x + i, y + top, bot - top, c);
  }
}

void vbar(TFT_eSPI& t, int x, int baseY, int w, int h, uint16_t c0, uint16_t c1,
          uint16_t bg) {
  (void)bg;
  if (h <= 0 || w <= 0) return;
  const int y = baseY - h + 1;
  // 圆头半径不能超过柱高的一半,否则矮柱子会被圆角吃掉。
  // 频谱静音时 16 根都很矮,退化成一排圆点很难看,夹一下就还是"短柱子"。
  int r = w / 2;
  if (r > h / 2) r = h / 2;

  for (int row = 0; row < h; ++row) {
    // row=0 在顶,取 c1;row=h-1 在底,取 c0
    const uint8_t k = (h > 1) ? (uint8_t)((h - 1 - row) * 255 / (h - 1)) : 255;
    const uint16_t c = lerp565(c0, c1, k);
    // 只有顶端做圆头,底端是平的(坐在基线上)
    int ins = 0;
    if (r > 0 && row < r) {
      const int d = r - 1 - row;
      ins = r - (int)(sqrtf((float)(r * r - d * d)) + 0.5f);
      if (ins < 0) ins = 0;
    }
    const int lw = w - ins * 2;
    if (lw > 0) t.drawFastHLine(x + ins, y + row, lw, c);
  }
}

// ---------------------------------------------------------------------
// 圆环 / 发光
// ---------------------------------------------------------------------

void ring(TFT_eSPI& t, int cx, int cy, int r, int thick, float frac,
          uint16_t track, uint16_t c, uint16_t bg) {
  const int ir = r - thick + 1;
  if (track) t.drawSmoothArc(cx, cy, r, ir, 0, 360, track, bg, false);
  if (frac <= 0.0f) return;
  if (frac > 1.0f) frac = 1.0f;

  // drawSmoothArc 从 6 点方向顺时针起算,所以 12 点 = 180°
  const float sweep = frac * 360.0f;
  // 极短弧:drawArc 可能一个像素都不画,但两端的圆头照画,
  // 结果是两个重叠的点。直接画一个点,干净。
  if (sweep < 3.0f) {
    const int mr = (r + ir) / 2;
    t.drawSpot((float)cx, (float)(cy - mr), (r - ir) / 2.0f + 0.5f, c, bg);
    return;
  }
  uint32_t a0 = 180;
  uint32_t a1 = (uint32_t)(180.0f + sweep) % 360u;
  if (frac >= 0.999f) { a0 = 0; a1 = 360; }   // 整圈
  t.drawSmoothArc(cx, cy, r, ir, a0, a1, c, bg, true);
}

void glow(TFT_eSPI& t, int cx, int cy, int r, uint16_t c, uint16_t bg) {
  // 由外到内画同心圆环,越靠内越亮 —— 合起来就是一块径向渐变的光斑。
  // 层与层首尾相接(inner 正好等于下一圈的 outer),中间不留缝。
  // 层数少一点省时间,15% 的最大强度下肉眼看不出分层。
  constexpr int LAYERS = 5;
  const int step = r / LAYERS + 1;
  for (int i = LAYERS; i >= 1; --i) {
    const int rr = r * i / LAYERS;
    if (rr < 1) continue;
    int ir = rr - step;
    if (ir < 0) ir = 0;
    const uint8_t a = (uint8_t)(38 * (LAYERS - i + 1) / LAYERS);
    t.drawSmoothArc(cx, cy, rr, ir, 0, 360, lerp565(bg, c, a), bg, false);
  }
}

// ---------------------------------------------------------------------
// 大号时间
// ---------------------------------------------------------------------

int timeWidth(const char* s, int digitW, int digitH, int gap) {
  (void)digitH;
  int w = 0;
  for (const char* p = s; *p; ++p) {
    if (*p == ':') w += digitW / 3;
    else if (*p >= '0' && *p <= '9') w += digitW;
    else continue;
    if (*(p + 1)) w += gap;
  }
  return w;
}

int bigTime(TFT_eSPI& t, int x, int y, const char* s, int digitW, int digitH,
            int gap, uint16_t c0, uint16_t c1, uint16_t bg, bool glowOn) {
  const int total = timeWidth(s, digitW, digitH, gap);
  const uint16_t glowC = glowOn ? lerp565(bg, lerp565(c0, c1, 128), 70) : 0;

  int cx = x;
  for (const char* p = s; *p; ++p) {
    if (*p == ':') {
      const int w = digitW / 3;
      drawColon(t, cx, y, w, digitH, c0, c1, y, y + digitH, bg, glowC);
      cx += w;
    } else if (*p >= '0' && *p <= '9') {
      // 横向也带一点渐变:按这个字在整块里的位置,把上下两个端色一起偏移
      const uint8_t hk = total > digitW
                             ? (uint8_t)((cx - x) * 200 / (total - digitW))
                             : 0;
      const uint16_t a = lerp565(c0, c1, hk / 4);
      const uint16_t b = lerp565(c0, c1, (uint8_t)(255 - (255 - hk) / 4));
      drawDigit(t, cx, y, digitW, digitH, DIGIT_SEG[*p - '0'], a, b, y,
                y + digitH, bg, glowC);
      cx += digitW;
    } else {
      continue;
    }
    if (*(p + 1)) cx += gap;
  }
  return total;
}

// ---------------------------------------------------------------------
// 图标
// ---------------------------------------------------------------------

void glyph(TFT_eSPI& t, Glyph g, int cx, int cy, int box, uint16_t c,
           uint16_t bg) {
  const float h = box * 0.5f;
  const float lw = box * 0.11f < 1.6f ? 1.6f : box * 0.11f;

  switch (g) {
    case GL_PLAY: {
      // 圆头三角:三条粗线围出来,比 fillTriangle 柔和
      const float x0 = cx - h * 0.45f, x1 = cx + h * 0.62f;
      t.drawWideLine(x0, cy - h * 0.72f, x1, cy, lw, c, bg);
      t.drawWideLine(x1, cy, x0, cy + h * 0.72f, lw, c, bg);
      t.drawWideLine(x0, cy + h * 0.72f, x0, cy - h * 0.72f, lw, c, bg);
      // 填心
      for (float k = 0.12f; k < 1.0f; k += 0.11f) {
        const float xe = x0 + (x1 - x0) * k;
        const float ye = h * 0.72f * (1.0f - k);
        t.drawWideLine(xe, cy - ye, xe, cy + ye, lw * 0.9f, c, bg);
      }
      break;
    }
    case GL_PAUSE: {
      const float w = box * 0.17f;
      const int32_t bw = ri(w) < 2 ? 2 : ri(w);
      t.fillSmoothRoundRect(ri(cx - h * 0.55f - w / 2), ri(cy - h * 0.7f), bw,
                            ri(h * 1.4f), bw / 2, c, bg);
      t.fillSmoothRoundRect(ri(cx + h * 0.55f - w / 2), ri(cy - h * 0.7f), bw,
                            ri(h * 1.4f), bw / 2, c, bg);
      break;
    }
    case GL_PREV:
    case GL_NEXT: {
      const float dir = (g == GL_NEXT) ? 1.0f : -1.0f;
      // 两个 V 形箭头 + 一条竖挡板
      for (int i = 0; i < 2; ++i) {
        const float ox = cx + dir * (i * box * 0.24f - box * 0.14f);
        t.drawWideLine(ox - dir * h * 0.28f, cy - h * 0.58f, ox + dir * h * 0.3f,
                       cy, lw, c, bg);
        t.drawWideLine(ox + dir * h * 0.3f, cy, ox - dir * h * 0.28f,
                       cy + h * 0.58f, lw, c, bg);
      }
      const float bx = cx + dir * h * 0.78f;
      t.drawWideLine(bx, cy - h * 0.58f, bx, cy + h * 0.58f, lw, c, bg);
      break;
    }
    case GL_BLUETOOTH: {
      const float t0 = h * 0.78f;
      t.drawWideLine(cx, cy - t0, cx, cy + t0, lw, c, bg);
      t.drawWideLine(cx, cy - t0, cx + h * 0.5f, cy - t0 * 0.42f, lw, c, bg);
      t.drawWideLine(cx + h * 0.5f, cy - t0 * 0.42f, cx - h * 0.5f,
                     cy + t0 * 0.42f, lw, c, bg);
      t.drawWideLine(cx, cy + t0, cx + h * 0.5f, cy + t0 * 0.42f, lw, c, bg);
      t.drawWideLine(cx + h * 0.5f, cy + t0 * 0.42f, cx - h * 0.5f,
                     cy - t0 * 0.42f, lw, c, bg);
      break;
    }
    case GL_WIFI: {
      // 三段同心弧 + 底部圆点。drawSmoothArc 的 0° 在 6 点,角度增大是
      // 6点→9点→12点→3点(屏幕上顺时针),所以 12 点 = 180°,
      // 开口朝上的扇形 = 130°~230°。
      const int by = (int)(cy + h * 0.62f + 0.5f);
      for (int i = 1; i <= 3; ++i) {
        const int r = (int)(box * 0.17f * i + box * 0.1f + 0.5f);
        int ir = r - (int)(lw + 0.5f);
        if (ir < 1) ir = 1;
        t.drawSmoothArc(cx, by, r, ir, 130, 230, c, bg, true);
      }
      t.fillSmoothCircle(cx, by, (int)(lw * 0.9f + 0.5f), c, bg);
      break;
    }
    case GL_LOCK: {
      // WiFi 加密网络:锁体圆角矩形 + 上方锁环(开口弧)
      const float bw = h * 0.72f, bh = h * 0.58f;
      const float by = cy + h * 0.08f;
      t.fillSmoothRoundRect(ri(cx - bw / 2), ri(by), ri(bw), ri(bh), ri(bw * 0.22f),
                            c, bg);
      const int32_t sr = ri(h * 0.34f);
      t.drawSmoothArc(cx, ri(by + 2), sr, sr - ri(lw), 60, 300, c, bg, true);
      break;
    }
    case GL_NOTE: {
      const float sx = cx + h * 0.42f;
      t.drawWideLine(sx, cy + h * 0.42f, sx, cy - h * 0.78f, lw, c, bg);
      t.fillSmoothCircle(cx - h * 0.06f, cy + h * 0.46f, (int)(box * 0.15f), c,
                         bg);
      t.drawWideLine(sx, cy - h * 0.78f, sx + h * 0.52f, cy - h * 0.5f, lw, c,
                     bg);
      break;
    }
  }
}

void wxIcon(TFT_eSPI& t, WxIcon ic, int cx, int cy, int box, uint16_t bg) {
  const float h = box * 0.5f;
  const float lw = box * 0.09f < 1.5f ? 1.5f : box * 0.09f;

  // 太阳:圆心 + 八道光芒
  auto sun = [&](float ox, float oy, float scale, uint16_t c) {
    const float r = h * 0.34f * scale;
    t.fillSmoothCircle(ri(cx + ox), ri(cy + oy), ri(r), c, bg);
    for (int i = 0; i < 8; ++i) {
      const float a = i * PI / 4.0f;
      const float r0 = r + h * 0.14f * scale, r1 = r + h * 0.36f * scale;
      t.drawWideLine(cx + ox + cosf(a) * r0, cy + oy + sinf(a) * r0,
                     cx + ox + cosf(a) * r1, cy + oy + sinf(a) * r1,
                     lw * scale, c, bg);
    }
  };

  // 云:三个圆叠出鼓包 + 一条平底。三个圆都和基线相切,底边才齐。
  auto cloud = [&](float oy, float scale, uint16_t c) {
    const float r1 = h * 0.36f * scale, r2 = h * 0.28f * scale,
                r3 = h * 0.24f * scale;
    const float base = cy + oy + h * 0.22f * scale;
    t.fillSmoothCircle(ri(cx - h * 0.28f * scale), ri(base - r3), ri(r3), c, bg);
    t.fillSmoothCircle(ri(cx + h * 0.02f * scale), ri(base - r1), ri(r1), c, bg);
    t.fillSmoothCircle(ri(cx + h * 0.4f * scale), ri(base - r2), ri(r2), c, bg);
    t.fillSmoothRoundRect(ri(cx - h * 0.58f * scale), ri(base - r3),
                          ri(h * 1.16f * scale), ri(r3), ri(r3 * 0.5f), c, bg);
  };

  // 雨滴 / 雪花 / 冰晶:云下面排 n 个
  auto drops = [&](float oy, int n, bool snow, uint16_t c) {
    for (int i = 0; i < n; ++i) {
      const float dx = cx + (i - (n - 1) * 0.5f) * h * 0.36f;
      const float y0 = cy + oy;
      if (snow) {
        for (int k = 0; k < 3; ++k) {
          const float a = k * PI / 3.0f;
          const float r = h * 0.12f;
          t.drawWideLine(dx - cosf(a) * r, y0 - sinf(a) * r, dx + cosf(a) * r,
                         y0 + sinf(a) * r, lw * 0.7f, c, bg);
        }
      } else {
        t.drawWideLine(dx + h * 0.06f, y0, dx - h * 0.06f, y0 + h * 0.3f,
                       lw * 0.85f, c, bg);
      }
    }
  };

  // 闪电:三段折线
  auto bolt = [&](uint16_t c) {
    const float y0 = cy + h * 0.26f;
    t.drawWideLine(cx + h * 0.12f, y0, cx - h * 0.14f, y0 + h * 0.34f,
                   lw * 1.1f, c, bg);
    t.drawWideLine(cx - h * 0.14f, y0 + h * 0.34f, cx + h * 0.14f,
                   y0 + h * 0.3f, lw * 1.1f, c, bg);
    t.drawWideLine(cx + h * 0.14f, y0 + h * 0.3f, cx - h * 0.06f,
                   y0 + h * 0.72f, lw * 1.1f, c, bg);
  };

  switch (ic) {
    case WX_CLEAR:
      sun(0, 0, 1.15f, TH::AMBER);
      break;
    case WX_MOSTLY:   // 基本晴:大太阳 + 旁边一朵小云
      sun(-h * 0.16f, -h * 0.1f, 1.05f, TH::AMBER);
      cloud(h * 0.3f, 0.62f, TH::TXT_MID);
      break;
    case WX_PARTLY:   // 少云:半露的太阳 + 主体云
      sun(h * 0.34f, -h * 0.34f, 0.78f, TH::AMBER);
      cloud(h * 0.16f, 1.0f, TH::TXT);
      break;
    case WX_OVERCAST:
      cloud(-h * 0.12f, 0.86f, TH::TXT_MID);
      cloud(h * 0.2f, 1.0f, TH::TXT);
      break;
    case WX_FOG:
      cloud(-h * 0.16f, 0.92f, TH::TXT_MID);
      for (int i = 0; i < 3; ++i) {
        const float y = cy + h * 0.36f + i * h * 0.22f;
        const float w = h * (0.86f - i * 0.12f);
        t.drawWideLine(cx - w, y, cx + w, y, lw * 0.75f, TH::TXT_LO, bg);
      }
      break;
    case WX_DRIZZLE:
      cloud(-h * 0.2f, 1.0f, TH::TXT);
      drops(h * 0.42f, 2, false, TH::BLUE);
      break;
    case WX_RAIN:
      cloud(-h * 0.2f, 1.0f, TH::TXT);
      drops(h * 0.42f, 3, false, TH::BLUE);
      break;
    case WX_HEAVYRAIN: {   // 大雨:双层云 + 4 滴 + 风感斜线
      cloud(-h * 0.24f, 0.9f, TH::TXT_MID);
      cloud(-h * 0.14f, 1.0f, TH::TXT);
      drops(h * 0.44f, 4, false, TH::BLUE);
      t.drawWideLine(cx - h * 0.72f, cy + h * 0.1f, cx - h * 0.44f,
                     cy + h * 0.1f, lw * 0.6f, TH::TXT_LO, bg);
      t.drawWideLine(cx + h * 0.44f, cy + h * 0.9f, cx + h * 0.72f,
                     cy + h * 0.9f, lw * 0.6f, TH::TXT_LO, bg);
      break;
    }
    case WX_FREEZING: {   // 冻雨:云 + 雨滴 + 两侧冰晶闪光
      cloud(-h * 0.2f, 1.0f, TH::TXT);
      drops(h * 0.42f, 2, false, TH::BLUE);
      for (int i = 0; i < 2; ++i) {
        const float dx = cx + (i ? 1 : -1) * h * 0.78f;
        const float dy = cy + h * (i ? 0.62f : 0.1f);
        const float r = h * 0.14f;
        for (int k = 0; k < 4; ++k) {
          const float a = k * PI / 4.0f;
          t.drawWideLine(dx - cosf(a) * r, dy - sinf(a) * r,
                         dx + cosf(a) * r, dy + sinf(a) * r, lw * 0.55f,
                         TH::BLUE, bg);
        }
      }
      break;
    }
    case WX_SNOW:
      cloud(-h * 0.2f, 1.0f, TH::TXT);
      drops(h * 0.5f, 3, true, TH::TXT_MID);
      break;
    case WX_HEAVYSNOW:   // 大雪:双层云 + 5 片雪花
      cloud(-h * 0.24f, 0.9f, TH::TXT_MID);
      cloud(-h * 0.14f, 1.0f, TH::TXT);
      drops(h * 0.5f, 5, true, TH::TXT_MID);
      break;
    case WX_THUNDER:
      cloud(-h * 0.2f, 1.0f, TH::TXT_MID);
      bolt(TH::AMBER);
      drops(h * 0.66f, 2, false, TH::BLUE);
      break;
    case WX_UNKNOWN:
    default: {
      // 圆圈 + 手绘问号。不用 Txt —— 它靠全局状态记着 u8g2 绑在哪个 target,
      // 而这里的 t 是调用者传的,两者可能不一致,字就画到别的缓冲里去了。
      const int32_t r = ri(h * 0.82f);
      t.drawSmoothArc(cx, cy, r, r - ri(lw), 0, 360, TH::TXT_LO, bg, false);
      // 问号上半的钩:跨过 12 点(=180°)的弧,开口朝下。
      const int32_t hr = ri(h * 0.3f);
      t.drawSmoothArc(cx, ri(cy - h * 0.22f), hr, hr - ri(lw), 100, 300,
                      TH::TXT_LO, bg, true);
      t.drawWideLine(cx + h * 0.02f, cy - h * 0.02f, cx + h * 0.02f,
                     cy + h * 0.2f, lw, TH::TXT_LO, bg);
      t.fillSmoothCircle(ri(cx + h * 0.02f), ri(cy + h * 0.44f), ri(lw * 0.7f),
                         TH::TXT_LO, bg);
      break;
    }
  }
}

}  // namespace Gfx
