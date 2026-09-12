# YeImageViewer 图片语料测试集

这套语料回答的不是「某个扩展名能不能打开」，而是：解码结果对不对、异常文件会不会
把程序搞死、极端尺寸和奇怪路径能不能扛住。它与既有的 `test/format corpus`（按扩展
名锁定解码结果）和 `runTests.ps1`（窗口行为回归）并行，互不替代。

## 目录

| 目录 | 内容 | 判定标准 |
| --- | --- | --- |
| `00-reference/` | 人工视觉参考图 | 能解码，尺寸与生成时一致 |
| `01-modern-formats/` | AVIF / JXL / WebP / QOI / JP2 的 Smoke、Alpha、奇数尺寸、位深与编码变体 | 能解码，尺寸精确匹配；带透明的素材还要求 alpha 通道没被丢掉 |
| `12-exif/` | EXIF Orientation 1～8、无 EXIF、超大 EXIF | 八个方向解出的尺寸都应是基准的 600×400 |
| `13-dimensions/` | 尺寸边界：1×1、1×10000、19200×200、8191×8193 等 | 能解码，尺寸精确匹配 |
| `14-corrupt/` | 空文件、随机字节、截断、坏 CRC、伪造巨大尺寸头 | 允许解码失败，但绝不允许崩溃、卡死、界面冻结 |
| `15-extension-mismatch/` | 扩展名与真实格式不符、大小写混写、无扩展名 | 按内容识别，能正常解码 |
| `16-path-filename/` | 中文、日文、韩文、Emoji、空格、特殊字符、超长名、深层路径 | 能正常打开 |

`00-reference/orientation_text.png` 上下左右和四角都写了标记，旋转、镜像、错误裁切、
拉伸都能一眼看出来，也用作 `12-exif/` 的生成基准。

## 生成与运行

素材能算出来的一律不手工保存，由脚本重建，构造方式本身就是可审阅的代码。需要
ImageMagick。

```powershell
# 生成素材（可按类别，也可 -All）
.\scripts\generate-test-corpus.ps1 -All

# 扫描素材、锁定预期，生成 manifest.json
.\scripts\build-corpus-manifest.ps1

# 执行
.\tools\image-test-runner\run-tests.ps1 -Suite core
.\tools\image-test-runner\run-tests.ps1 -Suite modern
.\tools\image-test-runner\run-tests.ps1 -Suite corrupt
.\tools\image-test-runner\run-tests.ps1 -All
```

改动解码路径后，`-Suite modern` 与 `-Suite corrupt` 都要跑：前者验证解码结果，后者验证
异常文件不会把界面搞死，两者判定的是不同的东西。

报告写到 `artifacts/test-report/`：`summary.md` 给人看，`results.json` 给机器读。
存在 release-blocker 失败时退出码非 0，CI 可据此阻断发布。

## 两种探测方式

两者分工不同，互相补不了位：

- **decode**：`--decode-probe` 走正式解码分派，验证解码结果（成败、尺寸），不开窗口，快。
- **gui**：真开窗口，验证进程不崩溃、窗口能出现、消息泵还在转（界面没冻死）。

损坏素材默认两种都跑：解码失败是允许的，界面冻死不是。

## 预期从哪来

`manifest.json` 由脚本扫描生成，能正常解码的素材用 ImageMagick 读出真实尺寸锁定。
两处例外需要人工判断：

- `12-exif/` 的预期尺寸不能取自 `identify`——它读的是存储尺寸、不应用方向，
  orientation 5～8 会读成 400×600。正确应用方向后八张都应是 600×400，所以这一类在
  脚本里固定写死预期。
- 已知不支持的格式变体、已知限制写在 `manifest.overrides.json`，重新生成 manifest 时
  会被合并进来，不会被覆盖掉。

## 不进仓库的素材

超大素材放在 `test/bigimage/`，由 `.gitignore` 挡在仓库外（`moon_81M.png` 单文件就有
304 MB，仓库总共才 59 MB）。测试时本地缺失记为 `SKIPPED`，不算失败，也不会被悄悄跳过
而不留痕迹。

## 状态含义

| 状态 | 含义 |
| --- | --- |
| `PASS` | 实际行为符合预期 |
| `FAIL` | 已支持的能力发生回退，或出现崩溃、卡死 |
| `KNOWN_UNSUPPORTED` | 该格式或变体明确尚未实现 |
| `KNOWN_LIMITATION` | 能打开，但存在明确登记的已知限制 |
| `SKIPPED` | 素材不在本地，未执行 |
| `ERROR` | 测试框架自身出错——绝不记为 PASS |

