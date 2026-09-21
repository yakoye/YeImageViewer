# YeImageViewer 大图 / 极端 PNG 测试集 v2

> 目标：不仅验证“单张能不能打开”，还要验证 **连续切换、预加载、缓存、缩放、内存释放、异常文件容错**。
>
> 推荐用法：每个分类至少 10 张；重点使用 `12_Mixed_Switch` 连续按 `←/→`、长按方向键和来回快速切换。

---

## 1. 测试集结构

运行 `Build-YeImageViewer-Testset.ps1` 后会建立：

```text
YeImageViewer-Extreme-Testset-v2-data/
├─ 01_Real_PNG_50-70MB/        # 真实航拍 PNG，10 张
├─ 02_Real_100MP_Plus/         # 100MP~144MP 真实 PNG，10 张
├─ 03_Generated_200MP_Plus/    # 200MP~320MP，10 张；文件可很小但解码压力巨大
├─ 04_Alpha_RGBA/              # Alpha / RGBA，10 张
├─ 05_16bit_PNG/               # 16-bit PNG，10 张
├─ 06_Adam7_Interlaced/        # Adam7 隔行 PNG，10 张
├─ 07_ICC_WideGamut/           # ICC / P3 / Rec.2020，10 张
├─ 08_UltraWide_Tall/          # 超宽 10 张 + 超长 10 张
├─ 09_Broken_PNG/              # 损坏/非法 PNG，10 张
├─ 10_Metadata_PNG/            # gAMA/pHYs/tEXt/iTXt/zTXt/tIME/sRGB/cHRM 等，10 张
├─ 11_PngSuite_Mixed/          # PngSuite 基础格式混合，30 张
├─ 12_Mixed_Switch/            # 从各组混排 40 张，专门快速切换
├─ _licenses/
└─ MANIFEST.csv
```

实际物理测试文件约 **140+ 张**；`12_Mixed_Switch` 优先使用硬链接，因此通常不会重复占用几十/几百 MB 磁盘。

---

## 2. 一键生成

### 最简单

双击：

```text
run-testset-builder.bat
```

