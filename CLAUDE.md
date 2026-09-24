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
- 主窗口渲染路径以 OpenCV `cv::Mat` 作为 CPU 画布，再交给 Direct3D 显示；避免在高频绘制路径中引入阻塞 I/O 或昂贵同步操作。
- Debug 构建会分配控制台并启用 `JARK_LOG`；Release 下日志宏为空。
