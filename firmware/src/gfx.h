#pragma once
/**
 * =====================================================================
 *  gfx.h —— 绘图底座:共享 Sprite + 抗锯齿卡片 / 渐变 / 手绘大号数字
 * =====================================================================
 *  (自参考工程 E:\xiangmu\开发板 移植,Sprite 策略与抗锯齿封装原样保留)
 *
 *  1) Sprite 策略 —— 三块固定条带,开机一次性分配(WiFi/BLE 起来之前堆
 *     还是整块的),全程复用,共 53KB:
 *       big   240×68   大号时间数字 / 弹窗
 *       strip 320×28   标题 / 歌词 / 状态行 + 跑马灯
 *       bar    20×64   频谱单根柱子
 *     时钟页的天空场景不整屏开 Sprite(150KB 开不起),按 2px 横带直接画屏。
 *
 *  2) 抗锯齿图元强制带 bg 参数(TFT_eSPI 2.5.x 的约定,传错会出脏边)。
 *
 *  3) 天气图标:12 组场景 + 未知,与 preview/ui.html 的 wmoScene 一一对应。
 */

#include <TFT_eSPI.h>
#include <stdint.h>

#include "config.h"

// ---------------------------------------------------------------------
// Sprite 尺寸
// ---------------------------------------------------------------------
constexpr int GFX_BIG_W = 240;
constexpr int GFX_BIG_H = 68;
constexpr int GFX_STRIP_W = SCR_W;   // 320,整屏宽,跑马灯直接靠边界裁剪
constexpr int GFX_STRIP_H = 28;
constexpr int GFX_BAR_W = 20;       // 18px 柱 + 2px 柱间距
constexpr int GFX_BAR_H = 64;       // 频谱最大高度;每柱整块覆盖,缩短无残影

/**
 * 天气图标类型 = 场景组的图标化。wx 模块把 WMO code 映射到场景组,
 * 场景组同时驱动时钟页背景和这里的图标,一套映射两处用。
 */
enum WxIcon : uint8_t {
  WX_CLEAR = 0,   // 晴
  WX_MOSTLY,      // 基本晴(太阳为主 + 小云)
  WX_PARTLY,      // 少云(云为主 + 露太阳)
  WX_OVERCAST,    // 阴
  WX_FOG,         // 雾
  WX_DRIZZLE,     // 毛毛雨
  WX_RAIN,        // 雨
  WX_HEAVYRAIN,   // 大雨
  WX_FREEZING,    // 冻雨
  WX_SNOW,        // 雪
  WX_HEAVYSNOW,   // 大雪
  WX_THUNDER,     // 雷暴
  WX_UNKNOWN
};

/** 播放器 / 状态图标 */
enum Glyph : uint8_t {
  GL_PLAY = 0,
  GL_PAUSE,
  GL_PREV,
  GL_NEXT,
  GL_BLUETOOTH,
  GL_WIFI,
  GL_LOCK,        // WiFi 列表里的锁(加密网络)
  GL_NOTE,        // 音符
};

