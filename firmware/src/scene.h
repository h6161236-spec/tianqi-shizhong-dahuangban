#pragma once
/**
 * =====================================================================
 *  scene.h —— 时钟页背景场景引擎(天空 × 天气 × 时段)
 * =====================================================================
 *  与 preview/ui.html 的 drawScene 逐条对应:
 *   - 天空:11 个时间关键帧(深夜→黎明→日出→正午→日落→黄昏)竖向插值,
 *     2px 色带在 320x28 离屏条带中合成;阴天/雾/雷暴/冻雨再调色
 *   - 天体:白天太阳 6→18 走弧(早晚偏橙),夜里月亮 + 26 颗固定种子
 *     闪烁星星;月相按农历日期(朔→望→朔)画月牙
 *   - 云:像素块造型 3 条高度带随风漂移;雷暴随机闪电(整屏闪 + 电弧)
 *   - 地面:远山半椭圆 + 草皮/泥土,雪天积雪覆盖、雨天草色变深
 *   - 粒子:雨(最多 82 条斜线,风决定倾角)/ 雪(56 片飘摆)/ 雾带 /
 *     冻雨地面冰晶闪光
 *   - 皮卡丘固定在 (130,140) 奔跑,影子随跳跃缩放
 *
 *  八条复用同一块 320x28 缓冲,完整合成背景和动层后推送。
 *  动画状态每帧只推进一次,旧/新对象范围只决定哪些条带需要更新。
 *  所有 24bit 颜色在内部混合,落到屏幕前转 RGB565,和预览的量化顺序一致。
 */

#include <TFT_eSPI.h>
#include <stdint.h>

namespace Scene {

/** 场景组 = WMO 分组,与 Gfx::WxIcon 一一对应 */
enum Group : uint8_t {
  GR_CLEAR = 0,
  GR_MOSTLY,
  GR_PARTLY,
  GR_OVERCAST,
  GR_FOG,
  GR_DRIZZLE,
  GR_RAIN,
  GR_HEAVYRAIN,
  GR_FREEZING,
  GR_SNOW,
  GR_HEAVYSNOW,
  GR_THUNDER,
  GR_COUNT
};

/** 场景参数(0..1 浮点,ice 是布尔) */
struct Params {
  float clouds, dark, rain, snow, fog, bolt, wind;
  bool  ice;
};

/** 组 → 参数表 */
const Params& params(Group g);

/** WMO weather_code → 场景组(28 个代码全覆盖) */
Group wmoGroup(int wmoCode);

/** 夜色浓度 0..1(21:00-4:30 = 1,白天 = 0) */
float nightness(float hourF);

/**
 * 画一帧场景。屏上 y18..y240(horizon=188)。
 * @param hourF  小数小时(含秒,天体/天空随时间动)
 * @param g      场景组
 * @param dt     本帧时间步(秒)。0 = 冻结动画,静态渲染(页面切换时)
 * @param moonPh 月相 0..1(0 朔,0.5 望);<0 用默认 0.5
 * @param withPika 是否画皮卡丘(时钟页 true)
 */
void draw(TFT_eSPI& t, float hourF, Group g, float dt, float moonPh,
          bool withPika);

/** 使场景下次绘制完整重建。页面切换后必须调用。 */
void invalidateCache();
/** Toast 退出后仅恢复地面提示区，不触发整片天空重建。 */
void restoreGroundOverlay();
/**
 * Clock page overlays already painted on the screen are excluded from strip
 * transfers. Pass their current visibility before draw(); disappearing
 * overlays are restored from the scene on that same frame.
 */
void setOverlayVisibility(bool networkBadge, bool toast);

// ---- 给页面叠元素用的取样色(和当帧场景同一路调色,纯函数) ----

/** 场景内 y 行的天空色(含组调色;越界取端点) */
uint16_t skyPixel(float hourF, Group g, int y);

/** 状态栏暗纱色(场景顶色向 0x0E1430 混 32%,对应预览 rgba 纱) */
uint16_t statusVeil(float hourF, Group g);

/** 地面色(泥土区,网络角标的底色取样) */
uint16_t groundColor(float hourF, Group g);

// ---- 其他四页的淡蓝导航区(时钟页地面由场景绘制) ----

/** 页脚高度,同预览 FOOT_H */
constexpr int FOOT_H = 16;
/** 页脚顶边 y,同预览 FOOT_Y = 240-16 */
constexpr int FOOT_TOP = 240 - FOOT_H;

/**
 * 绘制淡蓝导航区。
 * @param bg 页面底色(保留现有调用接口)
 */
void footer(TFT_eSPI& t, uint16_t bg);

/** 推进时钟页草丛滚动(其它页不要调) */
void advanceGround(float dt);

}  // namespace Scene
