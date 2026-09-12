# YeImageViewer 图片语料测试集

这套语料回答的不是「某个扩展名能不能打开」，而是：解码结果对不对、异常文件会不会
把程序搞死、极端尺寸和奇怪路径能不能扛住。它与既有的 `test/format corpus`（按扩展
名锁定解码结果）和 `runTests.ps1`（窗口行为回归）并行，互不替代。

## 目录

| 目录 | 内容 | 判定标准 |
| --- | --- | --- |
| `00-reference/` | 人工视觉参考图 | 能解码，尺寸与生成时一致 |
| `01-modern-formats/` | AVIF / JXL / WebP / QOI / JP2 的 Smoke、Alpha、奇数尺寸、位深与编码变体 | 能解码，尺寸精确匹配；带透明的素材还要求 alpha 通道没被丢掉 |
| `02-professional/` | EXR / Radiance HDR / PFM / TIFF / PSD / ICO 的压缩、位深、透明、多页变体 | 同上；另外不透明的素材必须解出不透明 |
| `_local/03-raw/` | 28 个厂商的相机 RAW（不进仓库，按需下载） | 能解码，转正后尺寸精确匹配；确实不支持的格式记 KNOWN_UNSUPPORTED |
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
.\tools\image-test-runner\run-tests.ps1 -Suite pro
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

## HDR 与专业格式（测试规格 Phase 4）

`02-professional/` 共 17 个用例：

| 格式 | 变体 | 为什么值得单列 |
|---|---|---|
| EXR | color、alpha | |
| Radiance HDR | radiance | |
| PFM | color（魔数 `PF`）、gray（魔数 `Pf`） | 两种魔数走不同的解析分支 |
| TIFF | lzw、deflate、uncompressed、16bit、alpha、multipage | 三种压缩各走不同的 libtiff 路径；多页按动图处理 |
| PSD | 8bit、16bit、alpha | |
| ICO | multisize、single、alpha | |

### 当前有一个真实缺陷在阻断发布

`psd_16bit.psd` 是 **FAIL**，这不是夹具问题，也不要把它改成 PASS：

```text
16 位 PSD 带透明通道时，alpha 整层解码为 0，图在界面上完全不可见。
```

证据链（都可复现）：

```text
同一张 alpha 均值 0.5 的源图，导出三种位深的 PSD：
  8 位   ImageMagick 读 alpha 均值 0.5   程序解出 meanAlpha 128  ✓
  16 位  ImageMagick 读 alpha 均值 0.5   程序解出 meanAlpha   0  ✗
不透明源图同样如此：8 位得 255/255，16 位得 0/0（整层全零，不是个别像素）。
把 16 位 PSD 叠到红底上，红色不透出来 —— 独立证实文件里的 alpha 是不透明的。
16 位 TIFF 正常（255/255），所以不是通用的 16 位问题。
连测 4 次结果一致，不是未初始化内存。
3 通道的 PSD（无透明通道）不受影响：那条分支直接填 alphaMax。
```

定位：`loadPSD` 对 4 通道 PSD 走 `isRgb = false` 分支，把 `imageData->images[3]` 当 alpha
读。8 位那条能读到正确数据，16 位读到的恒为零。`psd_sdk` 在本仓库只有头文件，实现是
预编译的 `Psd_MT.lib`，根因在库内，无法就地修改。

未确认的部分：手上只有 ImageMagick 写出的 16 位 PSD，**没有 Photoshop 原生文件**，
所以尚不能断定真实相机/设计稿工作流产出的 16 位 PSD 是否同样受影响。补一个原生样本
是确认影响面的第一步。

可选的兜底（尚未实施，会改变渲染行为，需先定夺）：检测 alpha 整层为零时视为不透明。
这能让不透明的 16 位 PSD 恢复可见，代价是真正带透明的 16 位 PSD 会丢掉透明度。

### 这一类踩过的三个坑，都已由脚本挡住

**一、源图是黑白渐变时，EXR / PFM 会被静默存成灰度**，「彩色」变体是假的。现在 HDR
源图用彩色渐变，PFM 另外核对魔数 `PF`（彩色）与 `Pf`（灰度）确实不同。

**二、Q16 构建下不写 `-depth`，TIFF / PSD 一律存成 16 位**，于是「8 位」和「16 位」两个
变体逐字节相同，测试拿同一份数据跑两遍，看着覆盖变多、实际什么也没多测。现在每个变体
都显式指定位深，并由 `Assert-DistinctVariants` 兜底：同目录下出现内容完全相同的素材就报错。

