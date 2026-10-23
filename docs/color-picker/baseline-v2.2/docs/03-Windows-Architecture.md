# Windows 技术架构与集成要求

> 请先审计真实YeImageViewer仓库，确定C++/Win32/Qt/其它技术栈；本文件为功能契约，不强迫重建框架。

## 0. 必须真实落地的模块

```text
YeImageViewer
  Existing Viewer/Toolbar/Settings/Theme/Shortcuts/Build
  ColorFeatureController
    ScreenPickerSession (State: Idle, Preparing, Live, Frozen, Confirming, Cancelled, Error)
      MonitorTopologyService (physical coordinates, negative origin, mixed-DPI)
      ScreenSampler (backend interface + capability detection + capture recovery)
      PickerHUD (per-monitor positioning and noncontaminating overlay)
      ClipboardFormatter / HotkeyManager
    ImagePixelPicker (inverse view transform -> decoded RGBA pixel)
    ColorCore (canonical RGBA + HEX/HSL/HSV/HSI/CMYK/LAB/contrast math)
    ColorAssistantWindow (Editor, History, Palettes, Gradient, Favorites, RuleColor, AIOptional)
    HistoryStore / FavoritesStore / PaletteRepository / SettingsStore
    ImagePaletteExtractor (async, cancellable, bounded memory)
    GradientEngine / RulePaletteEngine / OptionalAIProviderAdapter
```

## 1. Windows显示与采样原则

- 建议运行进程为 Per-Monitor DPI Aware V2 (若与既有应用相容)，统一使用物理像素：显示器bounds/指针coords/crop rect。DIP只给HUD显示布局使用，不能混在物理采样。
- 取色需要“眼睛最终看到的像素”：使用适合系统的Windows捕获方式，例如Desktop Duplication / Windows Graphics Capture / GDI `BitBlt` 的受支持路径，并封装成后端，在GPU/RDP/HDR/权限不可用时回退/明确报错。
- 若使用DDA，必须做好帧Acquire/Release、屏幕旋转、坐标/像素格式转换、接口失效重建；GDI/Graphics Capture也需要测试Overlay排除和帧同步。不得采样到HUD自身。
- Windows安全桌面、DRM及其他保护内容不绕过安全边界。HDR高亮/ICC从屏幕呈现色映射为标准sRGB时须注明“呈现色/近似值”，不能冒充原始光学RGB。
- 多显卡/多屏分别建立输出映射；非矩形虚拟桌面空洞不能当有效屏幕像素取样。
- 热插拔、Win+P切换、锁屏/RDP设备失效：释放旧资源并安全恢复或退出，不能留下挡鼠标的透明窗。

## 2. 可观察状态与异步规则

- 采样会话一个active实例；重复启动只激活现有会话或按约定重开，不允许多个永久overlay。
- 复制、确认、取消具有原子性：提交一次历史事件，复制结果来自同一快照，不得把上一次颜色写入当前。
- C复制保留HUD/Enter左键确认结束的行为必须有明确规则：可选择复制本身是否记历史；默认实际复制算确认事件且会话内防重复，纯预览不记。
- 捕获和图片提色耗时任务在后台执行，UI回调携带generation/sessionId，切图、换页或退出后拒绝旧任务结果。
- 不启动取色时后台不频繁全屏采样；释放所有GPU帧、GDI对象、热键与窗口句柄。

## 3. 图片原始像素坐标映射

- 从Viewer视口经平移、缩放、旋转、镜像等逆变换映射到解码后的图片源像素；确定中心采样/像素取整规则。
- 读取源RGBA，不读取棋盘底色或缩放插值画面；动态图取当前帧；超出图片显示明确无像素。
- Alpha 0的像素仍可能保留原始RGB，用于精确读数；色块预览单独合成到棋盘/黑白底。

## 4. 颜色数学