或者 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\Build-YeImageViewer-Testset.ps1
```

默认输出到脚本旁边：

```text
YeImageViewer-Extreme-Testset-v2-data
```

指定目录：

```powershell
.\Build-YeImageViewer-Testset.ps1 -Root D:\ImageTest\YeImageViewer-v2
```

重新下载覆盖已有文件：

```powershell
.\Build-YeImageViewer-Testset.ps1 -Force
```

只想先生成小型/特殊编码测试，不下载约 1GB+ 的真实大图：

```powershell
.\Build-YeImageViewer-Testset.ps1 -SkipRealLarge
```

不生成 200MP / 超宽超长 / 损坏 / 元数据图片：

```powershell
.\Build-YeImageViewer-Testset.ps1 -SkipGenerated
```

---

## 3. 环境要求

### 必需

- Windows 10 / Windows 11
- PowerShell 5.1 或 PowerShell 7
- 网络连接（下载真实大图和标准测试集）

### 生成极端图片

`generate_extreme_png.py` **只用 Python 标准库，不需要 Pillow / numpy / ImageMagick**。

脚本会依次寻找：

```text
py -3
python
python3
```

如果没有 Python，真实大图、PngSuite、ICC 测试仍然可以下载，只会跳过自动生成组。

---

# 4. 第一组：真实 PNG 50~70 MB × 10

来源：Wikimedia Commons 上的 Bayerische Vermessungsverwaltung 2025 正射影像。

这些图片统一为 `5000 × 5000`，内容是真实航拍，非常适合测试：

- 大文件磁盘 IO
- PNG 解压
- 连续上一张/下一张
- 相邻图片预加载
- 缓存淘汰
- 关闭图片后的内存释放

| # | 文件 | 分辨率 | 约大小 |
|---|---|---:|---:|
| 01 | DOP 611000 5310000 | 5000×5000 | 59.01 MB |
| 02 | DOP 606000 5434000 | 5000×5000 | 50.59 MB |
| 03 | DOP 611000 5300000 | 5000×5000 | 55.23 MB |
| 04 | DOP 555000 5510000 | 5000×5000 | 55.69 MB |
| 05 | DOP 605000 5310000 | 5000×5000 | 55.89 MB |
| 06 | DOP 556000 5534000 | 5000×5000 | 62.25 MB |
| 07 | DOP 606000 5513000 | 5000×5000 | 64.26 MB |
| 08 | DOP 715000 5563000 | 5000×5000 | 62.52 MB |
| 09 | DOP 675000 5560000 | 5000×5000 | 62.83 MB |
| 10 | DOP 568000 5510000 | 5000×5000 | 68.50 MB |

下载脚本通过 Wikimedia Commons `Special:Redirect/file` 获取原始文件，不会下载预览缩略图。

---

# 5. 第二组：100MP+ 真实 PNG × 10

| # | 文件 | 分辨率 | 约大小 |
|---|---|---:|---:|
| 01 | Mandelbrot Set Image 106 | 10000×10000 | 49.05 MB |
| 02 | Mandelbrot Set Image 107 | 10000×10000 | 100MB 级 |
| 03 | Mandelbrot Set Image 108 | 10000×10000 | 43.14 MB |
| 04 | Mandelbrot Set Image 109 | 10000×10000 | 73.74 MB |
| 05 | Mandelbrot Set Image 110 | 10000×10000 | 57.82 MB |
| 06 | Mandelbrot Set Image 111 | 10000×10000 | 55.45 MB |
| 07 | Mandelbrot Set Image 112 | 8000×12600 | 43.65 MB |
| 08 | Mandelbrot Set Image 113 | 10000×10000 | 约百 MB |
| 09 | Burning Ship Ultra HD Render By Graphiq | 12000×12000 | 94.65 MB |
| 10 | Lyapunov exponent of double pendulums | 12000×12000 | 96.64 MB |

以 `10000×10000 RGBA8` 为例，如果查看器最终展开为 4 bytes/pixel：

```text
10000 × 10000 × 4 = 400,000,000 bytes ≈ 381 MiB
```

`12000×12000 RGBA8` 则约：

```text
549 MiB / 张
```

所以“磁盘只有 50~100 MB”完全不代表加载时只占 50~100 MB。

---

# 6. 第三组：200MP+ 解码炸弹式压力图 × 10

这组由脚本本地生成，主要是 **1-bit grayscale PNG**。

特点：

- 磁盘文件可能非常小
- PNG 内部像素数达到 2~3.2 亿
- 查看器若统一展开为 BGRA/RGBA，单张可能需要约 0.75~1.2 GiB
- 非常适合检查是否在读取 IHDR 后先做尺寸/内存预算
- 检查是否会直接 OOM、UI 卡死或整个进程退出

尺寸示例：

```text
20000 × 10000 = 200 MP
25000 ×  8000 = 200 MP
20000 × 12000 = 240 MP
30000 ×  8000 = 240 MP
16000 × 16000 = 256 MP
32768 ×  8192 = 268 MP
18000 × 15000 = 270 MP
24000 × 12000 = 288 MP
30000 × 10000 = 300 MP
20000 × 16000 = 320 MP
```

## 建议行为

查看器不一定必须把它们全部成功解码。

**合理实现可以在解码前拒绝，并明确提示：**

```text
图片尺寸：30000 × 10000 (300 MP)
预计解码内存：约 1.12 GiB (RGBA8)
超过当前安全限制。
```

比直接卡死或崩溃更好。

---

# 7. Alpha / 16-bit / Adam7 / PngSuite

这些组来自 PngSuite。

PngSuite 专门用于测试 PNG viewer / converter / editor，覆盖：

- grayscale
- RGB
- indexed/palette
- grayscale + alpha
- RGBA
- 1 / 2 / 4 / 8 / 16-bit
- Adam7 interlace
- gamma
- transparency
- ancillary chunks
- 部分非法/异常 PNG

项目：

```text
https://github.com/lunapaint/pngsuite
```

脚本会下载整个标准集，然后自动抽取：

- `04_Alpha_RGBA`：10 张
- `05_16bit_PNG`：10 张
- `06_Adam7_Interlaced`：10 张
- `11_PngSuite_Mixed`：30 张

---

# 8. ICC / P3 / Rec.2020 × 10

来源：

```text
https://github.com/codelogic/wide-gamut-tests
```

包含：

- DCI-P3 / sRGB 对比
- Rec.2020 / sRGB 对比
- Rec.2020 / P3 对比
- PNG 内嵌 color profile

脚本优先抽：

```text
P3-sRGB-red.png
P3-sRGB-green.png
P3-sRGB-blue.png
P3-sRGB-color-ring.png
P3-sRGB-color-bars.png
R2020-sRGB-red.png
R2020-sRGB-green.png
R2020-sRGB-blue.png
R2020-sRGB-color-ring.png
R2020-sRGB-color-bars.png
```

测试时除了“能不能打开”，还要对照其他支持色彩管理的软件观察颜色是否一致。

---

# 9. 超宽 / 超长 × 20

生成器创建：

- UltraWide × 10
- UltraTall × 10

例如：

```text
100000 × 64
 80000 × 96
 65535 × 128
 ...
