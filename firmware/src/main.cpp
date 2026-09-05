/**
 * =====================================================================
 *  main.cpp —— 皮卡丘天气钟 主程序(横屏 320x240,5 页系统)
 * =====================================================================
 *  职责:
 *   - 上电顺序:TFT → Sprite(必须先于 WiFi/BLE,53KB 一次分够) →
 *     UI 偏好 → 背光 PWM → WiFi/天气 → 触摸校准 → BLE → 自动连保存的 WiFi
 *   - 主循环:WifiMgr::service + WiFi 状态迁移(SNTP/首拉/30 分钟周期) +
 *     触摸采样 → 手势 → UI::handleTap/handleGesture + UI::service(页面
 *     层自己排帧重绘)+ 皮卡丘动画时钟
 *   - BLE:Nordic UART Service。手机端推 META/LRC/PLAY/SPEC/POS 行协议;
 *     Android 按协商 MTU 拆包,所以在字节层跨回调累计到换行再解析。
 *     按钮回发 BTN|play/pause/next/prev。数据只写 g_media,不做任何绘制。
 *   - 触摸:XPT2046 位翻转驱动(SPI mode 0,独立 GPIO 不占 VSPI)+
 *     五点二维仿射校准(NVS 持久,带版本号;参数或算法升级自动重校准)
 *
 *  页面/弹窗/键盘在 pages.cpp,天气在 wx.cpp,配网在 wifi_config.cpp,
 *  渲染原语在 gfx/text。这里只做调度,不画页面。
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>

#ifndef DEMO_MODE
#include <NimBLEDevice.h>
#endif

#include "config.h"
#include "theme.h"
#include "gfx.h"
#include "text.h"
#include "pika.h"
#include "pages.h"
#include "touch.h"
#include "wifi_config.h"
#include "wx.h"

// =====================================================================
//  屏幕 / 背光
// =====================================================================
TFT_eSPI tft;   // pages.cpp 里 extern 引用

/** 亮度 10..100 → PWM 占空比。0.6 次幂让低亮度端变化更接近人眼线性 */
static uint8_t brightToDuty(uint8_t br) {
  const float f = br / 100.0f;
  return (uint8_t)(255.0f * powf(f, 0.6f) + 0.5f);
}

/** UI 偏好里的亮度变了(设置页点了亮度行)就跟着调背光 */
static void applyBacklight() {
  static uint8_t lastBr = 0xFF;
  const uint8_t br = UI::bright();
  if (br == lastBr) return;
  lastBr = br;
  ledcWrite(BL_LEDC_CH, brightToDuty(br));
}

// =====================================================================
//  XPT2046 电阻触摸(bit-bang,不占 SPI 总线,与 TFT 完全独立)
//  驱动与校准算法移植自参考工程 E:\xiangmu\开发板(同一块板,实测可用)
// =====================================================================
#define TP_SWAP_XY 1   // 无校准数据时的保底方向:1 = 交换 X/Y
#define TP_INV_X   0   // 1 = X 反向
#define TP_INV_Y   0   // 1 = Y 反向

static constexpr int TP_PRESSURE_MIN = 300;
static constexpr int TP_RAW_PAIR_MAX = 70;
static constexpr int TP_RAW_EDGE_MARGIN = 80;

/**
 * SPI mode 0 bit-bang。
 *
 * XPT2046 在下降沿移出下一位,主机必须在随后的上升沿采样。若在下降沿后
 * 立刻采样,12 位结果会整体左移一位:低端点看似正常,高端点会变成
 * 6000~8000,压力也会因 Z2 被翻倍而成为负数。
 */
static uint16_t tpTransfer(uint16_t out, uint8_t bits) {
  uint16_t in = 0;
  for (int i = bits - 1; i >= 0; --i) {
    digitalWrite(TP_MOSI, (out >> i) & 1);
    delayMicroseconds(1);  // DIN setup time
    digitalWrite(TP_CLK, HIGH);
    delayMicroseconds(1);
    // Mode 0:上升沿采样由前一个下降沿准备好的位。
    in = (uint16_t)((in << 1) | digitalRead(TP_MISO));
    digitalWrite(TP_CLK, LOW);
    delayMicroseconds(1);
  }
  return in;
}

/** 三个值里找距离最近的一对并平均,同时返回这一对的离散度。 */
static int bestTwoAverage(int v0, int v1, int v2, int* pairSpread) {
  const int d01 = abs(v0 - v1);
  const int d02 = abs(v0 - v2);
  const int d12 = abs(v1 - v2);
  if (d01 <= d02 && d01 <= d12) {
    if (pairSpread) *pairSpread = d01;
    return (v0 + v1) / 2;
  }
  if (d02 <= d01 && d02 <= d12) {
    if (pairSpread) *pairSpread = d02;
    return (v0 + v2) / 2;
  }
  if (pairSpread) *pairSpread = d12;
  return (v1 + v2) / 2;
}