- 内部统一8-bit RGBA（高级计算使用浮点暂态）；HEX6/HEX8（RGBA顺序），RGB/A、HSV/HSB、HSL、CMYK近似、HSI、CIE-LAB D65相互换算，使用经检验的参考向量。
- sRGB分量先线性化，再XYZ D65再Lab。支持逆变换时外色域裁剪并提示；CMYK仅数学近似并标明不是ICC印刷分色。
- HSV/HSL hue取[0,360)且灰色hue需约定。Lab、HSI、CMYK在接近黑灰时不得NaN/Inf。格式化精度显示与内部计算分离，以避免重复取整累积误差。
- 推荐色取互补/类似/分裂互补/中差等色相关系，明确公式；WCAG计算相对亮度和对比度。
- 渐变实现sRGB为默认、可选OKLab，不得将CSS和PNG导出的插值不一致却不提示。

## 5. 依赖、测试与可维护性

- 纯算法与UI解耦；ColorMath、CoordinateMapper、HistoryStore、GradientEngine等应能在无桌面会话的CI环境单元测试。
- Windows真实多屏/HDR/全局热键实测记录硬件、系统构建号、缩放比例，无法模拟不可宣称已通过。
- 默认优先用项目已有UI及构建体系，避免引入大型Electron/React运行时；任何新依赖需许可证审核和打包体积评估。
- 安装/便携模式的存储路径与原应用一致；不硬编码`C:\Users\color`。

---

# YeImageViewer Color Picker / Color Assistant — 技术设计

- 设计版本：1.0
- 约束：**先扫描已有 YeImageViewer 源码再决定 C++/Qt/Win32/其它实现细节**。不得以本文中的抽象伪代码擅自假设项目技术栈。
- 总原则：取样数据与浮层渲染隔离；屏幕色与图片原始像素分源；转换算法可单元测试；兼容多屏高 DPI；渐进发布。

## 1. 架构分层

```text
Existing YeImageViewer Window / Toolbar / Settings / Theme
                    |
       ColorFeatureController
       /          |           \
  ScreenPicker  ImagePicker  ColorAssistantWindow
       |          |           |
 IScreenSampler IImageSampler ColorWorkbenchViewModel
       \          /           /
        ColorSelectionService
                  |
   ColorMath + PaletteEngine + ContrastMath
                  |
   History / Favorites / Palettes / Export
                  |
    Settings + JSON Store (versioned)
```

### 1.1 子模块契约

- `ColorFeatureController`：入口绑定、重入、工作台生命周期、跨窗口消息、从现有图像查看器恢复焦点/状态。
- `IScreenSampler`：虚拟屏幕拓扑、每显示器采集、物理像素 RGB、显示器物理边界与错误原因；不依赖 UI 绘制代码。
- `IImageSampler`：查看器图像帧，视图坐标到图像原始坐标的变换，精确像素 RGBA。
- `PickerOverlayController`：HUD 位置、十字网格、捕获与释放键鼠、快捷键状态、冻结/确认/取消。
- `ColorMath`：sRGB、HSV、HSL、CMYK approximate、HSI、CIELAB D65，反向转换与精度策略；纯函数库。
- `ColorSelectionService`：候选颜色/提交颜色、历史记录、事件观察者；屏幕与图片来源元数据。
- `ColorAssistantWindow`：7 页签内容，布局自适应、快捷键范围、键盘焦点。
- `PaletteEngine`：配色公式、渐变插值、图片聚类、色库查询，与 UI 分离。
- `StorageService`：文件版本迁移、自动保存、备份/恢复、导入导出。

### 1.2 建议数据结构（概念接口，非强制源码）

