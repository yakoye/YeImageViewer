# YeImageViewer 测试体系开发规范

> 文档用途：交给 Claude / Codex 等开发助手，专门用于为 YeImageViewer 建立图片格式兼容性、稳定性、异常处理、性能与回归测试体系。  
> 本文只针对“测试体系”开发，不要求改动图片浏览器现有产品功能，除非为了测试可观测性必须增加最小化测试接口或日志能力。

---

## 1. 目标

YeImageViewer 当前已经支持大量图片扩展名关联，覆盖普通位图、现代图片格式、HDR、动画、多页格式、专业图像格式以及大量相机 RAW 格式。

测试体系不能只验证“某个扩展名能否打开”，而应至少回答以下问题：

1. 文件关联是否正确。
2. 实际解码是否成功。
3. 图片显示结果是否正确。
4. EXIF Orientation、透明通道、色彩空间、ICC Profile、位深等信息是否正确处理。
5. 动画、多页、多图像容器是否稳定。
6. 损坏图片是否会导致程序崩溃、卡死、内存异常或界面失去响应。
7. 大图片、超长图片、超宽图片、奇数尺寸图片是否能稳定显示。
8. 大目录、连续切换、缩放、旋转、全屏等场景下是否存在明显性能问题。
9. 连续浏览大量图片时，内存、句柄、GDI/GPU 资源是否持续泄漏。
10. 每次发布新版本时，是否可以重复执行同一套回归测试。

最终目标是建立一套可重复、可扩展、可自动化的 **YeImageViewer Test Corpus + Test Runner + Release Gate**。

---

# 2. 测试原则

## 2.1 不以“扩展名能打开”作为支持格式的唯一标准

必须把以下两个概念分开：

### Association Support

用于验证：

- Windows 文件关联是否正确注册；
- 双击文件能否启动 YeImageViewer；
- 目标文件是否能正确传入程序；
- 大小写扩展名是否兼容；
- 文件路径中含空格、中文、特殊字符时是否仍能正常启动。

### Decode Support

用于验证：

- 文件内容是否真的能被正确识别；
- decoder 是否支持该格式的主要变体；
- 输出尺寸是否正确；
- 颜色是否明显异常；
- 透明是否正确；
- EXIF Orientation 是否正确；
- 遇到异常文件时是否安全失败。

不能因为 `.cr3` 已经注册为文件关联，就认为 CR3 已经完整支持。

---

## 2.2 测试需要覆盖“格式家族”，而不仅是扩展名

例如：

```text
.jpg
.jpeg
.jpe
.jfif
```

属于同一 JPEG 家族。

文件关联 Smoke Test 可以每种扩展名各准备一个文件，但真正的 JPEG 解码测试应集中到 JPEG 测试组中。

类似关系还包括：

```text
.tif / .tiff
.ico / .icon
.avif / .avifs
.heic / .heif
```

---

## 2.3 Release 测试最重要的底线

无论图片文件多么异常：

> YeImageViewer 都不能崩溃、卡死、死循环或永久失去响应。

错误文件可以显示“无法读取图片”，但以下操作必须继续可用：

- 上一张；
- 下一张；
- 打开其他图片；
- 关闭当前文件；
- 设置；
- 退出程序。

---

# 3. 当前扩展名覆盖范围

根据当前 YeImageViewer 设置页，测试体系至少要覆盖以下扩展名：

```text
3fr
apng
ari
arw
avif
avifs
bay
blp
bmp
cap
cr2
cr3
crw
dcr
dcs
dds
dib
dng
drf
eip
erf
exr
fff
gif
gpr
hdr
heic
heif
ico
icon
iiq
jfif
jp2
jpe
jpeg
jpg
jxl
jxr
k25
kdc
lep
livp
mdc
mef
mos
mrw
nef
nrw
orf
pbm
pcx
pef
pfm
pgm
pic
png
pnm
ppm
psd
psdt
ptx
pxm
qoi
r3d
raf
ras
raw
rw2
rwl
rwz
sr
sr2
srf
srw
svg
tga
tif
tiff
webm
webp
wp2
x3f
```

