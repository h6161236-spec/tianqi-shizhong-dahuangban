# U8g2_for_TFT_eSPI(vendor 精简版)

上游:https://github.com/Bodmer/U8g2_for_TFT_eSPI (v1.7.0)

## 为什么不用 lib_deps 直接拉

**上游 `library.properties` 里写着 `depends=TFT_eSPI Library`。**
这个包名在 PlatformIO registry 里不存在 —— 真名是 `TFT_eSPI`,没有 " Library"
后缀。结果每次构建 PlatformIO 都会去联网找一个找不到的包:

```
Library Manager: Installing TFT_eSPI Library
HTTPClientError:
```

网络通的时候这个错误被后续流程盖过去了,网一断就直接构建失败。所以 vendor
进来,删掉那一行。TFT_eSPI 本来就在 `platformio.ini` 的 `lib_deps` 里,
不需要这条依赖声明。

## 改了什么

1. `library.properties` —— 删掉 `depends=TFT_eSPI Library`
2. `src/u8g2_fonts.c` —— 上游 1436 个字体 / 27MB,这里只留本项目用的 4 个:

   | 字体 | 用途 | Flash |
   |---|---|---|
   | `u8g2_font_wqy12_t_gb2312` | 12px 中文全字库 | 204 KB |
   | `u8g2_font_wqy16_t_gb2312` | 16px 中文全字库 | 311 KB |
   | `u8g2_font_t0_14b_tn` | 14px 粗体数字 | 237 B |
   | `u8g2_font_t0_22b_tn` | 22px 粗体数字 | 292 B |
   | | **合计** | **515 KB** |

   链接器本来就会丢掉没引用的字体,所以这一步不省 Flash,省的是每次干净
   构建都要编译 27MB 源文件的时间。

`U8g2_for_TFT_eSPI.cpp/.h`、`u8g2_fonts.h` 与上游逐字节一致。头文件里 1436 个
字体的 extern 声明也全部保留 —— 声明不占空间,以后加字体不用改头文件。

## 要加字体

1. 在 `firmware/tools/trim_u8g2_fonts.py` 的 `FONTS` 列表里加字体名
2. `python tools/trim_u8g2_fonts.py` 重新生成 `src/u8g2_fonts.c`
3. 同步 `firmware/src/text.cpp` 的 `FONTS[]` 表和 `text.h` 的 `TxtFont` 枚举

脚本需要上游源文件在 `.pio/libdeps/cyd/U8g2_for_TFT_eSPI/`。如果已经清过,
临时把 `lib_deps` 里的 git 行加回去跑一次 `pio run` 下载,或用 `--src` 指路径。

字体版权归 olikraus / 文泉驿 等原作者,见 `LICENSE`。
