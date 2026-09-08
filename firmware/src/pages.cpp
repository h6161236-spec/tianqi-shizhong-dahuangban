/**
 * =====================================================================
 *  pages.cpp —— 5 页面渲染 + 输入动作 + 弹窗 + 演示数据
 * =====================================================================
 *  移植自 preview/ui.html(布局参数逐条对应),适配:
 *   - 时钟页 110ms 全帧重绘(场景动画),音乐页 100ms,其余按需重绘
 *   - canvas 半透明 → 向底色 lerp 的实色近似(问 Scene 要取样色)
 *   - 弹窗/键盘整屏米白接管(参考工程做法,不做半透明遮罩)
 *   - 音乐页文字带「变了才擦重画」,圆盘+音波每帧推 disc,三键按需画
 *   - 密码键盘复用 keyboard 模块(4×10,参考工程验证过)
 */

#include "pages.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>

#include "cities.h"
#include "config.h"
#include "gfx.h"
#include "keyboard.h"
#include "lunar.h"
#include "pika.h"
#include "scene.h"
#include "text.h"
#include "theme.h"
#include "wifi_config.h"
#include "wx.h"

extern TFT_eSPI tft;

MediaState g_media = {};

namespace {

// ================================================================ 状态
uint8_t s_page = PG_CLOCK;
bool s_dirty = true;        // 需要重绘
bool s_needClear = true;    // 重绘前整屏清底(切页/关弹窗后)
bool s_clockHudDirty = true; // 时钟页 HUD(时间药丸/天气chip/状态条)重画
uint32_t s_lastRenderMs = 0;
uint16_t s_lastMinute = 0xFFFF;
int8_t s_demoLrcRow = -1;   // 演示版当前歌词行(变了才重绘音乐页文字区)

// 偏好(NVS)
uint8_t s_bright = 72, s_specStyle = 0, s_wxStyle = 0;

// 设置 / 日期
int8_t s_setSel = 0, s_setScroll = 0;
int8_t s_monthOff = 0;

// 弹窗
enum ModalKind : uint8_t {
  MOD_NONE = 0,
  MOD_WIFI,        // 网络列表
  MOD_CONNECTING,  // 正在连接(转圈)
  MOD_BT,          // 蓝牙面板
  MOD_RESET,
  MOD_REBOOT,
  MOD_CITY         // 选城市(自动定位 / 热门 / 省份 / 拼音)
};
ModalKind s_modal = MOD_NONE;
int8_t s_modalSel = 0;      // 弹窗按钮的上下键焦点
uint8_t s_modalBtnN = 0;    // 本弹窗按钮数(焦点循环用)
char s_connSsid[33] = "";   // 正在连的网
uint32_t s_connStartMs = 0;
bool s_scanWasDone = false;

// 城市选择
int8_t s_cityProv = -1;     // -1 顶层(自动+热门+省份),>=0 该省
int16_t s_citySel = 0;
char s_cityFilter[16] = ""; // 拼音过滤,空 = 不滤
uint8_t s_kbWhy = 0;        // 0=WiFi 密码  1=城市拼音

// toast
char s_toast[48] = "";
float s_toastT = 0.0f;

// 自动跳页
float s_manualT = 0.0f, s_idleT = 0.0f;

// 频谱(平滑值 + 峰值,0..255)
float s_spec[16] = {0}, s_peak[16] = {0}, s_specPhase = 0.0f;

// 音乐页进页标记(环形音波每帧全变,这页整屏重绘,不再做文字带缓存)
bool s_musicReset = true;

// 演示音乐(DEMO_MODE 用,数据同预览)
const char* DEMO_TITLE = "夜曲";
const char* DEMO_ARTIST = "周杰伦 · 十一月的萧邦";
const int DEMO_DUR = 227;
const char* DEMO_LRC[5] = {
    "蝴蝶自在飞 像那破碎的过往", "为你弹奏萧邦的夜曲", "纪念我死去的爱情",
    "而我为你隐姓埋名", "在月光下弹琴"};
int s_demoPos = 78;
float s_demoAcc = 0.0f;

// ================================================================ 小工具
bool timeValid() { return time(nullptr) > 1000000000L; }

uint16_t minuteKey() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
}

float hourFNow() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
}

Scene::Group curSceneGroup() { return Scene::wmoGroup(Wx::data().wmo); }