测试框架必须允许以后继续增加扩展名，而不需要重写整个测试系统。

---

# 4. 测试集整体结构

建议建立：

```text
tests/
└── image-corpus/
    ├── 00-reference/
    ├── 01-format-smoke/
    ├── 02-jpeg/
    ├── 03-png/
    ├── 04-modern/
    ├── 05-hdr/
    ├── 06-animation/
    ├── 07-multipage/
    ├── 08-raw/
    ├── 09-psd/
    ├── 10-svg/
    ├── 11-color-profile/
    ├── 12-exif/
    ├── 13-dimensions/
    ├── 14-corrupt/
    ├── 15-extension-mismatch/
    ├── 16-path-filename/
    ├── 17-large-image/
    ├── 18-performance/
    ├── manifest.json
    └── README.md
```

---

# 5. 三层测试包

为了避免 Git 仓库过大，测试数据建议分三层。

## 5.1 Core Corpus

目标：

```text
150～200 个文件
建议 < 300 MB
```

用途：

- 每次代码修改后执行；
- CI 执行；
- PR / Merge 前执行；
- 快速回归。

必须覆盖：

- 主要格式；
- EXIF；
- Alpha；
- 奇数尺寸；
- 方向；
- 基础损坏文件；
- Unicode 路径；
- 常见现代格式。

---

## 5.2 Extended Corpus

目标：

```text
400～800 个文件
约 1～5 GB
```

用途：

- RC 版本；
- 正式 Release 前；
- Decoder 更新后；
- 图像库升级后。

增加：

- 更多 TIFF；
- 更多 EXR；
- 更多 AVIF / HEIF / JXL；
- 多种压缩方式；
- 多页文件；
- 动画；
- 更大尺寸；
- 更多异常文件。

---

## 5.3 RAW Corpus

RAW 单独管理。

目标：

```text
100～300 个 RAW 文件
容量可能达到 10 GB～几十 GB
```

禁止直接把全部 RAW 文件提交 Git 仓库。

推荐方式：

```text
tests/raw_manifest.json
scripts/download_raw_testdata.*
```

RAW 文件按相机、格式、位深、压缩方式组织。

---

# 6. 00-reference：人工视觉参考图

自己生成一批非常简单、非常明确的参考图片。

建议包含：

```text
checkerboard.png
rgb_bars.png
grayscale_0_255.png
alpha_gradient.png
resolution_grid.png
one_pixel_grid.png
orientation_text.png
color_corners.png
```

其中 `orientation_text.png` 建议直接包含：

```text
TOP

LEFT                RIGHT

BOTTOM
```

四角增加：

```text
TL                  TR


BL                  BR
```

用途：

- 检测旋转；
- 检测镜像；
- 检测上下翻转；
- 检测错误 Crop；
- 检测拉伸；
- 检测缩放算法；
- 检测坐标问题。

---

# 7. 01-format-smoke：所有扩展名 Smoke Test

当前所有扩展名至少准备一个有效文件。

例如：

```text
01-format-smoke/
├── jpg/
│   └── valid.jpg
├── jpeg/
│   └── valid.jpeg
├── png/
│   └── valid.png
├── cr3/
│   └── valid.cr3
...
└── x3f/
    └── valid.x3f
```

测试要求：

1. 能启动 YeImageViewer。
2. 文件能传递到程序。
3. 程序不崩溃。
4. 文件要么成功显示，要么明确报告“不支持/无法读取”。
5. 报错后仍能继续使用软件。
6. 测试完成后进程可以正常退出。

---

# 8. 02-jpeg：JPEG 专项测试

至少包含：

```text
baseline_rgb.jpg
progressive.jpg
grayscale.jpg
cmyk.jpg

quality_1.jpg
quality_50.jpg
quality_100.jpg

1x1.jpg
1x10000.jpg
10000x1.jpg
101x99.jpg

srgb.jpg
adobe_rgb.jpg
icc_profile.jpg

no_exif.jpg
large_exif.jpg
embedded_thumbnail.jpg
```

另外必须准备：

