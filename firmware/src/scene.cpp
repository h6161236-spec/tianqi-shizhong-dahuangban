/**
 * =====================================================================
 *  scene.cpp —— 场景引擎实现(移植自 preview/ui.html drawScene)
 * =====================================================================
 *  所有 canvas 的 globalAlpha 叠加改成「向背景色 lerp」:先算 24bit 混合
 *  色,落屏前转 RGB565。八条 320x28 带完整合成后再推屏。
 */

#include "scene.h"
#include "gfx.h"
#include "pika.h"
#include "theme.h"
#include "color_math.h"

#include <math.h>
#include <string.h>

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
  return ColorMath::mix24(a, b, t);
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

// 闪电
struct Bolt { float t, next, x, seed; };
Bolt g_bolt = {0.0f, 2.5f, 150.0f, 0.0f};

float g_clock = 0.0f;   // 场景累计时钟(秒)

// 云的像素行造型:[内缩, 跨度] —— 宽 26 高 8 的云
const int CLOUD_SPR[8][2] = {{8, 10}, {6, 14}, {4, 18}, {2, 22},
                             {0, 24}, {0, 24}, {2, 22}, {4, 20}};

// Cached sky colors and the previous geometry select complete strips to rebuild.
constexpr int SKY_BANDS = (HORIZON - SCENE_TOP) / 2;   // 85 条 2px 带
uint32_t g_skyRow24[SKY_BANDS];   // 每条带的 24bit 混色(供天体/星混色取样)
uint16_t g_skyRow16[SKY_BANDS];   // 对应 RGB565(与屏上像素比对用)
bool     g_skyInit = false;
bool     g_bgValid = false;
bool     g_groundOverlayDirty = false;
Scene::Group g_bgGrp = (Scene::Group)0xFF;

struct CloudPos { float x, y, w; bool valid; };
CloudPos g_cloudOld[6] = {};
float    g_bodyOldX = -999.0f, g_bodyOldY = -999.0f;
bool     g_bodyWasSun = true, g_bodyValid = false;
struct FogPos { float x, y; bool valid; };
FogPos   g_fogOld[3] = {};

/** 场景内 y 行的天空 24bit 色,直接取缓存带(与屏上已画的一致) */
inline uint32_t skyAtCache(float y) {
  int idx = ((int)y - SCENE_TOP) / 2;
  if (idx < 0) idx = 0;
  if (idx >= SKY_BANDS) idx = SKY_BANDS - 1;
  return g_skyRow24[idx];
}

void stampCloud(TFT_eSPI& t, float cx, float cy, float cw, uint16_t col) {
  for (int ri = 0; ri < 8; ++ri) {
    const int rw = (int)(cw * CLOUD_SPR[ri][1] / 24.0f);
    const int drawW = rw < 3 ? 3 : rw;
    const int rxOff = (int)((cw - drawW) / 2.0f + CLOUD_SPR[ri][0] * 0.5f);
    const int rh = (int)(cw / 26.0f);
    const int drawH = rh < 2 ? 2 : rh;
    t.fillRect((int)cx + rxOff, (int)cy + ri * drawH, drawW, drawH, col);
  }
}

