# 皮卡丘天气钟固件发布

目标板：ESP32-2432S028（ESP32 + ILI9341，320×240 电阻触摸）。五页顺序为时钟、音乐、天气、日期、设置。

## 当前正式版：2026-09-24

使用仓库根目录的 `正式版刷机包-20260924.zip`，完整解压后运行包内 `flash.cmd`。也可在 `release/正式版刷机包-20260924/` 中执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\flash.ps1 -Port COM3
```

默认自动探测串口并排除蓝牙虚拟串口。单个候选（例如 `COM7`）会作为完整端口名使用。默认波特率 115200；正常刷写不加 `-Erase`，保留 WiFi、城市偏好、触摸校准与设置。`-Erase` 会清空这些数据。需要时可加 `-Monitor` 查看 115200 串口日志。

本版修复天气雨点残留，改为复用 `320×28` Sprite 的八条带完整合成；天气数据按地点归属缓存并校验 API 响应，刷新与配网按主循环调度。其余四页改为蓝白界面，保留时钟页天气天空、皮卡丘和地面。手机 BLE 协议及硬件引脚未变。

正式构建 `cyd` 成功：静态 RAM 68,236 / 327,680 字节（20.8%），Flash 1,750,609 / 3,145,728 字节（55.7%）。主机、场景像素、WiFi、调度、键盘、主题、精灵及网页预览回归已通过。场景测试逐像素比较局部更新与完整重绘，并复现旧版一分钟小雨残留。

**真机验收未完成。** 当前只发现蓝牙虚拟串口，尚未烧录开发板。晴天、小雨、多云、密集降水的残影与闪烁、约 9–10 fps、触摸响应及运行时堆内存须在真机检查；编译和预览结果不能代替这些结论。

## 镜像与校验

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| `bootloader.bin` | 17,536 | `0F65515A7E0DFE9CC4BC78C01BEF79C941AF1B2DEB34D01DDD500EE3D465EEDF` |
| `partitions.bin` | 3,072 | `AAAE2888C5A6A348004B5B436F47ABB25AE32E72D9003902955A998EDA723EDD` |
| `firmware.bin` | 1,756,912 | `965A7F9165B204BA706542B3EC51D2CC838D91A0C58D5A14AE2AAB8D101CA125` |

ZIP SHA-256：`F90FC71BCD0F5ABA7B2666FFC6315D97BC244BC51119362EDE057D9BAC5CB67C`。

脚本按固定偏移刷写：`0x1000 bootloader.bin`、`0x8000 partitions.bin`、`0x10000 firmware.bin`。这些是三个独立镜像，不要当成单个合并文件烧录。脚本优先使用 PlatformIO 自带 esptool，找不到时使用 Python 环境中的 esptool。

## 旧版本

`release/final/` 和 `release/flash.ps1` 仍指向 **2026-09-08 旧正式版**。`release/正式版刷机包-20260908/`、仓库根目录同名 ZIP，以及 `release/backup-before-20260908/` 均保留供回退。请使用新包内脚本刷入 2026-09-24 版。

首次配网：设置页 → WiFi → 扫描 → 输入密码。音乐页通过 NimBLE Nordic UART 接收手机端 `META|`、`LRC|`、`SPEC|` 等数据。源码构建与预览入口见上一层 [README.md](../README.md)。