```cpp
enum class ColorSource { Screen, Image, ColorWheel, Palette, Gradient, Imported };
enum class CoordinateMode { ScreenPhysical, MonitorPhysical, ImagePixel, OptionalLogical };
enum class ColorFormat { HEX, HEX8, RGB, RGBA, HSV, HSL, CMYK, HSI, LAB };

struct RGBA8 { uint8_t r, g, b, a; };
struct ScreenPosition { int32_t x, y; }; // signed: 允许负坐标
struct ImagePosition { int64_t x, y; };  // 超大图使用更宽坐标
struct ColorSelection {
    RGBA8 rgba;
    ColorSource source;
    std::optional<ScreenPosition> screenPhysical;
    std::optional<ScreenPosition> monitorPhysical;
    std::optional<ImagePosition> imagePixel;
    std::string displayId;
    std::string colorSpace;              // e.g. display-sRGB / image-sRGB / ICC converted
    std::string timestampIso8601;
};
```

注意：`RGBA8` 适合常规 8-bit UI，但对 16-bit/浮点 HDR 图像不得强制在底层提前截断；若项目已经支持更高位深，应保留原始 precision，8-bit 仅用于普通显示和 HTML/CSS 复制输出。

## 2. Windows 全桌面取样

### 2.1 取样策略与优先级

**建议第一阶段先可靠捕获第一帧快照，然后优先尝试支持动态实时更新**；如果取色浮层排除采集/后端不支持实时刷新，则明确显示“快照模式”并允许用户重新抓取。千万不能把已覆盖 HUD 的帧假装真实屏幕颜色。这样兼顾实时取色体验与安全回退。

1. 获取 `EnumDisplayMonitors` 与 `GetMonitorInfo`，记录虚拟桌面每块显示器 `rcMonitor`，采样使用物理像素坐标；需要设置并核实进程 Per-Monitor DPI Awareness V2，避免物理坐标与 DPI 虚拟化坐标混用。
2. **捕获顺序**：隐藏/排除取色 overlay → 获取各显示器源帧或虚拟桌面快照 → 显示 HUD/接管取色交互。先以快照初始化采样；正常情况下 Live 模式定期更新屏幕帧；如果实时模式不可用则明确退回快照模式，且不能把 overlay 捕进去。
3. 可选技术后端：
   - P0 兼容回退：GDI `BitBlt` + `CAPTUREBLT` 视可行性获取各屏桌面位图，BitBlt 使用物理坐标并保留边界；需验证合成窗口、HDR、全屏场景行为。
   - 高性能：DXGI Desktop Duplication 按输出获取帧，或根据项目基础设施选择 Windows Graphics Capture；某些后端有系统版本、会话权限、用户授予捕获及多 GPU 限制，不能假设统一可用。
   - 非法/受保护源：返回明确“无法捕获当前区域”，不默默给出黑色像素当正常颜色。
4. 多显示器分别缓存帧 + 坐标映射；虚拟屏幕的空白区域（显示器不相邻）不得作为有效像素。
5. 按鼠标实际物理坐标查找所属显示器，再转换成该显示器帧的局部像素索引；屏幕重排/断开要重建映射，避免越界访问。
6. 第一次启动时不强制安装新的系统服务、不申请管理员权限，不允许为取色关闭杀软、UAC 或更改安全策略。

### 2.2 取色 HUD 的正确绘制方式

- HUD 视图读取已经采集的区域 `N×N` **源像素**，按整数倍 nearest-neighbor 放大，并叠加网格和十字；叠加层永远不写回源帧。
- 推荐**每显示器一个顶部浮层**，取色任务协调器为单实例；或者根据现有 UI 框架以一个覆盖虚拟桌面的大窗口绘制，必须验证跨不同 DPI/负坐标。多窗口方案通常便于保证 HUD 留在物理显示器范围内。
- HUD 位置算法：当前显示器 `rect` + HUD 测量结果 `w/h`；先尝试 `(cursor.x+offset,cursor.y+offset)`，右侧空间不足 → 水平翻转到左，底部空间不足 → 垂直翻转到上；仍溢出 → clamp 到显示器 bounds。若 HUD 比显示器大 → 缩小 HUD 或紧凑布局。
- 使用不激活主窗口/不抢焦点 UI 策略；若需要键盘快捷键，取色模式持有受控焦点或注册有限生命周期热键，**不要让常驻低层全局 keyboard hook 拦截其它进程输入**。
- 需要鼠标左键选择任意区域时，全桌面 overlay 在取色模式内暂时接收点击；退出前必须卸载覆盖层，不能把点击透传和取样选择逻辑混乱。
- Windows UI 像素坐标、DIP、DWM 物理坐标必须文档化，所有点/矩形转换集中到 `CoordinateMapper`，不在 UI 里随意乘除 125%/150%。