/**
 * 一次完整的稳定采样批次。
 *
 * 整批保持 CS 为低、PD0=1 保持驱动开启,丢弃通道切换后的首个 X,再采三组
 * X/Y 取最接近两点平均;最后用 PD0=0 掉电并恢复 PENIRQ。
 */
static bool touchReadRaw(int* channelA, int* channelB,
                         int* outPressure = nullptr,
                         int* outPairSpread = nullptr) {
  // 部分 CYD 批次的 T_IRQ 没接对或一直保持高电平。因此不能在读 ADC 前用
  // IRQ 作硬门槛;实际按下由压力、坐标范围和连续采样离散度共同确认。
  const bool irqLowBefore = digitalRead(TP_IRQ) == LOW;

  int a[3] = {};
  int b[3] = {};

  digitalWrite(TP_CS, LOW);
  delayMicroseconds(2);

  tpTransfer(0xB1, 8);                       // Z1, drivers stay on
  const int z1 = (int)(tpTransfer(0x00C1, 16) >> 3);  // Z2 command
  const int z2 = (int)(tpTransfer(0x0091, 16) >> 3);  // first A command
  const int pressure = z1 + 4095 - z2;

  (void)tpTransfer(0x0091, 16);              // discard first noisy A
  a[0] = (int)(tpTransfer(0x00D1, 16) >> 3);
  b[0] = (int)(tpTransfer(0x0091, 16) >> 3);
  a[1] = (int)(tpTransfer(0x00D1, 16) >> 3);
  b[1] = (int)(tpTransfer(0x0091, 16) >> 3);
  a[2] = (int)(tpTransfer(0x00D0, 16) >> 3);  // last B powers down
  b[2] = (int)(tpTransfer(0x0000, 16) >> 3);

  digitalWrite(TP_CS, HIGH);
  digitalWrite(TP_MOSI, LOW);
  delayMicroseconds(2);

  int spreadA = 0, spreadB = 0;
  const int rawA = bestTwoAverage(a[0], a[1], a[2], &spreadA);
  const int rawB = bestTwoAverage(b[0], b[1], b[2], &spreadB);
  const int maxSpread = spreadA > spreadB ? spreadA : spreadB;

  if (outPressure) *outPressure = pressure;
  if (outPairSpread) *outPairSpread = maxSpread;

  bool samplesValid = true;
  for (int i = 0; i < 3; ++i) {
    if (a[i] < TP_RAW_EDGE_MARGIN || a[i] > 4095 - TP_RAW_EDGE_MARGIN ||
        b[i] < TP_RAW_EDGE_MARGIN || b[i] > 4095 - TP_RAW_EDGE_MARGIN) {
      samplesValid = false;
      break;
    }
  }

  const bool valid = samplesValid && pressure >= TP_PRESSURE_MIN &&
                     spreadA <= TP_RAW_PAIR_MAX && spreadB <= TP_RAW_PAIR_MAX;
  if (!valid) {
    static uint32_t lastRejectLog = 0;
    const uint32_t now = millis();
    if (irqLowBefore && now - lastRejectLog >= 1000) {
      lastRejectLog = now;
      Serial.printf("[touch] sample rejected z=%d spread=%d/%d raw=%d,%d\n",
                    pressure, spreadA, spreadB, rawA, rawB);
    }
    return false;
  }

  if (!irqLowBefore) {
    static bool loggedIrqBypass = false;
    if (!loggedIrqBypass) {
      loggedIrqBypass = true;
      Serial.println("[touch] valid sample with IRQ high; pressure fallback active");
    }
  }

  *channelA = rawA;
  *channelB = rawB;
  return true;
}

// ---- 五点二维仿射校准(保存到 NVS) ----
struct TouchCalibration {
  bool valid;
  // screenX = xA * rawA + xB * rawB + xC
  // screenY = yA * rawA + yB * rawB + yC
  float xA, xB, xC;
  float yA, yB, yC;
  float rmsError;
  float maxError;
};

static constexpr uint16_t TOUCH_CAL_VERSION = 3;
static constexpr uint8_t CAL_POINT_COUNT = 5;
static constexpr uint8_t CAL_SAMPLE_COUNT = 11;
static constexpr uint32_t CAL_SETTLE_MS = 100;
static constexpr int CAL_CLUSTER_MAX = 120;
static constexpr int CAL_MAX_ERROR_PX = 10;
static constexpr float CAL_MAX_RMS_PX = 6.0f;

static TouchCalibration g_touchCal = {};
static bool g_calibrationMode = false;
static uint8_t g_calibrationStep = 0;
static int g_calRaw[CAL_POINT_COUNT][2] = {};
static int g_calSamples[CAL_SAMPLE_COUNT][2] = {};
static uint8_t g_calSampleCount = 0;
static uint32_t g_calTouchStartMs = 0;
static bool g_calAwaitRelease = false;
static bool g_calCompletePending = false;