void mmss(char* out, size_t n, int s) {
  if (s < 0) { snprintf(out, n, "--:--"); return; }
  snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

// ================================================================ 状态条
void statusIcons(TFT_eSPI& target, uint16_t bg) {
  const bool online = WifiMgr::isConnected();
  const WiFiConfig* cfg = WifiMgr::getConfig();
  const uint16_t wifiC = online ? TH::GREEN
                       : (cfg->saved ? TH::AMBER : TH::TXT_LO);
  const uint16_t btC = (g_media.connected || g_media.advOn) ? TH::ACCENT
                                                            : TH::TXT_LO;
  Gfx::glyph(target, GL_BLUETOOTH, 296, 9, 13, btC, bg);
  Gfx::glyph(target, GL_WIFI, 278, 9, 13, wifiC, bg);
}

void statusBar(const char* left, const char* right, bool light = false) {
  const uint16_t bg = light ? Scene::statusVeil(hourFNow(), curSceneGroup()) : TH::BG;
  Gfx::beginStrip(bg);
  TFT_eSprite& st = Gfx::strip();
  if (light) {
    // 时钟页:叠在场景上的暗纱条
    Txt::draw(8, 13, CN12, hex565(0xF5F2E8), left);
    statusIcons(st, bg);
    if (right && right[0])
      Txt::drawRight(272, 13, CN12, hex565(0xD8D4C4), right);
  } else {
    Txt::draw(8, 13, CN12, TH::TXT_MID, left);
    statusIcons(st, bg);
    if (right && right[0])
      Txt::drawRight(272, 13, CN12, TH::TXT_MID, right);
    Gfx::divider(st, 0, 18, SCR_W, TH::LINE, TH::BG);
  }
  Gfx::pushStripH(0, 0, 19);
}

// ================================================================ 页点
// light=true:点画在草地上(时钟页的场景地面 / 其余四页的共用页脚),
// 底色不同但都要加深色描边才压得住 —— 同预览。
void pageDots(int cur, bool light) {
  if (light) {
    // 时钟页取场景地面色;其余四页页脚的草皮是固定色 0x7CB44E
    const uint16_t bg = (s_page == PG_CLOCK)
                            ? Scene::groundColor(hourFNow(), curSceneGroup())
                            : hex565(0x7CB44E);
    Gfx::dots(tft, SCR_W / 2, 234, PG_N, cur, TH::ACCENT,
              lerp565(bg, hex565(0xF5F2E8), 140), bg);
  } else {
    Gfx::dots(tft, SCR_W / 2, 237, PG_N, cur, TH::ACCENT, TH::TXT_LO, TH::BG);
  }
}

// ================================================================ 文本按钮
void textBtn(const char* id, int x, int y, int w, int h, const char* label,
             uint16_t c, int focusIdx = -1) {
  hitAdd(id, x, y, w, h);
  const bool down = hitIsPressed(id);
  const bool focused = (focusIdx >= 0 && s_modalSel == focusIdx);
  if (down)
    Gfx::cardGrad(tft, x, y, w, h, 7, lerp565(TH::CARD_HI, c, 77), TH::CARD_HI,
                  TH::BG);
  else if (focused)
    Gfx::cardRim(tft, x, y, w, h, 7, TH::CARD_HI, c, TH::BG);
  else
    Gfx::cardRim(tft, x, y, w, h, 7, TH::CARD, TH::RIM, TH::BG);
  Txt::drawCenter(x + w / 2, y + h / 2 + 5, CN12, down ? TH::TXT_HI : c, label);
}

// ================================================================ 页 1:时钟
void pageClock(float dt) {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  const int y = lt.tm_year + 1900, mo = lt.tm_mon + 1, dd = lt.tm_mday;
  LunarDate l = Lunar::fromSolar(y, mo, dd);

  // 状态条(节日/节气放右侧)画在场景之后,避免天空带盖住底 1px
  const char* fest = nullptr;
  if (l.valid) {
    fest = Lunar::festival(l, mo, dd);
    if (!fest) fest = Lunar::termToday(y, mo, dd);
  }

  // 场景(时段×天气)。时间没同步时固定正午,画面不乱
  const float hF = timeValid() ? hourFNow() : 12.0f;
  const Scene::Group grp = curSceneGroup();
  const float moonPh = l.valid ? ((l.day - 1) / 29.5f) : 0.5f;
  Scene::draw(tft, hF, grp, dt, moonPh, true);
  // 皮卡丘热区:比精灵本体放宽一圈(含尾巴和跳起空间),点它会跳。
  // 边距同预览 hit('c.pika', PK_X-16, gy-34, PK_W+32, PK_H*PK_SC+46)
  {
    constexpr int px = (SCR_W - Pika::PK_W) / 2;   // 136
    constexpr int gy = 188 + 4 - Pika::PK_H;       // 146(horizon=188)
    hitAdd("c.pika", px - 16, gy - 34, Pika::PK_W + 32, Pika::PK_H + 46);
  }

  // 热区每帧都要登记(时钟页每帧 hitClear)。HUD 本体只在变了才重画,
  // 否则时间药丸/天气 chip 每帧 AA 重描会自己闪。
  hitAdd("c.chip", 214, 22, 100, 50);
  {
    char badge[48];
    if (Wx::badgeText(badge, sizeof(badge))) hitAdd("c.net", 6, 222, 168, 20);
  }

  static int s_hudHM = -1;
  static uint8_t s_hudNightQ = 255;
  static Scene::Group s_hudGrp = Scene::GR_COUNT;
  const int hm = timeValid() ? (lt.tm_hour * 60 + lt.tm_min) : -2;
  const uint8_t nq = (uint8_t)(Scene::nightness(hF) * 40.0f);
  const bool hud = s_clockHudDirty || hm != s_hudHM || nq != s_hudNightQ ||
                   grp != s_hudGrp;
  if (hud) {
    s_clockHudDirty = false;
    s_hudHM = hm;
    s_hudNightQ = nq;
    s_hudGrp = grp;

    if (timeValid()) {
      char dateStr[20];
      snprintf(dateStr, sizeof(dateStr), "%d月%d日 %s", mo, dd,
               Lunar::weekName(lt.tm_wday));
      statusBar(dateStr, fest, true);
    } else {
      statusBar("时间未同步", "配网后自动校时", true);
    }

    // ---- 大时间:药丸画进 big 精灵(数字 AA 只在纯色底上做),分区推送 ----
    if (timeValid()) {
      char hhmm[8];
      snprintf(hhmm, sizeof(hhmm), "%02d:%02d", lt.tm_hour, lt.tm_min);
      const uint16_t skyC = Scene::skyPixel(hF, grp, 44);   // 药丸处的天空色
      const uint16_t pill = lerp565(skyC, hex565(0x10142C), 115);
      int digitW = 24;
      int w = Gfx::timeWidth(hhmm, digitW, 38, 3);
      if (w > 124) { digitW = 22; w = Gfx::timeWidth(hhmm, digitW, 38, 3); }
      Gfx::beginBig(skyC);
      Gfx::big().fillSmoothRoundRect(0, 0, 128, 48, 10, pill, skyC);
      Gfx::bigTime(Gfx::big(), (128 - w) / 2, 5, hhmm, digitW, 38, 3, hex565(0xFDF9EC),
                   hex565(0xF0EAD0), pill, true);
      Gfx::pushBigRegion(6, 20, 128, 48, 0, 0);
    } else {
      Gfx::beginBig(Scene::skyPixel(hF, grp, 44));
      Txt::drawCenter(64, 30, CN16, hex565(0xFDF9EC), "--:--");
      Gfx::pushBigRegion(6, 20, 128, 48);
    }

    // ---- 农历行(白字 + 暗影;场景侧为这行挖了 HUD 洞) ----
    {
      char luna[48];
      if (l.valid) {
        char lt1[24];
        Lunar::dateText(l, lt1, sizeof(lt1));
        snprintf(luna, sizeof(luna), "%s · %s年", lt1, Lunar::zodiac(l.year));
      } else {
        snprintf(luna, sizeof(luna), "%d年%d月%d日", y, mo, dd);
      }
      const uint16_t sky75 = Scene::skyPixel(hF, grp, 78);
      Gfx::beginBig(sky75);
      for (int row = 0; row < 18; ++row)
        Gfx::big().drawFastHLine(0, row, 202, Scene::skyPixel(hF, grp, 70 + row));
      Txt::draw(6, 14, CN12, lerp565(sky75, hex565(0x101018), 96), luna);
      Txt::draw(6, 13, CN12, hex565(0xF0EAD0), luna);
      Gfx::pushBigRegion(8, 70, 202, 18);
    }

    // ---- 天气 chip(磨砂白 = 天空色混白;点它跳天气页) ----
    {
      const Wx::Data& wx = Wx::data();
      const uint16_t sky47 = Scene::skyPixel(hF, grp, 47);
      const uint16_t fill = lerp565(sky47, hex565(0xFCFAF2), 199);
      const uint16_t rim = lerp565(sky47, hex565(0x786E50), 90);
      Gfx::beginBig(sky47);
      Gfx::big().fillSmoothRoundRect(0, 0, 100, 50, 9, fill, sky47);
      Gfx::big().drawSmoothRoundRect(0, 0, 9, 7, 100, 50, rim, sky47);
      Scene::Group igrp = grp;
      if (igrp == Scene::GR_MOSTLY) igrp = Scene::GR_CLEAR;
      Gfx::wxIcon(Gfx::big(), (WxIcon)igrp, 20, 18, 24, fill);
      char tempS[8];
      snprintf(tempS, sizeof(tempS), "%d", wx.temp);
      const int tw = Txt::draw(38, 26, NUM22, TH::TXT_HI, tempS);
      Txt::draw(tw, 26, CN12, TH::TXT_HI, "°");
      char wtx[24];
      Txt::clip(CN12, Wx::wmoText(wx.wmo), 56, wtx, sizeof(wtx));
      Txt::draw(38, 40, CN12, TH::TXT_MID, wtx);
      Gfx::pushBigRegion(214, 22, 100, 50);
    }

    // 角标和页点压在地面上;地面重建时 hud 也会为 true,一起补回。
    {
      char badge[48];
      const char* b = Wx::badgeText(badge, sizeof(badge));
      if (b) {
        const uint16_t groundC = Scene::groundColor(hF, grp);
        tft.fillSmoothRoundRect(6, 222, 168, 20, 9,
                                lerp565(groundC, hex565(0x141008), 140), groundC);
        Txt::draw(14, 236, CN12, Wx::data().demo ? TH::RED : TH::AMBER, b);
      }
    }
    pageDots(0, true);
  }
}

// ================================================================ 页 2:音乐
// 布局同预览 MU:圆封面当主角,音波沿封面外圈跳,右侧是文字栏
constexpr int MU_CX = 88, MU_CY = 100, MU_CR = 42;   // 圆封面
constexpr int MU_TXTX = 162, MU_TXTW = 148;          // 右侧文字栏
constexpr int MU_BTNCY = 178, MU_BTNGAP = 44;        // 三个圆形传输键
constexpr uint32_t MU_LRC_HOLD_MS = 1200;             // 新歌词先完整展示开头
constexpr int MU_LRC_SPEED = 24;                      // 跑马灯速度(px/s)
// disc Sprite:圆盘+环形音波。底边 36+132=168,style0 最长射线到 y≈165。
// 三键顶边 play=158 / prev=164,推屏时裁掉键区,避免每帧盖住按钮。
constexpr int MU_DISC_X = 26, MU_DISC_Y = 36;
constexpr int MU_BTN_TOP = MU_BTNCY - 21;            // 含按钮 Sprite 的 1px 边距

// 环上分格数:16 段镜像成 32,环才匀(同预览 SPEC_N)
constexpr int SPEC_N = 32;
// 左右对称取样(同预览 specAt)
inline float specAt(int i) { return s_spec[i < 16 ? i : 31 - i] / 255.0f; }

// 角度:0 度在 12 点方向,顺时针(同预览 arcAngle)
inline float ringAngle(int i) {
  return (i * (360.0f / SPEC_N) - 180.0f) * PI / 180.0f - PI / 2.0f;
}

/**
 * 环形音波:16 段沿封面圆盘外圈跳动(数据来自 BLE 的 SPEC 帧)。
 * 3 种样式都画在这个环上,上下滑切换。同预览 specRing。
 * 背景是纯底色,擦除靠调用方先清区域。t 可以是屏幕或 disc Sprite。
 */
void specRing(TFT_eSPI& t, int cx, int cy, int r, int style) {
  if (style == 0) {
    // 放射条:圆头橙红射线,同预览 lineCap=round / neon(v) / r+3+v*17
    for (int i = 0; i < SPEC_N; ++i) {
      const float a = ringAngle(i), v = specAt(i);
      const float ca = cosf(a), sa = sinf(a);
      float r0 = (float)r;
      float r1 = r + 3.0f + v * 17.0f;
      auto maxR = [&](float lim, float p, float d) -> float {
        if (fabsf(d) < 1e-4f) return r1;
        return (lim - p) / d;
      };
      if (ca > 0) r1 = fminf(r1, maxR((float)(GFX_DISC_W - 2), (float)cx, ca));
      if (ca < 0) r1 = fminf(r1, maxR(1.0f, (float)cx, ca));
      if (sa > 0) r1 = fminf(r1, maxR((float)(GFX_DISC_H - 2), (float)cy, sa));
      if (sa < 0) r1 = fminf(r1, maxR(1.0f, (float)cy, sa));
      if (r1 < r0 + 2.0f) r1 = r0 + 2.0f;
      const uint16_t col = TH::neon((uint8_t)(v * 255.0f));
      const float x0 = cx + ca * r0, y0 = cy + sa * r0;
      const float x1 = cx + ca * r1, y1 = cy + sa * r1;
      t.drawWideLine(x0, y0, x1, y1, 3.0f, col, TH::BG);
      t.fillCircle((int)(x1 + 0.5f), (int)(y1 + 0.5f), 1, col);
      t.fillCircle((int)(x0 + 0.5f), (int)(y0 + 0.5f), 1, col);
    }
  } else if (style == 1) {
    // 环形波纹:峰值连成一条闭合曲线
    float px = 0, py = 0;
    for (int i = 0; i <= SPEC_N; ++i) {
      const int k = i % SPEC_N;
      const float a = ringAngle(k), rr = r + 4.0f + specAt(k) * 16.0f;
      const float x = cx + cosf(a) * rr, y = cy + sinf(a) * rr;
      if (i) t.drawWideLine(px, py, x, y, 2.0f, TH::RED, TH::BG);
      px = x; py = y;
    }
    // 峰点点缀
    for (int i = 0; i < SPEC_N; i += 4) {
      const float a = ringAngle(i), v = specAt(i);
      const float rr = r + 4.0f + v * 16.0f;
      t.fillSmoothCircle(cx + cosf(a) * rr, cy + sinf(a) * rr, 2,
                           TH::neon((uint8_t)(v * 255.0f)), TH::BG);
    }
  } else {
    // 光点绕圈:每段一颗珠子,大小/亮度随音量
    for (int i = 0; i < SPEC_N; ++i) {
      const float a = ringAngle(i), v = specAt(i);
      const float rr = r + 6.0f + v * 15.0f;
      // 无 alpha:用向底色淡出近似预览的 .35+v*.65 透明度
      const uint16_t c = lerp565(TH::BG, TH::neon((uint8_t)(v * 255.0f)),
                                 (uint8_t)((0.35f + v * 0.65f) * 255.0f));
      t.fillSmoothCircle(cx + cosf(a) * rr, cy + sinf(a) * rr,
                           (int)(1.6f + v * 2.4f + 0.5f), c, TH::BG);
    }
    t.drawSmoothArc(cx, cy, r + 1, r, 0, 360,
                      lerp565(TH::BG, TH::GOLD_DK, 204), TH::BG, false);
  }
}

/**
 * 封面圆盘:浅黄唱盘 + 音符。皮卡丘只出现在时钟页,这里不画。
 * 同预览 coverDisc。spin 是虚线环自转角度(度)。
 * 频谱走环形样式时不画虚线环(dashRing=false),让位给音波。
 */
void coverDisc(TFT_eSPI& t, int cx, int cy, int r, float spin, bool dashRing) {
  if (dashRing) {
    constexpr int SEG = 22;
    for (int i = 0; i < SEG; ++i) {
      const float a0 = i * (360.0f / SEG) + spin;
      t.drawSmoothArc(cx, cy, r + 7, r + 3, (int)a0,
                        (int)(a0 + 360.0f / SEG * 0.6f),
                        (i % 2) ? TH::ACCENT : TH::RED_DK, TH::BG, false);
    }
  }
  t.fillSmoothCircle(cx, cy, r, TH::GOLD_DK, TH::BG);
  t.drawSmoothCircle(cx, cy, (int)(r * 0.72f),
                     lerp565(TH::GOLD_DK, TH::ACCENT, 140), TH::GOLD_DK);
  t.fillSmoothCircle(cx, cy, (int)(r * 0.38f), TH::ACCENT, TH::GOLD_DK);
  Gfx::glyph(t, GL_NOTE, cx, cy, (int)(r * 0.72f), TH::TXT_HI, TH::ACCENT);
  t.drawSmoothCircle(cx, cy, r, TH::ACCENT, TH::GOLD_DK);
}

/**
 * 仅切页/弹窗返回/连接状态切换时清主体。逐项比较显示缓存，文字、状态栏
 * 和按钮都在 Sprite 中合成后推送；歌词心跳不会重画未变化的区域。
 */
void pageMusic(float dt, bool rebuild) {
  static bool valid = false, wasConnected = false;
  static char oldStatus[80] = "", oldTitle[64] = "", oldArtist[80] = "";
  static char oldLyrics[3][256] = {};
  static uint32_t lyricStartMs = 0;
  static int oldButtons = -1;
  static bool hadToast = false;
#ifdef DEMO_MODE
  const bool connected = true;
#else
  const bool connected = g_media.connected;
#endif
  const bool reset = rebuild || s_musicReset || !valid || connected != wasConnected;
  valid = true;
  wasConnected = connected;
  s_musicReset = false;
  char status[80];
  snprintf(status, sizeof(status), "%d/%d/%d/%d/%s", g_media.playing,
           g_media.advOn, WifiMgr::isConnected(), WifiMgr::getConfig()->saved,
           g_media.peer);
  const bool statusChanged = reset || strcmp(status, oldStatus) != 0;
  if (reset) {
    tft.fillRect(0, 0, SCR_W, Scene::FOOT_TOP, TH::BG);
    Scene::footer(tft, TH::BG);
    pageDots(1, true);
  }
  if (!reset && hadToast && s_toastT <= 0.0f) {
    Gfx::beginStrip(TH::BG);
    Gfx::pushStripH(0, 200, Scene::FOOT_TOP - 200);
    Scene::footer(tft, TH::BG);
    pageDots(1, true);
  }
  hadToast = s_toastT > 0.0f;
#ifndef DEMO_MODE
  if (!connected) {
    // 等手机来连 / 蓝牙关了:主体+静止页脚按需重绘
    if (statusChanged) {
      const bool adv = g_media.advOn;
      tft.fillRect(0, 0, SCR_W, Scene::FOOT_TOP, TH::BG);
      statusBar("音乐", adv ? "广播中" : "蓝牙已关");
      Gfx::glyph(tft, GL_BLUETOOTH, SCR_W / 2, 82, 40,
                 adv ? TH::ACCENT : TH::TXT_LO, TH::BG);
      Txt::drawCenter(SCR_W / 2, 124, CN16, TH::TXT_MID,
                      adv ? "等手机来连" : "蓝牙已关闭");
      Txt::drawCenter(SCR_W / 2, 143, NUM14, adv ? TH::ACCENT : TH::TXT_LO,
                      "Pika-Music");
      Txt::drawCenter(SCR_W / 2, 158, CN12, TH::TXT_LO,
                      "手机打开配套 App,搜到名字点连接");
      textBtn("m.btpanel", 96, 170, 128, 30, adv ? "蓝牙设置" : "开启蓝牙",
              TH::ACCENT);
    }
    if (!statusChanged) hitAdd("m.btpanel", 96, 170, 128, 30);
    strlcpy(oldStatus, status, sizeof(oldStatus));
    return;
  }
#endif

  const bool playing = g_media.playing;
  const int pos = g_media.pos >= 0 ? g_media.pos : s_demoPos;
  if (statusChanged) {
    statusBar(playing ? "正在播放" : "已暂停",
              g_media.peer[0] ? g_media.peer : "", false);
    strlcpy(oldStatus, status, sizeof(oldStatus));
  }
  const char* ti = g_media.title[0] ? g_media.title : DEMO_TITLE;
  const char* ar = g_media.artist[0] ? g_media.artist : DEMO_ARTIST;
  if (reset || strcmp(ti, oldTitle) || strcmp(ar, oldArtist)) {
      Gfx::beginBig(TH::BG);
      char c[48];
      Txt::clip(CN16, ti, MU_TXTW, c, sizeof(c));
      Txt::draw(0, 18, CN16, TH::TXT_HI, c);
      Txt::clip(CN12, ar, MU_TXTW, c, sizeof(c));
      Txt::draw(0, 36, CN12, TH::TXT_MID, c);
      Gfx::divider(Gfx::big(), 0, 48, MU_TXTW, TH::RIM, TH::BG);
      Gfx::pushBigRegion(MU_TXTX, 44, MU_TXTW, 50);
      strlcpy(oldTitle, ti, sizeof(oldTitle));
      strlcpy(oldArtist, ar, sizeof(oldArtist));
  }
#ifdef DEMO_MODE
      const int li = (pos / 6) % 5;
      const char* l0 = DEMO_LRC[li];
      const char* l1 = DEMO_LRC[(li + 1) % 5];
      const char* l2 = DEMO_LRC[(li + 2) % 5];
#else
      // BLE 模式手机只推当前行,后两行留空
      const char* l0 = g_media.lyric;
      const char* l1 = "";
      const char* l2 = "";
#endif
  const bool lyricsChanged = reset || strcmp(l0, oldLyrics[0]) ||
                             strcmp(l1, oldLyrics[1]) ||
                             strcmp(l2, oldLyrics[2]);
  if (lyricsChanged) lyricStartMs = millis();
  const bool lyricScrolls = l0[0] && Txt::width(CN12, l0) > MU_TXTW;
  if (lyricsChanged || lyricScrolls) {
      Gfx::beginBig(TH::BG);
      char c[48];
      if (l0[0]) {
        const uint32_t elapsed = millis() - lyricStartMs;
        const int offset = lyricScrolls && elapsed > MU_LRC_HOLD_MS
                               ? (int)(((elapsed - MU_LRC_HOLD_MS) / 1000U) *
                                           (uint32_t)MU_LRC_SPEED +
                                       ((elapsed - MU_LRC_HOLD_MS) % 1000U) *
                                           (uint32_t)MU_LRC_SPEED / 1000U)
                               : 0;
        Txt::marquee(16, MU_TXTW, CN12, TH::RED, l0, offset);
        if (offset > 0)
          Txt::fadeEdges(Gfx::big(), MU_TXTW, 20, 8, TH::BG);
      }
      if (l1[0]) {
        Txt::clip(CN12, l1, MU_TXTW, c, sizeof(c));
        Txt::draw(0, 32, CN12, TH::TXT_LO, c);
      }
      if (l2[0]) {
        Txt::clip(CN12, l2, MU_TXTW, c, sizeof(c));
        Txt::draw(0, 47, CN12, TH::TXT_LO, c);
      }
      Gfx::pushBigRegion(MU_TXTX, 96, MU_TXTW, 54);
      strlcpy(oldLyrics[0], l0, sizeof(oldLyrics[0]));
      strlcpy(oldLyrics[1], l1, sizeof(oldLyrics[1]));
      strlcpy(oldLyrics[2], l2, sizeof(oldLyrics[2]));
  }

  // ---- 圆盘 + 环形音波:画进 disc Sprite,推屏时避开三键 ----
  // 背景是纯色 TH::BG,不透明覆盖,不会闪白也不留残影。
  TFT_eSprite& d = Gfx::disc();
  d.fillSprite(TH::BG);
  specRing(d, MU_CX - MU_DISC_X, MU_CY - MU_DISC_Y, MU_CR + 3, s_specStyle);
  coverDisc(d, MU_CX - MU_DISC_X, MU_CY - MU_DISC_Y, MU_CR,
            playing ? s_specPhase * 26.0f : 0.0f, false);

  // 3 个圆形传输键:上一曲 / 播放(大) / 下一曲,横排在封面下方。
  // 主键实心红大圆,副键描边小圆(同预览 roundBtn)。按钮直接画屏。
  const int buttonState = (playing ? 1 : 0) | (hitIsPressed("m.prev") ? 2 : 0) |
                          (hitIsPressed("m.play") ? 4 : 0) |
                          (hitIsPressed("m.next") ? 8 : 0);
  const bool paintButtons = reset || buttonState != oldButtons;
  auto roundBtn = [paintButtons](const char* id, int screenX, int r, Glyph k,
                                bool primary) {
    const int hitR = (r + 6 > 15) ? r + 6 : 15;
    hitAdd(id, screenX - hitR, MU_BTNCY - hitR, hitR * 2, hitR * 2);
    if (!paintButtons) return;
    Gfx::beginBig(TH::BG);
    TFT_eSprite& t = Gfx::big();
    const int cx = r + 1, cy = r + 1;
    const bool down = hitIsPressed(id);
    if (primary) {
      Gfx::glow(t, cx, cy, r, TH::RED, TH::BG);
      const uint16_t fill = down ? TH::ACCENT : TH::RED;
      t.fillSmoothCircle(cx, cy, r, fill, TH::BG);
      Gfx::glyph(t, k, cx, cy, r,
                 down ? TH::TXT_HI : hex565(0xFFFBF0), fill);
    } else {
      const uint16_t fill = down ? TH::GOLD_DK : TH::CARD;
      t.fillSmoothCircle(cx, cy, r, fill, TH::BG);
      t.drawSmoothCircle(cx, cy, r, down ? TH::ACCENT : TH::RIM, fill);
      Gfx::glyph(t, k, cx, cy, (int)(r * 0.92f),
                 down ? TH::TXT_HI : TH::TXT_MID, fill);
    }
    Gfx::pushBigRegion(screenX - cx, MU_BTNCY - cy, 2 * r + 3, 2 * r + 3);
  };
  // 整块 push 会把精灵里的米白底盖到三键上。主键顶 y=158,副键圆顶 y=164。
  // 先推到主键顶;再把封面正下方的射线尾巴(x 停在上一曲键左侧)补上。
  {
    const int visH = MU_BTN_TOP - MU_DISC_Y;
    Gfx::pushDiscRegion(MU_DISC_X, MU_DISC_Y, GFX_DISC_W, visH);
    // y=158..164 上一曲圆还没到,可以把射线补到播放键左缘;再往下停在上一曲左缘
    const int playLeft = SCR_W / 2 - 21;
    const int prevLeft = SCR_W / 2 - MU_BTNGAP - 15;
    const int prevTop = MU_BTNCY - 15;
    const int discBot = MU_DISC_Y + GFX_DISC_H;
    const int srcBase = MU_BTN_TOP - MU_DISC_Y;
    if (prevTop > MU_BTN_TOP)
      Gfx::pushDiscRegion(MU_DISC_X, MU_BTN_TOP, playLeft - MU_DISC_X,
                          prevTop - MU_BTN_TOP, 0, srcBase);
    if (discBot > prevTop)
      Gfx::pushDiscRegion(MU_DISC_X, prevTop, prevLeft - MU_DISC_X,
                          discBot - prevTop, 0, prevTop - MU_DISC_Y);
  }
  roundBtn("m.prev", SCR_W / 2 - MU_BTNGAP, 14, GL_PREV, false);
  roundBtn("m.play", SCR_W / 2, 20, playing ? GL_PAUSE : GL_PLAY, true);
  roundBtn("m.next", SCR_W / 2 + MU_BTNGAP, 14, GL_NEXT, false);
  oldButtons = buttonState;
}

// ================================================================ 页 3:天气
// 温度区间条:槽 + [lo,hi] 子段蓝→黄渐变(圆头)
void tempBar(int lo, int hi, int allLo, int allHi, int x, int y, int w, int h) {
  tft.fillSmoothRoundRect(x, y, w, h, h / 2, TH::CARD_HI, TH::BG);
  const int span = (allHi - allLo) > 0 ? (allHi - allLo) : 1;
  int x1 = x + (lo - allLo) * w / span;
  int x2 = x + (hi - allLo) * w / span;
  if (x2 - x1 < 6) x2 = x1 + 6;
  if (x2 > x + w) x2 = x + w;
  if (x1 < x) x1 = x;
  const int r = h / 2;
  for (int i = x1; i < x2; ++i) {
    const uint16_t c = lerp565(TH::BLUE, TH::AMBER,
                               (uint8_t)((i - x) * 255 / (w - 1)));
    int top = 0, bot = h;
    if (i - x1 < r) {
      const int d = r - 1 - (i - x1);
      const int cut = r - (int)(sqrtf((float)(r * r - d * d)) + 0.5f);
      top = cut;
      bot = h - cut;
    } else if (i >= x2 - r) {
      const int d = i - (x2 - r);
      const int cut = r - (int)(sqrtf((float)(r * r - d * d)) + 0.5f);
      top = cut;
      bot = h - cut;
    }
    if (bot > top) tft.drawFastVLine(i, y + top, bot - top, c);
  }
}

WxIcon sceneToIcon(Scene::Group g) {
  if (g == Scene::GR_MOSTLY) return WX_CLEAR;  // 基本晴 = 纯太阳(同预览)
  return (WxIcon)g;
}

void kvRow(int x, int y, const char* k, const char* v, uint16_t c) {
  Txt::draw(x, y, CN12, TH::TXT_LO, k);
  Txt::drawRight(x + 62, y, CN12, c, v);
}

void pageWeather(float dt) {
  const Wx::Data& wx = Wx::data();
  char badge[48];
  const char* b = Wx::badgeText(badge, sizeof(badge));
  char rightS[48];
  if (b) {
    strlcpy(rightS, b, sizeof(rightS));
  } else {
    snprintf(rightS, sizeof(rightS), "在线 · %d分钟",
             wx.fetchedAt > 0 ? (int)((time(nullptr) - wx.fetchedAt) / 60) : 0);
  }

  // 7 天公共温度范围
  int allLo = 99, allHi = -99;
  for (int i = 0; i < wx.dayN; ++i) {
    if (wx.days[i].tmin < allLo) allLo = wx.days[i].tmin;
    if (wx.days[i].tmax > allHi) allHi = wx.days[i].tmax;
  }

  statusBar(wx.city, rightS);

  if (s_wxStyle == 1) {
    // 整页七日(坐标同预览:行距 26,首行 y=24,今天一张白卡)
    for (int i = 0; i < wx.dayN; ++i) {
      const Wx::Day& d = wx.days[i];
      const int y = 24 + i * 26;
      if (i == 0) Gfx::card(tft, 6, y, 308, 24, 6, TH::CARD, TH::BG);
      Txt::draw(12, y + 16, CN12, i == 0 ? TH::TXT_HI : TH::TXT, Wx::dayLabel(i));
      Gfx::wxIcon(tft, sceneToIcon(Scene::wmoGroup(d.wmo)), 55, y + 12, 18,
                  i == 0 ? TH::CARD : TH::BG);
      char wtx[24];
      Txt::clip(CN12, Wx::wmoText(d.wmo), 44, wtx, sizeof(wtx));
      Txt::draw(70, y + 16, CN12, TH::TXT_MID, wtx);
      char ts[8];
      snprintf(ts, sizeof(ts), "%d°", d.tmin);
      Txt::drawRight(148, y + 16, CN12, TH::BLUE, ts);
      tempBar(d.tmin, d.tmax, allLo, allHi, 154, y + 9, 114, 7);
      snprintf(ts, sizeof(ts), "%d°", d.tmax);
      Txt::drawRight(302, y + 16, CN12, TH::AMBER, ts);
    }
    Scene::footer(tft, TH::BG);
    pageDots(2, true);
    return;
  }

  // 样式 0:当前概览 + 七日紧凑列表
  Gfx::wxIcon(tft, sceneToIcon(Scene::wmoGroup(wx.wmo)), 39, 55, 58, TH::BG);
  char tempS[8];
  snprintf(tempS, sizeof(tempS), "%d", wx.temp);
  Txt::draw(74, 70, NUM22, TH::TXT_HI, tempS);
  Txt::draw(74 + Txt::width(NUM22, tempS) + 2, 48, CN12, TH::TXT_MID, "°C");
  Txt::draw(76, 84, CN16, TH::TXT, Wx::wmoText(wx.wmo));

  Gfx::card(tft, 180, 26, 132, 62, 7, TH::CARD, TH::BG);
  char vs[16];
  snprintf(vs, sizeof(vs), "%d°", wx.feels);
  kvRow(188, 44, "体感", vs, TH::AMBER);
  snprintf(vs, sizeof(vs), "%d%%", wx.hum);
  kvRow(188, 60, "湿度", vs, TH::BLUE);
  kvRow(188, 76, "风", wx.wind, TH::TXT);

  Gfx::divider(tft, 8, 98, 304, TH::LINE, TH::BG);
  Txt::draw(10, 114, CN12, TH::TXT_MID, "未来七天");

  // 七日紧凑列表(坐标同预览:首行 y=116,行距 13,7 行止于 207,让开页脚)
  for (int i = 0; i < wx.dayN; ++i) {
    const Wx::Day& d = wx.days[i];
    const int y = 116 + i * 13;
    Txt::draw(10, y + 8, CN12, i == 0 ? TH::TXT_HI : TH::TXT_MID, Wx::dayLabel(i));
    Gfx::wxIcon(tft, sceneToIcon(Scene::wmoGroup(d.wmo)), 50, y + 6, 12, TH::BG);
    char ts[8];
    snprintf(ts, sizeof(ts), "%d°", d.tmin);
    Txt::drawRight(88, y + 8, CN12, TH::BLUE, ts);
    tempBar(d.tmin, d.tmax, allLo, allHi, 92, y + 3, 174, 5);
    snprintf(ts, sizeof(ts), "%d°", d.tmax);
    Txt::drawRight(310, y + 8, CN12, TH::AMBER, ts);
  }
  Scene::footer(tft, TH::BG);
  pageDots(2, true);
}

// ================================================================ 页 4:日期
void pageDate(float dt) {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  int vy = lt.tm_year + 1900, vm = lt.tm_mon + 1;
  int m = vm + s_monthOff;
  while (m > 12) { m -= 12; ++vy; }
  while (m < 1) { m += 12; --vy; }
  const bool isThis = (s_monthOff == 0);

  char head[24];
  snprintf(head, sizeof(head), "%d 年 %d 月", vy, m);
  statusBar(head, s_monthOff ? "滑回本月" : "今天", false);

  const int cw = 42, x0 = 8, y0 = 28;
  for (int i = 0; i < 7; ++i) {
    // weekName "周日" 跳过首字 → "日"
    Txt::drawCenter(x0 + i * cw + cw / 2, y0 + 9, CN12,
                    (i == 0 || i == 6) ? TH::PINK : TH::TXT_MID,
                    Lunar::weekName(i) + 3);
  }
  Gfx::divider(tft, x0, y0 + 14, cw * 7, TH::LINE, TH::BG);

  const int firstDow = Lunar::weekday(vy, m, 1);
  const int dim = Lunar::monthLen(vy, m);
  // 格高同预览 min(28, 150/rows):150 是让开底部信息带+页脚后的可用高度
  const int rows = (firstDow + dim + 6) / 7;
  const int rh = (150 / rows > 28) ? 28 : 150 / rows;
  for (int i = firstDow; i < firstDow + dim; ++i) {
    const int day = i - firstDow + 1;
    const int col = i % 7, row = i / 7;
    const int cx = x0 + col * cw + cw / 2, cy = y0 + 20 + row * rh;
    const bool today = isThis && timeValid() && day == lt.tm_mday;
    const LunarDate l = Lunar::fromSolar(vy, m, day);
    const char* fest = Lunar::festival(l, m, day);
    const char* term = Lunar::termToday(vy, m, day);

    if (today) {
      const int tx = x0 + col * cw + 2, ty = cy - 3;
      Gfx::cardGrad(tft, tx, ty, cw - 4, rh - 2, 5, TH::GOLD_DK, TH::CARD_HI,
                    TH::BG);
      tft.drawSmoothRoundRect(tx, ty, 5, 3, cw - 4, rh - 2, TH::ACCENT, TH::BG);
    }
    const bool weekend = (col == 0 || col == 6);
    char ds[5];
    snprintf(ds, sizeof(ds), "%d", day);
    Txt::drawCenter(cx, cy + 10, NUM14,
                    today ? TH::TXT_HI : (weekend ? TH::PINK : TH::TXT), ds);
    // 副行:节日 > 节气 > 农历(初一给月名)
    char sub[20];
    if (fest) {
      strlcpy(sub, fest, sizeof(sub));
    } else if (term) {
      strlcpy(sub, term, sizeof(term));
    } else if (l.valid) {
      if (l.day == 1) Lunar::dateText(l, sub, sizeof(sub));
      else strlcpy(sub, Lunar::dayName(l.day), sizeof(sub));
    } else {
      sub[0] = 0;
    }
    const uint16_t subC = fest ? TH::RED
                       : term ? TH::GREEN
                       : today ? TH::ACCENT : TH::TXT_LO;
    if (rh >= 24 && sub[0]) {
      char subClip[16];
      Txt::clip(CN12, sub, 40, subClip, sizeof(subClip));
      Txt::drawCenter(cx, cy + 21, CN12, subC, subClip);
    }
  }

  // 今日农历信息带(坐标同预览:分割线 196、文字基线 207,让开页脚)
  if (isThis) {
    Gfx::divider(tft, 8, 196, 304, TH::LINE, TH::BG);
    LunarDate l = Lunar::fromSolar(lt.tm_year + 1900, lt.tm_mon + 1,
                                          lt.tm_mday);
    char line[64];
    if (l.valid) {
      char lt1[24], sb[10];
      Lunar::dateText(l, lt1, sizeof(lt1));
      Lunar::stemBranch(l.year, sb, sizeof(sb));
      snprintf(line, sizeof(line), "%s · %s%s年 · 上下滑切月", lt1, sb,
               Lunar::zodiac(l.year));
    } else {
      snprintf(line, sizeof(line), "上下滑切月");
    }
    char lineClip[52];
    Txt::clip(CN12, line, 300, lineClip, sizeof(lineClip));
    Txt::draw(10, 207, CN12, TH::AMBER, lineClip);
  }
  Scene::footer(tft, TH::BG);
  pageDots(3, true);
}

// ================================================================ 页 5:设置
struct SetRow {
  const char* k;
  char v[48];
  uint16_t c;
  const char* act;   // nullptr = 只读行
  bool arrow, warn, bar;
};
constexpr int SET_ROW_MAX = 12;

void buildSettingRows(SetRow* rows, int& n) {
  n = 0;
  const WiFiConfig* cfg = WifiMgr::getConfig();
  const bool online = WifiMgr::isConnected();
  SetRow r;

  r = SetRow{"WiFi", "", TH::GREEN, "wifi", true, false, false};
  if (online) snprintf(r.v, sizeof(r.v), "%s", cfg->ssid);
  else if (cfg->saved && cfg->ssid[0])
    snprintf(r.v, sizeof(r.v), "离线 · %s", cfg->ssid);
  else snprintf(r.v, sizeof(r.v), "未配置,点此扫描");
  if (!online) r.c = cfg->saved ? TH::AMBER : TH::RED;
  rows[n++] = r;

  r = SetRow{"城市", "", TH::ACCENT, "city", true, false, false};
  if (Wx::isAuto())
    snprintf(r.v, sizeof(r.v), "%s · 自动", Wx::place().name);
  else
    strlcpy(r.v, Wx::place().name, sizeof(r.v));
  rows[n++] = r;

  r = SetRow{"蓝牙", "", TH::ACCENT, "bt", true, false, false};
  if (g_media.connected) snprintf(r.v, sizeof(r.v), "已连接 %s", g_media.peer);
  else if (g_media.advOn) snprintf(r.v, sizeof(r.v), "广播中·等连接");
  else { snprintf(r.v, sizeof(r.v), "已关闭"); r.c = TH::TXT_LO; }
  rows[n++] = r;

  r = SetRow{"亮度", "", TH::AMBER, "bright", false, false, true};
  snprintf(r.v, sizeof(r.v), "%d%%", s_bright);
  rows[n++] = r;

  static const char* SPEC_NAMES[3] = {"放射条", "环形波纹", "光点绕圈"};
  r = SetRow{"频谱样式", "", TH::ACCENT, "spec", false, false, false};
  strlcpy(r.v, SPEC_NAMES[s_specStyle], sizeof(r.v));
  rows[n++] = r;

  r = SetRow{"天气视图", "", TH::ACCENT, "wxview", false, false, false};
  strlcpy(r.v, s_wxStyle ? "整页七日" : "概览+七日", sizeof(r.v));
  rows[n++] = r;

  r = SetRow{"恢复默认设置", "", TH::AMBER, "reset", true, true, false};
  rows[n++] = r;

  r = SetRow{"重启", "", TH::RED, "reboot", true, true, false};
  rows[n++] = r;

  r = SetRow{"关于", "ESP32-2432S028 · 5页系统", TH::TXT_LO, nullptr, false, false,
       false};
  rows[n++] = r;
}

void pageSettings(float dt) {
  SetRow rows[SET_ROW_MAX];
  int n = 0;
  buildSettingRows(rows, n);

  char head[16];
  snprintf(head, sizeof(head), "%d/%d", s_setSel + 1, n);
  statusBar("设置", head);

  // 滚动窗口(同预览 SET_VIS/SET_PITCH/SET_TOP:5 行 × 36 + 24,让开页脚)
  const int SET_VIS = 5, PITCH = 36, TOP = 24;
  int lo = s_setSel - SET_VIS + 1;
  if (lo < 0) lo = 0;
  if (lo > n - SET_VIS) lo = n > SET_VIS ? n - SET_VIS : 0;
  s_setScroll = lo;

  for (int i = lo; i < n && i < lo + SET_VIS; ++i) {
    const SetRow& r = rows[i];
    const int y = TOP + (i - lo) * PITCH;
    const bool sel = (i == s_setSel);
    char id[16];
    snprintf(id, sizeof(id), "s.row.%d", i);
    if (r.act) hitAdd(id, 8, y, 298, 34);
    const bool down = hitIsPressed(id);
    if (down)
      Gfx::cardGrad(tft, 8, y, 298, 34, 7, lerp565(TH::CARD_HI, TH::ACCENT, 77),
                    TH::CARD_HI, TH::BG);
    else if (sel)
      Gfx::cardGrad(tft, 8, y, 298, 34, 7, TH::GOLD_DK, TH::CARD, TH::BG);
    else
      Gfx::cardRim(tft, 8, y, 298, 34, 7, TH::CARD, TH::LINE, TH::BG);
    if (sel || down) {
      const uint16_t rim = r.warn ? TH::AMBER : TH::ACCENT;
      tft.drawSmoothRoundRect(8, y, 7, 5, 298, 34, rim, TH::BG);
    }

    Txt::draw(18, y + 21, CN16, (sel || down) ? TH::TXT_HI : TH::TXT, r.k);
    if (r.bar) {
      Gfx::progress(tft, 150, y + 15, 100, 5, s_bright / 100.0f, TH::GOLD_DK,
                    TH::ACCENT, TH::AMBER, TH::BG);
      Txt::drawRight(296, y + 21, NUM14, r.c, r.v);
    } else if (r.v[0]) {
      char vc[40];
      Txt::clip(CN12, r.v, 150, vc, sizeof(vc));
      Txt::drawRight(r.arrow ? 282 : 296, y + 21, CN12, r.c, vc);
    }
    if (r.arrow) {
      const uint16_t ac = (sel || down) ? TH::TXT_MID : TH::TXT_LO;
      const uint16_t cbg = down ? TH::CARD_HI : (sel ? TH::CARD : TH::CARD);
      tft.drawWideLine(290, y + 13, 297, y + 17, 1.4f, ac, cbg);
      tft.drawWideLine(297, y + 17, 290, y + 21, 1.4f, ac, cbg);
    }
  }

  if (n > SET_VIS) {
    const int trackY = TOP, trackH = SET_VIS * PITCH - 4;
    tft.fillRect(310, trackY, 2, trackH, TH::LINE);
    const int thH = (trackH * SET_VIS / n) > 14 ? (trackH * SET_VIS / n) : 14;
    const int thY = trackY + (trackH - thH) * s_setScroll / (n - SET_VIS);
    tft.fillRoundRect(310, thY, 2, thH, 1, TH::ACCENT);
  }
  Scene::footer(tft, TH::BG);
  pageDots(4, true);
}

// ================================================================ 弹窗
void modalFrame(int x, int y, int w, int h, const char* title, uint16_t rim) {
  tft.fillScreen(TH::BG);
  hitAdd("x.dismiss", 0, 0, SCR_W, SCR_H);
  hitAdd("x.card", x, y, w, h);
  Gfx::glow(tft, x + w / 2, y + h / 2, (w > h ? w : h) / 2 + 10, rim, TH::BG);
  Gfx::cardGrad(tft, x, y, w, h, 9, TH::CARD_HI, TH::CARD, TH::BG);
  tft.drawSmoothRoundRect(x, y, 9, 7, w, h, rim, TH::BG);
  Txt::draw(x + 14, y + 20, CN16, TH::TXT_HI, title);
  Gfx::divider(tft, x + 12, y + 27, w - 24, TH::RIM, TH::CARD_HI);
}

// 信号强度柱(1-3 根)
void sigBars(int x, int y, int n) {
  for (int i = 0; i < 3; ++i) {
    const bool on = i < n;
    const int h = 3 + i * 3;
    tft.fillRect(x + i * 4, y + 9 - h, 3, h,
                 on ? (n == 3 ? TH::GREEN : n == 2 ? TH::AMBER : TH::TXT_LO)
                    : TH::LINE);
  }
}

int rssiToSig(int rssi) {
  if (rssi > -55) return 3;
  if (rssi > -70) return 2;
  return 1;
}

// ---------------------------------------------------------------- 城市列表
enum CityKind : uint8_t { CK_AUTO = 0, CK_HOT, CK_PROV, CK_CITY };

int cityListCount() {
  if (s_cityFilter[0]) return Cities::matchCount(s_cityFilter);
  if (s_cityProv >= 0) return Cities::provCount((uint8_t)s_cityProv);
  return 1 + Cities::HOT_N + Cities::PROV_N;
}

bool cityListNth(int n, CityKind& kind, int& payload, const char*& name,
                 const char*& sub) {
  if (n < 0) return false;
  if (s_cityFilter[0]) {
    const int idx = Cities::matchNth(s_cityFilter, n);
    if (idx < 0) return false;
    kind = CK_CITY;
    payload = idx;
    name = Cities::LIST[idx].name;
    sub = Cities::PROV_NAMES[Cities::LIST[idx].prov];
    return true;
  }
  if (s_cityProv >= 0) {
    const int idx = Cities::provNth((uint8_t)s_cityProv, n);
    if (idx < 0) return false;
    kind = CK_CITY;
    payload = idx;
    name = Cities::LIST[idx].name;
    sub = nullptr;
    return true;
  }
  if (n == 0) {
    kind = CK_AUTO;
    payload = 0;
    name = "自动定位";
    sub = Wx::isAuto() ? "按公网 IP · 已开" : "按公网 IP · 点此打开";
    return true;
  }
  n -= 1;
  if (n < Cities::HOT_N) {
    kind = CK_HOT;
    payload = (int)Cities::HOT_IDX[n];
    name = Cities::LIST[payload].name;
    sub = "热门";
    return true;
  }
  n -= Cities::HOT_N;
  if (n < Cities::PROV_N) {
    kind = CK_PROV;
    payload = n;
    name = Cities::PROV_NAMES[n];
    sub = "省份";
    return true;
  }
  return false;
}

void openCityModal() {
  s_modal = MOD_CITY;
  s_modalSel = 0;
  s_cityProv = -1;
  s_cityFilter[0] = 0;
  s_citySel = 0;
  s_dirty = true;
}

void pullWeatherForPlace() {
  if (!WifiMgr::isConnected()) {
    s_dirty = true;
    return;
  }
  if (Wx::fetch()) {
    s_dirty = true;
  } else {
    UI::toast("天气刷新失败,城市已改");
  }
}

void pinCityIdx(int idx) {
  if (idx < 0 || idx >= Cities::N) return;
  const Cities::Rec& c = Cities::LIST[idx];
  Wx::setPlace(c.name, c.lat, c.lon, true);
  s_modal = MOD_NONE;
  s_needClear = true;
  char msg[40];
  snprintf(msg, sizeof(msg), "已钉死 %s", c.name);
  UI::toast(msg);
  pullWeatherForPlace();
}

void goAutoLocate() {
  Wx::setPlace(Wx::place().name, Wx::place().lat, Wx::place().lon, false);
  s_modal = MOD_NONE;
  s_needClear = true;
  if (!WifiMgr::isConnected()) {
    UI::toast("已改回自动 · 连上网后再定位");
    s_dirty = true;
    return;
  }
  if (Wx::locate()) {
    char msg[40];
    snprintf(msg, sizeof(msg), "定位到 %s", Wx::place().name);
    UI::toast(msg);
    pullWeatherForPlace();
  } else {
    UI::toast("定位失败,保持原城市");
    s_dirty = true;
  }
}

void drawModal() {
  if (s_modal == MOD_WIFI) {
    s_modalBtnN = 2;
    modalFrame(14, 20, 292, 200, "选择网络 · 点击连接", TH::ACCENT);
    WiFiAP aps[6];
    const int n = WifiMgr::getScanResults(aps, 6);
    if (n <= 0) {
      if (WifiMgr::getScanState() == WIFI_MGR_SCAN_RUNNING)
        Txt::drawCenter(160, 110, CN16, TH::TXT_MID, "正在扫描…");
      else
        Txt::drawCenter(160, 110, CN16, TH::TXT_MID, "没扫到网络");
    }
    for (int i = 0; i < n; ++i) {
      const int y = 36 + i * 24;
      char id[16];
      snprintf(id, sizeof(id), "w.net.%d", i);
      hitAdd(id, 22, y, 276, 22);
      const bool down = hitIsPressed(id);
      if (down)
        Gfx::cardGrad(tft, 22, y, 276, 22, 5,
                      lerp565(TH::CARD_HI, TH::ACCENT, 77), TH::CARD_HI, TH::BG);
      else
        Gfx::cardRim(tft, 22, y, 276, 22, 5, TH::CARD, TH::LINE, TH::BG);
      const WiFiConfig* cfg = WifiMgr::getConfig();
      if (cfg->saved && strcmp(cfg->ssid, aps[i].ssid) == 0)
        tft.fillSmoothCircle(28, y + 11, 2, TH::GREEN, TH::CARD);
      char ss[28];
      Txt::clip(CN12, aps[i].ssid, 180, ss, sizeof(ss));
      Txt::draw(36, y + 16, CN12, TH::TXT, ss);
      if (aps[i].secured)
        Gfx::glyph(tft, GL_LOCK, 238, y + 11, 13, TH::TXT_MID, TH::CARD);
      sigBars(258, y + 4, rssiToSig(aps[i].rssi));
    }
    textBtn("w.refresh", 30, 186, 126, 30, "重新扫描", TH::ACCENT, 0);
    textBtn("w.cancel", 164, 186, 126, 30, "取消", TH::TXT_MID, 1);
  } else if (s_modal == MOD_CONNECTING) {
    s_modalBtnN = 0;
    modalFrame(60, 66, 200, 108, "正在连接", TH::ACCENT);
    // 转圈:底环 + 旋转亮弧
    tft.drawSmoothArc(160, 108, 16, 11, 0, 360, TH::GOLD_DK, TH::BG, true);
    const float a = (float)(millis() % 1400) / 1400.0f * 360.0f;
    tft.drawSmoothArc(160, 108, 16, 11, a, a + 130.0f, TH::ACCENT, TH::BG, true);
    Txt::drawCenter(160, 138, CN16, TH::TXT, s_connSsid);
    Txt::drawCenter(160, 154, CN12, TH::TXT_MID, "连上后自动定位并拉天气");
  } else if (s_modal == MOD_BT) {
    modalFrame(18, 34, 284, 172, "蓝牙", TH::ACCENT);
    const bool on = g_media.connected || g_media.advOn;
    Gfx::glyph(tft, GL_BLUETOOTH, 43, 87, 26, on ? TH::ACCENT : TH::TXT_LO,
               TH::CARD);
    Txt::draw(66, 82, CN16, TH::TXT_HI, "Pika-Music");
    char st[48];
    uint16_t stC;
    if (g_media.connected) {
      snprintf(st, sizeof(st), "已连接 %s", g_media.peer);
      stC = TH::GREEN;
    } else if (g_media.advOn) {
      snprintf(st, sizeof(st), "广播中·等连接");
      stC = TH::ACCENT;
    } else {
      snprintf(st, sizeof(st), "已关闭");
      stC = TH::TXT_LO;
    }
    Txt::draw(66, 98, CN12, stC, st);
    Txt::draw(30, 124, CN12, TH::TXT_MID, "这是 BLE 连接,不用在系统蓝牙里配对。");
    Txt::draw(30, 140, CN12, TH::TXT_MID, "打开配套 App,搜到名字点连接即可。");
    if (g_media.connected) {
      s_modalBtnN = 2;
      textBtn("x.bt.disc", 30, 162, 126, 30, "断开", TH::ACCENT, 0);
      textBtn("x.bt.readv", 164, 162, 126, 30, "重新广播", TH::AMBER, 1);
    } else if (g_media.advOn) {
      s_modalBtnN = 2;
      textBtn("x.bt.off", 30, 162, 126, 30, "关闭蓝牙", TH::AMBER, 0);
      textBtn("x.bt.readv", 164, 162, 126, 30, "重新广播", TH::ACCENT, 1);
    } else {
      s_modalBtnN = 1;
      textBtn("x.bt.on", 96, 162, 128, 30, "开启蓝牙", TH::GREEN, 0);
    }
  } else if (s_modal == MOD_RESET) {
    s_modalBtnN = 2;
    modalFrame(26, 56, 268, 128, "恢复默认设置", TH::AMBER);
    Txt::draw(40, 104, CN12, TH::TXT, "会清掉:亮度、样式、WiFi、城市定位。");
    Txt::draw(40, 122, CN12, TH::TXT_MID, "清完回到时钟页。");
    textBtn("x.reset.go", 40, 138, 110, 30, "恢复默认", TH::RED, 0);
    textBtn("x.reset.no", 170, 138, 110, 30, "取消", TH::TXT_MID, 1);
  } else if (s_modal == MOD_REBOOT) {
    s_modalBtnN = 2;
    modalFrame(26, 56, 268, 128, "重启设备", TH::AMBER);
    Txt::draw(40, 112, CN12, TH::TXT, "设置会保留,大约 3 秒后回到时钟页。");
    textBtn("x.reboot.go", 40, 138, 110, 30, "重启", TH::RED, 0);
    textBtn("x.reboot.no", 170, 138, 110, 30, "取消", TH::TXT_MID, 1);
  } else if (s_modal == MOD_CITY) {
    s_modalBtnN = 0;   // 上下滑滚列表,不走按钮焦点
    const char* title = "选择城市";
    if (s_cityFilter[0]) title = "搜索城市";
    else if (s_cityProv >= 0) title = Cities::PROV_NAMES[s_cityProv];
    modalFrame(14, 20, 292, 200, title, TH::ACCENT);

    const int n = cityListCount();
    const int VIS = 6;
    int lo = s_citySel - VIS + 1;
    if (lo < 0) lo = 0;
    if (lo > n - VIS) lo = n > VIS ? n - VIS : 0;
    if (n <= 0) {
      Txt::drawCenter(160, 110, CN16, TH::TXT_MID, "没有匹配");
    }
    for (int i = lo; i < n && i < lo + VIS; ++i) {
      CityKind kind;
      int payload = 0;
      const char* name = "";
      const char* sub = nullptr;
      if (!cityListNth(i, kind, payload, name, sub)) break;
      const int y = 36 + (i - lo) * 24;
      char id[16];
      snprintf(id, sizeof(id), "c.it.%d", i);
      hitAdd(id, 22, y, 276, 22);
      const bool down = hitIsPressed(id);
      const bool sel = (i == s_citySel);
      if (down)
        Gfx::cardGrad(tft, 22, y, 276, 22, 5,
                      lerp565(TH::CARD_HI, TH::ACCENT, 77), TH::CARD_HI, TH::BG);
      else if (sel)
        Gfx::cardGrad(tft, 22, y, 276, 22, 5, TH::GOLD_DK, TH::CARD, TH::BG);
      else
        Gfx::cardRim(tft, 22, y, 276, 22, 5, TH::CARD, TH::LINE, TH::BG);
      const bool here =
          (kind == CK_CITY || kind == CK_HOT) &&
          strcmp(name, Wx::place().name) == 0;
      if (kind == CK_AUTO && Wx::isAuto())
        tft.fillSmoothCircle(28, y + 11, 2, TH::GREEN, TH::CARD);
      else if (here)
        tft.fillSmoothCircle(28, y + 11, 2, TH::GREEN, TH::CARD);
      const int nameX = (here || (kind == CK_AUTO && Wx::isAuto())) ? 36 : 30;
      char nc[28];
      Txt::clip(CN12, name, 150, nc, sizeof(nc));
      Txt::draw(nameX, y + 16, CN12, TH::TXT, nc);
      if (sub && sub[0]) {
        char sc[16];
        Txt::clip(CN12, sub, 80, sc, sizeof(sc));
        Txt::drawRight(290, y + 16, CN12, TH::TXT_LO, sc);
      } else if (kind == CK_PROV) {
        Txt::drawRight(290, y + 16, CN12, TH::TXT_LO, ">");
      }
    }
    textBtn("c.search", 30, 186, 126, 30, "拼音搜索", TH::ACCENT, -1);
    if (s_cityFilter[0] || s_cityProv >= 0)
      textBtn("c.back", 164, 186, 126, 30, "返回", TH::TXT_MID, -1);
    else
      textBtn("c.cancel", 164, 186, 126, 30, "取消", TH::TXT_MID, -1);
  }
}

// ================================================================ toast
void drawToast() {
  if (s_toastT <= 0.0f || !s_toast[0]) return;
  char clipT[40];
  Txt::clip(CN12, s_toast, GFX_BIG_W - 26, clipT, sizeof(clipT));
  int w = Txt::width(CN12, clipT) + 26;
  if (w < 80) w = 80;
  const int x = (SCR_W - w) / 2, y = s_page == PG_MUSIC ? 200 : 196;
  Gfx::beginBig(TH::BG);
  Gfx::cardGrad(Gfx::big(), 0, 0, w, 24, 12, TH::RED_DK, TH::CARD, TH::BG);
  // 红描边(同预览 cardGrad 的第三个色参),压在草地上也看得清边界
  Gfx::big().drawSmoothRoundRect(0, 0, 12, 10, w, 24, TH::RED, TH::BG);
  Txt::drawCenter(w / 2, 16, CN12, TH::TXT_HI, clipT);
  Gfx::pushBigRegion(x, y, w, 24);
}

// ================================================================ 偏好持久化
void loadPrefs() {
  Preferences p;
  if (p.begin(NVS_UI_NS, true)) {
    s_bright = p.getUChar("br", 72);
    s_specStyle = p.getUChar("sp", 0);
    s_wxStyle = p.getUChar("wv", 0);
    p.end();
  }
  if (s_bright < 10) s_bright = 10;
  if (s_bright > 100) s_bright = 100;
  if (s_specStyle > 2) s_specStyle = 0;
  if (s_wxStyle > 1) s_wxStyle = 0;
}

void savePrefs() {
  Preferences p;
  if (p.begin(NVS_UI_NS, false)) {
    p.putUChar("br", s_bright);
    p.putUChar("sp", s_specStyle);
    p.putUChar("wv", s_wxStyle);
    p.end();
  }
}

// ================================================================ 渲染调度
// contentChanged:内容真变了(切页/数据更新/按下态变化)。
// 静态三页只在这时重绘主体+静止页脚;时钟/音乐页每帧有自己的动画层。
void render(float dt, bool contentChanged) {
  if (Keyboard::isActive()) {
    hitClear();
    Keyboard::render();
    drawToast();
    return;
  }
  if (s_modal != MOD_NONE) {
    hitClear();
    drawModal();
    drawToast();
    return;
  }
  // 清屏策略:
  //   时钟页 —— 场景自己覆盖整屏,不清
  //   音乐页 —— 圆盘+音波每帧推 disc;主体/页脚只在内容变化时重绘
  //   其余三页 —— 主体+静止页脚只在内容变化时重绘
  const bool resetClockScene = s_page == PG_CLOCK && s_needClear;
  const bool resetMusic = s_page == PG_MUSIC && s_needClear;
  const bool statics = (s_page != PG_CLOCK && s_page != PG_MUSIC);
  const bool redrawBody = (s_page == PG_CLOCK) || s_needClear || contentChanged;
  // 热区由页面绘制时注册,所以只在真要重绘主体时才清 ——
  // 静态页按需重绘,热区随主体一起重建。
  if (redrawBody || s_page == PG_MUSIC) hitClear();
  if (statics && redrawBody) {
    // 主体重绘必须先清底,否则新文字会叠在旧文字上(页脚随后重画,不受影响)
    tft.fillScreen(TH::BG);
  }
  if (resetClockScene) {
    Scene::invalidateCache();
    s_clockHudDirty = true;
  }
  s_needClear = false;
  switch (s_page) {
    case PG_CLOCK: pageClock(dt); break;
    case PG_MUSIC: pageMusic(dt, resetMusic); break;
    case PG_WEATHER: if (redrawBody) pageWeather(dt); break;
    case PG_DATE: if (redrawBody) pageDate(dt); break;
    case PG_SETTINGS: if (redrawBody) pageSettings(dt); break;
    default: break;
  }
  drawToast();
}

// ---------------------------------------------------------------- 连接流程
void startConnect(const char* ssid, const char* pass) {
  strlcpy(s_connSsid, ssid, sizeof(s_connSsid));
  WifiMgr::saveConfig(ssid, pass);
  WifiMgr::connect(ssid, pass);
  s_connStartMs = millis();
  s_modal = MOD_CONNECTING;
  s_dirty = true;
}

// ---------------------------------------------------------------- 频谱
void tickSpec(float dt, const float* target) {
  for (int i = 0; i < 16; ++i) {
    const float tv = target[i];
    const float k = (tv > s_spec[i]) ? 33.0f : 9.6f;   // 快涨慢落
    const float f = k * dt;
    s_spec[i] += (tv - s_spec[i]) * (f < 1.0f ? f : 1.0f);
    const float pk = s_peak[i] - 107.0f * dt;          // 峰值坠落
    s_peak[i] = s_spec[i] > pk ? s_spec[i] : pk;
  }
}

void tickSpecDemo(float dt) {
  s_specPhase += dt;
  float target[16];
  for (int i = 0; i < 16; ++i) {
    float v = 0.0f;
    if (g_media.playing) {
      const float lowBias = powf(1.0f - i / 16.0f, 0.75f);
      v = lowBias * (0.45f + 0.55f * fabsf(
          sinf(s_specPhase * 2.4f + i * 0.7f) *
              sinf(s_specPhase * 1.1f + i * 0.31f) +
          sinf(s_specPhase * 5.3f + i) * 0.25f));
      if (v < 0.0f) v = 0.0f;
      if (v > 1.0f) v = 1.0f;
    }
    target[i] = v * 255.0f;
  }
  tickSpec(dt, target);
}

void tickSpecBle(float dt) {
  s_specPhase += dt;   // 封面圆盘自转也用它,BLE 模式同样要走
  float target[16];
  for (int i = 0; i < 16; ++i) target[i] = (float)g_media.spec[i];
  tickSpec(dt, target);
}

}  // namespace