### 2.3 混合 DPI/多 GPU/HDR 特别说明

- `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` 必须在允许的应用启动阶段设定；如果现有进程 manifest 已声明，则**不要在打开取色器时再次设置**。按已有 UI 框架方式兼容 DPI。
- 统一单次采集数据以 `(displayId, sourceFrame, localXY, originalFormat)` 标识，避免 primary/secondary 颜色错位。
- HDR、Wide Color Gamut、ICC：屏幕复制到 8-bit sRGB 时可能发生 gamut mapping/tone mapping；需要在 UI 或帮助说明“屏幕采样色值可能是显示链路转换后的 SDR 近似”；如果后续支持 scRGB / 16-bit float / ICC profile，应记录实际转换步骤，不能假装所有色都属于 sRGB。
- DXGI `ACCESS_LOST`、设备重置、RDP 切换、屏幕旋转都应捕获并重建，重建失败清理 overlay。
- Windows Secure Desktop/UAC 和某些版权保护区域不可取色是预期限制；不得绕过系统保护。

## 3. 图片原始像素采样

### 3.1 正确映射公式

- View 坐标（主窗口内容区局部，DIP）→ `inverse(RenderTransform)` → 图像原始像素坐标。
- RenderTransform 可能包含：平移、缩放、90/180/270°旋转、任意旋转（如已支持）、左右镜像、裁切、DPI 缩放、图片布局偏移。
- 对解码像素使用**nearest-pixel selection**，采样前说明取整（建议 `floor` 对象素半开区间 `[i, i+1)`）；`x<0 || y<0 || x>=width || y>=height` 直接报告图像外。
- 多帧图片读取当前帧，切图/切帧时校验 generation ID 防止后台指针悬挂。
- 透明图片原始 RGBA 与合成预览分别实现，不能把 alpha 混合后的灰/白色记录成源像素。

### 3.2 现有模块兼容

- 不要在全图尺寸（尤其数十万像素宽）上频繁做独立全图复制。尽可能使用查看器已有的已解码图像/tiles，按需请求单像素。
- 取色开始记录查看器当前模式状态，结束恢复，而不是强制 reset view、缩放或重新打开图片。
- 退出后恢复鼠标指针和光标占用，避免与右键退出、手势、缩放、拖拽倍率、Shift 精细拖动等现有特性交叉冲突。

## 4. 数值换算的基准

### 4.1 HEX/RGB

- 输入 HEX 接受 `#RGB`、`#RRGGBB`、（图片通道可启用）`#RRGGBBAA`，不接受非法长度/字符；输出六位大写含 `#`。
- `#53CAA4` ↔ `(83, 202, 164)`；RGB 每通道必须 clamp 到 `[0,255]`。

### 4.2 HSV/HSL

- 使用标准 sRGB 分量归一化计算，Hue 归一化到 `[0,360)`；彩色样本 hue 由 max/min 分支决定；灰度零饱和时 hue 统一为 0（或保持 UI 最近 hue 作体验，但导出定义必须清楚）。
- S/V/L 0–100%，UI 显示 1–2 位小数，内部用浮点保持精度；用户修改 RGB→HSV→RGB 的每通道往返误差目标 ≤1（8-bit 量化）。

### 4.3 CIELAB

- sRGB `[0,1]` 做 inverse transfer function → linear RGB → XYZ (D65) → Lab (D65 reference white)。
- 某些标准库默认 D50/ICC PCS；不得混用 D50 与 D65 而不做色度适应；确保参考值与同一白点库交叉验证。
- LAB 行显示 `L*, a*, b*` 和小数 2–3 位，超出 sRGB 的反向 LAB 输入需要明确选择裁剪并提示。