static const int CAL_POINT_X[CAL_POINT_COUNT] = {24, 296, 296, 24, 160};
static const int CAL_POINT_Y[CAL_POINT_COUNT] = {24, 24, 216, 216, 120};

/** 采样一次触摸。返回 false = 未按下。坐标已映射到 320x240。 */
bool touchSample(int* ox, int* oy) {
  int channelA = 0, channelB = 0;
  if (!touchReadRaw(&channelA, &channelB)) {
    // IRQ 正常的板子上,按住期间偶发一批压力/离散度不合格时保留最后
    // 坐标;IRQ 不可靠的板子则由 touchUpdate 的连续两帧失败确认抬手。
    if (g_touch.down && digitalRead(TP_IRQ) == LOW) {
      *ox = g_touch.x;
      *oy = g_touch.y;
      return true;
    }
    return false;
  }

  if (g_touchCal.valid) {
    const float fx = g_touchCal.xA * channelA + g_touchCal.xB * channelB +
                     g_touchCal.xC;
    const float fy = g_touchCal.yA * channelA + g_touchCal.yB * channelB +
                     g_touchCal.yC;
    if (!isfinite(fx) || !isfinite(fy)) return false;
    const int px = (int)lroundf(fx);
    const int py = (int)lroundf(fy);
    *ox = constrain(px, 0, SCR_W - 1);
    *oy = constrain(py, 0, SCR_H - 1);
    return true;
  }

  int sx = channelA;
  int sy = channelB;

#if TP_SWAP_XY
  int t = sx; sx = sy; sy = t;
#endif
#if TP_INV_X
  sx = 4095 - sx;
#endif
#if TP_INV_Y
  sy = 4095 - sy;
#endif
  // CYD 常见有效范围约 200..3900,留出边缘校准余量。
  sx = constrain(sx, 200, 3900);
  sy = constrain(sy, 200, 3900);
  int px = map(sx, 200, 3900, 0, SCR_W - 1);
  int py = map(sy, 200, 3900, 0, SCR_H - 1);
  if (px < 0) px = 0; if (px >= SCR_W) px = SCR_W - 1;
  if (py < 0) py = 0; if (py >= SCR_H) py = SCR_H - 1;
  *ox = px; *oy = py;
  return true;
}

static bool touchCalibrationLooksValid(const TouchCalibration& cal) {
  if (!isfinite(cal.xA) || !isfinite(cal.xB) || !isfinite(cal.xC) ||
      !isfinite(cal.yA) || !isfinite(cal.yB) || !isfinite(cal.yC) ||
      !isfinite(cal.rmsError) || !isfinite(cal.maxError)) {
    return false;
  }
  const float determinant = cal.xA * cal.yB - cal.xB * cal.yA;
  if (fabsf(determinant) < 0.0001f || fabsf(determinant) > 1.0f) return false;
  if (fabsf(cal.xC) > 5000.0f || fabsf(cal.yC) > 5000.0f) return false;
  return cal.rmsError <= CAL_MAX_RMS_PX && cal.maxError <= CAL_MAX_ERROR_PX;
}

static void loadTouchCalibration() {
  Preferences prefs;
  prefs.begin(NVS_TOUCH_NS, true);
  const uint16_t version = prefs.getUShort("ver", 0);
  const uint8_t rotation = prefs.getUChar("rot", 0xFF);
  TouchCalibration loaded = {};
  loaded.xA = prefs.getFloat("m00", NAN);
  loaded.xB = prefs.getFloat("m01", NAN);
  loaded.xC = prefs.getFloat("m02", NAN);
  loaded.yA = prefs.getFloat("m10", NAN);
  loaded.yB = prefs.getFloat("m11", NAN);
  loaded.yC = prefs.getFloat("m12", NAN);
  loaded.rmsError = prefs.getFloat("rms", NAN);
  loaded.maxError = prefs.getFloat("max", NAN);
  prefs.end();

  loaded.valid = version == TOUCH_CAL_VERSION && rotation == SCR_ROTATION &&
                 touchCalibrationLooksValid(loaded);
  g_touchCal = loaded;
  if (g_touchCal.valid) {
    Serial.printf("[touch] calibration loaded rms=%.2f max=%.2f\n",
                  g_touchCal.rmsError, g_touchCal.maxError);
  } else {
    Serial.printf("[touch] calibration missing/expired (stored v%u rot%u, need v%u rot%u)\n",
                  version, rotation, TOUCH_CAL_VERSION, SCR_ROTATION);
  }
}

static void saveTouchCalibration() {
  Preferences prefs;
  prefs.begin(NVS_TOUCH_NS, false);
  // 版本最后写:中途掉电时不会把半套参数当作有效校准。
  prefs.putUShort("ver", 0);
  prefs.putUChar("rot", SCR_ROTATION);
  prefs.putFloat("m00", g_touchCal.xA);
  prefs.putFloat("m01", g_touchCal.xB);
  prefs.putFloat("m02", g_touchCal.xC);
  prefs.putFloat("m10", g_touchCal.yA);
  prefs.putFloat("m11", g_touchCal.yB);
  prefs.putFloat("m12", g_touchCal.yC);
  prefs.putFloat("rms", g_touchCal.rmsError);
  prefs.putFloat("max", g_touchCal.maxError);
  prefs.putUShort("ver", TOUCH_CAL_VERSION);
  prefs.end();
}

