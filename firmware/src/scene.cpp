/**
 * =====================================================================
 *  scene.cpp —— 场景引擎实现(移植自 preview/ui.html drawScene)
 * =====================================================================
 *  所有 canvas 的 globalAlpha 叠加改成「向背景色 lerp」:先算 24bit 混合
 *  色,落屏前转 RGB565。天空每 2px 一条带,一帧 ~85 条 drawFastHLine。
 */

#include "scene.h"
#include "pika.h"
#include "theme.h"

#include <math.h>

namespace {

// 匿名命名空间在全局作用域:把 Scene::Params 引进来,组参数引用不用全限定
using Scene::Params;

// ---------------------------------------------------------------- 几何常量
constexpr int SCENE_TOP = 18;    // 场景顶(状态栏下面)
constexpr int HORIZON = 188;     // 地平线
constexpr int SCENE_BOT = 240;

// ---------------------------------------------------------------- 颜色工具
// 24bit 混合(与预览 mixHex 一致:先在 24bit 域混合,最后量化)
inline uint32_t mix3(uint32_t a, uint32_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return ((uint32_t)(ar + (br - ar) * t) << 16) |
         ((uint32_t)(ag + (bg - ag) * t) << 8) |
         (uint32_t)(ab + (bb - ab) * t);
}
inline uint16_t c565(uint32_t c) { return hex565(c); }

// ---------------------------------------------------------------- 天空关键帧
struct SkyKey { float h; uint32_t top, bot; };
const SkyKey SKY_KEYS[] = {
    {0.0f, 0x0A0E2A, 0x24325E},  {4.5f, 0x141A3E, 0x3A4470},
    {6.0f, 0x3E4A8E, 0xC97A52},  {7.5f, 0x5A8CD8, 0xF2C590},
    {10.0f, 0x4E9CE8, 0xB5DFF8}, {13.0f, 0x4290E8, 0xBDE6FA},
    {16.5f, 0x5288D0, 0xE0CFA0}, {18.0f, 0x54508E, 0xE88A54},
    {19.5f, 0x2B3268, 0x8A5468}, {21.0f, 0x12162E, 0x2E3A62},
    {24.0f, 0x0A0E2A, 0x24325E},
};
constexpr int SKY_N = sizeof(SKY_KEYS) / sizeof(SKY_KEYS[0]);

void skyAt24(float h, uint32_t& top, uint32_t& bot) {
  int i = 0;
  while (i < SKY_N - 2 && SKY_KEYS[i + 1].h <= h) ++i;
  const SkyKey& a = SKY_KEYS[i];
  const SkyKey& b = SKY_KEYS[i + 1];
  float t = (h - a.h) / (b.h - a.h);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  top = mix3(a.top, b.top, t);
  bot = mix3(a.bot, b.bot, t);
}

// 天空关键帧 + 组调色(阴天/雾/雷暴/冻雨)。draw() 和取色助手共用一条路,
// 保证页面叠的元素(暗纱/时间药丸)和场景底色一致
void skyMods(float hourF, const Params& p, uint32_t& top, uint32_t& bot) {
  skyAt24(hourF, top, bot);
  if (p.clouds >= 0.999f && p.dark > 0.0f) {
    top = mix3(top, 0x6E747C, p.dark * 0.6f);
    bot = mix3(bot, 0x8A9096, p.dark * 0.55f);
  }
  if (p.fog > 0.3f) {
    top = mix3(top, 0xB9B5A6, p.fog * 0.3f);
    bot = mix3(bot, 0xC5C1B2, p.fog * 0.45f);
  }
  if (p.bolt > 0.0f) {
    top = mix3(top, 0x23273A, 0.5f);
    bot = mix3(bot, 0x353A50, 0.5f);
  }
  if (p.ice) {
    top = mix3(top, 0x8FB4D8, 0.25f);
    bot = mix3(bot, 0xB8D0E6, 0.3f);
  }
}

// 雪覆盖度(snow*1.2,封顶 1)
inline float snowCoverOf(const Params& p) {
  if (p.snow <= 0.0f) return 0.0f;
  const float s = p.snow * 1.2f;
  return s > 1.0f ? 1.0f : s;
}

// 泥土色(夜里压暗,积雪罩灰白)——角标和 draw() 共用
inline uint32_t dirtColor24(float hourF, const Params& p) {
  const float night = Scene::nightness(hourF);
  uint32_t dirt = mix3(0x8A6034, 0x2A2418, night * 0.6f);
  const float sc = snowCoverOf(p);
  if (sc > 0.0f) dirt = mix3(dirt, 0xC8CCD4, sc * 0.5f);
  return dirt;
}

// ---------------------------------------------------------------- 伪随机
// mulberry32 —— 和预览同种子同调用顺序,星星/草丛位置完全一致
uint32_t g_mbSeed = 7;
float mbRng() {
  g_mbSeed += 0x6D2B79F5u;
  uint32_t t = (g_mbSeed ^ (g_mbSeed >> 15)) * (1u | g_mbSeed);
  t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
  return (float)((t ^ (t >> 14)) & 0xFFFFFFFFu) / 4294967296.0f;
}
// 效果随机(闪电/粒子重生)与星星流分开,用 xorshift32
uint32_t g_fx = 0x9E3779B9u;
float fxRng() {
  g_fx ^= g_fx << 13;
  g_fx ^= g_fx >> 17;
  g_fx ^= g_fx << 5;
  return (float)(g_fx & 0xFFFFFFu) / 16777216.0f;
}

// ---------------------------------------------------------------- 静态元素
struct Star { float x, y, ph; uint8_t s; };
Star STARS[26];
struct Tuft { float x; uint8_t h; };
Tuft TUFTS[14];
bool g_staticsInit = false;

// 皮卡丘踩地扬尘(同预览 DUST:最多 12 颗,落地帧在后脚位置生成)
struct Dust { float x, y, vx, vy, life; };
Dust DUST[12];
int  g_dustN = 0;
// 地面累计滚动距离(px):步频由它算,地面走 22px = 一个 4 帧步态循环
float g_groundRun = 0.0f;

void initStatics() {
  for (int i = 0; i < 26; ++i) {
    STARS[i].x = 6.0f + mbRng() * 308.0f;
    STARS[i].y = 24.0f + mbRng() * 92.0f;
    STARS[i].s = (mbRng() < 0.3f) ? 2 : 1;
    STARS[i].ph = mbRng() * 6.28f;
  }
  for (int i = 0; i < 14; ++i) {
    TUFTS[i].x = i * 24.0f + (mbRng() * 14.0f - 7.0f);
    TUFTS[i].h = (uint8_t)(2 + (int)(mbRng() * 2.0f + 0.5f));
  }
  g_staticsInit = true;
}

// ---------------------------------------------------------------- 粒子池
struct RainDrop { float x, y, v; };
struct SnowFlake { float x, y, v, ph; uint8_t s; };
RainDrop RAIN[82];
SnowFlake SNOWP[56];
int g_rainN = 0, g_snowN = 0;
float g_rainSlant = 0.0f;   // 记住上一帧的风向,新粒子按它初始化

// 闪电
struct Bolt { float t, next, x, seed; };
Bolt g_bolt = {0.0f, 2.5f, 150.0f, 0.0f};

float g_clock = 0.0f;   // 场景累计时钟(秒)

// 云的像素行造型:[内缩, 跨度] —— 宽 26 高 8 的云
const int CLOUD_SPR[8][2] = {{8, 10}, {6, 14}, {4, 18}, {2, 22},
                             {0, 24}, {0, 24}, {2, 22}, {4, 20}};

}  // namespace

