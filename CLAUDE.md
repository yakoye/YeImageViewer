# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概览

YeImageViewer 是基于 JarkViewer 开发的 Windows 10/11 x64 原生图片查看器，使用 C++23、Win32、Direct3D 11 和 OpenCV 构建。它重点支持大量静态图、动图、RAW、LivePhoto/MotionPhoto、EXIF 信息显示、打印/简单编辑和文件关联。

## 常用命令

本项目优先使用 PowerShell 执行仓库根目录下的 `buildRelease.ps1` 脚本进行编译、构建：

```powershell
# Release x64 构建
./buildRelease.ps1

# 运行已构建程序
./x64/Release/YeImageViewer.exe
./x64/Release/YeImageViewer.exe "D:/path/to/image.png"
```

每次修改后至少保证 `buildRelease.ps1` 能干净编译通过。

测试用发布闸门，一条命令跑完全部环节（构建、单元测试、窗口行为、格式语料、图片语料、
渐进加载、翻页响应、实况声音、性能压测），存在阻断失败时退出码非 0：

```powershell
# 全量（含 10000 张性能压测，约 30 分钟）
./run-release-tests.ps1

# 提交前自检：跳过性能压测
./run-release-tests.ps1 -SkipPerformance

# 单独跑某一环
./runTests.ps1                                    # 单元测试 + 窗口行为 + 格式语料
./tools/image-test-runner/run-tests.ps1 -All      # 图片语料 145 例
./tools/image-test-runner/probe-extreme-png.ps1   # 极端 PNG：大图/200MP/损坏/快速连切
```

和其他看图软件横向对比打开与切换速度（不属于发布闸门，按需跑）：

```powershell
./tools/image-test-runner/compare-viewers.ps1 -ImageDir "D:\photos"
```

报告在 `artifacts/release-gate/`：`summary.md` 给人看，`results.json` 给机器读，
`performance.csv` 是性能数据，`stages/*.log` 是各环节完整输出。

测试体系的结构、素材约定和踩过的坑都在 `test/corpus/README.md`——**改解码路径前先读它**，
尤其是「先证明素材是对的」那几节：`identify` 不应用 EXIF/RAW 方向、ImageMagick 写不了某些
格式时会静默降级成 PNG、Q16 构建下 `-depth` 会被忽略，这些都曾让人误判成产品缺陷。

行为变更仍需手动冒烟验证静态图加载、动图播放、EXIF 显示、打印预览和导出流程。

`test/corpus/_local/` 和 `test/bigimage/` 是不进仓库的大体积素材，本地缺失时测试记 SKIPPED。
RAW 样本用 `./scripts/fetch-raw-corpus.ps1` 按需下载（CC0 来源，约 356 MB）。
实况照片素材在 `test/live-photo/`（入库），用 `./scripts/generate-live-photo-fixtures.ps1` 重建，说明见该目录的 README。

## 构建前提

- 项目文件是 `YeImageViewer/YeImageViewer.vcxproj`，工具集为 `v145`，语言标准为 C++23，目标平台为 x64；需要安装支持 v145 工具集的 Visual Studio/Build Tools。
- `YeImageViewer.vcxproj` 中 `VcpkgEnabled=false`，默认使用仓库内的静态库目录：`YeImageViewer/lib*`、`YeImageViewer/libffmpeg`、`YeImageViewer/include`。
- README 说明第三方静态库需从 release 的 `static_lib` 包准备；如果改为 vcpkg，需要在项目属性中启用并补齐依赖。
- `avif.lib` 与 `heif.lib` 不能直接用上游 `static_lib` 包里的：本仓库去掉了它们携带的 aom / x265 编码器（看图软件只解码），主程序也不再链接 `x265-static.lib`。新环境先跑 `./scripts/build-thirdparty-slim.ps1 -Install` 重建，否则链接失败。
- `lib/libdeflate.lib` 由 `./scripts/build-libdeflate.ps1 -Install` 生成，大 PNG 的快路径要用它整块解压。缺了会链接失败。
- `libopencv/zlib.lib` 换成了 zlib-ng 的 compat 构建（`./scripts/build-zlib-ng.ps1 -Install`）。不换也能链接能跑，只是 PNG 解压慢约 1.4 倍——一张 291 MB 的 16 位 PNG 差 0.45 秒。`include/` 下的 zlib 头文件已经是 zlib-ng 的，compat 模式不改符号名（`zlib_name_mangling.h` 是空的），和原版 zlib.lib 混用也不会出问题。
- Release 输出程序位于 `x64/Release/YeImageViewer.exe`，中间文件位于 `YeImageViewer/x64/<Configuration>/YeImageViewer`。

## 高层架构

