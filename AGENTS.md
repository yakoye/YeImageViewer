# AGENTS.md

This file provides guidance to Codex and other coding agents when working with code in this repository.

## 项目概览

YeImageViewer 是基于 JarkViewer 开发的 Windows 10/11 x64 原生图片查看器，使用 C++23、Win32、Direct3D 11 和 OpenCV 构建。它重点支持大量静态图、动图、RAW、LivePhoto/MotionPhoto、EXIF 信息显示、打印/简单编辑和文件关联。

## 常用命令

本项目优先使用 PowerShell 执行仓库根目录下的 `buildRelease.ps1` 脚本进行编译、构建：

```powershell
# Release x64 构建
./buildRelease.ps1

# 需要带符号排查时
./buildRelease.ps1 -Configuration Debug

# Release x64 构建并运行全部自动化回归测试
./runTests.ps1

# 运行已构建程序
./x64/Release/YeImageViewer.exe
./x64/Release/YeImageViewer.exe "D:/path/to/image.png"
```

每次修改后必须运行 `runTests.ps1`，保证 Release x64 编译和全部已登记回归用例通过；行为变更还需手动冒烟验证受影响流程。每个已修复缺陷都必须新增能在修复前失败、修复后通过的自动化测试，并把真实复现素材保存在 `test/`（素材过大或涉及隐私时需改为最小化夹具）。

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
- 四个缩放/窗口命令的规则都在 `ZoomPolicy.h`（有配套单元测试），分工是：**适应窗口**只改缩放，**实际大小**只改缩放到 100%，**适应图片**窗口和缩放一起定（客户区正好等于画面，零留白），**沉浸显示**只改窗口。只动一半必出问题——「适应图片」就曾经只改窗口不改缩放，四周露出一圈棋盘格。
  「适应窗口」只缩不放：图片比窗口小就停在 100%。
  `applyImageFittedWindowSize()` 必须在动窗口**之前**关掉 `framedWindowAnchored`：`SetWindowPos` 会同步派发 WM_SIZE，而锚定那条路径会按工作区重算缩放，把刚算好的覆盖掉。
- 预览图（系统缩略图）的像素尺寸和真图可以毫无关系——一张 280x288 的 SVG，系统给的缩略图是 995x1024。所以：缩放要按真图尺寸算再折算回缩略图像素（`ZoomPolicy::previewZoom`），标题报的尺寸和百分比也要按真图口径（`ZoomPolicy::reportedPercent`），否则换成真图那一刻画面和数字都会跳。系统报不出真图尺寸时（SVG 就报不出）干脆不显示预览，直接等真图。
- `scripts/check-source-invariants.ps1` 查六件跑起来也看不出来的事，`runTests.ps1`
  会调它，提交前也可以单独跑（只读源码，几秒钟）：
  1. 三语字符串表三列齐全，`// N` 索引标注没错位——那些索引是硬编码的，中间插一条
     会让后面所有 stringID 整体指错。
  2. 没有谁把「界面是不是中文」写成 `UI_LANG == 0`（繁體是 2，会掉进英文分支）。
  3. 继承 `LRU<>` 的类都在自己的析构函数里先调了 `stopPreloadWorker()`。这是一条真
     bug 的护栏：预读线程跑的是派生类的 `loader()`、用的是派生类的成员，等 `~LRU()`
     才停线程时派生部分已经销毁，在途那次解码正访问已释放的内存。
  4. 格式清单在 `supportExt`/`supportRaw`/`videoExt`、默认关联列表、两份 README 之间
     对得上。加格式漏改一处不会报错，只会让某个格式「能开但没人知道」。
  5. PSD 的解码顺序是 psd_sdk 优先、stb 兜底。反过来的话 16 位 RLE 的 PSD 会解成
     全透明而且不报错——stb 返回「成功」，兜底永远轮不到（经过见
     `test/corpus/README.md`）。
  6. 没人靠「尺寸 + 左上角像素颜色」去认内置的「打不开」提示图。那是在猜，会误伤真图。
  静态检查最危险的失效方式是什么都抓不到（正则写歪一个字符就照样 PASS），所以配了
  `scripts/verify-source-invariant-checks.ps1`：逐条制造该抓的错误，确认真会报错，
  跑完按原字节还原（不用 `git checkout --`，那会连未提交的改动一起抹掉）。
  新增一条检查就在它里面加一条对应的破坏，发布闸门里有这一环。