```text
exif_orientation_1.jpg
exif_orientation_2.jpg
exif_orientation_3.jpg
exif_orientation_4.jpg
exif_orientation_5.jpg
exif_orientation_6.jpg
exif_orientation_7.jpg
exif_orientation_8.jpg
```

### 通过标准

EXIF Orientation 1～8 最终显示结果应该方向一致。

---

# 9. 03-png：PNG 专项测试

PNG 建议至少覆盖：

```text
rgb8.png
rgba8.png

gray1.png
gray2.png
gray4.png
gray8.png
gray16.png

rgb16.png
rgba16.png

palette_2colors.png
palette_16colors.png
palette_256colors.png
palette_alpha.png

transparent_full.png
transparent_half.png
alpha_gradient.png

interlaced.png
non_interlaced.png

gamma.png
icc_profile.png

1x1.png
1x10000.png
10000x1.png
101x99.png
```

额外生成：

```text
alpha_checker_test.png
```

需要包含：

```text
Alpha 100%
Alpha 75%
Alpha 50%
Alpha 25%
Alpha 0%
```

---

# 10. 04-modern：现代图片格式

目录建议：

```text
04-modern/
├── avif/
├── heic/
├── heif/
├── jxl/
├── webp/
├── qoi/
├── jxr/
└── wp2/
```

---

## 10.1 AVIF

至少覆盖：

```text
rgb_8bit.avif
rgb_10bit.avif
rgb_12bit.avif

yuv420.avif
yuv422.avif
yuv444.avif

monochrome.avif
alpha.avif

hdr.avif

odd_size.avif
rotation.avif
mirror.avif
```

如果当前 Decoder 并不支持其中某些变体，测试结果必须记录为：

```text
KNOWN_UNSUPPORTED
```

不能直接删除测试样本。

---

## 10.2 HEIC / HEIF

至少准备：

```text
heic_8bit.heic
heic_10bit.heic
heic_alpha.heic
heic_rotation.heic

heif_single.heif
heif_multi_image.heif
```

需要明确当前 YeImageViewer 行为：

- 显示 Primary Image；
- 或支持多图切换；
- 或当前只保证第一张。

测试系统要把行为记录在 manifest，不允许靠人工猜测。

---

## 10.3 JPEG XL

建议：

```text
lossy.jxl
lossless.jxl
grayscale.jxl
alpha.jxl
hdr.jxl
icc.jxl
animation.jxl
jpeg_reconstruction.jxl
```

---

## 10.4 WebP

至少：

```text
lossy.webp
lossless.webp
alpha.webp
animated.webp
animated_alpha.webp
odd_size.webp
1x1.webp
```

---

# 11. 05-hdr：HDR / Float 测试

目录：

```text
05-hdr/
├── exr/
├── hdr/
└── pfm/
```

---

## 11.1 OpenEXR

建议准备：

```text
half_float.exr
float32.exr

rgb.exr
rgba.exr

zip.exr
piz.exr
rle.exr

scanline.exr
tiled.exr

negative_values.exr
high_dynamic_range.exr
unusual_data_window.exr
```

重点检查：

- 不崩溃；
- 尺寸正确；
- Data Window / Display Window 不导致错误；
- 负值、Float、Half Float 能安全处理；
- 不出现越界或巨额内存申请。

---

# 12. 06-animation：动画测试

涉及：

```text
gif
apng
webp
avifs
```

如 WebM 在项目中按动态图像处理，也可加入。

建议：

```text
06-animation/
├── gif/
│   ├── 2frames.gif
│   ├── 100frames.gif
│   ├── transparency.gif
│   ├── disposal_1.gif
│   ├── disposal_2.gif
│   ├── disposal_3.gif
│   ├── loop_forever.gif
│   ├── loop_once.gif
│   ├── frame_10ms.gif
│   └── frame_1000ms.gif
│
├── apng/
│   ├── alpha.apng
│   ├── disposal.apng
│   └── 100frames.apng
│
└── webp/
    ├── animated.webp
    ├── animated_alpha.webp
    └── different_frame_sizes.webp
```

如果当前 YeImageViewer 不播放动画：