static void drawCalibrationTarget() {
  tft.fillScreen(TH::BG);
  const int targetX = CAL_POINT_X[g_calibrationStep];
  const int targetY = CAL_POINT_Y[g_calibrationStep];
  const int bannerY = targetY > SCR_H / 2 ? 4 : SCR_H - GFX_STRIP_H - 4;

  Gfx::beginStrip(TH::BG);
  char title[48];
  snprintf(title, sizeof(title), "校准 %u/%u  按住十字中心",
           g_calibrationStep + 1, CAL_POINT_COUNT);
  Txt::drawCenter(SCR_W / 2, 20, CN12, TH::TXT_HI, title);
  Gfx::pushStrip(0, bannerY);

  tft.drawCircle(targetX, targetY, 12, TH::CYAN);
  tft.drawCircle(targetX, targetY, 5, TH::PURPLE);
  tft.drawFastHLine(targetX - 17, targetY, 35, TH::CYAN);
  tft.drawFastVLine(targetX, targetY - 17, 35, TH::CYAN);
  tft.fillCircle(targetX, targetY, 2, TH::TXT_HI);
}

static void drawCalibrationMessage(const char* message, uint16_t color) {
  tft.fillScreen(TH::BG);
  Gfx::beginStrip(TH::BG);
  Txt::drawCenter(SCR_W / 2, 20, CN12, color, message);
  Gfx::pushStrip(0, (SCR_H - GFX_STRIP_H) / 2);
}

static void startTouchCalibration() {
  g_calibrationMode = true;
  g_calibrationStep = 0;
  memset(g_calRaw, 0, sizeof(g_calRaw));
  memset(g_calSamples, 0, sizeof(g_calSamples));
  g_calSampleCount = 0;
  g_calTouchStartMs = 0;
  g_calAwaitRelease = false;
  g_calCompletePending = false;
  g_touchCal.valid = false;
  g_touch.down = false;
  g_touch.pressedHit[0] = 0;
  g_touch.tappedHit[0] = 0;
  drawCalibrationTarget();
  Serial.println("[touch] five-point affine calibration started");
}

static bool solve3x3(const double matrix[3][3], const double rhs[3],
                     double out[3]) {
  double a[3][4] = {};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) a[r][c] = matrix[r][c];
    a[r][3] = rhs[r];
  }

  for (int col = 0; col < 3; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 3; ++row) {
      if (fabs(a[row][col]) > fabs(a[pivot][col])) pivot = row;
    }
    if (fabs(a[pivot][col]) < 1e-9) return false;
    if (pivot != col) {
      for (int c = col; c < 4; ++c) {
        const double tmp = a[col][c];
        a[col][c] = a[pivot][c];
        a[pivot][c] = tmp;
      }
    }

    const double divisor = a[col][col];
    for (int c = col; c < 4; ++c) a[col][c] /= divisor;
    for (int row = 0; row < 3; ++row) {
      if (row == col) continue;
      const double factor = a[row][col];
      for (int c = col; c < 4; ++c) a[row][c] -= factor * a[col][c];
    }
  }

  for (int i = 0; i < 3; ++i) out[i] = a[i][3];
  return true;
}

static bool finishTouchCalibration() {
  double normal[3][3] = {};
  double rhsX[3] = {};
  double rhsY[3] = {};

  for (int i = 0; i < CAL_POINT_COUNT; ++i) {
    const double a = g_calRaw[i][0];
    const double b = g_calRaw[i][1];
    const double basis[3] = {a, b, 1.0};
    for (int r = 0; r < 3; ++r) {
      rhsX[r] += basis[r] * CAL_POINT_X[i];
      rhsY[r] += basis[r] * CAL_POINT_Y[i];
      for (int c = 0; c < 3; ++c) normal[r][c] += basis[r] * basis[c];
    }
  }

  double coeffX[3] = {};
  double coeffY[3] = {};
  if (!solve3x3(normal, rhsX, coeffX) || !solve3x3(normal, rhsY, coeffY)) {
    Serial.println("[touch] calibration rejected: singular point set");
    return false;
  }

  TouchCalibration candidate = {};
  candidate.xA = (float)coeffX[0];
  candidate.xB = (float)coeffX[1];
  candidate.xC = (float)coeffX[2];
  candidate.yA = (float)coeffY[0];
  candidate.yB = (float)coeffY[1];
  candidate.yC = (float)coeffY[2];

  double sumSquared = 0.0;
  double maxError = 0.0;
  for (int i = 0; i < CAL_POINT_COUNT; ++i) {
    const double px = coeffX[0] * g_calRaw[i][0] +
                      coeffX[1] * g_calRaw[i][1] + coeffX[2];
    const double py = coeffY[0] * g_calRaw[i][0] +
                      coeffY[1] * g_calRaw[i][1] + coeffY[2];
    const double dx = px - CAL_POINT_X[i];
    const double dy = py - CAL_POINT_Y[i];
    const double error = sqrt(dx * dx + dy * dy);
    sumSquared += error * error;
    if (error > maxError) maxError = error;
  }
  candidate.rmsError = (float)sqrt(sumSquared / CAL_POINT_COUNT);
  candidate.maxError = (float)maxError;
  candidate.valid = touchCalibrationLooksValid(candidate);
  if (!candidate.valid) {
    Serial.printf("[touch] calibration rejected: rms=%.2f max=%.2f\n",
                  candidate.rmsError, candidate.maxError);
    return false;
  }

  g_touchCal = candidate;
  saveTouchCalibration();
  Serial.printf("[touch] calibrated rms=%.2f max=%.2f\n",
                g_touchCal.rmsError, g_touchCal.maxError);
  return true;
}