void stampBody(TFT_eSPI& t, int ox, int oy, float bx, float by, bool isSun,
               float hourF, float night, float moonPh, bool rays) {
  const int x = (int)bx - ox;
  const int y = (int)by - oy;
  const uint32_t sky = skyAtCache(by);
  if (isSun && night < 0.6f) {
    const bool warm = (hourF < 8.0f || hourF > 16.5f);
    const uint32_t core = warm ? 0xFFB53Au : 0xFFDD46u;
    t.fillCircle(x, y, 20,
                 c565(mix3(sky, mix3(core, 0xFFE89A, 0.5f),
                           0.12f + night * 0.1f)));
    t.fillCircle(x, y, 14,
                 c565(mix3(sky, mix3(core, 0xFFE89A, 0.4f), 0.2f)));
    t.fillCircle(x, y, 9, c565(mix3(core, 0xFFF0A0, 0.25f)));
    if (rays) {
      const uint16_t rayC = c565(mix3(sky, core, 0.75f));
      for (int i = 0; i < 8; ++i) {
        const float a = i * PI / 4.0f + g_clock * 0.1f;
        t.drawLine((int)(bx + cosf(a) * 12.0f) - ox,
                   (int)(by + sinf(a) * 12.0f) - oy,
                   (int)(bx + cosf(a) * 16.0f) - ox,
                   (int)(by + sinf(a) * 16.0f) - oy, rayC);
      }
    }
  } else if (!isSun && night > 0.4f) {
    const float ph = (moonPh < 0.0f) ? 0.5f : moonPh;
    const int r = 8;
    t.fillCircle(x, y, r + 7, c565(mix3(sky, 0xC8D4E8, 0.1f)));
    t.fillCircle(x, y, r, c565(0xEEEDE0));
    const float illum = 1.0f - fabsf(ph - 0.5f) * 2.0f;
    const float off = (ph < 0.5f ? -1.0f : 1.0f) * (1.0f - illum) * 2.2f * r;
    t.fillCircle(x + (int)off, y, r - 1, c565(mix3(sky, 0x121A34, 0.8f)));
    const uint16_t cr = c565(0xD8D4C4);
    t.fillCircle(x - 2, y - 2, 1, cr);
    t.fillCircle(x + 3, y + 1, 1, cr);
    t.fillCircle(x - 1, y + 3, 1, cr);
  }
}

}  // namespace

namespace Scene {

void drawBands(TFT_eSPI& t, float hourF, Group g, const Params& p,
               float night, float dt, float moonPh, bool withPika);

void invalidateCache() {
  g_skyInit = false;
  g_bgValid = false;
  for (auto& c : g_cloudOld) c.valid = false;
  for (auto& f : g_fogOld) f.valid = false;
  g_bodyValid = false;
}

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
  if (w == 1) return GR_MOSTLY;
  if (w == 2) return GR_PARTLY;
  if (w == 3) return GR_OVERCAST;
  if (w == 45 || w == 48) return GR_FOG;
  // 冻雨类要放在毛毛雨/雨前面判断
  if (w == 56 || w == 57 || w == 66 || w == 67) return GR_FREEZING;
  if (w >= 51 && w <= 55) return GR_DRIZZLE;
  if (w == 65) return GR_HEAVYRAIN;
  if (w == 61 || w == 63) return GR_RAIN;
  if (w >= 80 && w <= 82) return w == 82 ? GR_HEAVYRAIN : GR_RAIN;
  if (w == 75 || w == 86) return GR_HEAVYSNOW;
  if (w == 71 || w == 73 || w == 77 || w == 85) return GR_SNOW;
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
  drawBands(t, hourF, g, p, nightness(hourF), dt, moonPh, withPika);
}

void restoreGroundOverlay() { g_groundOverlayDirty = true; }


// =====================================================================
// Eight complete 320x28 strips replace the old erase-and-redraw path. A strip
// is always built from one frozen animation state, then transferred once.
// =====================================================================