namespace Scene {

const Params& params(Group g) {
  static const Params P[GR_COUNT] = {
      /* clear     */ {0.06f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.25f, false},
      /* mostly    */ {0.22f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.30f, false},
      /* partly    */ {0.45f, 0.05f, 0.00f, 0.00f, 0.00f, 0.00f, 0.35f, false},
      /* overcast  */ {1.00f, 0.35f, 0.00f, 0.00f, 0.12f, 0.00f, 0.45f, false},
      /* fog       */ {0.50f, 0.10f, 0.00f, 0.00f, 0.90f, 0.00f, 0.10f, false},
      /* drizzle   */ {0.85f, 0.20f, 0.25f, 0.00f, 0.15f, 0.00f, 0.30f, false},
      /* rain      */ {1.00f, 0.30f, 0.55f, 0.00f, 0.08f, 0.00f, 0.45f, false},
      /* heavyrain */ {1.00f, 0.50f, 1.00f, 0.00f, 0.06f, 0.00f, 0.80f, false},
      /* freezing  */ {0.90f, 0.25f, 0.40f, 0.00f, 0.20f, 0.00f, 0.40f, true},
      /* snow      */ {0.95f, 0.15f, 0.00f, 0.50f, 0.08f, 0.00f, 0.25f, false},
      /* heavysnow */ {1.00f, 0.30f, 0.00f, 1.00f, 0.06f, 0.00f, 0.50f, false},
      /* thunder   */ {1.00f, 0.65f, 0.80f, 0.00f, 0.04f, 1.00f, 0.70f, false},
  };
  return P[g < GR_COUNT ? g : GR_PARTLY];
}

Group wmoGroup(int w) {
  if (w == 0) return GR_CLEAR;
  if (w == 1 || w == 2) return GR_MOSTLY;
  if (w == 3) return GR_OVERCAST;
  if (w == 45 || w == 48) return GR_FOG;
  // 冻雨类要放在毛毛雨/雨前面判断
  if (w == 56 || w == 57 || w == 66 || w == 67) return GR_FREEZING;
  if (w >= 51 && w <= 57) return GR_DRIZZLE;
  if (w >= 61 && w <= 65) return GR_RAIN;        // 61-65 雨夹雪的雪量按雨处理
  if (w >= 66 && w <= 67) return GR_FREEZING;    // (前面已接住,防漏)
  if (w >= 80 && w <= 82) return w == 82 ? GR_HEAVYRAIN : GR_RAIN;
  if (w >= 71 && w <= 77 || w == 85) return GR_SNOW;
  if (w == 75 || w == 86) return GR_HEAVYSNOW;
  if (w >= 95) return GR_THUNDER;
  return GR_PARTLY;
}

float nightness(float h) {
  if (h >= 21.0f || h < 4.5f) return 1.0f;
  if (h < 7.5f) return 1.0f - (h - 4.5f) / 3.0f;
  if (h < 16.5f) return 0.0f;
  return (h - 16.5f) / 4.5f;
}

uint16_t skyPixel(float hourF, Group g, int y) {
  const Params& p = params(g);
  uint32_t top, bot;
  skyMods(hourF, p, top, bot);
  float k = (float)(y - SCENE_TOP) / (float)(HORIZON - SCENE_TOP);
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  return c565(mix3(top, bot, k));
}

uint16_t statusVeil(float hourF, Group g) {
  const Params& p = params(g);
  uint32_t top, bot;
  skyMods(hourF, p, top, bot);
  return c565(mix3(top, 0x0E1430, 0.32f));
}

uint16_t groundColor(float hourF, Group g) {
  return c565(dirtColor24(hourF, params(g)));
}

void draw(TFT_eSPI& t, float hourF, Group g, float dt, float moonPh,
          bool withPika) {
  if (!g_staticsInit) initStatics();
  const Params& p = params(g);
  if (dt > 0.0f) g_clock += dt;
  const float night = nightness(hourF);

  // ---- 天空底色(含阴天/雾/雷暴/冻雨调色) ----
  uint32_t top, bot;
  skyMods(hourF, p, top, bot);
  const int skyH = HORIZON - SCENE_TOP;
  // 闪电闪光强度(先算出来,直接混进每条带)
  float fl = 0.0f;
  if (p.bolt > 0.0f && g_bolt.t > 0.0f) fl = sinf(g_bolt.t / 0.28f * PI);
  // 逐带画(2px 一条);同时记录几条关键 y 的颜色供天体/云混色用
  uint32_t bandCol = 0;
  for (int i = 0; i < skyH; i += 2) {
    uint32_t c = mix3(top, bot, (float)i / skyH);
    if (fl > 0.0f) c = mix3(c, 0xFFF6D0, fl * 0.28f);
    t.drawFastHLine(0, SCENE_TOP + i, 320, c565(c));
    if (i == 0) bandCol = c;  // 备用:状态栏兜底色
  }
  (void)bandCol;
  // 场景内任意 y 的天空色(天体/云/星星混色用)
  auto skyAtY = [&](float y) -> uint32_t {
    float k = (y - SCENE_TOP) / skyH;
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    return mix3(top, bot, k);
  };

  // ---- 星星 ----
  if (night > 0.35f) {
    const float saBase = powf(night, 1.4f);
    for (const auto& s : STARS) {
      const float tw = 0.55f + 0.45f * sinf(g_clock * (1.2f + s.s) + s.ph);
      float a = saBase * tw;
      if (a < 0.05f) continue;
      const uint32_t bg = skyAtY(s.y);
      t.fillRect((int)s.x, (int)s.y, s.s, s.s, c565(mix3(bg, 0xF5F2E8, a)));
    }
  }

  // ---- 天体位置:太阳 6→18,月亮 18→6 ----
  float pb;
  if (hourF >= 6.0f && hourF < 18.0f) pb = (hourF - 6.0f) / 12.0f;
  else if (hourF >= 18.0f) pb = (hourF - 18.0f) / 12.0f;
  else pb = (hourF + 6.0f) / 12.0f;
  if (pb < 0.0f) pb = 0.0f;
  if (pb > 1.0f) pb = 1.0f;
  const float bx = 36.0f + pb * 248.0f;
  const float by = 102.0f - sinf(PI * pb) * 66.0f;
  const bool isSun = (hourF >= 6.0f && hourF < 18.0f);

  if (isSun && night < 0.6f) {
    // 太阳:早晚偏橙;光环 + 本体 + 光芒
    const bool warm = (hourF < 8.0f || hourF > 16.5f);
    const uint32_t core = warm ? 0xFFB53Au : 0xFFDD46u;
    const uint32_t sky = skyAtY(by);
    t.fillCircle((int)bx, (int)by, 20, c565(mix3(sky, mix3(core, 0xFFE89A, 0.5f), 0.12f + night * 0.1f)));
    t.fillCircle((int)bx, (int)by, 14, c565(mix3(sky, mix3(core, 0xFFE89A, 0.4f), 0.2f)));
    t.fillCircle((int)bx, (int)by, 9, c565(mix3(core, 0xFFF0A0, 0.25f)));
    if (p.clouds < 0.8f) {
      const uint16_t rayC = c565(mix3(sky, core, 0.75f));
      for (int i = 0; i < 8; ++i) {
        const float a = i * PI / 4.0f + g_clock * 0.1f;
        t.drawLine((int)(bx + cosf(a) * 12.0f), (int)(by + sinf(a) * 12.0f),
                   (int)(bx + cosf(a) * 16.0f), (int)(by + sinf(a) * 16.0f),
                   rayC);
        t.drawLine((int)(bx + cosf(a) * 12.0f) + 1, (int)(by + sinf(a) * 12.0f),
                   (int)(bx + cosf(a) * 16.0f) + 1, (int)(by + sinf(a) * 16.0f),
                   rayC);
      }
    }
  } else if (!isSun && night > 0.4f) {
    // 月亮:月相遮出月牙 + 三个坑
    const float ph = (moonPh < 0.0f) ? 0.5f : moonPh;
    const int r = 8;
    const uint32_t sky = skyAtY(by);
    t.fillCircle((int)bx, (int)by, r + 7, c565(mix3(sky, 0xC8D4E8, 0.1f)));
    t.fillCircle((int)bx, (int)by, r, c565(0xEEEDE0));
    const float illum = 1.0f - fabsf(ph - 0.5f) * 2.0f;
    const float off = (ph < 0.5f ? -1.0f : 1.0f) * (1.0f - illum) * 2.2f * r;
    t.fillCircle((int)(bx + off), (int)by, r - 1, c565(mix3(sky, 0x121A34, 0.8f)));
    const uint16_t cr = c565(0xD8D4C4);
    t.fillCircle((int)bx - 2, (int)by - 2, 1, cr);
    t.fillCircle((int)bx + 3, (int)by + 1, 1, cr);
    t.fillCircle((int)bx - 1, (int)by + 3, 1, cr);
  }

  // ---- 云:量由场景参数决定,3 条高度带随风漂移 ----
  const int cloudN = (int)(p.clouds * 6.0f + 0.5f);
  const float cSpeed = (3.0f + p.wind * 16.0f);
  const uint32_t cloudLight = mix3(0xFFFFFF, 0x5E646C, p.dark);
  const uint32_t cloudDarkU = mix3(0xD9DCE2, 0x3A4048, p.dark + 0.2f > 1.0f ? 1.0f : p.dark + 0.2f);
  for (int i = 0; i < cloudN; ++i) {
    const int band = i % 3;
    const float baseX = fmodf((float)(i * 137.5), 340.0f) - 10.0f;
    float cx = fmodf(baseX - g_clock * cSpeed * (0.5f + band * 0.35f), 340.0f);
    if (cx < 0.0f) cx += 340.0f;
    cx -= 10.0f;
    const float cy = 52.0f + band * 26.0f + (i & 1) * 6.0f;
    const float cw = 30.0f + (i % 3) * 14.0f;
    const uint32_t cc = (band == 0) ? cloudDarkU : cloudLight;
    const float alpha = (band == 0) ? 0.95f : 0.8f;
    const uint32_t sky = skyAtY(cy + 4.0f);
    const uint16_t col = c565(mix3(sky, cc, alpha));
    // 像素云:8 行圆角横条
    for (int ri = 0; ri < 8; ++ri) {
      const int rw = (int)(cw * CLOUD_SPR[ri][1] / 24.0f);
      const int rxOff = (int)((cw - (rw < 3 ? 3 : rw)) / 2.0f + CLOUD_SPR[ri][0] * 0.5f);
      const int rh = (int)(cw / 26.0f);
      const int r2 = (int)(rh * 0.55f);
      t.fillRoundRect((int)cx + rxOff, (int)cy + ri * (rh < 2 ? 2 : rh),
                      rw < 3 ? 3 : rw, rh < 2 ? 2 : rh, r2 < 1 ? 1 : r2, col);
    }
  }

  // ---- 闪电 ----
  if (p.bolt > 0.0f) {
    if (g_bolt.t <= 0.0f) {
      g_bolt.next -= dt;
      if (g_bolt.next <= 0.0f) {
        g_bolt.t = 0.28f;
        g_bolt.x = 60.0f + fxRng() * 200.0f;
        g_bolt.seed = fxRng();
      }
    } else {
      g_bolt.t -= dt;
    }
    if (g_bolt.t > 0.0f && g_bolt.t > 0.16f) {
      // 电弧:5 段折线,只在放电前半程画
      const uint16_t bc = c565(0xFFF8C8);
      float bx2 = g_bolt.x, by2 = 66.0f;
      const int jit = 6 + (int)(g_bolt.seed * 977.0f) % 7;
      const float dir = (g_bolt.seed > 0.5f) ? 1.0f : -1.0f;
      for (int i = 1; i <= 5; ++i) {
        const float nx = bx2 + dir * jit * ((i % 2) ? 1.0f : -1.0f);
        const float ny = by2 + 20.0f;
        t.drawWideLine(bx2, by2, nx, ny, 2.0f, bc,
                       c565(skyAtY((by2 + ny) * 0.5f)));
        bx2 = nx;
        by2 = ny;
      }
    }
  }

  // ---- 远山(整椭圆,下半截由地面盖住 = 预览的半椭圆) ----
  const uint32_t hillC = mix3(mix3(0x4E8230, 0x2E4A5E, night * 0.7f), 0xE8E6DC, p.snow > 0.0f ? 0.75f : 0.0f);
  const uint32_t hillC2 = mix3(mix3(0x3E6A24, 0x22384C, night * 0.7f), 0xD8D6CE, p.snow > 0.0f ? 0.75f : 0.0f);
  t.fillEllipse(70, HORIZON + 6, 120, 26, c565(hillC2));
  t.fillEllipse(250, HORIZON + 10, 140, 30, c565(hillC));

  // ---- 地面 ----
  const float snowCover = snowCoverOf(p);
  uint32_t grassC = mix3(0x5E9C3A, 0x24384E, night * 0.72f);
  uint32_t dirtC = dirtColor24(hourF, p);
  if (p.rain > 0.0f) grassC = mix3(grassC, 0x3E7030, p.rain * 0.4f);
  if (p.ice) grassC = mix3(grassC, 0xB8D0E6, 0.3f);
  t.fillRect(0, HORIZON, 320, 10, c565(grassC));
  t.fillRect(0, HORIZON + 10, 320, SCENE_BOT - HORIZON - 10, c565(dirtC));
  t.fillRect(0, HORIZON, 320, 2,
             c565(mix3(mix3(0x6EB044, 0x2E4458, night * 0.6f), 0xF2F0E8, snowCover * 0.8f)));

  // ---- 草丛(视差滚动,风吹摆动) ----
  const uint32_t tuftC = (snowCover > 0.5f)
      ? mix3(0xF2F0E8, 0xFFFFFF, 0.3f)
      : mix3(mix3(0x4E8A2E, 0x22362C, night * 0.7f), 0xFFFFFF, snowCover);
  Scene::advanceGround(dt);
  for (auto& tf : TUFTS) {
    const int tx = (int)tf.x;
    if (tx < -8 || tx > 320) continue;
    const float sway = (p.wind > 0.5f) ? sinf(g_clock * 6.0f + tf.x * 0.3f) : 0.0f;
    const uint16_t tc = c565(tuftC);
    t.fillRect(tx, HORIZON + 4 + (tx % 7), 2, tf.h * 2, tc);
    t.fillRect(tx + 2 + (int)sway, HORIZON + 3 + (tx % 5), 2, tf.h * 2 + 2, tc);
    t.fillRect(tx + 1 + (int)(sway * 0.5f), HORIZON + 6 + (tx % 11), 2, tf.h * 2 - 2, tc);
  }

  // ---- 皮卡丘(屏幕正中原地奔跑,影子随跳跃缩放) ----
  if (withPika) {
    // 位置同预览:PK_X 居中,脚底落在 horizon+4
    constexpr int PK_X = (320 - Pika::PK_W) / 2;      // 136
    constexpr int PK_Y = HORIZON + 4 - Pika::PK_H;    // 146
    // 步频跟地面滚动挂钩(g_groundRun 由 advanceGround 累计):
    // 地面走 22px = 一个 4 帧步态循环,脚不打滑
    Pika::setFrame((int)(g_groundRun / 22.0f * 4.0f) & 3);

    // 跳起高度(空中为正),影子按它缩(同预览:1 - lift/40,下限 0.5)
    float lift = 0.0f;
    const float jp = Pika::jumpPhase();
    if (jp >= 0.0f) lift = sinf(PI * jp) * 26.0f;
    float shrink = 1.0f - lift / 40.0f;
    if (shrink < 0.5f) shrink = 0.5f;
    const int shadowR = (int)(Pika::PK_W * 0.42f * shrink + 0.5f);
    t.fillEllipse(PK_X + Pika::PK_W / 2, PK_Y + 22 * Pika::PK_SC + 3,
                  shadowR, 3, c565(mix3(grassC, 0x141C08, 0.3f)));

    // 踩地扬尘:落地帧(f0/f2)在后脚位置补粒子,画在皮卡丘之前(尘在身后)
    const uint16_t dustC = c565(
        (snowCover > 0.4f)
            ? 0xEAEAE2u
            : mix3(mix3(0xB09068, 0x3A3226, night * 0.6f), 0xFFFFFF,
                   snowCover * 0.6f));
    const int pf = Pika::frame();
    if (dt > 0.0f && lift == 0.0f && (pf == 0 || pf == 2) && g_dustN < 12 &&
        fxRng() < 0.5f) {
      Dust& d = DUST[g_dustN++];
      d.x = PK_X + 5.0f;
      d.y = PK_Y + Pika::PK_H - 1.0f;
      d.vx = -16.0f - fxRng() * 14.0f;
      d.vy = -5.0f - fxRng() * 8.0f;
      d.life = 1.0f;
    }
    for (int i = g_dustN - 1; i >= 0; --i) {
      Dust& d = DUST[i];
      if (dt > 0.0f) {
        d.life -= 1.68f * dt;   // 预览 .028/帧 @60fps = 1.68/s
        if (d.life <= 0.0f) { DUST[i] = DUST[--g_dustN]; continue; }
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        d.vy += 10.8f * dt;     // 预览 .18/帧 @60fps
      } else if (d.life <= 0.0f) {
        DUST[i] = DUST[--g_dustN];
        continue;
      }
      const int s = (d.life > 0.6f) ? 2 : 1;
      // 无 alpha:按 life 把尘色向地面色淡出,近似预览的 life*.5 透明度
      const uint8_t a = (uint8_t)(d.life * 0.5f * 255.0f);
      t.fillRect((int)d.x, (int)d.y, s, s, lerp565(c565(dirtC), dustC, a));
    }

    Pika::draw(t, PK_X, PK_Y);
  }

  // ---- 雨 ----
  if (p.rain > 0.0f) {
    const uint32_t rcU = mix3(0x9CBEF0, 0x51648C, night);
    const uint16_t rc = c565(rcU);
    const float slant = p.wind * 1.1f;
    g_rainSlant = slant;
    const int want = (int)(12.0f + p.rain * 70.0f + 0.5f);
    if (g_rainN == 0) {
      for (int i = 0; i < 14; ++i) {
        RAIN[i].x = fxRng() * 340.0f - 10.0f;
        RAIN[i].y = fxRng() * 190.0f + 18.0f;
        RAIN[i].v = 260.0f + fxRng() * 90.0f;
      }
      g_rainN = 14;
    }
    while (g_rainN > want) --g_rainN;
    while (g_rainN < want) {
      RAIN[g_rainN].x = fxRng() * 340.0f - 10.0f;
      RAIN[g_rainN].y = 18.0f + fxRng() * 180.0f;
      RAIN[g_rainN].v = 260.0f + fxRng() * 90.0f;
      ++g_rainN;
    }
    for (int i = 0; i < g_rainN; ++i) {
      auto& d = RAIN[i];
      if (dt > 0.0f) {
        d.y += d.v * dt;
        d.x -= g_rainSlant * 3.0f * dt;
        if (d.y > SCENE_BOT + 8.0f || d.x < -10.0f) {
          d.y = 14.0f;
          d.x = fxRng() * 350.0f - 10.0f;
        }
      }
      t.drawLine((int)d.x, (int)d.y, (int)(d.x - slant * 4.0f), (int)(d.y - 7.0f), rc);
    }
  } else {
    g_rainN = 0;
  }

  // ---- 雪 ----
  if (p.snow > 0.0f) {
    const uint16_t sc = c565(0xF6F4EC);
    const int want = (int)(10.0f + p.snow * 46.0f + 0.5f);
    if (g_snowN == 0) {
      for (int i = 0; i < 14; ++i) {
        SNOWP[i].x = fxRng() * 340.0f - 10.0f;
        SNOWP[i].y = fxRng() * 190.0f + 18.0f;
        SNOWP[i].v = 26.0f + fxRng() * 20.0f;
        SNOWP[i].ph = fxRng() * 6.28f;
        SNOWP[i].s = (fxRng() < 0.4f) ? 3 : 2;
      }
      g_snowN = 14;
    }
    while (g_snowN > want) --g_snowN;
    while (g_snowN < want) {
      SNOWP[g_snowN].x = fxRng() * 340.0f - 10.0f;
      SNOWP[g_snowN].y = 18.0f + fxRng() * 180.0f;
      SNOWP[g_snowN].v = 26.0f + fxRng() * 20.0f;
      SNOWP[g_snowN].ph = fxRng() * 6.28f;
      SNOWP[g_snowN].s = (fxRng() < 0.4f) ? 3 : 2;
      ++g_snowN;
    }
    for (int i = 0; i < g_snowN; ++i) {
      auto& f = SNOWP[i];
      if (dt > 0.0f) {
        f.y += f.v * dt;
        // 预览的横移是「每帧 0.5px」(60fps),换算成 px/s 再乘 dt
        f.x += (sinf(g_clock * 1.5f + f.ph) * 0.5f - p.wind * 0.4f) * 60.0f * dt;
        if (f.y > SCENE_BOT + 4.0f || f.x < -6.0f) {
          f.y = 14.0f;
          f.x = fxRng() * 340.0f - 10.0f;
        }
      }
      t.fillRect((int)f.x, (int)f.y, f.s, f.s, sc);
    }
  } else {
    g_snowN = 0;
  }

  // ---- 雾:三条缓慢横移的扁椭圆 ----
  if (p.fog > 0.05f) {
    for (int i = 0; i < 3; ++i) {
      const float fy = 70.0f + i * 46.0f;
      const float fx = fmodf(g_clock * (2.0f + i), 60.0f) - 30.0f;
      const float ecx = 160.0f + sinf(g_clock * 0.3f + i) * 40.0f + fx * 0.3f;
      const uint32_t bg = skyAtY(fy);
      t.fillEllipse((int)ecx, (int)fy, 210, 13, c565(mix3(bg, 0xC5C1B2, p.fog * 0.38f)));
    }
  }

  // ---- 冻雨:地面冰晶闪光 ----
  if (p.ice) {
    for (int i = 0; i < 7; ++i) {
      const float tw = sinf(g_clock * 3.0f + i * 2.4f);
      if (tw > 0.3f) {
        t.fillRect(20 + i * 42, HORIZON + 6 + (i % 3) * 8, 2, 2,
                   c565(mix3(dirtC, 0xEAF4FF, (tw - 0.3f) * 0.8f)));
      }
    }
  }
}

// =====================================================================
//  地面滚动 + 五页共用页脚
// =====================================================================

void advanceGround(float dt) {
  if (dt <= 0.0f) return;
  if (!g_staticsInit) initStatics();
  // 滚速 = 皮卡丘步频基准:它原地跑,是地面在向左动(同预览 PK_SPD)
  const float spd = Pika::PK_SPD;
  for (auto& tf : TUFTS) {
    tf.x -= spd * dt;
    if (tf.x < -8.0f) {
      tf.x += 336.0f;
      tf.h = (uint8_t)(2 + (int)(fxRng() * 2.0f + 0.5f));
    }
  }
  // 步频用的累计距离(取模避免长跑后精度流失)
  g_groundRun += spd * dt;
  if (g_groundRun > 22.0f * 4096.0f) g_groundRun -= 22.0f * 4096.0f;
}

void footer(TFT_eSPI& t, float dt, uint16_t bg) {
  if (!g_staticsInit) initStatics();
  advanceGround(dt);

  const int gy = FOOT_TOP + 8;   // 草皮顶边

  // 页面底色 → 草地的 8px 浅色过渡,避免硬切(同预览 lerp(BG,0x8FC45E,i/7*.5))
  for (int i = 0; i < 8; ++i)
    t.drawFastHLine(0, FOOT_TOP + i, 320,
                    lerp565(bg, hex565(0x8FC45E),
                            (uint8_t)(i / 7.0f * 0.5f * 255.0f)));

  // 草皮 + 顶边亮线
  t.fillRect(0, gy, 320, FOOT_H - 8, hex565(0x7CB44E));
  t.drawFastHLine(0, gy, 320, hex565(0x9AD066));

  // 草丛:与时钟页同一份 TUFTS、同一速度 —— 整机是同一片地面
  const uint16_t tc = hex565(0x5E9C3A);
  for (const auto& tf : TUFTS) {
    const int tx = (int)tf.x;
    if (tx < -8 || tx > 320) continue;
    t.fillRect(tx, gy + 2 + (tx % 4), 2, tf.h * 2, tc);
    t.fillRect(tx + 2, gy + 1 + (tx % 3), 2, tf.h * 2 + 1, tc);
  }
}

}  // namespace Scene
