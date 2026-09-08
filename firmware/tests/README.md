# 固件回归验证

在项目根目录执行 `powershell -File firmware/tools/test_host.ps1`。
需要 Visual Studio C++ Build Tools；产物位于忽略的 `.pio/host-tests/`。

测试直接编译 `src/touch.cpp` 和生产使用的 `src/color_math.h`，仅替换
Arduino 时钟、串口和触摸采样输入。覆盖 RGB888 降序/混合方向/边界插值、
慢帧横滑、快速横滑、抬手消抖超时、播放按钮坐标漂移、跳点拒绝。
颜色旧算法可复现 1908 个失败，改为有符号通道后全部通过。

辅助检查：

- `python firmware/tools/check_theme.py`：固件与预览调色板一致。
- `python firmware/tools/check_sprite.py`：皮卡丘点阵一致。
- `node preview/tools/smoke.js`：网页预览的页面/操作/50 秒模拟运行。
- `pio run -e cyd`（在 firmware 目录）：正式版固件编译；本次不交付演示版。

这些检查不等于硬件显示或 BLE 端到端测试。刷机后需确认：

1. 唱盘浅黄、皮卡丘黄色，局部矩形与周围背景颜色一致，天空无异常彩条。
2. 时钟页向左/右快速和慢速滑动各 10 次，能翻页。
3. 手机播放时点暂停，再点播放，各重复 5 次，手机实际状态随之切换。
4. 连续切换至少 10 行歌词，只有歌词区替换；重复相同歌词不重画文字区。
5. 时钟页观察至少一分钟，检查时间/农历/天气卡片、脚部、雾天和提示退出。

若按钮仍无效，串口 115200 的 `[touch]` 和 `[ble] btn=... notify=...`
可区分未识别点击、BLE 通知失败、手机媒体控制未执行；notify=ok 仅表示通知
已提交，不证明手机播放器执行成功。雷暴的闪电亮屏是保留的天气效果。