namespace {
constexpr int BAND_H = GFX_STRIP_H;
constexpr int BAND_N = (SCENE_BOT - SCENE_TOP + BAND_H - 1) / BAND_H;
bool g_bandDirty[BAND_N];
bool g_badgeVisible = false, g_toastVisible = false;
bool g_prevBadgeVisible = false, g_prevToastVisible = false;
bool g_prevWithPika = false, g_prevBoltVisible = false;
int g_prevPikaFrame = -1;
float g_prevJumpPhase = -2.0f;
float g_prevMoonPh = -2.0f;
uint16_t g_prevGrass = 0, g_prevDirt = 0, g_prevHill = 0, g_prevHill2 = 0;
struct DropPos { int16_t x, y; uint8_t size; };
DropPos g_prevRain[82], g_prevSnow[56];
int g_prevRainN = 0, g_prevSnowN = 0;

void dirtyY(int top, int bottom) {
  if (bottom <= SCENE_TOP || top >= SCENE_BOT) return;
  if (top < SCENE_TOP) top = SCENE_TOP;
  if (bottom > SCENE_BOT) bottom = SCENE_BOT;
  for (int i = (top - SCENE_TOP) / BAND_H;
       i <= (bottom - 1 - SCENE_TOP) / BAND_H; ++i) g_bandDirty[i] = true;
}

void advanceParticles(const Params& p, float dt, bool withPika) {
  if (dt > 0.0f) {
    g_clock += dt;
    advanceGround(dt);
  }
  Pika::setFrame((int)(g_groundRun / 22.0f * 4.0f) & 3);

  if (p.bolt > 0.0f && dt > 0.0f) {
    if (g_bolt.t > 0.0f) g_bolt.t -= dt;
    else if ((g_bolt.next -= dt) <= 0.0f) {
      g_bolt.t = 0.28f;
      g_bolt.x = 60.0f + fxRng() * 200.0f;
      g_bolt.seed = fxRng();
      g_bolt.next = 2.5f + fxRng() * 3.0f;
    }
  }

  const int rainWant = p.rain > 0.0f ? (int)(12.0f + p.rain * 70.0f + 0.5f) : 0;
  if (g_rainN > rainWant) g_rainN = rainWant;
  while (g_rainN < rainWant) {
    RainDrop& d = RAIN[g_rainN++];
    d.x = fxRng() * 340.0f - 10.0f;
    d.y = fxRng() * 190.0f + 18.0f;
    d.v = 260.0f + fxRng() * 90.0f;
  }
  if (dt > 0.0f) for (int i = 0; i < g_rainN; ++i) {
    RainDrop& d = RAIN[i];
    d.y += d.v * dt;
    d.x -= p.wind * 1.1f * 3.0f * dt;
    if (d.y > SCENE_BOT + 8.0f || d.x < -10.0f) {
      d.y = 14.0f;
      d.x = fxRng() * 350.0f - 10.0f;
    }
  }

  const int snowWant = p.snow > 0.0f ? (int)(10.0f + p.snow * 46.0f + 0.5f) : 0;
  if (g_snowN > snowWant) g_snowN = snowWant;
  while (g_snowN < snowWant) {
    SnowFlake& f = SNOWP[g_snowN++];
    f.x = fxRng() * 340.0f - 10.0f;
    f.y = fxRng() * 190.0f + 18.0f;
    f.v = 26.0f + fxRng() * 20.0f;
    f.ph = fxRng() * 6.28f;
    f.s = (fxRng() < 0.4f) ? 3 : 2;
  }
  if (dt > 0.0f) for (int i = 0; i < g_snowN; ++i) {
    SnowFlake& f = SNOWP[i];
    f.y += f.v * dt;
    f.x += (sinf(g_clock * 1.5f + f.ph) * 0.5f - p.wind * 0.4f) * 60.0f * dt;
    if (f.y > SCENE_BOT + 4.0f || f.x < -6.0f) {
      f.y = 14.0f;
      f.x = fxRng() * 340.0f - 10.0f;
    }
  }

  if (!withPika) return;
  float lift = 0.0f;
  const float jp = Pika::jumpPhase();
  if (jp >= 0.0f) lift = sinf(PI * jp) * 26.0f;
  if (dt > 0.0f && lift == 0.0f &&
      (Pika::frame() == 0 || Pika::frame() == 2) &&
      g_dustN < 12 && fxRng() < 0.5f) {
    Dust& d = DUST[g_dustN++];
    d.x = 141.0f;
    d.y = HORIZON + 3.0f;
    d.vx = -16.0f - fxRng() * 14.0f;
    d.vy = -5.0f - fxRng() * 8.0f;
    d.life = 1.0f;
  }
  if (dt > 0.0f) for (int i = g_dustN - 1; i >= 0; --i) {
    Dust& d = DUST[i];
    d.life -= 1.68f * dt;
    if (d.life <= 0.0f) { DUST[i] = DUST[--g_dustN]; continue; }
    d.x += d.vx * dt;
    d.y += d.vy * dt;
    d.vy += 10.8f * dt;
  }
}

CloudPos cloudAt(int i, const Params& p) {
  if (i >= (int)(p.clouds * 6.0f + 0.5f)) return {0, 0, 0, false};
  const int band = i % 3;
  const float baseX = fmodf(i * 137.5f, 340.0f) - 10.0f;
  float x = fmodf(baseX - g_clock * (3.0f + p.wind * 16.0f) *
                  (0.5f + band * 0.35f), 340.0f);
  if (x < 0.0f) x += 340.0f;
  return {x - 10.0f, 52.0f + band * 26.0f + (i & 1) * 6.0f,
          30.0f + band * 14.0f, true};
}

FogPos fogAt(int i, const Params& p) {
  if (p.fog <= 0.05f) return {0, 0, false};
  const float fx = fmodf(g_clock * (2.0f + i), 60.0f) - 30.0f;
  return {160.0f + sinf(g_clock * 0.3f + i) * 40.0f + fx * 0.3f,
          70.0f + i * 46.0f, true};
}

void bodyAt(float hourF, float& x, float& y, bool& sun) {
  sun = hourF >= 6.0f && hourF < 18.0f;
  float phase = sun ? (hourF - 6.0f) / 12.0f
                    : (hourF >= 18.0f ? hourF - 18.0f : hourF + 6.0f) / 12.0f;
  if (phase < 0.0f) phase = 0.0f;
  if (phase > 1.0f) phase = 1.0f;
  x = 36.0f + phase * 248.0f;
  y = 102.0f - sinf(PI * phase) * 66.0f;
}

void renderBand(TFT_eSprite& st, int bandTop, int height, float hourF,
                const Params& p, float night, float moonPh, bool withPika,
                uint16_t grass, uint16_t dirt, uint16_t hill, uint16_t hill2) {
  const uint32_t grassC = mix3(0x5E9C3A, 0x24384E, night * 0.72f);
  const float snowCover = snowCoverOf(p);
  for (int row = 0; row < height; ++row) {
    const int y = bandTop + row;
    st.drawFastHLine(0, row, SCR_W,
                     y < HORIZON ? g_skyRow16[(y - SCENE_TOP) / 2]
                     : (y < HORIZON + 10 ? grass : dirt));
  }

  if (night > 0.35f) for (const auto& s : STARS) {
    const int y = (int)s.y;
    if (y + s.s <= bandTop || y >= bandTop + height) continue;
    const float tw = 0.55f + 0.45f * sinf(g_clock * (1.2f + s.s) + s.ph);
    const float a = powf(night, 1.4f) * tw;
    if (a >= 0.05f)
      st.fillRect((int)s.x, y - bandTop, s.s, s.s,
                  c565(mix3(skyAtCache(s.y), 0xF5F2E8, a)));
  }

  float bx, by;
  bool sun;
  bodyAt(hourF, bx, by, sun);
  if (by + 25 >= bandTop && by - 25 < bandTop + height)
    stampBody(st, 0, bandTop, bx, by, sun, hourF, night, moonPh,
              p.clouds < 0.8f);

  const uint32_t cloudLight = mix3(0xFFFFFF, 0x5E646C, p.dark);
  const float shade = p.dark + 0.2f > 1.0f ? 1.0f : p.dark + 0.2f;
  const uint32_t cloudDark = mix3(0xD9DCE2, 0x3A4048, shade);
  for (int i = 0; i < 6; ++i) {
    const CloudPos c = cloudAt(i, p);
    if (!c.valid || c.y + 20 < bandTop || c.y >= bandTop + height) continue;
    const uint32_t tint = i % 3 == 0 ? cloudDark : cloudLight;
    const float alpha = i % 3 == 0 ? 0.95f : 0.8f;
    stampCloud(st, c.x, c.y - bandTop, c.w,
               c565(mix3(skyAtCache(c.y + 4.0f), tint, alpha)));
  }

  if (p.fog > 0.05f) for (int i = 0; i < 3; ++i) {
    const FogPos f = fogAt(i, p);
    const uint16_t color = c565(mix3(skyAtCache(f.y), 0xC5C1B2,
                                      p.fog * 0.38f));
    for (int dy = -13; dy <= 13; ++dy) {
      const int y = (int)f.y + dy;
      if (y < bandTop || y >= bandTop + height) continue;
      const int half = (int)(210.0f * sqrtf(1.0f - dy * dy / 169.0f));
      st.drawFastHLine((int)f.x - half, y - bandTop, half * 2 + 1, color);
    }
  }

  // Both hills are below the sky but behind the ground at y=188.
  const int skyHeight = HORIZON - bandTop < height ? HORIZON - bandTop : height;
  if (skyHeight > 0) {
    st.setViewport(0, 0, SCR_W, skyHeight, false);
    st.fillEllipse(70, HORIZON + 6 - bandTop, 120, 26, hill2);
    st.fillEllipse(250, HORIZON + 10 - bandTop, 140, 30, hill);
    st.resetViewport();
  }

  if (bandTop < HORIZON + 2 && bandTop + height > HORIZON)
    st.fillRect(0, HORIZON - bandTop, SCR_W, 2,
                c565(mix3(mix3(0x6EB044, 0x2E4458, night * 0.6f),
                          0xF2F0E8, snowCover * 0.8f)));

  const uint32_t tuftC = snowCover > 0.5f
      ? mix3(0xF2F0E8, 0xFFFFFF, 0.3f)
      : mix3(mix3(0x4E8A2E, 0x22362C, night * 0.7f),
             0xFFFFFF, snowCover);
  for (const auto& tf : TUFTS) {
    const int x = (int)tf.x;
    if (x < -8 || x > SCR_W) continue;
    const int sway = p.wind > 0.5f ? (int)sinf(g_clock * 6.0f + tf.x * 0.3f) : 0;
    const uint16_t color = c565(tuftC);
    st.fillRect(x, HORIZON + 4 + (x % 7) - bandTop, 2, tf.h * 2, color);
    st.fillRect(x + 2 + sway, HORIZON + 3 + (x % 5) - bandTop,
                2, tf.h * 2 + 2, color);
    st.fillRect(x + 1, HORIZON + 6 + (x % 11) - bandTop,
                2, tf.h * 2 - 2, color);
  }

  if (withPika && bandTop < HORIZON + 20 && bandTop + height > 112) {
    constexpr int PK_X = (SCR_W - Pika::PK_W) / 2;
    constexpr int PK_Y = HORIZON + 4 - Pika::PK_H;
    float lift = 0.0f;
    const float jp = Pika::jumpPhase();
    if (jp >= 0.0f) lift = sinf(PI * jp) * 26.0f;
    float shrink = 1.0f - lift / 40.0f;
    if (shrink < 0.5f) shrink = 0.5f;
    st.fillEllipse(PK_X + Pika::PK_W / 2,
                   PK_Y + 22 * Pika::PK_SC + 3 - bandTop,
                   (int)(Pika::PK_W * 0.42f * shrink + 0.5f), 3,
                   c565(mix3(grassC, 0x141C08, 0.3f)));
    const uint16_t dustC = c565(snowCover > 0.4f ? 0xEAEAE2u
        : mix3(mix3(0xB09068, 0x3A3226, night * 0.6f),
                0xFFFFFF, snowCover * 0.6f));
    for (int i = 0; i < g_dustN; ++i) {
      const Dust& d = DUST[i];
      const int size = d.life > 0.6f ? 2 : 1;
      st.fillRect((int)d.x, (int)d.y - bandTop, size, size,
                  lerp565(dirt, dustC, (uint8_t)(d.life * 0.5f * 255.0f)));
    }
    Pika::draw(st, PK_X, PK_Y - bandTop);
  }

  if (p.rain > 0.0f) {
    const uint16_t color = c565(mix3(0x9CBEF0, 0x51648C, night));
    const float slant = p.wind * 1.1f;
    for (int i = 0; i < g_rainN; ++i) {
      const RainDrop& d = RAIN[i];
      if (d.y < bandTop - 1 || d.y - 7 >= bandTop + height) continue;
      st.drawLine((int)d.x, (int)d.y - bandTop,
                  (int)(d.x - slant * 4.0f), (int)(d.y - 7.0f) - bandTop,
                  color);
    }
  }
  if (p.snow > 0.0f) for (int i = 0; i < g_snowN; ++i) {
    const SnowFlake& f = SNOWP[i];
    st.fillRect((int)f.x, (int)f.y - bandTop, f.s, f.s, c565(0xF6F4EC));
  }
  if (p.bolt > 0.0f && g_bolt.t > 0.16f) {
    float x = g_bolt.x, y = 66.0f;
    const int jit = 6 + (int)(g_bolt.seed * 977.0f) % 7;
    const float dir = g_bolt.seed > 0.5f ? 1.0f : -1.0f;
    for (int i = 1; i <= 5; ++i) {
      const float nx = x + dir * jit * (i % 2 ? 1.0f : -1.0f);
      const float ny = y + 20.0f;
      st.drawWideLine(x, y - bandTop, nx, ny - bandTop, 2.0f,
                      c565(0xFFF8C8), c565(skyAtCache((y + ny) * 0.5f)));
      x = nx; y = ny;
    }
  }
  if (p.ice) for (int i = 0; i < 7; ++i) {
    const float tw = sinf(g_clock * 3.0f + i * 2.4f);
    if (tw > 0.3f)
      st.fillRect(20 + i * 42, HORIZON + 6 + (i % 3) * 8 - bandTop,
                  2, 2, c565(mix3(dirtColor24(hourF, p), 0xEAF4FF,
                                   (tw - 0.3f) * 0.8f)));
  }
  // The page indicator is a scene overlay, so a badge disappearing can
  // restore the entire ground strip without leaving a rectangular dot hole.
  if (bandTop + height > 230)
    Gfx::dots(st, SCR_W / 2, 234, 5, 0, TH::ACCENT,
              lerp565(dirt, hex565(0xF4FAFF), 140), dirt);
}

struct Span { int left, right; };
int transferSpans(int y, Span spans[8]) {
  int holes[6][2];
  int n = 0;
  if (y >= 20 && y < 68) { holes[n][0] = 6; holes[n++][1] = 134; }
  if (y >= 22 && y < 72) { holes[n][0] = 214; holes[n++][1] = 314; }
  if (y >= 70 && y < 88) { holes[n][0] = 8; holes[n++][1] = 210; }
  if (g_badgeVisible && y >= 222) {
    holes[n][0] = 6; holes[n++][1] = 174;
  }
  if (g_toastVisible && y >= 196 && y < 220) {
    holes[n][0] = 0; holes[n++][1] = SCR_W;
  }
  for (int i = 1; i < n; ++i) {
    const int a = holes[i][0], b = holes[i][1];
    int j = i;
    while (j > 0 && holes[j - 1][0] > a) {
      holes[j][0] = holes[j - 1][0];
      holes[j][1] = holes[j - 1][1];
      --j;
    }
    holes[j][0] = a; holes[j][1] = b;
  }
  int cur = 0, count = 0;
  for (int i = 0; i < n; ++i) {
    if (holes[i][0] > cur) spans[count++] = {cur, holes[i][0]};
    if (holes[i][1] > cur) cur = holes[i][1];
  }
  if (cur < SCR_W) spans[count++] = {cur, SCR_W};
  return count;
}

void pushBand(int top, int height) {
  for (int row = 0; row < height;) {
    Span spans[8];
    const int n = transferSpans(top + row, spans);
    int run = row + 1;
    for (; run < height; ++run) {
      Span next[8];
      if (transferSpans(top + run, next) != n) break;
      bool same = true;
      for (int i = 0; i < n; ++i)
        if (next[i].left != spans[i].left || next[i].right != spans[i].right)
          same = false;
      if (!same) break;
    }
    for (int i = 0; i < n; ++i)
      Gfx::pushStripRegion(spans[i].left, top + row,
                           spans[i].right - spans[i].left, run - row,
                           spans[i].left, row);
    row = run;
  }
}
}  // namespace