> 测试标准改为“稳定显示第一帧，不崩溃、不死循环”。

以后支持动画后，再切换期望结果。

---

# 13. 07-multipage：多页 / 多图像容器

至少覆盖：

```text
multi_page.tiff
multi_size.ico
multi_image.heif
multi_image.avif
```

ICO 建议一个文件内同时包含：

```text
16x16
24x24
32x32
48x48
64x64
128x128
256x256
```

测试必须记录程序策略：

- 选最大图；
- 选最佳匹配图；
- 选第一图；
- 或允许切换。

---

# 14. 08-raw：RAW 测试

RAW 不能只按扩展名一张图。

需要按“厂商 + 位深 + 压缩方式 + RAW 模式”建立样本。

---

## 14.1 Sony ARW

例如：

```text
Sony/
├── 12bit_compressed.arw
├── 12bit_uncompressed.arw
├── 14bit_compressed.arw
├── 14bit_lossless.arw
├── 14bit_uncompressed.arw
├── fullframe.arw
└── apsc_crop.arw
```

---

## 14.2 Nikon NEF

例如：

```text
Nikon/
├── 12bit.nef
├── 14bit.nef
├── compressed.nef
├── lossless.nef
├── uncompressed.nef
├── small_raw.nef
├── medium_raw.nef
└── full_raw.nef
```

---

## 14.3 Canon

```text
Canon/
├── cr2_raw.cr2
├── cr2_mraw.cr2
├── cr2_sraw.cr2
├── cr3_raw.cr3
└── cr3_craw.cr3
```

---

## 14.4 Fujifilm

```text
Fuji/
├── uncompressed.raf
└── compressed.raf
```

---

## 14.5 其他 RAW

逐步覆盖当前已有扩展名：

```text
3fr
ari
bay
cap
crw
dcr
dcs
dng
drf
eip
erf
fff
gpr
iiq
k25
kdc
lep
mdc
mef
mos
mrw
nrw
orf
pef
ptx
r3d
raw
rw2
rwl
rwz
sr
sr2
srf
srw
x3f
```

---

# 15. 09-psd：PSD / PSDT

建议：

```text
flattened.psd
rgb.psd
cmyk.psd
grayscale.psd

1_layer.psd
50_layers.psd

hidden_layers.psd
transparency.psd

huge_canvas.psd

text_layer.psd
smart_object.psd
```

YeImageViewer 不需要实现 Photoshop。

最低要求是：

- 能读取 composite / preview；
- 能安全失败；
- 不崩溃；
- 不因几十层文件产生异常内存。

---

# 16. 10-svg：SVG 测试

SVG 不是位图，需要单独测试。

建议：

```text
simple.svg
text.svg
chinese_text.svg

gradient.svg
transparency.svg

viewbox.svg
no_width_height.svg

1x1.svg
huge_viewbox.svg

embedded_png.svg
embedded_jpeg.svg

clip_path.svg
mask.svg

invalid_external_resource.svg
```

重点：

- SVG 无 width/height；
- 巨大 viewBox；
- 内嵌图片；
- 中文文字；
- 外部资源失效；
- clip / mask；
- 异常 SVG。

外部资源加载失败时，YeImageViewer 不能崩溃或长时间卡死。

---

# 17. 11-color-profile：色彩管理测试

至少准备：

```text
srgb.jpg
adobe_rgb.jpg
display_p3.jpg
cmyk.jpg

srgb.png
display_p3.png

icc_valid.jpg
icc_valid.png
icc_missing.jpg
icc_corrupt.jpg
```

测试重点：

- 色彩明显不偏色；
- ICC Profile 不导致崩溃；
- 无 ICC 时使用合理默认；
- 异常 ICC 能安全处理。

如果当前产品不实现完整色彩管理，也应该把测试项保留并标记：

```text
KNOWN_LIMITATION
```

---

# 18. 12-exif：EXIF 与 Metadata

至少检查：

```text
Orientation
Width
Height
DateTime
Camera
Lens
GPS
Embedded Thumbnail
ColorSpace
ICC Profile
```