static void sortInts(int* values, int count) {
  for (int i = 1; i < count; ++i) {
    const int value = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > value) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = value;
  }
}

static bool finishCalibrationPoint(int* outA, int* outB,
                                   int* outSpreadA, int* outSpreadB) {
  int a[CAL_SAMPLE_COUNT] = {};
  int b[CAL_SAMPLE_COUNT] = {};
  for (int i = 0; i < CAL_SAMPLE_COUNT; ++i) {
    a[i] = g_calSamples[i][0];
    b[i] = g_calSamples[i][1];
  }
  sortInts(a, CAL_SAMPLE_COUNT);
  sortInts(b, CAL_SAMPLE_COUNT);

  // 忽略两端各两个值,再检查中间七个值是否聚成一簇。
  const int spreadA = a[CAL_SAMPLE_COUNT - 3] - a[2];
  const int spreadB = b[CAL_SAMPLE_COUNT - 3] - b[2];
  if (outSpreadA) *outSpreadA = spreadA;
  if (outSpreadB) *outSpreadB = spreadB;
  if (spreadA > CAL_CLUSTER_MAX || spreadB > CAL_CLUSTER_MAX) return false;

  // 中间五个值做截尾平均,比整段按压普通平均更不怕落指/抬手离群点。
  int32_t sumA = 0, sumB = 0;
  for (int i = 3; i <= CAL_SAMPLE_COUNT - 4; ++i) {
    sumA += a[i];
    sumB += b[i];
  }
  const int kept = CAL_SAMPLE_COUNT - 6;
  *outA = (int)(sumA / kept);
  *outB = (int)(sumB / kept);
  return true;
}

static void updateTouchCalibration() {
  const uint32_t now = millis();
  int a = 0, b = 0, pressure = 0, pairSpread = 0;
  const bool physicallyDown = touchReadRaw(&a, &b, &pressure, &pairSpread);

  if (g_calAwaitRelease) {
    if (!physicallyDown) {
      g_calAwaitRelease = false;
      g_calSampleCount = 0;
      g_calTouchStartMs = 0;
      if (g_calCompletePending) {
        g_calCompletePending = false;
        g_calibrationMode = false;
        g_touch.down = false;
        g_touch.pressedHit[0] = 0;
        g_touch.tappedHit[0] = 0;
        tft.fillScreen(TH::BG);
        UI::markDirty();
      } else {
        drawCalibrationTarget();
      }
    }
    return;
  }

  if (!physicallyDown) {
    g_calTouchStartMs = 0;
    g_calSampleCount = 0;
    return;
  }

  if (g_calTouchStartMs == 0) {
    g_calTouchStartMs = now;
    g_calSampleCount = 0;
    return;
  }
  if (now - g_calTouchStartMs < CAL_SETTLE_MS) return;

  if (g_calSampleCount < CAL_SAMPLE_COUNT) {
    g_calSamples[g_calSampleCount][0] = a;
    g_calSamples[g_calSampleCount][1] = b;
    ++g_calSampleCount;
  }
  if (g_calSampleCount < CAL_SAMPLE_COUNT) return;

  int pointA = 0, pointB = 0, spreadA = 0, spreadB = 0;
  if (!finishCalibrationPoint(&pointA, &pointB, &spreadA, &spreadB)) {
    Serial.printf("[touch] point %u moving/noisy spread=%d,%d; retry\n",
                  g_calibrationStep + 1, spreadA, spreadB);
    g_calSampleCount = 0;
    g_calTouchStartMs = now;
    return;
  }

  g_calRaw[g_calibrationStep][0] = pointA;
  g_calRaw[g_calibrationStep][1] = pointB;
  Serial.printf("[touch] point %u raw=%d,%d z=%d pair=%d cluster=%d,%d\n",
                g_calibrationStep + 1, pointA, pointB, pressure, pairSpread,
                spreadA, spreadB);
  ++g_calibrationStep;
  g_calAwaitRelease = true;

  if (g_calibrationStep >= CAL_POINT_COUNT) {
    if (finishTouchCalibration()) {
      g_calCompletePending = true;
      drawCalibrationMessage("校准完成，请松手", TH::GREEN);
    } else {
      g_calibrationStep = 0;
      memset(g_calRaw, 0, sizeof(g_calRaw));
      drawCalibrationMessage("误差过大，请松手后重试", TH::AMBER);
    }
  } else {
    drawCalibrationMessage("已记录，请松手点下一处", TH::CYAN);
  }
}