64 × 100000
96 × 80000
128 × 65535
...
```

重点测试：

- Fit to Window
- 100% 缩放
- 横向/纵向滚动条
- 极端长宽比下缩放计算
- 鼠标中心缩放
- 缩略图计算
- 浮点/整数溢出
- viewport 裁剪

---

# 10. Broken PNG × 10

生成器主动制造 10 种异常：

1. PNG Signature 错误
2. 文件头截断
3. IDAT 中途截断
4. 缺少 IEND
5. IHDR CRC 错误
6. IDAT CRC 错误
7. width = 0（CRC 正确，但语义非法）
8. 重复 IHDR
9. 未知 critical chunk
10. 非法 chunk length

**正确目标不是“把坏图强行显示出来”。**

目标是：

- 不崩溃
- 不死循环
- 不无限申请内存
- 不闪退
- UI 仍可操作
- 能继续切换下一张
- 错误信息尽量说明原因

例如：

```text
PNG decode failed
Reason: IDAT CRC mismatch
File: 06_bad_idat_crc.png
```

---

# 11. Metadata PNG × 10

本地生成并分别加入：

```text
tEXt
zTXt
iTXt
compressed iTXt
gAMA
pHYs
tIME
sRGB
cHRM
multiple text chunks
```

其中包含 UTF-8 / 中文 iTXt，用来测试元数据读取和 Unicode。

---

# 12. Mixed_Switch：真正重点

脚本从前面的目录抽取约 40 张，并故意混合：

```text
50MB真实航拍
→ 200MP
→ 16-bit
→ ICC
→ 损坏 PNG
→ Alpha
→ 100MP
→ 超长图
→ Metadata
→ Adam7
→ ...
```

文件名前面加序号：

```text
001_...
002_...
003_...
...
```

尽可能使用硬链接，因此不会重新复制一份 50~100 MB 大文件。

---

# 13. YeImageViewer 建议测试动作

## A. 基础加载

每个文件观察：

- 是否正确显示
- 是否保持宽高比
- 是否错误旋转
- Alpha 是否正确
- 色彩是否异常
- 16-bit 是否出现黑屏/色阶异常

## B. 连续切换

在每个 10 张组：

```text
→ × 30
← × 30
```

然后：

```text
← → ← → ← → ...
```

再测试：

```text
长按 →
长按 ←
```

关注：

- 是否丢按键
- 是否顺序错乱
- 是否显示上一张残影
- 是否切换到已经释放的 bitmap
- 是否出现“先显示 A，随后突然跳 B”
- 是否大量创建后台解码任务

## C. Mixed_Switch

重点做：

```text
连续快速按 → 50~100 次
```

随后停止。

观察软件是否最终稳定在正确图片，而不是后台旧任务完成后把画面覆盖回来。

这对异步解码尤其重要。

### 建议设计

每次切图带 generation/request ID：

```text
request #101 -> A.png
request #102 -> B.png
request #103 -> C.png
```

即使 A 最后才解码完成：

```text
if (requestId != currentRequestId)
    discard();