- `YeImageViewer/src/main.cpp` 定义 `YeImageViewerApp` 和 `wWinMain`。入口初始化 Exiv2 BMFF、禁用 IME、初始化 COM，然后创建窗口、解析命令行图片路径并进入主循环。
- `YeImageViewer/include/D3D11App.h` 与 `YeImageViewer/src/D3D11App.cpp` 提供 Win32 窗口、消息分发、Direct3D 11 设备/交换链和 `PresentCanvas()`。业务层通过继承并实现鼠标、键盘、拖放、右键菜单和绘制回调。
- `YeImageViewer/include/ImageDatabase.h` 与 `YeImageViewer/src/ImageDatabase.cpp` 负责图片加载、格式分派、EXIF 处理和 LRU 缓存。核心路径是 `ImageDatabase::loader()` → `myLoader()` → 按扩展名调用 JXL/WP2/AVIF/HEIF/RAW/SVG/PSD/OpenCV/WIC/FFmpeg 等解码器 → 统一转为 OpenCV `cv::Mat`。
- `YeImageViewer/include/jarkUtils.h` 与 `YeImageViewer/src/jarkUtils.cpp` 集中放置 Win32/OpenCV 工具、主题/设置全局状态、剪贴板、全屏、资源读取、文件操作和日志。
- `YeImageViewer/include/Printer.h` 和 `YeImageViewer/include/Setting.h` 是打印与设置界面，均继承自轻量基类 `YeImageViewer/include/MatWindow.h`。`MatWindow` 用纯 Win32 API（`RegisterClassExW` + `CreateWindowExW` + 自己的 `wndProc` 与消息循环）创建独立窗口，子类把 UI 绘制到 `cv::Mat m_uiCanvas` 上，最后通过 GDI `StretchDIBits` 把 BGRA Mat 贴到窗口 DC，这里 OpenCV 只用作画布像素操作（`cv::rectangle`、`cv::cvtColor` 等）。
- `YeImageViewer/src/TextDrawer.cpp`、`stringRes.cpp`、`exifParse.cpp`、`videoDecoder.cpp`、`blpDecoder.cpp` 分别支撑文字绘制、多语言字符串、元数据解析、视频帧解码和 BLP 解码。

## 代码约定

- 源码使用 UTF-8 和 C++23；现有代码主要采用 4 空格缩进。
- 类型名多用 `PascalCase`，函数、方法和局部变量多用 `camelCase`；新增代码优先贴合相邻文件风格。
- 提交信息惯例是简短中文描述，例如“优化PSD解码”“更新版本号”。

## 运行时数据流

1. `wWinMain` 读取命令行路径并调用 `YeImageViewerApp::initOpenFile()`。
2. `initOpenFile()` 扫描同目录下所有受支持图片扩展，按 Windows 自然排序建立 `imgFileList`。
3. 当前图片通过 `ImageDatabase::getSafePtr()` 进入缓存；切换图片时会预取相邻图片。
4. 鼠标、键盘、滚轮、拖放和菜单事件转成 `ActionENUM` 放入 `OperateQueue`。
5. `YeImageViewerApp::DrawScene()` 消费操作队列，更新缩放、平移、旋转、帧索引、EXIF 显示、打印/设置窗口等状态。
6. 当前帧绘制到 CPU 端 `cv::Mat mainCanvas`，最后通过 `D3D11App::PresentCanvas()` 上传到 D3D11 纹理并显示。

## 修改注意事项

- `SettingParameter` 按固定 4096 字节持久化在 `YeImageViewer.db` 开头；不要随意调整成员顺序、大小或删除保留字段，否则会破坏旧设置兼容性。
- 那 4096 字节之后是 UTF-8 文本区（`ConfigFile.h`），外部编辑器、复制/移动目标、图片旋转记录都写在那里，落地文件只有本体、缩略图 DLL 和这一个配置。写设置必须用 `r+b` 就地改写，用 `wb` 会截断文件、把文本区连同三样配置一起抹掉。三块数据各认自己的键前缀，保存时要把别人的行原样带回去（`ConfigFile::foreignLines`）。
- 新增图片格式时，同时检查 `ImageDatabase::supportExt` / `supportRaw`、加载分派逻辑、EXIF/方向处理、设置页文件关联列表和 README 格式列表。
- UI 文本来自 `stringRes`（三列：0 简体中文 / 1 English / 2 繁體中文，表在 `stringRes.cpp`，三选一工具在 `UiLanguage.h`）。加语言要同时改两张表、`Setting.h` 的语言单选和 `ShortcutItem` 的名字列。
  界面里凡是「中文一套、英文一套」的判断一律走 `isChineseUI()` / `tr()` / `UiLanguage::pick()`，写成 `UI_LANG == 0` 会让繁體界面掉进英文分支。
  设置/帮助/关于和打印按钮大量使用资源图切片，那是位图不是字符串，繁體沿用简体那套切图；改文案或布局时要同步检查中文、英文、浅色、深色资源。
