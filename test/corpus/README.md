# YeImageViewer 图片语料测试集

这套语料回答的不是「某个扩展名能不能打开」，而是：解码结果对不对、异常文件会不会
把程序搞死、极端尺寸和奇怪路径能不能扛住。它与既有的 `test/format corpus`（按扩展
名锁定解码结果）和 `runTests.ps1`（窗口行为回归）并行，互不替代。

## 目录

| 目录 | 内容 | 判定标准 |
| --- | --- | --- |
| `00-reference/` | 人工视觉参考图 | 能解码，尺寸与生成时一致 |
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
.\tools\image-test-runner\run-tests.ps1 -Suite corrupt
.\tools\image-test-runner\run-tests.ps1 -All
```

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
