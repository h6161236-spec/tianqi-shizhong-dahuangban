// =====================================================================
//  皮卡丘天气钟 —— 全局配置(引脚 / 行为参数 / 默认值)
//  引脚与参考工程 E:\xiangmu\开发板 完全一致(同一块 ESP32-2432S028)
// =====================================================================
#pragma once

// ---------------------------------------------------------------- 用户可改
// WiFi 默认凭据:留空 = 首次上电进入「未配网」状态,在屏幕上完成配网
// (设置页 → WiFi → 扫描列表 → 键盘输密码)。想跳过配网可在这里写死。
#define CFG_WIFI_SSID ""
#define CFG_WIFI_PASS ""

// 定位出厂默认(联网后会被 IP 自动定位覆盖;设置里可手选钉死)
#define CFG_CITY_NAME "苏州"
#define CFG_LAT "31.30"
#define CFG_LON "120.58"

// 时区:东八区
#define CFG_TZ_HOURS 8

// 蓝牙名称(NimBLE NUS,手机端连接时看到的名字)
#define CFG_BLE_NAME "Pika-Music"

// ---------------------------------------------------------------- 引脚
// 触摸 XPT2046:这块板走的是独立 GPIO 位翻转(SPI mode 0),不占用 VSPI
#define TP_CLK 25
#define TP_MOSI 32
#define TP_MISO 39
#define TP_CS   33
#define TP_IRQ  36

// 背光:LEDC PWM 调光(亮度设置项用它)
#define TFT_BL_PIN   21
#define BL_LEDC_CH   7      // 通道 7:避开 BLE/其他占用的低通道
#define BL_LEDC_FREQ 20000  // 20kHz,无源听不到
#define BL_LEDC_BITS 8

// 光敏电阻(ADC,占位:自动亮度后续可加,现在不启用)
#define LDR_PIN 34

// 板载 RGB 指示灯(蓝色/绿色可控,红色与 TFT_RST 共用不碰)——本项目不用
// #define LED_G_PIN 16
// #define LED_B_PIN 17

// ---------------------------------------------------------------- 屏幕
#define SCR_W 320
#define SCR_H 240
#define SCR_ROTATION 1   // 横屏,和参考工程一致(触摸校准数据带旋转守卫)

// ---------------------------------------------------------------- 频谱
#define SPEC_BANDS 16   // BLE 协议 SPEC| 16 段,与手机端约定一致

// ---------------------------------------------------------------- 手势阈值
// 与参考工程/网页预览一致:滑动 45px、主轴至少是次轴 1.6 倍、900ms 内完成
#define SWIPE_MIN_PX     45
#define SWIPE_AXIS_RATIO 1.6f
#define SWIPE_MAX_MS     900
#define TAP_MAX_PX       28

// ---------------------------------------------------------------- 页面行为
#define MUSIC_IDLE_MS   10000  // 音乐停播后回时钟页的延时
#define MANUAL_HOLD_MS  30000  // 手动操作后暂停自动切页的时间
#define WEATHER_PERIOD_MS (30UL * 60UL * 1000UL)  // 天气刷新周期 30 分钟
#define BLE_HEARTBEAT_MS 3000  // PLAY 心跳超时判定

// ---------------------------------------------------------------- 帧率
// 时钟页是全帧重绘(天空场景动画),受 SPI 带宽限制跑 ~10fps 就够
#define CLOCK_FRAME_MS 110
// 音乐页频谱动画帧
#define MUSIC_FRAME_MS 100
// 天气/日期/设置页无动画,按需重绘;分钟变化时刷新
#define IDLE_FRAME_MS  1000

// ---------------------------------------------------------------- NVS 命名空间
#define NVS_UI_NS    "uicfg"     // 界面偏好(亮度/频谱样式/天气视图)
#define NVS_WX_NS    "wx1"       // 天气缓存
#define NVS_LOC_NS   "loc1"      // 城市定位(自动/钉死 + 经纬)
#define NVS_TOUCH_NS "touchcal"  // 触摸校准(参考工程同款,直接搬)