## 现代格式专项（测试规格 Phase 3）

`01-modern-formats/` 按 Smoke / Alpha / 奇数尺寸 / 位深或编码变体四类建立，适用才建，
共 21 个用例，全部是本机真实编码，不是改扩展名伪装。

| 格式 | 变体 | 为什么值得单列 |
|---|---|---|
| AVIF | smoke、alpha、odd(199×101)、10bit、grayscale | 10 位与单色（yuv400）是 AV1 特有的编码路径 |
| JXL | smoke、alpha、odd、16bit、lossless | 无损写出 ISOBMFF 容器（`JXL ` box），有损是裸码流（`FF 0A`），解码入口不同 |
| WebP | smoke、alpha、odd、lossless、lowquality | 有损与无损是两条完全独立的解码路径 |
| QOI | smoke、alpha、odd | 规范只有 8 位 RGB/RGBA，没有位深变体可言 |
| JP2 | smoke、alpha、odd | |

损坏变体放在 `14-corrupt/`（`*_truncated.*`，截在 40%）。这些格式各有独立解码器
（AVIF→dav1d、JXL→libjxl、JP2→openjpeg、QOI 是手写解码），PNG/JPEG 的容错不能代表它们。
`qoi_truncated.qoi` 的预期是**解码成功**：QOI 流里没有长度字段和校验，截断后解码器会按
run-length 把剩余缓冲填完，不崩溃即为合格。

### 已知覆盖缺口：HEIC / HEIF / JXR / WP2 只有 Smoke

ImageMagick 7.1.2 的 HEIC 是只读的（`r--`），请它写 `.heic` **不报错**，而是写出一个 PNG
只换扩展名——实测头部是 `89 50 4E 47`。程序按内容嗅探，照样报「解码成功 160×80」。
这种文件冒充不了 HEIC 覆盖，所以本目录不含 HEIC 变体；HEIC/HEIF 的 Smoke 继续依赖
`test/format corpus` 里 libheif 的真实样本。JXR（ImageMagick 不支持）、WP2（同样不支持）
情况相同。要补齐必须引入真正的编码器或许可清晰的上游样本。

生成脚本里的 `Assert-NotFallbackEncoding` 逐个核对魔数，发现退化就删掉文件并报错，
避免以后有人无意间把这类文件当成新增覆盖提交进来。

### 透明通道的断言：别用 `%[opaque]`

`%[opaque]` 只判「是否存在非 255 的 alpha」。实测 `avif_smoke.avif` 本无透明，但 AV1 把恒
255 的 alpha 面压成了最低 254，`%[opaque]` 因此报 false。若据此断言「alpha 必须小于 255」，
解码器真把 alpha 丢了也能靠 254 蒙过去——断言就成了装饰。

现在看 `%[fx:minima.a]` 的最小值，三种情况分得很开：

```text
真有透明        0        ~ 0.0046
不透明          0.99     ~ 1
无 alpha 通道   2.7e+303（垃圾值）
```

只有最小值 ≤ 128（归一化 0.5）的素材才会被写入 `channels` 和 `maxMinAlpha` 预期，当前是
5 个 alpha 素材。`maxMinAlpha = 16` 是解码后允许的最小 alpha 上限：有损编码会让 0 变成
1~2，而丢掉 alpha 会得到 255，两者离得很远。

`--decode-probe` 为此追加了通道数与最小 alpha 两个字段（第 6、7 列）。旧版程序不输出这两
列，runner 记为未知并明确报「请用当前版本重新构建」，不会静默当成通过。

断言做过红灯验证：把不透明的 `webp_smoke.webp` 硬标上透明预期，通道数与 alpha 两条断言
都触发，退出码非 0。

## 一条教训：先证明素材是对的

`12-exif/` 最初用 ImageMagick 的 `-orient` 生成，测出 Orientation 5～8 失败，一度被当成
查看器的缺陷。实际是素材问题：源图是 PNG，没有 EXIF 结构，`-orient` 只把方向记在
ImageMagick 的内部属性里，产出的 JPEG 中 `EXIF:Orientation` 是空的——测的是素材缺陷，
不是产品行为。改为手工插入最小 EXIF APP1 段后，八个方向全部通过；把代码改动撤掉重测
同样全部通过，可见方向处理原本就是对的。

所以新增一类素材时，先用独立工具验证素材本身带着预期的属性，再让它参与判定：

```powershell
magick identify -format "%[EXIF:Orientation]" test\corpus\12-exif\exif_orientation_6.jpg
```