- 打印的逐像素处理（亮度/对比度、黑白抖动、反相）在 `PrintAdjustments.h`，不依赖 OpenCV，
  有配套单元测试；`Printer.h` 只负责把 Mat 的数据指针和步长交过去。这类错只有纸出来才
  看得见，界面不报错、预览图又小，所以必须拿测试钉住。
  亮度那条伽马曲线要先把对比度的结果夹到 0~255 再做 `pow`：对比度大于 1 时暗部会算成负数，
  `pow(负数, 非整数)` 是 NaN，NaN 转字节的结果是平台说了算的。
- 打印路径上的尺寸都走 `PrintLayout.h`（有配套单元测试），它保证算出来的边长至少 1 像素、
  排好的矩形完整落在页内。这里犯过一个只在极端长宽比下才露出来的错：按比例缩放只算缩放
  系数、不看算出来的边长，一张 10000x1 的图缩到 800 宽，高就成了 `round(1 * 0.08) = 0`，
  预览窗口建不起来——点「打印」什么都不发生，也没有任何提示。`limitSizeTo16K` 是同一个毛病。
  打印线程是 detach 的且没有 try/catch，这类退化尺寸一旦让 OpenCV 抛出来就是整个进程没了。
- 缩略图组件（`YeThumbnailProvider.dll`）的用例在单元测试程序里：它直接 `LoadLibrary`
  构建产物那份 DLL，走 `DllGetClassObject` → `IInitializeWithStream` →
  `IThumbnailProvider::GetThumbnail`，和资源管理器调它的路子一样，但不依赖注册表、
  不受 shell 缩略图缓存干扰。注册了哪些扩展看 `ThumbnailRegistrar.h` 的
  `kThumbnailEligibleExtensions`，仓库里有素材的每一个都在测。
  「出图了」不等于「出对了」：`common.*` 是同一张 160x80 参考图导出的各种格式，
  测试拿 PNG 当基准逐像素比 tga / ras / sr / pcx——通道顺序搞反这种错，只看
  「有没有图」看不出来。
  确实解不出来的（EXR、JP2）登记在 `main.cpp` 的 `thumbnailKnownGaps` 里，连原因一起；
  哪天补上了解码器，测试会提醒把记录删掉。
  PSD 在组件里也走 psd_sdk（`ThumbPsd.cpp`，用内存版的 `psd::File`），和主程序一致。
  **那个文件必须单独一个编译单元**：psd_sdk 的 `PsdPlatform.h` 会 `#define NOMSG`
  `NOUSER` `NOGDI` 一整串把 windows.h 砍瘦，只要它排在 COM 头之前，后面的
  `oleidl.h` / `ocidl.h` 就会因为缺 `MSG` 编译失败。
- 窗口行为测试里要点对话框按钮时，给**按钮子窗口**发 `BM_CLICK`（`GetDlgItem(dlg, IDYES)`
  再 `SendMessage(btn, 0x00F5)`）。给对话框本身发 `WM_COMMAND` + IDOK/IDYES 关不掉
  MessageBox，实测提示框还在原地；只有一个按钮的提示框可以用 `WM_CLOSE` 顶替确定，
  但 Yes/No 那种 `WM_CLOSE` 等于取消，必须真点按钮。
- 标题里的序号只有总数到两位才补零（`[1/3]`、`[01/12]`），匹配时别写死 `/03]`。
- 设置页单选组的「每组几个选项」在 `SettingCommand::GENERAL_RADIO_OPTION_COUNTS`，必须和
  `Setting.h` 里 `generalTabRadioList` 每组 `stringIDs` 的条数（减掉开头的标签）相等。
  加语言、加选项时两边都要改；单元测试拿界面那边的条数反核这张表，别把期望值照抄过去
  ——照抄过一次，结果「加繁體中文漏改选项数」两边一起错，测试还是绿的。
- 窗口行为测试里要读「界面现在是什么语言」，用设置窗口自己的标题（设置 / Settings / 設定，
  来自 `stringRes` 第 39 条），不要用剪贴板：剪贴板是全机器共享的，程序和测试会抢，
  实测会时不时读到空。标题在建窗口时就定了，所以改完语言要关掉再开一次才看得到。