// =====================================================================
//  BLE:Nordic UART Service,收音乐数据 / 发播放控制
//  (DEMO_MODE 构建完全没有蓝牙,四个接口落成空实现)
// =====================================================================
#ifndef DEMO_MODE

// Android 会按协商 MTU 把一行拆成多次 onWrite;必须在字节层跨回调累计
// 到换行,否则残片会被当成坏协议行。
static char g_bleRxBuf[512] = {};
static size_t g_bleRxLen = 0;
static bool g_bleRxOverflow = false;

static NimBLECharacteristic* g_notifyChar = nullptr;
static NimBLEServer* g_srv = nullptr;
static uint16_t g_connHandle = 0xFFFF;

/** 一行协议 → g_media。只写数据,不触发任何绘制(音乐页自己 100ms 刷) */
static void parseBleLine(char* line) {
  if (strncmp(line, "META|", 5) == 0) {
    char* p = line + 5;
    char* br = strchr(p, '|');
    if (br) *br = 0;
    strlcpy(g_media.title, p, sizeof(g_media.title));
    strlcpy(g_media.artist, br ? br + 1 : "", sizeof(g_media.artist));
    Serial.printf("[ble] meta: %s - %s\n", g_media.title, g_media.artist);
  } else if (strncmp(line, "LRC|", 4) == 0) {
    strlcpy(g_media.lyric, line + 4, sizeof(g_media.lyric));
  } else if (strncmp(line, "PLAY|", 5) == 0) {
    const bool np = line[5] == '1';
    // PLAY|1 是每 3 秒一次的存活心跳;PLAY|0 只在状态变化时刷新。
    if (np || np != g_media.playing) g_media.lastPlayMs = millis();
    if (np != g_media.playing) {
      g_media.playing = np;
      UI::notifyBtChanged();   // 状态带「播放中/已暂停」要换
    }
    if (!np) memset(g_media.spec, 0, sizeof(g_media.spec));
  } else if (strncmp(line, "SPEC|", 5) == 0) {
    char* p = line + 5;
    for (int i = 0; i < SPEC_BANDS; ++i) {
      int v = atoi(p);
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      g_media.spec[i] = (uint8_t)v;
      p = strchr(p, ',');
      if (!p) break;
      ++p;
    }
  } else if (strncmp(line, "POS|", 4) == 0) {
    // POS|cur|dur(dur 可缺省,缺省时沿用旧值)
    const int cur = atoi(line + 4);
    const char* sep = strchr(line + 4, '|');
    if (cur >= 0) g_media.pos = cur;
    if (sep) {
      const int dur = atoi(sep + 1);
      if (dur > 0) g_media.dur = dur;
    }
  }
}

class ChrCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    NimBLEAttValue v = c->getValue();
    const uint8_t* data = v.data();
    for (size_t i = 0; i < v.length(); ++i) {
      const char ch = (char)data[i];
      if (ch == '\n') {
        if (!g_bleRxOverflow && g_bleRxLen > 0) {
          if (g_bleRxBuf[g_bleRxLen - 1] == '\r') --g_bleRxLen;
          g_bleRxBuf[g_bleRxLen] = 0;
          parseBleLine(g_bleRxBuf);
        }
        g_bleRxLen = 0;
        g_bleRxOverflow = false;
      } else if (!g_bleRxOverflow) {
        if (g_bleRxLen < sizeof(g_bleRxBuf) - 1) {
          g_bleRxBuf[g_bleRxLen++] = ch;
        } else {
          // 丢弃这条超长坏包直到下一个换行,避免残片被当成新协议行。
          g_bleRxLen = 0;
          g_bleRxOverflow = true;
        }
      }
    }
  }
};

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* srv, NimBLEConnInfo& info) override {
    g_bleRxLen = 0;
    g_bleRxOverflow = false;
    g_connHandle = info.getConnHandle();
    // 上一台的歌名/歌词/频谱不能漏给新连接
    g_media.connected = true;
    g_media.playing = false;
    g_media.pos = -1;
    g_media.dur = 0;
    memset(g_media.spec, 0, sizeof(g_media.spec));
    g_media.title[0] = 0;
    g_media.artist[0] = 0;
    g_media.lyric[0] = 0;
    strlcpy(g_media.peer, "手机", sizeof(g_media.peer));
    UI::notifyBtChanged();
    Serial.println("[ble] connected");
    (void)srv;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    g_bleRxLen = 0;
    g_bleRxOverflow = false;
    g_connHandle = 0xFFFF;
    g_media.connected = false;
    g_media.playing = false;
    g_media.pos = 0;
    g_media.dur = 0;
    memset(g_media.spec, 0, sizeof(g_media.spec));
    g_media.title[0] = 0;
    g_media.artist[0] = 0;
    g_media.lyric[0] = 0;
    g_media.peer[0] = 0;
    UI::notifyBtChanged();
    // 用户主动关广播时不趁断线偷偷恢复广播
    if (g_media.advOn) NimBLEDevice::startAdvertising();
    Serial.println("[ble] disconnected");
  }
};