如果 YeImageViewer 当前没有 EXIF 信息显示界面，至少验证：

- EXIF 不导致崩溃；
- Orientation 正确；
- 超大 EXIF 安全；
- 损坏 EXIF 安全。

---

# 19. 13-dimensions：尺寸边界测试

必须生成：

```text
1x1
1x2
2x1

1x10000
10000x1

101x99
4095x4097
8191x8193

19200x200
200x19200
```

这些图片建议同时覆盖：

```text
jpg
png
webp
avif
```

重点测试：

- 超宽图；
- 超长图；
- 奇数尺寸；
- texture 对齐；
- stride；
- 大图缩放；
- 滚动条；
- Fit to Window；
- 100% 显示。

---

# 20. 14-corrupt：损坏文件测试

这一组建议作为 Release 阻断测试。

目录：

```text
14-corrupt/
├── empty.jpg
├── empty.png
├── random_bytes.jpg
├── jpeg_cut_1percent.jpg
├── jpeg_cut_10percent.jpg
├── jpeg_cut_50percent.jpg
├── jpeg_cut_99percent.jpg
├── png_bad_signature.png
├── png_bad_crc.png
├── png_missing_iend.png
├── gif_bad_frame.gif
├── webp_bad_chunk.webp
├── tiff_bad_ifd.tif
├── raw_truncated.cr3
└── huge_dimension_header.png
```

### 通过标准

可以显示错误提示，但以下情况一律 Fail：

```text
Crash
Hang
Deadlock
Infinite Loop
Permanent UI Freeze
Memory Explosion
Unhandled Exception
```

---

# 21. 15-extension-mismatch：扩展名欺骗

准备：

```text
jpeg_named_png.png
png_named_jpg.jpg
webp_named_jpg.jpg
avif_named_png.png

real_jpeg.no_extension

UPPERCASE.JPG
MiXeD.JpEg
```

建议 YeImageViewer 的策略：

```text
Extension
    ↓
用于文件筛选和文件关联

Magic Number / Signature
    ↓
用于实际格式判断
```

测试要记录：

- 扩展名不匹配时是否仍能识别；
- 无扩展名是否能打开；
- 大小写是否兼容。

---

# 22. 16-path-filename：文件名与路径测试

至少：

```text
中文图片.jpg
中文 空格 图片.png
日本語画像.webp
한글사진.jpg

😀.png
🌍🌊🌌.jpg

a b c.jpg
a.b.c.jpg

#test.png
[test].jpg
(test).webp
'quote'.png
```

还需要：

```text
VeryLongFilename_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.jpg
```

并建立深路径：

```text
C:\图片测试\中文\很深\很深\很深\很深\很深\...\图片.jpg
```

重点测试：

- Unicode；
- 空格；
- Emoji；
- 特殊字符；
- Windows 长路径；
- 双击打开；
- 拖放；
- 右键打开；
- 文件关联启动。

---

# 23. 17-large-image：超大图片测试

建议至少包含：

```text
8K
16K
约 50 MP
约 100 MP
超长全景图
超宽截图
超长截图
```

建议样例尺寸：

```text
7680x4320
15360x8640
12000x8000
20000x5000
5000x20000
```

测试：

- 首次打开时间；
- Fit to Window；
- 100%；
- 快速缩放；
- 拖动画布；
- 连续切换；
- 关闭后内存是否回落。

---

# 24. 18-performance：性能与压力测试

建议建立三档数据集。

---

## 24.1 Small

```text
100 张
```

用于日常目录操作。

---

## 24.2 Medium

```text
1000 张
```

测试：

- 目录扫描；
- 缩略图；
- 预读取；
- 缓存；
- 上一张/下一张；
- 快速切换。

---

## 24.3 Stress

```text
10000 张
```

建议比例：

```text
JPG       5000
PNG       2000
WebP      1000
HEIC       500
AVIF       500
RAW        500
GIF        300
其他       200
```

自动化操作：

```text
打开目录
↓
连续切换图片
↓
按住 Right 10 秒
↓
快速滚轮缩放
↓
进入全屏
↓
退出全屏
↓
旋转
↓
继续切换
↓
关闭程序
```