namespace Gfx {

// ---------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------

/**
 * 分配三块 Sprite。必须在 WiFi / NimBLE 起来之前调用 ——
 * 那时候堆还是整块的,大块分配不会失败。
 * @return 全部分配成功返回 true
 */
bool init(TFT_eSPI& screen);

/** 屏幕对象(给需要直接画屏的地方用) */
TFT_eSPI& screen();

// ---------------------------------------------------------------------
// Sprite 取用。约定:begin* 清底并把 Txt 目标切过去,push* 送屏。
// ---------------------------------------------------------------------

TFT_eSprite& big();
TFT_eSprite& strip();
TFT_eSprite& bar();

/** 清成 bg 并把文字目标切到这块 Sprite 上 */
void beginBig(uint16_t bg);
void beginStrip(uint16_t bg);
void beginBar(uint16_t bg);

/** 送屏。push 完会把文字目标切回屏幕,避免忘了切导致画错地方。 */
void pushBig(int x, int y);
void pushStrip(int x, int y);
void pushBar(int x, int y);

/** 以 transparentColor 为透明色送屏(在场景/照片上叠文字用) */
void pushBigTransparent(int x, int y, uint16_t transparentColor);
void pushStripTransparent(int x, int y, uint16_t transparentColor);

/** 只送 strip 的前 h 行(只画了一行字时不必推满 28) */
void pushStripH(int x, int y, int h);

/**
 * 只送 big 的一个子区域(左上 srcX,srcY 起 w×h)。big 的宽是 240,
 * 子区域跨行有跨距,所以逐行 pushImage。用于时钟页:时间数字画进 big,
 * 只把数字实际占的矩形推上屏,不把整块 240×68 的底色糊到天空上。
 */
void pushBigRegion(int x, int y, int w, int h, int srcX = 0, int srcY = 0);

// ---------------------------------------------------------------------
// 卡片 / 分隔 / 指示器 —— 都可以画在屏幕或 Sprite 上,传 t 决定
// ---------------------------------------------------------------------

/** 抗锯齿圆角实心卡片 */
void card(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t fill,
          uint16_t bg);

/** 卡片 + 描边 */
void cardRim(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t fill,
             uint16_t rim, uint16_t bg);

/** 竖向渐变填充的圆角卡片(顶 c0 -> 底 c1) */
void cardGrad(TFT_eSPI& t, int x, int y, int w, int h, int r, uint16_t c0,
              uint16_t c1, uint16_t bg);

/** 一像素细分隔线,两端淡出 */
void divider(TFT_eSPI& t, int x, int y, int w, uint16_t c, uint16_t bg);

/** 页面指示点。n 个点,第 cur 个高亮(画成小横条) */
void dots(TFT_eSPI& t, int cx, int y, int n, int cur, uint16_t on, uint16_t off,
          uint16_t bg);

/** 圆头进度条。frac 取 0..1,已完成段 c0->c1 横向渐变 */
void progress(TFT_eSPI& t, int x, int y, int w, int h, float frac,
              uint16_t track, uint16_t c0, uint16_t c1, uint16_t bg);

/** 竖向渐变的圆头柱子,频谱用。从底边 baseY 往上长 h 像素,底 c0 顶 c1 */
void vbar(TFT_eSPI& t, int x, int baseY, int w, int h, uint16_t c0, uint16_t c1,
          uint16_t bg);

// ---------------------------------------------------------------------
// 圆环 / 发光
// ---------------------------------------------------------------------

/** 抗锯齿圆环进度。angle 从 12 点方向顺时针算,单位度 */
void ring(TFT_eSPI& t, int cx, int cy, int r, int thick, float frac,
          uint16_t track, uint16_t c, uint16_t bg);

/** 光晕:在 (cx,cy) 画一圈由内到外渐隐的圆 */
void glow(TFT_eSPI& t, int cx, int cy, int r, uint16_t c, uint16_t bg);

// ---------------------------------------------------------------------
// 大号时间数字(手绘七段矢量,任意字号都锐利)
// ---------------------------------------------------------------------

/** bigTime 在给定字号下的总宽度,用于居中 */
int timeWidth(const char* s, int digitW, int digitH, int gap);

/**
 * 画 "12:34" 这样的时间串。只认 0-9 和 ':'。
 * 注意:笔画 AA 用 BG_READ 哨兵(内部硬编码),只准画在纯色底的
 * Sprite 上,别直接画屏幕。
 */
int bigTime(TFT_eSPI& t, int x, int y, const char* s, int digitW, int digitH,
            int gap, uint16_t c0, uint16_t c1, uint16_t bg, bool glowOn);

// ---------------------------------------------------------------------
// 图标(全部矢量手绘,不占字库)
// ---------------------------------------------------------------------

/** 播放器 / 状态图标,box 是外接正方形边长 */
void glyph(TFT_eSPI& t, Glyph g, int cx, int cy, int box, uint16_t c,
           uint16_t bg);

/** 天气图标,box 是外接正方形边长(建议 16~56) */
void wxIcon(TFT_eSPI& t, WxIcon ic, int cx, int cy, int box, uint16_t bg);

}  // namespace Gfx