- 内置的「打不开」提示图用 `ImageDatabase::isErrorTipsFrame` 认（比 `data` 指针，
  `getErrorTipsMat()` 返回的是母本的浅拷贝，所以指针相等就是它）。**别靠「尺寸 +
  左上角像素颜色」去猜**：一张正好 800x600、左上角又正好是主题背景色的真图会被误判，
  后果是悄悄跳过色彩管理、少画边框，而且没有任何迹象。源码检查里有一项盯着这个。
  也别在 `ImageAsset` 上加标记——提示图有三十多个产出点，加标记就得改三十多处。
- 窗口行为测试里要「点图片外的空白处」，别写死坐标。窗口是满工作区的、图片居中缩放，
  左右留白和上下留白通常只有一边非零，写死的点会随图片长宽比落到画面上。从标题里读
  真实像素尺寸和缩放百分比算出画面范围，再挑确实在留白里的点。
- 读剪贴板别用 `Set-Clipboard` 写哨兵再轮询内容：那是去和程序抢同一个全机器资源，
  程序那边 `OpenClipboard` 一失败就什么都没写进去。用 `GetClipboardSequenceNumber`
  等版本号变化，再用带重试的 `OpenClipboard` 直读（`ClipboardText` / `ClipboardImageSize`）。
  PowerShell 7 也没有 `Get-Clipboard -Format Image`。
- 把 `wstring_view` 交给要求以 `\0` 结尾的 Win32 参数之前先拷成 `wstring`。
  `data()` 不保证有结尾零——现在的调用方都传 `wstring` 所以一直没出事，
  传 `substr` 的视图就会读过界。