---

# 25. 性能指标

至少记录：

```text
Cold Start
Warm Start
首次图片显示时间
切换下一张时间
目录扫描时间
缩略图生成时间
```

系统资源：

```text
CPU
Working Set
Private Bytes
GPU Memory
Disk IO
Handle Count
GDI Objects
USER Objects
```

尤其关注：

```text
第 1 张
第 100 张
第 500 张
第 1000 张
第 5000 张
```

资源变化。

例如：

```text
200 MB
260 MB
275 MB
280 MB
282 MB
```

通常可以认为趋于稳定。

如果：

```text
200 MB
350 MB
600 MB
1.2 GB
2.4 GB
```

则高度疑似存在：

- bitmap cache 泄漏；
- decoder object 泄漏；
- GPU texture 泄漏；
- COM 对象泄漏；
- GDI 资源泄漏；
- 预加载缓存无上限。

---

# 26. Golden Reference / 对照程序

建议使用以下工具作为参考结果：

```text
ImageMagick
ExifTool
Windows Photos
IrfanView
ImageGlass
XnView MP
```

其中：

## ImageMagick

用于检查：

```text
Geometry
Colorspace
Depth
Alpha
ICC
EXIF
Orientation
```

建议测试脚本可调用：

```powershell
magick identify image.jpg
magick identify -verbose image.jpg
```

---

## ExifTool

用于比对：

```text
Camera
Date
GPS
Orientation
ColorSpace
ICC Profile
Width
Height
```

例如：

```powershell
exiftool image.jpg
```

---

# 27. Manifest 设计

每一个测试文件必须有机器可读的预期结果。

建议：

```json
{
  "id": "jpeg-exif-orientation-6",
  "path": "02-jpeg/exif_orientation_6.jpg",
  "format": "jpeg",
  "extension": "jpg",
  "category": "exif",
  "expected": {
    "open": true,
    "crash": false,
    "width": 1200,
    "height": 1800,
    "orientationNormalized": true
  },
  "severity": "release-blocker",
  "notes": "EXIF Orientation=6，最终显示方向应正常。"
}
```

异常文件：

```json
{
  "id": "corrupt-jpeg-cut-50",
  "path": "14-corrupt/jpeg_cut_50percent.jpg",
  "format": "jpeg",
  "category": "corrupt",
  "expected": {
    "crash": false,
    "hang": false,
    "uiResponsive": true,
    "allowDecodeFailure": true
  },
  "severity": "release-blocker"
}
```

---

# 28. 测试结果状态

统一使用：

```text
PASS
FAIL
KNOWN_UNSUPPORTED
KNOWN_LIMITATION
SKIPPED
ERROR
```

含义：

### PASS

实际行为符合预期。

### FAIL

已支持功能发生回退，或者出现崩溃、卡死、严重错误。

### KNOWN_UNSUPPORTED

该格式/变体目前明确没有实现。

### KNOWN_LIMITATION

能够打开，但存在明确已知限制。

### SKIPPED

因环境或测试数据缺失未执行。

### ERROR

测试框架自身失败。

---

# 29. Test Runner

建议实现：

```text
tools/
└── image-test-runner/
```

功能：

1. 读取 `manifest.json`。
2. 自动启动 YeImageViewer。
3. 打开目标图片。
4. 设置超时时间。
5. 检查程序是否崩溃。
6. 检查是否失去响应。
7. 记录退出码。
8. 收集日志。
9. 可以自动截图。
10. 输出测试报告。

命令示例：

```powershell
.\run-tests.ps1 -Suite core
.\run-tests.ps1 -Suite corrupt
.\run-tests.ps1 -Suite raw
.\run-tests.ps1 -Suite performance
.\run-tests.ps1 -All
```

---

# 30. 自动截图测试

如果程序支持稳定窗口布局，建议增加 Screenshot Regression。

流程：

```text
打开 reference 图片
↓
窗口固定尺寸
↓
等待图片加载完成
↓
截图图片区
↓
与 golden screenshot 对比
```

不要要求所有像素 100% 相同。