static void bleInit() {
  NimBLEDevice::init(CFG_BLE_NAME);
  NimBLEDevice::setMTU(180);
  g_srv = NimBLEDevice::createServer();
  g_srv->setCallbacks(new SrvCb());
  NimBLEService* svc =
      g_srv->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  NimBLECharacteristic* wr =
      svc->createCharacteristic("6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
                                NIMBLE_PROPERTY::WRITE);
  wr->setCallbacks(new ChrCb());
  g_notifyChar =
      svc->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
                                NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_srv->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
  adv->start();
  g_media.advOn = true;
  Serial.printf("[ble] advertising %s\n", CFG_BLE_NAME);
}

// ---- pages.h 声明的四个接口(非演示构建的真实现) ----

void bleSendBtn(const char* btn) {
  if (!g_notifyChar || !g_media.connected) return;
  char msg[20];
  snprintf(msg, sizeof(msg), "BTN|%s\n", btn);
  g_notifyChar->setValue((const uint8_t*)msg, strlen(msg));
  const bool sent = g_notifyChar->notify();
  Serial.printf("[ble] btn=%s notify=%s\n", btn, sent ? "ok" : "failed");
}

void bleDisconnectPeer() {
  if (g_srv && g_media.connected && g_connHandle != 0xFFFF) {
    g_srv->disconnect(g_connHandle);   // 收尾在 onDisconnect 回调里
  }
}

void bleRestartAdv() {
  NimBLEDevice::stopAdvertising();
  NimBLEDevice::startAdvertising();
  g_media.advOn = true;
  UI::notifyBtChanged();
}

void bleSetAdv(bool on) {
  if (on) {
    NimBLEDevice::startAdvertising();
    g_media.advOn = true;
  } else {
    NimBLEDevice::stopAdvertising();
    g_media.advOn = false;
  }
  UI::notifyBtChanged();
}

#else   // DEMO_MODE

void bleSendBtn(const char*) {}
void bleDisconnectPeer() {}
void bleRestartAdv() {}
void bleSetAdv(bool) {}

#endif  // DEMO_MODE

// =====================================================================
//  WiFi / SNTP / 天气调度
// =====================================================================
static bool s_wifiUp = false;
static uint32_t s_wifiUpMs = 0;
static uint32_t s_lastWxMs = 0;
static bool s_sntpStarted = false;
static bool s_timeOk = false;

static void sntpStart() {
  char tz[16];
  snprintf(tz, sizeof(tz), "CST-%d", CFG_TZ_HOURS);
  configTzTime(tz, "ntp.aliyun.com", "pool.ntp.org");
  s_sntpStarted = true;
  Serial.println("[time] SNTP start");
}

/**
 * 拉一次天气。阻塞最多 ~16s(HTTP + 解析);每 30 分钟一次,期间
 * 动画会顿一下 —— 和参考工程同样的取舍(不做异步客户端省一大块代码)。
 * 拉不到且当前还是演示数据时,让演示数据漂移一点,像真的刷过。
 */
static void weatherFetch() {
  if (!WifiMgr::isConnected()) return;
  if (Wx::fetch()) {
    UI::markDirty();
    Serial.println("[wx] fetched");
  } else if (Wx::data().demo) {
    Wx::refreshDemo();
    UI::markDirty();
  }
  s_lastWxMs = millis();
}

// =====================================================================
//  setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== 皮卡丘天气钟 boot ===");

  // ---- 屏幕 ----
  tft.init();
  tft.setRotation(SCR_ROTATION);
  tft.fillScreen(TH::BG);

  // ---- 触摸引脚(XPT2046 位翻转,独立于 TFT 的 VSPI) ----
  pinMode(TP_CS, OUTPUT);
  pinMode(TP_CLK, OUTPUT);
  pinMode(TP_MOSI, OUTPUT);
  pinMode(TP_MISO, INPUT);
  pinMode(TP_IRQ, INPUT);
  digitalWrite(TP_CS, HIGH);
  digitalWrite(TP_CLK, LOW);

  // ---- 渲染核心:Sprite 必须先于 WiFi/BLE 分配(53KB 连续堆) ----
  if (!Gfx::init(tft)) Serial.println("[gfx] sprite alloc FAILED");
  Txt::init(tft);
  Serial.printf("[mem] free heap after sprites %u\n",
                (unsigned)ESP.getFreeHeap());

  // ---- UI 偏好 / 键盘 /(演示构建填假音乐数据) ----
  UI::init();

  // ---- 背光 PWM(亮度来自 UI 偏好;之后设置页改亮度由 loop 跟随) ----
  ledcSetup(BL_LEDC_CH, BL_LEDC_FREQ, BL_LEDC_BITS);
  ledcAttachPin(TFT_BL_PIN, BL_LEDC_CH);
  applyBacklight();

  // ---- WiFi / 天气(读 NVS,不联网) ----
  WifiMgr::init();
  Wx::init();

  // ---- 触摸:有效校准直接进主界面;没有(或版本过期)先五点校准 ----
  loadTouchCalibration();
  if (!g_touchCal.valid) startTouchCalibration();