void setOverlayVisibility(bool networkBadge, bool toast) {
  g_badgeVisible = networkBadge;
  g_toastVisible = toast;
}

void drawBands(TFT_eSPI& t, float hourF, Group g, const Params& p,
               float night, float dt, float moonPh, bool withPika) {
  if (dt < 0.0f) dt = 0.0f;
  memset(g_bandDirty, 0, sizeof(g_bandDirty));
  if (!g_bgValid || g_bgGrp != g || g_prevWithPika != withPika)
    dirtyY(SCENE_TOP, SCENE_BOT);

  advanceParticles(p, dt, withPika);

  float flash = p.bolt > 0.0f && g_bolt.t > 0.0f
                  ? sinf(g_bolt.t / 0.28f * PI) : 0.0f;
  uint32_t skyTop, skyBot;
  skyMods(hourF, p, skyTop, skyBot);
  for (int y = SCENE_TOP; y < HORIZON; y += 2) {
    uint32_t c24 = mix3(skyTop, skyBot,
                        (float)(y - SCENE_TOP) / (HORIZON - SCENE_TOP));
    if (flash > 0.0f) c24 = mix3(c24, 0xFFF6D0, flash * 0.28f);
    const int index = (y - SCENE_TOP) / 2;
    const uint16_t c16 = c565(c24);
    if (!g_skyInit || g_skyRow16[index] != c16) dirtyY(y, y + 2);
    g_skyRow16[index] = c16;
    g_skyRow24[index] = c24;
  }
  g_skyInit = true;

  uint32_t grassC = mix3(0x5E9C3A, 0x24384E, night * 0.72f);
  if (p.rain > 0.0f) grassC = mix3(grassC, 0x3E7030, p.rain * 0.4f);
  if (p.ice) grassC = mix3(grassC, 0xB8D0E6, 0.3f);
  const uint16_t grass = c565(grassC);
  const uint16_t dirt = c565(dirtColor24(hourF, p));
  const uint16_t hill = c565(mix3(mix3(0x4E8230, 0x2E4A5E, night * 0.7f),
                                    0xE8E6DC, p.snow > 0.0f ? 0.75f : 0.0f));
  const uint16_t hill2 = c565(mix3(mix3(0x3E6A24, 0x22384C, night * 0.7f),
                                     0xD8D6CE, p.snow > 0.0f ? 0.75f : 0.0f));
  if (grass != g_prevGrass || dirt != g_prevDirt) dirtyY(HORIZON, SCENE_BOT);
  if (hill != g_prevHill || hill2 != g_prevHill2) dirtyY(HORIZON - 30, HORIZON);

  float bx, by;
  bool sun;
  bodyAt(hourF, bx, by, sun);
  if (!g_bodyValid || (int)bx != (int)g_bodyOldX ||
      (int)by != (int)g_bodyOldY || sun != g_bodyWasSun ||
      moonPh != g_prevMoonPh || dt > 0.0f) {
    if (g_bodyValid) dirtyY((int)g_bodyOldY - 25, (int)g_bodyOldY + 26);
    dirtyY((int)by - 25, (int)by + 26);
  }
  for (int i = 0; i < 6; ++i) {
    const CloudPos c = cloudAt(i, p);
    const CloudPos& old = g_cloudOld[i];
    if (old.valid != c.valid ||
        (old.valid && ((int)old.x != (int)c.x || (int)old.y != (int)c.y))) {
      if (old.valid) dirtyY((int)old.y, (int)old.y + 22);
      if (c.valid) dirtyY((int)c.y, (int)c.y + 22);
    }
    g_cloudOld[i] = c;
  }
  for (int i = 0; i < 3; ++i) {
    const FogPos f = fogAt(i, p);
    const FogPos& old = g_fogOld[i];
    if (old.valid != f.valid ||
        (old.valid && ((int)old.x != (int)f.x || (int)old.y != (int)f.y))) {
      if (old.valid) dirtyY((int)old.y - 14, (int)old.y + 15);
      if (f.valid) dirtyY((int)f.y - 14, (int)f.y + 15);
    }
    g_fogOld[i] = f;
  }
  for (int i = 0; i < g_prevRainN || i < g_rainN; ++i) {
    const bool moved = i >= g_prevRainN || i >= g_rainN ||
        g_prevRain[i].x != (int16_t)RAIN[i].x ||
        g_prevRain[i].y != (int16_t)RAIN[i].y;
    if (!moved) continue;
    if (i < g_prevRainN) dirtyY(g_prevRain[i].y - 8, g_prevRain[i].y + 2);
    if (i < g_rainN) dirtyY((int)RAIN[i].y - 8, (int)RAIN[i].y + 2);
  }
  for (int i = 0; i < g_prevSnowN || i < g_snowN; ++i) {
    const bool moved = i >= g_prevSnowN || i >= g_snowN ||
        g_prevSnow[i].x != (int16_t)SNOWP[i].x ||
        g_prevSnow[i].y != (int16_t)SNOWP[i].y;
    if (!moved) continue;
    if (i < g_prevSnowN)
      dirtyY(g_prevSnow[i].y, g_prevSnow[i].y + g_prevSnow[i].size);
    if (i < g_snowN) dirtyY((int)SNOWP[i].y, (int)SNOWP[i].y + SNOWP[i].s);
  }
  const float jumpPhase = Pika::jumpPhase();
  if ((withPika || g_prevWithPika) &&
      (!g_bgValid || Pika::frame() != g_prevPikaFrame ||
       jumpPhase != g_prevJumpPhase || (dt > 0.0f && g_dustN > 0)))
    dirtyY(112, HORIZON + 20);
  if (dt > 0.0f) {
    if (night > 0.35f) for (const auto& s : STARS)
      dirtyY((int)s.y, (int)s.y + s.s);
    dirtyY(HORIZON, HORIZON + 20);
    if (p.ice) dirtyY(HORIZON, SCENE_BOT);
  }
  if (g_prevBoltVisible || (p.bolt > 0.0f && g_bolt.t > 0.16f))
    dirtyY(64, 169);
  if (g_prevBadgeVisible && !g_badgeVisible) dirtyY(222, SCENE_BOT);
  if (g_prevToastVisible && !g_toastVisible) dirtyY(196, 220);
  if (g_groundOverlayDirty) dirtyY(196, 220);

  for (int i = 0; i < BAND_N; ++i) {
    if (!g_bandDirty[i]) continue;
    const int top = SCENE_TOP + i * BAND_H;
    const int height = top + BAND_H <= SCENE_BOT ? BAND_H : SCENE_BOT - top;
    renderBand(Gfx::strip(), top, height, hourF, p, night, moonPh, withPika,
               grass, dirt, hill, hill2);
    pushBand(top, height);
  }

  g_bodyOldX = bx; g_bodyOldY = by; g_bodyWasSun = sun; g_bodyValid = true;
  g_prevGrass = grass; g_prevDirt = dirt;
  g_prevHill = hill; g_prevHill2 = hill2;
  g_prevRainN = g_rainN; g_prevSnowN = g_snowN;
  for (int i = 0; i < g_rainN; ++i)
    g_prevRain[i] = {(int16_t)RAIN[i].x, (int16_t)RAIN[i].y, 0};
  for (int i = 0; i < g_snowN; ++i)
    g_prevSnow[i] = {(int16_t)SNOWP[i].x, (int16_t)SNOWP[i].y, SNOWP[i].s};
  g_prevMoonPh = moonPh;
  g_prevWithPika = withPika;
  g_prevPikaFrame = Pika::frame();
  g_prevJumpPhase = jumpPhase;
  g_prevBoltVisible = p.bolt > 0.0f && g_bolt.t > 0.16f;
  g_prevBadgeVisible = g_badgeVisible;
  g_prevToastVisible = g_toastVisible;
  g_groundOverlayDirty = false;
  g_bgGrp = g; g_bgValid = true;
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

void footer(TFT_eSPI& t, uint16_t bg) {
  (void)bg;
  t.fillRect(0, FOOT_TOP, SCR_W, FOOT_H, TH::CARD_HI);
  t.drawFastHLine(0, FOOT_TOP, SCR_W, TH::RIM);
}

}  // namespace Scene