### 4.4 CMYK/HSI

- CMYK 使用从 RGB 最大值推算的设备无关**演示近似**，黑色 K=1 单独处理，避免除 0；提供使用说明“印刷请使用真实 ICC CMYK 配置文件”。
- HSI 采用已测试的定义：强度为 `(R+G+B)/3`，色相使用带 acos/atan2 的稳定公式，灰度零饱和情况专门处理；不同算法产生的 HSI 显示可能稍有差异，文档写明。

### 4.5 色差/对比/推荐颜色

- 配色 Hue 角处理 `normalize(h + delta)`；S/L 或 S/V 保持原设定并允许渐进修正，可选对低饱和源色进行适度增强，但必须可预期。
- W3C WCAG 2.2 相对亮度：每个 sRGB 通道按阈值线性化，`L=0.2126R+0.7152G+0.0722B`；`ratio=(Llight+0.05)/(Ldark+0.05)`。
- 普通文本：AA 4.5:1 / AAA 7:1；大文本：AA 3:1 / AAA 4.5:1；不能用色相差或颜色互补代表文字可读性。

## 5. 数据层与导出

### 5.1 推荐文件结构（实际依源码与架构调整）

```text
src/[existing layout]/color/
    core/ColorMath.*
    core/ColorValue.*
    sampling/ScreenSampler.*
    sampling/ImageSampler.*
    sampling/CoordinateMapper.*
    picker/PickerOverlay.*
    workbench/ColorAssistantWindow.*
    palette/PaletteEngine.*
    palette/ImagePaletteExtractor.*
    data/PaletteRepository.*
    data/ColorStore.*
    data/exporters/*
resources/color/palettes/{ui,traditional,gradients}/*.json
resources/icons/color-picker.*
docs/color-picker/{PRD,Design,Tests,TestReport,Defects}.md
```

### 5.2 JSON 结构示意

```json
{
  "schemaVersion": 1,
  "settings": {
    "format": "HEX",
    "coordinateMode": "ScreenPhysical",
    "pickerSize": 15,
    "pickerZoom": 10,
    "showGrid": true,
    "globalHotkeyEnabled": false,
    "theme": "system"
  },
  "favorites": [
    {
      "id": "example-uuid",
      "type": "solid",
      "rgba": [83, 202, 164, 255],
      "name": "示例薄荷绿",
      "tags": ["design"],
      "createdAt": "2026-10-20T11:00:00+08:00"
    }
  ]
}
```

注意：例中 `createdAt` 为数据结构演示，不应写死；序列化颜色要在保存时用真实时间戳和 UUID。

- 用户配置使用 Windows per-user 可写目录（例如 `%LOCALAPPDATA%/YeImageViewer/`，须遵循项目既有配置位置）。不要写安装目录/Program Files。
- JSON 增加 `schemaVersion`；保持旧设置兼容。编辑新功能时不能清空旧配置。
- 先写 `.tmp`、flush 成功后替换主文件，至少保留一份 `.bak`；损坏文件可以恢复或跳过，日志去隐私化。
- 自定义配色数据分离 `source`, `license`, `version` 字段，第三方数据的使用和归属可追踪。

### 5.3 导出与复制样式

```css
:root {
  --yi-primary: #53CAA4;
  --yi-secondary: #4967C5;
  --yi-surface: #F5F8F7;
  --yi-text: #18231F;
}

/* Gradient */
background: linear-gradient(90deg, #53CAA4 0%, #4967C5 100%);
```

- CSS、JSON、纯文本优先支持 UTF-8，Windows 剪贴板用 UTF-16 `CF_UNICODETEXT` 防止中文色名乱码。
- 对用户自定义的标签和名称进行必要转义（CSS 标识符/JSON 字符串），不能直接拼接形成无效导出。