允许少量差异：

- DPI；
- GPU；
- 抗锯齿；
- 色彩转换。

可以采用：

```text
SSIM
Perceptual Hash
Pixel Difference Threshold
```

---

# 31. 建议增加测试日志能力

为了测试稳定性，可以为 YeImageViewer 增加：

```text
--test-mode
--log-file
--open <file>
--exit-after-load
```

例如：

```powershell
YeImageViewer.exe `
  --test-mode `
  --open "test.png" `
  --log-file result.log `
  --exit-after-load
```

如果不希望影响正式用户，可以只在 Debug / Test Build 开启。

日志至少记录：

```text
File path
Detected format
Decoder
Width
Height
Bit depth
Frame count
Decode start
Decode finish
Decode duration
Error code
Exception
```

---

# 32. Crash / Hang 判定

建议：

```text
单张普通图片：
5 秒加载超时

RAW：
15～30 秒

大图：
30 秒

Stress：
根据场景单独设置
```

超过超时：

```text
HANG / TIMEOUT
```

如进程退出码异常：

```text
CRASH
```

如窗口长时间 Not Responding：

```text
UI_FREEZE
```

---

# 33. Release Gate

建议最终把 Release Gate 固定为：

## Gate A：Association

```text
82/82 当前扩展名关联注册正确
```

---

## Gate B：Core Decode

Core Corpus：

```text
100% Release Blocker 测试通过
```

允许：

```text
KNOWN_UNSUPPORTED
```

但必须是明确登记的已知状态。

---

## Gate C：Corrupt

所有 Corrupt 文件：

```text
0 Crash
0 Hang
0 Deadlock
0 Permanent UI Freeze
```

这是最严格的一层。

---

## Gate D：Performance

至少：

```text
1000 张连续浏览无明显持续内存泄漏
10000 张目录可以正常扫描
快速切换不会导致崩溃
```

---

## Gate E：Regression

与上一个稳定版本比较：

```text
不能新增 FAIL
```

如必须接受回退，必须在 Release Note 明确说明。

---

# 34. 测试报告

每次 RC / Release 生成：

```text
artifacts/
└── test-report/
    ├── summary.md
    ├── results.json
    ├── failures/
    ├── screenshots/
    └── performance.csv
```

`summary.md` 建议：

```markdown
# YeImageViewer vX.Y.Z Test Report

## Summary

Total: 238
PASS: 220
FAIL: 2
KNOWN_UNSUPPORTED: 12
KNOWN_LIMITATION: 4

## Release Blocker

PASS: 87
FAIL: 0

## Corrupt Corpus

Crash: 0
Hang: 0
Decode Failure: 18

## Performance

1000 images:
Peak RAM: xxx MB
Final RAM: xxx MB

10000 images:
Scan Time: xx s

## Regressions

None
```

---

# 35. 测试数据生成脚本

可以自动生成的文件不要手工保存。

建议建立：

```text
scripts/
├── generate_reference_images.py
├── generate_dimension_images.py
├── generate_corrupt_images.py
├── generate_mass_dataset.py
└── download_external_corpus.py
```

自动生成：

```text
1x1
1x10000
10000x1
101x99
19200x200
200x19200
alpha gradient
RGB bars
checkerboard
损坏文件
1000 张目录
10000 张目录
```

---

# 36. 外部测试集来源

建议测试脚本允许下载外部公开测试资源，但要注意 License。

优先使用：

```text
EXIF Orientation Examples
Pillow Tests/images
libpng PNGSuite
libwebp-test-data
AVIF Sample Images
OpenEXR test images
libjxl testdata
raw.pixls.us
```

外部文件不要无脑提交到主仓库。

建议：

```text
tests/external_manifest.json
```

记录：

```text
source
url
license
sha256
local path
```

---

# 37. CI 策略

CI 不运行完整 RAW 和 10000 张 Stress。

建议：

## 每次 Commit / PR

```text
Core Corpus
Corrupt Core
JPEG
PNG
EXIF
Path
```

---

## Nightly

```text
Extended Corpus
Animation
HDR
Modern Formats
```