- OpenCV 用的是自己重建的精简版：只含 core / imgproc / imgcodecs，去掉了 IPP、contrib、videoio 和 highgui（主程序一次 highgui 调用都没有）。重建脚本是 `scripts/build-opencv-slim.ps1`，换版本或换机器都用它，别直接拿官方全功能包。
- `imgcodecs` 的分辨率上限仍然必须改 OpenCV 源码，`build-opencv-slim.ps1` 里有这一步。不能改成在程序里设 `OPENCV_IO_MAX_IMAGE_*` 环境变量：那三个上限是 `loadsave.cpp` 里的命名空间作用域 `static const`，CRT 在进入 `wWinMain` 之前就初始化完了，设了也没用（曾经这样改过，结果 240MP 以上的 PNG 全被拒绝）。上游 README 提到的 HighGUI 光标改动（`IDC_CROSS` → `IDC_ARROW`）随 highgui 一起不再需要。
- 不要提交 `.vcxproj.user`、`.vs/` 或机器相关的本地库路径。
- 大的静态 PNG 不走 OpenCV，走 `PngFastDecode.h`：拼好 IDAT 用 libdeflate 一次解完，再一趟去滤波并直接写进目标 Mat。比 libpng 的逐行流式路径快不少（9000×9000 的 16 位图解码 1513 → 1270 毫秒），省下来的大头不是解压更快，而是少扫了好几百 MB——行刚去完滤波还在缓存里，顺手就转好写出去了。
  适用范围刻意收得很窄：非交错、位深 8 或 16、色彩类型 0/2/6、没有 tRNS、文件 ≥ 4 MiB、内存宽裕。调色板、交错、1/2/4 位、灰+alpha、APNG 一律退回 OpenCV。
  代价是内存：整块解压要解压缓冲和 Mat 同时在手，峰值比流式路径高约一倍（291 MB 的图 1.3 GB 对 0.85 GB），所以可用物理内存不够一半时会主动让开。
  **改这条路之后必须跑 `--png-decode-selftest <目录> <结果文件>`**（发布闸门里已有这一环，比了 143 个文件）：它拿同一批文件两条路都解一遍逐字节比对。解码错了不会崩也不会报错，只会把图画歪，没有这个自检根本发现不了。
- 查「打开一张图为什么慢」用 `YEIMAGEVIEWER_STARTUP_TRACE=<文件路径>` 环境变量（见 `StartupTrace.h`），会记下 CRT 静态初始化、建窗口、建 D3D 设备、解码、格式转换、色彩管理、首帧绘制各段的时刻。没设环境变量时一个字节都不写。
- 建 D3D 设备要七十多毫秒，建窗口只要五毫秒。图片解码在 `onWindowCreated()` 里就派出去了，和建设备并行跑；启动那一次的 `initOpenFile` 要传 `keepWarmCache = true`，否则 `imgDB.clear()` 会把在途的解码作废。
- 别再去给 libpng 的去滤波写 SIMD。做过一轮完整的：libpng 自带的 SSE2 只覆盖 3 和 4 字节像素（`intel/intel_init.c` 里 `if (bpp == 3) ... else if (bpp == 4)`），16 位图是 6 或 8 字节像素，确实掉回标量。补齐 6/8 字节的 SSE2 实现之后，自检确认结果与规范逐字节一致，解码耗时却一点没变——A/B 各跑四轮：原版 1506/1513/1513/1523 ms，SIMD 版 1512/1524/1594/1608 ms。
  原因是这一步受内存带宽限制，不受计算限制：SIMD 版每行（9000 像素 × 6 字节）25.4 µs，期间读 row、读 prev、写 row 共 162 KB，折合 6.4 GB/s，正好顶在单线程内存带宽上。libpng 原本的标量实现早就撞在同一堵墙上了。
  顺带记两件事：量性能必须多跑几轮取中位数，这次先后被两个单次测量（1733、1646）误导过；另外别拿「按规范直写的参考实现」当标量基线去算加速比，libpng 自己那份优化过的 C 比参考实现快十倍。
- 色彩管理排在解码之后，直接顶在出图时间上：源和目标同色彩空间时整步跳过（绝大多数图都是这种情况），大图按行分块并行。改 `ColorManager::applyToMat` 之后要跑 `./x64/Release/YeImageViewer.exe --color-selftest <文件>`，它用生产代码验证恒等真被跳过、并行结果和串行逐字节相同。
- 主窗口渲染路径以 OpenCV `cv::Mat` 作为 CPU 画布，再交给 Direct3D 显示；避免在高频绘制路径中引入阻塞 I/O 或昂贵同步操作。
- Debug 构建会分配控制台并启用 `JARK_LOG`；Release 下日志宏为空。