## 6. 交互、事件和线程

- 颜色变更管线：UI 编辑 → parse/validate → normalized color → publish `ColorChanged` → wheel/sliders/all fields/render/readability 同步；UI 更新不得再反向触发死循环，采用更新令牌或状态抑制。
- 取色光标 30–60 FPS 渲染不意味着每帧采集全屏；UI 用短帧缓存，鼠标位置改动即时更新采样索引。
- 用户确认颜色时发 `ColorCommitted`；历史记录只在提交时插入，避免拖动色轮一次产生几百条历史。
- 图片提色和大型色库过滤不应阻塞 UI 主线程；异步工作任务应有取消标识、generation ID、退出清理。
- 多次快速触发吸管：重用当前取色器，或者明确拒绝第二次并提示，不能重复创建多个顶层透明窗口。
- 程序启动和工作台不应隐式加载重型模型、联网 SDK；AI 功能明确可选。

## 7. 错误处理与日志

| 情况 | 预期处理 |
|---|---|
| 截屏 API 失败 | 提示原因并提供重试/替代后端；退出覆盖层 |
| 被采样区域不属于显示器 | 禁止提交并提示无效坐标 |
| 源颜色不可表示为标准 sRGB | 进行已定义的映射并说明；原始颜色留存（如项目支持） |
| 显示器断开/分辨率变化 | 重建拓扑，刷新采样；失败退出模式而非留下遮罩 |
| 失去焦点/按 Esc | 明确取消或在前台维持轻量暂停；无鼠标或键盘锁定 |
| 配置损坏 | 记录日志、从备份加载或安全默认配置，保留可恢复文件 |
| 传统色库加载失败 | 显示数据不可用，但不影响普通取色功能 |
| JSON 导入格式不符 | 先校验后修改数据；输出有意义的错误信息 |

## 8. 集成限制、源码审计清单

**Codex 第一件事必须在现有 repo 中核对**：

1. 使用何种 GUI 框架、Windows 原生入口、渲染系统、图像解码与像素格式、DPI 策略、测试框架、快捷键集中管理方式。
2. 现有工具栏和右键菜单定义、现有 Shift 功能、程序退出路径、全屏模式、窗口置顶、托盘/单实例策略。
3. 图像查看器的变换矩阵及旋转、放大、镜像、动图、透明通道策略。
4. 主题系统（三态）、资源图标尺寸、翻译机制、设置持久化路径、导入导出样式。
5. 已有 `build.bat` / CMake / VS 版本环境，需保证双击能编译真实 exe；产物拷贝到既有目标位置。
6. 检查和更新 docs/ 里需求、设计、真实测试报告、CHANGELOG、缺陷、发布 checklist，**不要虚构测试运行记录**。

## 9. 性能验证建议

- 分别测单屏 1080p、多屏 4K、125%/150%/200% DPI、RDP：首次取色 UI 出现延迟、移动 1 分钟 FPS、峰值 RAM、CPU/GPU、退出后的资源回收。
- 用自建纯红绿蓝白黑及边界条纹的校验图片显示在不同应用，取色 100 个随机点检查颜色与坐标；注意浏览器 CSS 与图像原像素在色彩管理后可能不同。
- 非取色状态下监测内存/CPU，确保新增模块不持续后台截图。
- 对 RGB/HSV/HSL/LAB 做公开算法参考向量单测，并采用端点值、随机值、灰度与低饱和值加上性质测试。

## 10. Release Gate

- P0：多显示器与负坐标、边缘 HUD、模式退出与剪贴板正确，零顶层遗留；不破坏原图片查看器。
- P1：图像 RGBA、旋转缩放采样、状态同步、收藏持久化。
- P2：色库来源授权记录齐全，图片提色可取消，CSS/JSON 可往返。
- P3：可选联网功能有知情同意与离线回退，HDR/高位深报告清晰。

**所有阶段必须先通过 Release Gate 再更新版本为 stable。**