**三、编码器不支持时静默降级。** 请 ImageMagick 写 32 位 PSD，它写出的是 16 位，与 16 位
变体逐字节相同；`icon:auto-resize` 给小于 16 的尺寸会写出 **0 字节**文件。后者一度让人以为
「程序打不开含 8×8 的 ICO」，实际是空文件，程序报错完全正确。因此这里没有 32 位 PSD 变体。

### 多尺寸 ICO：程序把各尺寸横向拼成一条

本程序显示多尺寸 ICO 时，把各尺寸并排拼成一张图：**宽 = 各尺寸之和，高 = 最大尺寸**。
`identify` 读的是首帧，两者对不上，所以预期写在 `manifest.overrides.json` 里。拼条规律用
16、16+32、32+64、16+32+48+64 四组独立验证过。拼条里小图标上下有留白，留白是透明的，
所以 `ico_multisize.ico` 的最小 alpha 必须贴近 0——若变成不透明，说明留白被填成了实色。

同理多页 TIFF：程序按动图处理，3 页即 3 帧，而 `identify` 对 `[0]` 只报 1 帧，帧数期望也在
overrides 里。

## 相机 RAW（测试规格 Phase 5）

素材不在仓库里，运行下面的脚本下到本地（约 356 MB）：

```powershell
.\scripts\fetch-raw-corpus.ps1                # 下载全部
.\scripts\fetch-raw-corpus.ps1 -MaxFileMB 12  # 只要小文件
.\scripts\fetch-raw-corpus.ps1 -Verify        # 只校验已有文件的 sha256
.\scripts\fetch-raw-corpus.ps1 -Relabel       # 不联网，只用本地文件重算尺寸/方向/标记
```

