# 格式解码测试集来源

`manifest.tsv` 是本测试集的唯一登记清单。每一行都固定文件 SHA-256、解码后尺寸、最少帧数和静态/动画类型；`runTests.ps1` 会调用正式程序的无界面解码入口逐项验证。

## 上游原始文件

| SourceId | 上游与固定版本 | 本仓库文件 | 许可 |
|---|---|---|---|
| `opencv-extra` | [opencv/opencv_extra](https://github.com/opencv/opencv_extra) commit `67aac02d4d8d2ae1e252ae253a341732d150879d` | `opencv-ordinary.bmp`、`opencv-float.exr`、`opencv-multipage.tif` | OpenCV 项目 Apache-2.0；该测试数据仓库未单列 LICENSE |
| `libavif` | [AOMediaCodec/libavif](https://github.com/AOMediaCodec/libavif) commit `b6fb1860837541d6e0c94386f2d09ba7c2341770` | `libavif-static.avif`、`libavif-animated.avifs`、`source/upstream-abc.png`（SHA-256 `5561862FBD409A3F86B02DB73EBB8572D0E2A307EB45ECF9017A1B2137B9F729`） | BSD-2-Clause，Copyright 2019 Joe Drago |
| `libheif` | [strukturag/libheif](https://github.com/strukturag/libheif) commit `2bc82b493dd8896fab3226f01977c7ac9d2ea3b8` | `libheif-rainbow.heic`；同一合法 HEIF 容器复制为 `.heif` 以覆盖扩展名分派 | LGPL-3.0（上游库及测试数据） |
| `libjxl-testdata` | [libjxl/testdata](https://github.com/libjxl/testdata) commit `73695d303670c90e4d506ea89d9901b081385089` | `libjxl-static.jxl`、`libjxl-animated.jxl` | CC-BY-4.0；修改仅限重命名 |
| `pinta365-blp` | [Pinta365/blp](https://github.com/Pinta365/blp) commit `b98905dea1bacfddc9bc174e3a0e31cf6fe72e69` | `blp-dxt1.blp` | MIT，Copyright 2025 Pinta |

## 本地生成的真实编码文件

- `generated-imagemagick`：以 libavif 的 `tests/data/abc.png` 为公开源图，缩放到 `160 × 80` 后由 ImageMagick 7.1.2-21 写成对应格式；别名扩展保留相同的合法文件字节。不是改扩展名伪装 PNG。
- `generated-wic`：同一源图由 Windows Imaging Component 的 `WmpBitmapEncoder` 编码为 JPEG XR。
- `generated-ffmpeg`：FFmpeg `testsrc2` 生成 `640 × 360` VP8 WebM，文件大于应用的视频最小缓冲阈值并含多帧。
- `generated-livp`：合法 JPEG 与 MOV 打包成 ZIP 结构的 `.livp`，验证 Live Photo 容器能够至少安全提取并显示主图。
- `generated-bundled-wp2`：以 `upstream-abc.png` 为输入，用项目当前捆绑的 WebP2 编码器生成，避免上游实验格式新旧版本不兼容造成假失败。
- `project-existing`、`user-svg-fixture`：复用仓库现有 GPL-3.0 项目素材和用户提供的 SVG 缺陷复现文件；PSD 的 `.psdt` 仅复制合法 PSD 字节以覆盖同一解码分支。

## 暂未登记

- `LEP`：解码器上游只提供库和解码示例，没有许可清晰的 `.lep` 小样本；因此自动覆盖检查只豁免 `lep`，没有用改后缀文件冒充。
- 相机 RAW：`supportRaw` 的 40 余个后缀共享 LibRaw 分支，但真正覆盖必须使用相机原始文件。为避免仓库膨胀、隐私和授权风险，本次没有下载来源/许可不明的 RAW；后续应按相机族逐步加入许可清晰、体积可控的代表样本。