- 发布就是绿色版，**不做安装程序**。`packageRelease.ps1` 出三样：裸的
  `YeImageViewer.exe`、`...-portable.zip`（Windows 自带能解压）、`...-portable.7z`
  （最小）。压缩包里 exe 躺在根目录，不要再埋进 `x64\Release\`。
  曾经有个 7z SFX 一键安装器，已经删掉：SFX 的版本信息写着 `7z Setup SFX small`，
  Windows 的「程序兼容性助手」据此判定它是安装程序，而它退出时又不写卸载注册项，
  于是**每装一次就弹一次「可能未正确安装此程序」**。程序本来就是绿色单文件，
  装不装都一样。打包契约测试现在反过来禁止 SFX 回来。
  `installLocal.ps1` 保留——它是 PowerShell 脚本，不触发兼容性助手。
- 真窗口测试要在临时目录里跑自己那份 exe 和素材，**别直接用仓库里的**。
  旋转角度和每图缩放是持久化的（存在 exe 旁边的 `YeImageViewer.db` 里），直接跑的话
  每次起始状态都不一样：实测连跑六轮，起始缩放 17%/46%/9%，旋转顺时针90°/180°/
  逆时针90°/无。结果不可重现的测试没法用来判断「是不是真的坏了」，而且还会把旋转
  记录写进开发目录的配置、影响别的环节。
- EXIF 的摄影参数显示走 `ExifValueFormat.h`（有配套单元测试）：曝光时间 `1/60 s`、
  光圈 `f/8`、焦距 `89.9 mm`、曝光补偿 `+1/3 EV`、白平衡/测光/方向等枚举翻成词。
  认得的标签才翻，不认得的返回 `nullopt` 退回通用的「小数 (原始分数)」——厂商私有
  扩展按自己的表编号，照规范翻会翻出错的词，而错的词比一个数字更误导人。
  **方向（Orientation）千万别再从拼好的 EXIF 文本里扒。** 原先五处写的是
  `exifInfo.find("方向: ")` 再把紧跟的那个字符减 `'0'`，于是「把 1 翻成正常」
  这一个显示改动就能让所有带旋转标记的 JPEG 不再转正，界面毫无异样。现在
  `ExifParse::getExif` 用出参 `orientationOut` 直接带出原始数值。
- 信息面板分两套：带边框窗口下是紧凑卡片（只有基础 7 项），沉浸显示下才是完整面板
  （基础信息 + 直方图 + 整份 EXIF），见 `main.cpp` 的
  `presentationMode ? Mode::Full : Mode::Compact`。完整面板的高度跟着窗口长
  （`ImageInfoPresentation::maxPanelHeight`），别再给它写死上限——写死 400 逻辑像素时，
  880 高的窗口里相机、光圈、快门全被压在视口外要滚好几次，而窗口还空着四百多像素。
  标签列宽要放得下四个汉字（`LOGICAL_COMPACT_LABEL_WIDTH`），48 的时候「文件大小」
  只显示出「文件大」。
- 「关于」页底下那行「觉得好用？请作者喝杯咖啡」点一下会把 hero 卡片翻到收款码那面
  （`SettingLayout::ABOUT_DONATE_*`，收起时只有那行小字是热区，展开时整张卡片都能点回去）。
  收款码是 `YeImageViewer/file/donate-{alipay,wechat}.png`，从原始收款海报里裁出码本体
  再补静区做成的。**换图或改显示尺寸之后必须跑 `runTests.ps1` 的「赞赏收款码」一环**：
  它逐像素量静区够不够四个模块、显示尺寸下每个模块够不够四个像素。踩过一次——
  按 184 逻辑像素画时支付宝那个码（41 模块）每模块不到四个像素，边界糊成灰的，
  界面上看着是个正常二维码，扫码器就是不认，而且没有任何报错。
- README 的界面预览图别手工截——跑 `./tools/make-preview.ps1`。手工截的尺寸、背景、
  窗口位置每次都不一样，还很容易把桌面上别的窗口截进去（沉浸显示下图片以外那圈是
  六成黑的半透明层，背后有什么全透出来，所以脚本自己铺了一张纯黑的全屏底）。
  远程桌面最小化或锁屏时屏幕抓不下来，脚本会在开程序之前就说清楚。
- 查「打开一张图为什么慢」用 `YEIMAGEVIEWER_STARTUP_TRACE=<文件路径>` 环境变量（见 `StartupTrace.h`），会记下 CRT 静态初始化、建窗口、建 D3D 设备、解码、格式转换、色彩管理、首帧绘制各段的时刻。没设环境变量时一个字节都不写。
- 建 D3D 设备要七十多毫秒，建窗口只要五毫秒。图片解码在 `onWindowCreated()` 里就派出去了，和建设备并行跑；启动那一次的 `initOpenFile` 要传 `keepWarmCache = true`，否则 `imgDB.clear()` 会把在途的解码作废。
- 别再去给 libpng 的去滤波写 SIMD。做过一轮完整的：libpng 自带的 SSE2 只覆盖 3 和 4 字节像素（`intel/intel_init.c` 里 `if (bpp == 3) ... else if (bpp == 4)`），16 位图是 6 或 8 字节像素，确实掉回标量。补齐 6/8 字节的 SSE2 实现之后，自检确认结果与规范逐字节一致，解码耗时却一点没变——A/B 各跑四轮：原版 1506/1513/1513/1523 ms，SIMD 版 1512/1524/1594/1608 ms。
  原因是这一步受内存带宽限制，不受计算限制：SIMD 版每行（9000 像素 × 6 字节）25.4 µs，期间读 row、读 prev、写 row 共 162 KB，折合 6.4 GB/s，正好顶在单线程内存带宽上。libpng 原本的标量实现早就撞在同一堵墙上了。
  顺带记两件事：量性能必须多跑几轮取中位数，这次先后被两个单次测量（1733、1646）误导过；另外别拿「按规范直写的参考实现」当标量基线去算加速比，libpng 自己那份优化过的 C 比参考实现快十倍。
- 色彩管理排在解码之后，直接顶在出图时间上：源和目标同色彩空间时整步跳过（绝大多数图都是这种情况），大图按行分块并行。改 `ColorManager::applyToMat` 之后要跑 `./x64/Release/YeImageViewer.exe --color-selftest <文件>`，它用生产代码验证恒等真被跳过、并行结果和串行逐字节相同。
- 主窗口渲染路径以 OpenCV `cv::Mat` 作为 CPU 画布，再交给 Direct3D 显示；避免在高频绘制路径中引入阻塞 I/O 或昂贵同步操作。
- Debug 构建会分配控制台并启用 `JARK_LOG`；Release 下日志宏为空。