#ifndef DEMO_MODE
  // ---- BLE(在 Sprite 之后初始化,NimBLE 要吃一大块堆) ----
  bleInit();
  Serial.printf("[mem] free heap after BLE %u\n",
                (unsigned)ESP.getFreeHeap());
#endif

  // ---- 自动连接已保存的 WiFi(没有保存就保持「未配网」,屏上配) ----
  WiFiConfig* cfg = WifiMgr::getConfig();
  if (cfg->saved && cfg->ssid[0]) {
    WifiMgr::connect(cfg->ssid, cfg->password);
  } else {
    Serial.println("[wifi] no saved config");
  }

  Serial.printf("[touch] IRQ=%d CS=%d CLK=%d MOSI=%d MISO=%d\n",
                TP_IRQ, TP_CS, TP_CLK, TP_MOSI, TP_MISO);
}

void loop() {
  const uint32_t now = millis();

  // ---- 校准模式:整屏被校准流程接管,其他一概暂停 ----
  if (g_calibrationMode) {
    static uint32_t s_lastCalMs = 0;
    if (now - s_lastCalMs >= 20) {
      s_lastCalMs = now;
      updateTouchCalibration();
    }
    delay(5);
    return;
  }

  // ---- dt(秒):阻塞式天气拉取后 dt 会很大,封顶防动画跳变 ----
  static uint32_t s_lastLoopMs = 0;
  float dt = s_lastLoopMs ? (now - s_lastLoopMs) / 1000.0f : 0.02f;
  if (dt < 0.0f) dt = 0.0f;
  if (dt > 0.25f) dt = 0.25f;
  s_lastLoopMs = now;

  // ---- 皮卡丘奔跑/跳跃动画时钟(时钟页 110ms 才画,但状态要连续推) ----
  Pika::tick(dt);

  // ---- WiFi 状态推进 + 断线自动重连(内部异步扫描) ----
  WifiMgr::service();
  const bool up = WifiMgr::isConnected();
  if (up != s_wifiUp) {
    s_wifiUp = up;
    s_wifiUpMs = up ? now : 0;
    UI::markDirty();   // 状态条 wifi 图标变色
    if (up) {
      Serial.printf("[wifi] connected, ip %s\n", WifiMgr::getIPAddress());
      if (!s_sntpStarted) sntpStart();
    } else {
      Serial.println("[wifi] disconnected");
    }
  }

  // ---- 天气:连上 1.5s 后首拉(等「连接中」弹窗转完),此后 30 分钟一轮 ----
  if (s_wifiUp) {
    const bool firstFetch = s_lastWxMs == 0 && now - s_wifiUpMs > 1500;
    const bool periodic =
        s_lastWxMs != 0 && now - s_lastWxMs > WEATHER_PERIOD_MS;
    if (firstFetch || periodic) weatherFetch();
  }

  // ---- 时间同步完成的瞬间刷一次(时钟页「时间未同步」→真时间) ----
  if (!s_timeOk && time(nullptr) > 1000000000L) {
    s_timeOk = true;
    UI::markDirty();
    Serial.println("[time] synced");
  }

  // ---- 触摸采样 → 手势(20ms 一次,跟参考工程一致) ----
  static uint32_t s_lastTouchMs = 0;
  static char s_lastPressed[24] = "";
  if (now - s_lastTouchMs >= 20) {
    s_lastTouchMs = now;
    const Gesture g = touchUpdate();
    if (g == GESTURE_TAP) {
      UI::handleTap(touchTappedId(), g_touch.x, g_touch.y);
    } else if (g != GESTURE_NONE) {
      UI::handleGesture(g);
    }
    // 按下/松开哪个控件变了 → 立刻重绘一次,按钮的按下态才看得见。
    // (天气/日期/设置页是按需重绘的,不靠动画帧顺带刷出按下反馈)
    if (strcmp(g_touch.pressedHit, s_lastPressed) != 0) {
      strlcpy(s_lastPressed, g_touch.pressedHit, sizeof(s_lastPressed));
      UI::markDirty();
    }
  }

  // ---- 背光跟随亮度设置 + 页面层排帧重绘 ----
  applyBacklight();
  UI::service(dt);

  delay(2);
}
