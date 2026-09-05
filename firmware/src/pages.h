#pragma once
/**
 * =====================================================================
 *  pages.h —— 5 页面系统:渲染 / 输入 / 偏好 / 弹窗 / 演示数据
 * =====================================================================
 *  页面:时钟(场景动画,110ms 全帧重绘) / 音乐(频谱 100ms) /
 *  天气(两种视图) / 日期(农历月历) / 设置(8 行 + 滚动)。
 *  弹窗:WiFi 列表 → 密码键盘(复用 keyboard 模块) → 连接中 /
 *  蓝牙面板 / 恢复默认 / 重启确认。
 *
 *  main.cpp 的职责:初始化各模块 → 循环里调 UI::service(dt)(内部
 *  自己决定何时重绘)+ 触摸事件转 UI::handleGesture/handleTap,
 *  BLE/WiFi/天气在各自模块里跑。页面不碰网络细节。
 *
 *  这些函数由 main.cpp 实现,页面代码只调用不实现:
 *    bleSendBtn()      发播放控制到手机(BTN|play/pause/next/prev)
 *    bleDisconnectPeer()  断开当前 BLE 连接
 *    bleRestartAdv()   重新广播
 *    bleSetAdv(on)     开/关广播(=蓝牙开关)
 */

#include "touch.h"
#include <stdint.h>

enum UiPage : uint8_t {
  PG_CLOCK = 0,
  PG_MUSIC,
  PG_WEATHER,
  PG_DATE,
  PG_SETTINGS,
  PG_N
};

/** 音乐/蓝牙显示状态。BLE 回调写它,音乐页读它 */
struct MediaState {
  bool advOn;            // 蓝牙在广播
  bool connected;        // NUS 已连接
  char peer[24];         // 对端设备名
  char title[40];
  char artist[40];
  char lyric[64];        // 当前行(BLE 模式只有一行)
  int32_t pos, dur;      // 秒;pos<0 = 手机端没推进度
  bool playing;
  uint8_t spec[16];      // 0..255
  uint32_t lastPlayMs;   // PLAY 心跳时刻(超时视为暂停)
};

extern MediaState g_media;

// ---- 由 main.cpp 提供(见上) ----
void bleSendBtn(const char* btn);
void bleDisconnectPeer();
void bleRestartAdv();
void bleSetAdv(bool on);

namespace UI {

void init();

/** 每个主循环调用。内部:演示数据推进 / 自动跳页 / 帧率调度 / 重绘 */
void service(float dt);

/** 手动标记:需要立即重绘(数据变了、BLE 推了新行…) */
void markDirty();

/** 当前页 / 切页(带脏标记) */
uint8_t page();
void setPage(uint8_t p);

// ---- 偏好(NVS 持久) ----
uint8_t bright();     // 10..100
uint8_t specStyle();  // 0 渐变竖条 1 镜像 2 环形
uint8_t wxStyle();    // 0 概览+七日 1 整页七日
void resetPrefs();    // 恢复默认(不动 WiFi/天气缓存)

// ---- 输入(main 转发) ----
void handleGesture(Gesture g);
void handleTap(const char* id, int x, int y);

// ---- 其他模块通知 ----
void toast(const char* msg);          // 顶部提示条
void notifyWifiResult(bool ok);       // 连接中弹窗的结果
void notifyBtChanged();               // BLE 状态变化 → 重绘

}  // namespace UI