// =====================================================================
//  公开接口
// =====================================================================
namespace UI {

void init() {
  loadPrefs();
  Keyboard::init();
#ifdef DEMO_MODE
  g_media.advOn = true;
  g_media.connected = true;
  strlcpy(g_media.peer, "演示模式", sizeof(g_media.peer));
  strlcpy(g_media.title, DEMO_TITLE, sizeof(g_media.title));
  strlcpy(g_media.artist, DEMO_ARTIST, sizeof(g_media.artist));
  strlcpy(g_media.lyric, DEMO_LRC[1], sizeof(g_media.lyric));
  g_media.pos = 78;
  g_media.dur = DEMO_DUR;
  g_media.playing = true;
  g_media.lastPlayMs = millis();
#endif
}

uint8_t page() { return s_page; }

void setPage(uint8_t p) {
  if (p >= PG_N || p == s_page) return;
  s_page = p;
  Scene::invalidateCache();
  if (p == PG_MUSIC) s_musicReset = true;
  s_needClear = true;
  s_dirty = true;
  s_clockHudDirty = true;
}

uint8_t bright() { return s_bright; }
uint8_t specStyle() { return s_specStyle; }
uint8_t wxStyle() { return s_wxStyle; }
void markDirty() { s_dirty = true; s_clockHudDirty = true; }

void toast(const char* msg) {
  strlcpy(s_toast, msg, sizeof(s_toast));
  s_toastT = 1.4f;
  s_dirty = true;
}

void notifyWifiResult(bool ok) {
  if (s_modal != MOD_CONNECTING) return;
  s_modal = MOD_NONE;
  s_needClear = true;
  if (ok) toast("已连接,正在定位并拉天气");
  else toast("连接失败,请重试");
  s_dirty = true;
}

void notifyBtChanged() { s_dirty = true; }

// ---------------------------------------------------------------- 主服务
void service(float dt) {
  // service 每个主循环都跑，场景只在绘制帧推进；积累两次绘制之间的时间。
  static float sceneDt = 0.0f;
  if (s_page == PG_CLOCK && s_modal == MOD_NONE && !Keyboard::isActive())
    sceneDt = fminf(sceneDt + dt, 0.25f);
  else
    sceneDt = 0.0f;
  // ---- 演示数据 / BLE 数据推进 ----
#ifdef DEMO_MODE
  if (g_media.playing) {
    s_demoAcc += dt;
    while (s_demoAcc >= 1.0f) {
      s_demoAcc -= 1.0f;
      if (++s_demoPos >= DEMO_DUR) s_demoPos = 0;
    }
    g_media.pos = s_demoPos;
    // 歌词行切了 → 重绘右侧文字区(音乐页主体按需重绘)
    const int dlr = (s_demoPos / 6) % 5;
    if (dlr != s_demoLrcRow) {
      s_demoLrcRow = (int8_t)dlr;
      if (s_page == PG_MUSIC) s_dirty = true;
    }
  }
  tickSpecDemo(dt);
#else
  // BLE 心跳超时 → 视为暂停(手机端死了)
  if (g_media.connected && g_media.playing &&
      millis() - g_media.lastPlayMs > BLE_HEARTBEAT_MS + 1500) {
    g_media.playing = false;
    s_dirty = true;
  }
  // POS 消息不来的间隙本地推秒
  if (g_media.connected && g_media.playing && g_media.pos >= 0 &&
      g_media.dur > 0) {
    g_media.pos += (int)(dt + 0.5f);
    if (g_media.pos > g_media.dur) g_media.pos = g_media.dur;
  }
  if (g_media.connected) tickSpecBle(dt);
#endif

  // ---- toast 计时(到点触发一次重绘把提示带覆盖掉) ----
  // 不在这里 fillRect:toast 压在共用页脚上沿,直接刷底色会挖掉草地。
  // 标脏让整页正常重绘一遍,自然把它盖掉。
  if (s_toastT > 0.0f) {
    s_toastT -= dt;
    if (s_toastT <= 0.0f) {
      s_toastT = 0.0f;
      if (s_page == PG_CLOCK) Scene::restoreGroundOverlay();
      s_dirty = true;
    }
  }

  // ---- WiFi 扫描完成 → 刷新列表 ----
  if (s_modal == MOD_WIFI) {
    const bool done = WifiMgr::isScanDone();
    if (done && !s_scanWasDone) s_dirty = true;
    s_scanWasDone = done;
  } else {
    s_scanWasDone = false;
  }

  // ---- 连接中弹窗:等结果 / 超时 ----
  if (s_modal == MOD_CONNECTING) {
    if (WifiMgr::isConnected()) {
      notifyWifiResult(true);
    } else if (millis() - s_connStartMs > 16000) {
      notifyWifiResult(false);
    } else {
      s_dirty = true;   // 转圈动画
    }
  }

  // ---- 自动跳页:放歌 → 音乐页;闲置 → 时钟页 ----
  if (s_manualT > 0.0f) s_manualT -= dt;
  const bool live = g_media.connected && g_media.playing;
  s_idleT = live ? 0.0f : s_idleT + dt;
  if (s_manualT <= 0.0f && s_modal == MOD_NONE && !Keyboard::isActive()) {
    const uint8_t want =
        live ? PG_MUSIC
             : (s_idleT >= MUSIC_IDLE_MS / 1000.0f ? PG_CLOCK : s_page);
    if (want != s_page) setPage(want);
  }

  // ---- 分钟变化:静态页刷一次(日期的今天、天气的分钟角标) ----
  const uint16_t mk = minuteKey();
  if (mk != s_lastMinute) {
    s_lastMinute = mk;
    if (s_page == PG_DATE || s_page == PG_WEATHER) s_dirty = true;
  }

  // ---- 帧率调度 ----
  // 时钟页场景动画 CLOCK_FRAME_MS;音乐页频谱 MUSIC_FRAME_MS;
  // 其余三页页脚静止,按需重绘。
  bool anim = false;
  uint32_t period = CLOCK_FRAME_MS;
  if (Keyboard::isActive()) {
    anim = true;             // 键盘光标闪烁
    period = 250;
  } else if (s_modal != MOD_NONE) {
    anim = (s_modal == MOD_CONNECTING);   // 只有转圈在动
  } else if (s_page == PG_CLOCK) {
    anim = true;
  } else if (s_page == PG_MUSIC) {
    anim = true;
    period = MUSIC_FRAME_MS;
  } else {
    anim = false;
    period = IDLE_FRAME_MS;
  }
  const uint32_t now = millis();
  const bool wasDirty = s_dirty;
  if ((anim && now - s_lastRenderMs >= period) || wasDirty) {
    s_lastRenderMs = now;
    s_dirty = false;
    // wasDirty = 内容真变了(切页/数据到/按下态变) → 静态页连主体+页脚一起重绘。
    const float renderDt = s_page == PG_CLOCK ? sceneDt : dt;
    render(anim ? renderDt : 0.0f, wasDirty);
    sceneDt = 0.0f;
  }
}

// ---------------------------------------------------------------- 手势
void handleGesture(Gesture g) {
  if (Keyboard::isActive()) return;
  if (g == GESTURE_SWIPE_LEFT || g == GESTURE_SWIPE_RIGHT) {
    if (s_modal != MOD_NONE) return;
    s_manualT = MANUAL_HOLD_MS / 1000.0f;
    const int dir = (g == GESTURE_SWIPE_LEFT) ? 1 : -1;   // 左滑 = 下一页
    setPage((uint8_t)((s_page + dir + PG_N) % PG_N));
    return;
  }
  if (g != GESTURE_SWIPE_UP && g != GESTURE_SWIPE_DOWN) return;
  const int dir = (g == GESTURE_SWIPE_UP) ? 1 : -1;
  s_manualT = MANUAL_HOLD_MS / 1000.0f;

  if (s_modal != MOD_NONE) {
    if (s_modal == MOD_CITY) {
      const int n = cityListCount();
      if (n > 0)
        s_citySel = (int16_t)((s_citySel + dir + n) % n);
      s_dirty = true;
      return;
    }
    // 弹窗里:上下移动按钮焦点
    if (s_modalBtnN > 1)
      s_modalSel = (int8_t)((s_modalSel + dir + s_modalBtnN) % s_modalBtnN);
    s_dirty = true;
    return;
  }
  switch (s_page) {
    case PG_DATE: {
      s_monthOff += dir;
      if (s_monthOff > 12 || s_monthOff < -12) s_monthOff = 0;
      if (s_monthOff == 0) toast("本月");
      else {
        char msg[20];
        snprintf(msg, sizeof(msg), "%s%d 个月", s_monthOff > 0 ? "+" : "",
                 s_monthOff);
        toast(msg);
      }
      s_dirty = true;
      break;
    }
    case PG_SETTINGS: {
      SetRow rows[SET_ROW_MAX];
      int n = 0;
      buildSettingRows(rows, n);
      s_setSel = (int8_t)((s_setSel + dir + n) % n);
      s_dirty = true;
      break;
    }
    case PG_MUSIC: {
      s_specStyle = (uint8_t)((s_specStyle + 3 + (dir > 0 ? 1 : -1)) % 3);
      savePrefs();
      static const char* SPEC_NAMES[3] = {"放射条", "环形波纹", "光点绕圈"};
      toast(SPEC_NAMES[s_specStyle]);
      break;
    }
    case PG_WEATHER: {
      s_wxStyle = (uint8_t)((s_wxStyle + 2 + (dir > 0 ? 1 : -1)) % 2);
      savePrefs();
      toast(s_wxStyle ? "整页七日" : "概览+七日");
      break;
    }
    default:
      toast("时钟页 ↑↓ 没有别的操作");
      break;
  }
}

// ---------------------------------------------------------------- 点击
static void doAct(const char* id, int x, int y);

void handleTap(const char* id, int x, int y) {
  // 密码键盘开着:点击全归键盘
  if (Keyboard::isActive()) {
    const KeyboardResult r = Keyboard::handleTouch(x, y);
    if (r == KB_RESULT_CONFIRM) {
      const char* in = Keyboard::getInput();
      if (s_kbWhy == 1) {
        Keyboard::endInput();
        s_needClear = true;
        if (in && in[0]) strlcpy(s_cityFilter, in, sizeof(s_cityFilter));
        else s_cityFilter[0] = 0;
        s_cityProv = -1;
        s_citySel = 0;
        s_modal = MOD_CITY;
        s_dirty = true;
        return;
      }
      if (!in[0]) {
        toast("密码没输入");
        s_dirty = true;
        return;
      }
      Keyboard::endInput();
      s_needClear = true;
      startConnect(s_connSsid, in);
    } else if (r == KB_RESULT_CANCEL) {
      Keyboard::endInput();
      s_needClear = true;
      if (s_kbWhy == 1) s_modal = MOD_CITY;
      s_dirty = true;
    } else {
      s_dirty = true;   // 按了一个字符,重画输入行
    }
    return;
  }
  if (!id || !id[0]) return;
  s_manualT = MANUAL_HOLD_MS / 1000.0f;
  doAct(id, x, y);
}

// ---- 动作表(热区 id → 行为) ----
static void doAct(const char* id, int x, int y) {
  // ---- 时钟页 ----
  if (strcmp(id, "c.chip") == 0) { setPage(PG_WEATHER); return; }
  if (strcmp(id, "c.net") == 0) { setPage(PG_SETTINGS); return; }
  if (strcmp(id, "c.pika") == 0) { Pika::tap(); return; }

  // ---- 音乐页 ----
  if (strcmp(id, "m.play") == 0) {
    const bool nextPlaying = !g_media.playing;
#ifndef DEMO_MODE
    // Android 的 play/pause 是明确动作，play 并不是播放/暂停切换。
    if (!bleSendBtn(nextPlaying ? "play" : "pause")) {
      toast("控制未发送，请重连蓝牙");
      return;
    }
#endif
    g_media.playing = nextPlaying;
    g_media.lastPlayMs = millis();
    toast(g_media.playing ? "播放" : "暂停");
    return;
  }
  if (strcmp(id, "m.prev") == 0) {
    g_media.pos = 0;
    toast("上一曲");
#ifndef DEMO_MODE
    if (g_media.connected) bleSendBtn("prev");
#endif
    return;
  }
  if (strcmp(id, "m.next") == 0) {
    g_media.pos = 0;
    toast("下一曲");
#ifndef DEMO_MODE
    if (g_media.connected) bleSendBtn("next");
#endif
    return;
  }
  if (strcmp(id, "m.btpanel") == 0) {
    s_modal = MOD_BT;
    s_modalSel = 0;
    s_dirty = true;
    return;
  }

  // ---- 设置行 ----
  if (strncmp(id, "s.row.", 6) == 0) {
    const int idx = atoi(id + 6);
    s_setSel = (int8_t)idx;
    SetRow rows[SET_ROW_MAX];
    int n = 0;
    buildSettingRows(rows, n);
    if (idx >= 0 && idx < n && rows[idx].act) doAct(rows[idx].act, x, y);
    return;
  }
  if (strcmp(id, "wifi") == 0) {
    s_modal = MOD_WIFI;
    s_modalSel = 0;
    s_scanWasDone = false;
    WifiMgr::startScan();
    s_dirty = true;
    return;
  }
  if (strcmp(id, "city") == 0) {
    openCityModal();
    return;
  }
  if (strcmp(id, "bt") == 0) {
    s_modal = MOD_BT;
    s_modalSel = 0;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "bright") == 0) {
    s_bright = (uint8_t)(s_bright + 15);
    if (s_bright > 100) s_bright = 10;
    savePrefs();
    s_dirty = true;
    return;
  }
  if (strcmp(id, "spec") == 0) {
    s_specStyle = (uint8_t)((s_specStyle + 1) % 3);
    savePrefs();
    static const char* SPEC_NAMES[3] = {"放射条", "环形波纹", "光点绕圈"};
    toast(SPEC_NAMES[s_specStyle]);
    return;
  }
  if (strcmp(id, "wxview") == 0) {
    s_wxStyle = (uint8_t)((s_wxStyle + 1) % 2);
    savePrefs();
    toast(s_wxStyle ? "整页七日" : "概览+七日");
    return;
  }
  if (strcmp(id, "reset") == 0) {
    s_modal = MOD_RESET;
    s_modalSel = 0;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "reboot") == 0) {
    s_modal = MOD_REBOOT;
    s_modalSel = 0;
    s_dirty = true;
    return;
  }

  // ---- WiFi 弹窗 ----
  if (strncmp(id, "w.net.", 6) == 0) {
    WiFiAP aps[6];
    const int n = WifiMgr::getScanResults(aps, 6);
    const int idx = atoi(id + 6);
    if (idx >= 0 && idx < n) {
      if (aps[idx].secured) {
        strlcpy(s_connSsid, aps[idx].ssid, sizeof(s_connSsid));
        s_kbWhy = 0;
        Keyboard::startInput("输入密码 · 点确定连接", "");
        s_dirty = true;
      } else {
        startConnect(aps[idx].ssid, "");
      }
    }
    return;
  }
  if (strcmp(id, "w.refresh") == 0) {
    s_scanWasDone = false;
    WifiMgr::startScan();
    toast("正在扫描…");
    s_dirty = true;
    return;
  }
  if (strcmp(id, "w.cancel") == 0) {
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }

  // ---- 城市弹窗 ----
  if (strncmp(id, "c.it.", 5) == 0) {
    const int idx = atoi(id + 5);
    s_citySel = (int16_t)idx;
    CityKind kind;
    int payload = 0;
    const char* name = "";
    const char* sub = nullptr;
    if (!cityListNth(idx, kind, payload, name, sub)) return;
    if (kind == CK_AUTO) {
      goAutoLocate();
      return;
    }
    if (kind == CK_PROV) {
      s_cityProv = (int8_t)payload;
      s_cityFilter[0] = 0;
      s_citySel = 0;
      s_dirty = true;
      return;
    }
    pinCityIdx(payload);
    return;
  }
  if (strcmp(id, "c.search") == 0) {
    s_kbWhy = 1;
    Keyboard::startInput("城市拼音 · 如 sz / suzhou", s_cityFilter);
    s_dirty = true;
    return;
  }
  if (strcmp(id, "c.back") == 0) {
    if (s_cityFilter[0]) s_cityFilter[0] = 0;
    else s_cityProv = -1;
    s_citySel = 0;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "c.cancel") == 0) {
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }

  // ---- 蓝牙弹窗 ----
  if (strcmp(id, "x.bt.disc") == 0) {
    bleDisconnectPeer();
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "x.bt.readv") == 0) {
    bleRestartAdv();
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "x.bt.off") == 0) {
    bleSetAdv(false);
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "x.bt.on") == 0) {
    bleSetAdv(true);
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }

  // ---- 确认弹窗 ----
  if (strcmp(id, "x.reset.go") == 0) {
    s_modal = MOD_NONE;
    resetPrefs();
    WifiMgr::clearConfig();
    Wx::reset();
    {  // 触摸校准也一并清掉:重启后自动进五点校准(重新对准的唯一入口)
      Preferences p;
      if (p.begin(NVS_TOUCH_NS, false)) {
        p.clear();
        p.end();
      }
    }
    toast("已恢复默认,重启中…");
    render(0.0f, true);   // 先把 toast 画出来再走
    delay(600);
    ESP.restart();
    return;
  }
  if (strcmp(id, "x.reset.no") == 0 || strcmp(id, "x.reboot.no") == 0) {
    s_modal = MOD_NONE;
    s_needClear = true;
    s_dirty = true;
    return;
  }
  if (strcmp(id, "x.reboot.go") == 0) {
    toast("重启中…");
    delay(400);
    ESP.restart();
    return;
  }
  // 点到弹窗卡片空白/背景:WiFi 列表和连接中不误关,其他点外关闭
  if (strcmp(id, "x.dismiss") == 0) {
    if (s_modal != MOD_WIFI && s_modal != MOD_CONNECTING &&
        s_modal != MOD_CITY && s_modal != MOD_NONE) {
      s_modal = MOD_NONE;
      s_needClear = true;
      s_dirty = true;
    }
    return;
  }
}

void resetPrefs() {
  s_bright = 72;
  s_specStyle = 0;
  s_wxStyle = 0;
  s_setSel = 0;
  s_monthOff = 0;
  Preferences p;
  if (p.begin(NVS_UI_NS, false)) {
    p.clear();
    p.end();
  }
  savePrefs();
}

}  // namespace UI