```

不能让旧结果覆盖 C。

---

# 14. 内存测试

打开任务管理器 / Process Explorer。

测试：

```text
启动 YeImageViewer
记录初始内存

连续切换 50 张
记录峰值

停留 30 秒
记录稳定值

关闭当前目录 / 打开小图
再次记录
```

重点区分：

- Working Set
- Private Bytes / Commit
- GPU Dedicated Memory（如果使用 D3D）
- GDI Objects
- USER Objects

## 需要警惕

如果：

```text
每切换一张 +300MB
300MB -> 600MB -> 900MB -> 1.2GB ...
```

且不回落，极可能存在：

- bitmap 没释放
- WIC frame/decoder 引用没释放
- Direct2D bitmap 未释放
- GPU texture cache 无上限
- future/thread 持有旧图
- prefetch 没有取消
- image history/cache 没有限额

---

# 15. 建议的缓存策略测试

例如只保留：

```text
previous
current
next
```

即：

```text
[i-1] [i] [i+1]
```

对于 100MP 图，缓存 10 张几乎一定不合适。

建议按 **实际解码内存** 而不是“文件大小”计算 cache budget。

例如：

```text
cacheBudget = min(1 GiB, 20% physical RAM)
```

每张图：

```text
decodedBytes ≈ stride × height
```

超预算时 LRU 淘汰。

---

# 16. 建议记录的结果

`MANIFEST.csv` 会列出文件。

你还可以增加：

```text
RESULT.csv
```

字段：

```csv
file,load_ok,load_ms,peak_memory_mb,switch_ok,zoom_ok,error_message,notes
```

后续 YeImageViewer 每个 RC 版本都可以使用同一测试集做回归。

---

# 17. 磁盘 / 内存提醒

真实大图两组本身约 **1GB+**。

建议至少预留：

```text
3 GB 磁盘空间
```

如果要同时做缓存、系统临时文件和其他格式测试，建议：

```text
5~10 GB
```

更重要的是内存：

200MP+ 图片如果统一展开为 RGBA/BGRA，单张就可能接近或超过 1GB。

因此极端组的目标之一就是验证：

> YeImageViewer 能否在真正申请巨量内存之前识别风险并优雅拒绝。

---

# 18. 数据来源

### Wikimedia Commons

真实 50~70MB 正射影像：
- Bayerische Vermessungsverwaltung - DOP 2025 系列

100MP+：
- Mandelbrot Set Image 106~113
- Burning Ship Ultra HD Render By Graphiq
- Lyapunov exponent of double pendulums

脚本只负责下载原始文件；版权和许可请以每个 Wikimedia Commons 文件页面为准。

### PngSuite

```text
https://github.com/lunapaint/pngsuite
```

### Wide Gamut Test Images

```text
https://github.com/codelogic/wide-gamut-tests
```

---

## 最推荐的日常回归方式

如果你不想每次把 140 多张全部看完：

1. 开 `12_Mixed_Switch`
2. 快速 `→` 50 次
3. 快速 `←` 50 次
4. 随机停在 100MP / 200MP 图上缩放
5. 切到损坏图，确认错误后还能继续下一张
6. 观察内存是否稳定
7. 再跑一次 `05_16bit_PNG` 和 `07_ICC_WideGamut`

这套最容易发现真实用户会遇到的问题。