---

## Release Candidate

```text
Core
Extended
RAW
Corrupt Full
Performance 1000
Performance 10000
```

---

# 38. 对 Claude 的具体开发要求

请严格按照以下顺序实施，不要一次性把所有逻辑塞到一个脚本中。

## Phase 1：测试框架

完成：

1. `tests/image-corpus/` 目录结构。
2. `manifest.json` schema。
3. Test Runner 基础框架。
4. PASS / FAIL / KNOWN_* 状态。
5. Markdown + JSON 报告。
6. 单文件打开测试。
7. Crash / Timeout 检测。

此阶段不要大规模下载 RAW。

---

## Phase 2：Core Corpus

完成：

1. Reference。
2. JPEG。
3. PNG。
4. EXIF。
5. Alpha。
6. Dimension。
7. Path/Filename。
8. Extension mismatch。
9. 基础 Corrupt。

目标：

```text
Core >= 100 cases
```

---

## Phase 3：Modern Formats

完成：

```text
AVIF
HEIC
HEIF
JXL
WebP
QOI
JXR
WP2
```

每个格式至少建立：

```text
Smoke
Alpha
Odd Size
Bit Depth / Codec Variant
Corrupt
```

适用项才测试。

---

## Phase 4：HDR / Professional

完成：

```text
EXR
HDR
PFM
PSD
PSDT
TIFF
ICO
SVG
```

---

## Phase 5：RAW

建立：

```text
RAW manifest
download script
camera/vendor 分类
```

优先：

```text
Canon
Nikon
Sony
Fujifilm
Panasonic
Pentax
Olympus/OM
Hasselblad
Phase One
Sigma
```

---

## Phase 6：Performance

完成：

```text
100
1000
10000
```

图片生成和压力测试。

记录：

```text
CPU
RAM
Handles
GDI
启动时间
首次显示
切换时间
扫描时间
```

---

## Phase 7：Release Gate

增加一条统一命令：

```powershell
.\run-release-tests.ps1
```

输出：

```text
PASS / FAIL
```

并生成：

```text
summary.md
results.json
performance.csv
```

如存在 Release Blocker FAIL：

```text
脚本退出码必须非 0
```

这样 CI 可以阻止 Release。

---

# 39. 禁止事项

开发测试体系时注意：

1. 不允许为了让测试通过而隐藏真实 Decoder 错误。
2. 不允许遇到打不开的测试文件就删掉。
3. 不允许把 KNOWN_UNSUPPORTED 当 PASS。
4. 不允许只测试 JPG/PNG 就声称 82 种格式测试完成。
5. 不允许把几 GB RAW 全塞入 Git。
6. 不允许性能测试只看程序“没有崩”。
7. 不允许测试框架自身异常时返回 PASS。
8. 不允许超时后残留大量 YeImageViewer 进程。
9. 不允许测试结果依赖人工记忆。
10. 所有预期结果必须进入 Manifest。

---

# 40. 最终验收标准

完整测试体系最终应达到：

```text
82 个当前关联扩展名都有 Smoke Test
+
JPEG / PNG 等核心格式有变体测试
+
AVIF / HEIF / JXL / WebP 等现代格式有专项测试
+
EXR / HDR / PFM 有 HDR 测试
+
RAW 有独立大型测试集
+
EXIF Orientation 1～8 全覆盖
+
Alpha / ICC / Color Profile 有测试
+
奇数尺寸、超宽、超高、大图有测试
+
损坏文件测试
+
Unicode / Emoji / 长路径测试
+
扩展名欺骗测试
+
动画 / 多页测试
+
100 / 1000 / 10000 张性能测试
+
Crash / Hang 自动检测
+
自动生成 Markdown / JSON 报告
+
Release Blocker 自动判定
```

测试体系的核心目标不是：

> “证明 YeImageViewer 能打开很多图片。”

而是：

> “让每一次 YeImageViewer 修改，都能够明确知道有没有破坏原来已经工作的图片格式、解码行为、稳定性和性能。”

这套测试应该成为 YeImageViewer 后续所有版本发布的固定基础设施。