来源是 [raw.pixls.us](https://raw.pixls.us/) 的 **CC0**（公有领域）样本库，每个扩展名取体积
最小的那一个：覆盖面最大、下载量最小。落到 `test/corpus/_local/03-raw/`（`.gitignore` 已挡）。

**文件不进仓库，但预期进仓库。** 预期（sha256、体积、转正后尺寸）写在
`test/corpus/raw-expectations.json` 并提交。不这样做的话，没下过素材的人重建 manifest 时
RAW 用例会凭空消失，而报告上看不出少测了东西。本地缺文件时 runner 记 SKIPPED，
如实显示「素材不存在于本地」。

`supportRaw` 声明 38 个扩展名，其中 **28 个**有 CC0 样本。以下 10 个样本库里没有，
如实记为缺口，没有用改后缀的文件冒充：`bay cap dcs drf eip k25 mef ptx r3d rwz`。

当前结果：**25 PASS，3 KNOWN_UNSUPPORTED，0 FAIL**。

### 三个确实解不开的格式

| 扩展 | 文件头 | 独立工具的判定 |
|---|---|---|
| `.ari` | `ARRI` 正常 | ImageMagick 连 ARI 解码模块都没有（缺 `IM_MOD_RL_ARI_.dll`）；LibRaw 不支持 ARRIRAW |
| `.gpr` | `II*.` 正常 | ImageMagick 报 `Nonstandard tile length`；GPR 用 VC-5 压缩，需 GoPro 的 GPR SDK |
| `.x3f` | `FOVb` 正常 | ImageMagick 报 `Unsupported file format or not RAW file`；Foveon 需 LibRaw 带 X3F 支持 |

这三个记 `KNOWN_UNSUPPORTED`——按规格既不算通过也不算失败，更不能因为打不开就把素材删掉。
标记和理由都写进 manifest，不靠人工记忆。

**反向断言**：这三个哪天能解开了，runner 会报 FAIL 并提示「标记过期，请去掉」。
过期的「不支持」标记会长期掩盖真实回归，所以必须让它显性失败。已做红灯验证：
把能正常解码的 `.cr2` 硬标成不支持，立刻 FAIL 并给出该提示。

### RAW 的 severity 分两档

能解的 RAW 是 **release-blocker**：38 个扩展名是主打功能，Canon CR2 之类突然解不开必须挡住发布。
已知不支持的那三个是 `normal`——它们的 FAIL 只意味着「标记该更新了」，不该因此拦住版本。

### 又踩了一次方向的坑

`.mos`（Leaf Aptus 22）一度被判成宽高颠倒：`identify` 报 `4008x5344`，程序报 `5344x4008`。

**程序是对的。** `identify` 给的是**存储**尺寸，方向单独放在 `Orientation` 字段里（这张是
`RightTop`，即需旋转 90°）；LibRaw 默认 `user_flip=-1`，会按元数据把图转正，所以程序输出的是
**已转正**的尺寸。这和 `12-exif` 那一类是同一个坑，README 早就记过，RAW 这条路上又踩了一次——
从 `identify` 推导预期时忘了补方向。

现在 `fetch-raw-corpus.ps1` 自己应用方向：读到 `RightTop / RightBottom / LeftTop / LeftBottom`
就把宽高互换再登记。

### 素材哈希会在每次运行前校验

runner 开测前先比对文件的 SHA-256 与 manifest 登记值，不符则记 **ERROR**（不是 FAIL）——
那是夹具问题，不是被测程序的问题。下载截断的 RAW 文件如果被当成真样本测，失败会被
归咎到解码器头上。ERROR 同样让退出码非 0：它的含义是「这条用例没测出结果」，
不是「测过了没问题」，放它过去 CI 就会带着一批根本没测到的用例放行。
急着调试时可以 `-SkipHashCheck` 跳过，常规运行不要加。

## 性能压测（测试规格 Phase 6）

```powershell
.\tools\image-test-runner\run-performance.ps1 -Count 100
.\tools\image-test-runner\run-performance.ps1 -Count 100,1000,10000 -Switches 200
```

素材由脚本生成到 `test/corpus/_local/06-performance/`（`.gitignore` 已挡）。10000 张即使
每张只有十几 KB 也不该进仓库。首次生成后复用。

采集的指标（都不依赖读屏幕，锁屏也能跑）：

| 指标 | 口径 |
|---|---|
| 启动时间 | 进程启动 → 顶层窗口出现 |
| 首次可交互 | 进程启动 → 窗口能在限定时间内回应 `WM_NULL`，说明 DrawScene 已跑起来 |
| 扫描时间 | 启动时间随图片数增长的部分（打开图片时要扫目录建列表），按相邻档位的增量估 |
| 切换延迟 | 连发翻页键，每次紧跟一次 `SendMessageTimeout` 探主线程被堵多久（中位/P95/最大） |
| 资源 | WorkingSet、私有内存、句柄数、GDI 对象、USER 对象、CPU 时间 |

**资源在切换前后各取一次**，专为查泄漏：句柄或 GDI 对象翻倍以上直接判 FAIL。
翻页期间出现主线程无响应同样判 FAIL。

结果写到 `artifacts/test-report/performance.csv`，逐版本对比才有意义——绝对值随机器变化，
所以脚本只卡「明显坏掉」的情形，不设固定的耗时阈值。

压测期间不要操作机器：CPU 和内存会被前台程序干扰。

素材生成的两个坑：逐张调 `magick` 的话，10000 次进程启动光开销就要十几分钟；
把 10000 张塞进一次调用则会超过 Windows 32KB 的命令行上限，`magick` 报「文件名或扩展名
太长」。现在先生成 32 张不同色调的基图，再复制成 N 个文件名。基图放在各档位目录**之外**，
否则会被程序当成待浏览的图片扫进列表，图片数就跟档位名对不上了。

## 发布闸门（测试规格 Phase 7）

一条命令跑完全部测试：

```powershell
.\run-release-tests.ps1                    # 全量
.\run-release-tests.ps1 -SkipBuild         # 复用已有构建
.\run-release-tests.ps1 -SkipPerformance   # 跳过压测，适合提交前自检
```

六个环节，全部标为阻断发布：

1. Release 构建
2. `runTests.ps1`：单元测试 + 窗口行为 + 格式语料
3. 图片语料全套 `run-tests.ps1 -All`
4. 大图渐进加载探针
5. 翻页响应性探针
6. 性能压测

产出在 `artifacts/release-gate/`：`summary.md` 给人看，`results.json` 给机器读
（含语料的逐用例数据），`performance.csv` 是各档位的性能与资源数据，
`stages/*.log` 是每个环节的完整输出。存在阻断失败时退出码非 0。

两条刻意的设计：

- **单个环节失败不中断后续环节**，最后统一判定。一次跑完能看到全部问题，
  不用修一个再跑一遍才发现下一个。
- **跳过的环节在报告里单独列出，并明写「不代表它们通过」**。
  `-SkipPerformance` 之后报告仍可能是 PASS，必须让人看清哪些没跑。

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
