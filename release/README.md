# 皮卡丘天气钟 · 可烧录固件

目标板:**ESP32-2432S028**(ESP32 + ILI9341 2.8 寸 320×240 电阻触摸,俗称 CYD)。
界面按 `preview/ui.html` 1:1 移植,五页系统:时钟 / 音乐 / 天气 / 日期 / 设置。

## 怎么刷

插上 USB,然后:

```
powershell -File flash.ps1
```

脚本会自己找串口(自动跳过蓝牙虚拟 COM 口)、按正确偏移刷三个镜像。
1.7MB 在 115200 下约 2.5 分钟 —— 慢是故意的,这块板的 CH340 在 460800 会偶发掉线。

常用参数:

| 参数 | 作用 |
|---|---|
| `-Port COM3` | 指定串口(自动探测到多个候选时用) |
| `-Demo` | 刷演示版(假数据,见下) |
| `-Monitor` | 刷完打开串口监视(115200) |
| `-Erase` | 先整片擦除,**会清掉已保存的 WiFi 和设置** |
| `-Baud 460800` | 线材可靠时提速 |

刷不进去时:按住板上 BOOT 再开始刷;换根数据线(有些线只供电);再降速到 74880。

## 两个版本

- **normal/**(1686 KB)—— 日常用。首次上电没有 WiFi,滑到设置页 → WiFi → 扫描 → 键盘输密码配网。
  配好后自动校时、拉 7 天预报(open-meteo),之后每 30 分钟刷新,断网继续用缓存。
  音乐页需要手机端通过蓝牙(NimBLE Nordic UART)推 `META|`/`LRC|`/`SPEC|` 数据。
- **demo/**(1456 KB)—— 演示版。假频谱、假歌词、假天气,不连 WiFi 和蓝牙。
  没手机、没网也能看全部界面动效,验机用。

## 手上没有 esptool 也行

脚本优先用 PlatformIO 自带的 esptool(和构建时同一版本),没有就退回 `pip install esptool` 装的那个。
两个都没有时它会提示你装。

想手动刷的话,偏移是固定的(huge_app 分区表):

```
0x1000   bootloader.bin
0x8000   partitions.bin
0x10000  firmware.bin
```

## 操作方式

- **左右滑**翻页(五页循环)
- **上下滑**页内操作:音乐页切音波样式、天气页切视图、日期页切月份、设置页移动焦点
- **点按**触发按钮;时钟页点皮卡丘它会跳
- 放歌时自动跳到音乐页,停播 10 秒回时钟页;手动操作后 30 秒内不自动跳

## 镜像校验

```
[normal]
  bootloader.bin      17536 bytes  sha256 0f65515a7e0dfe9c...
  partitions.bin       3072 bytes  sha256 aaae2888c5a6a348...
  firmware.bin      1726112 bytes  sha256 7ba4025ef1ecf61c...

[demo]
  bootloader.bin      17536 bytes  sha256 0f65515a7e0dfe9c...
  partitions.bin       3072 bytes  sha256 aaae2888c5a6a348...
  firmware.bin      1491232 bytes  sha256 637b36ce443ad1cb...
```

占用:normal 版 Flash 54.7%(1.72MB / 3.15MB)、RAM 19.7%(64KB / 320KB)。

要改代码重新构建,见上一层的 `firmware/`(PlatformIO 工程)和 `../README.md`。
