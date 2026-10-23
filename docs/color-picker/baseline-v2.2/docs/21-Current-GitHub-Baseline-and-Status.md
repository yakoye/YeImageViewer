# 21 · YeImageViewer GitHub main 静态源码审计与候选版基线（v2.2）

> 审计日期：2026-10-20；远端仓库：https://github.com/yakoye/YeImageViewer；分支：`main`；记录到的 HEAD：`bd6b56973597e9ab23b1a2b8699b2b055cf81e8b`，最近提交 UTC 2026-10-15 18:45:11。**用户已确认当前讨论的版本均为开发候选、尚未正式发布。不得把 README、CHANGELOG、文档标题或仓库提交当作生产发布证明。**
>
> 本文仅为远端 `main` 只读源码审计；未获得用户截图对应的工作区/未推送候选分支，因此与截图不一致处标记为“候选分支待核实”，不直接判定用户正在运行的程序没有该能力。

## 1. 真实代码与可复用基础

| 子系统 | main 静态证据 | 状态 | 增量开发决定 |
|---|---|---|---|
| PSD/PSB 合成图 | `YeImageViewer/include/PsdCompositeDecoder.h` 注释：“只读取 Photoshop 保存的最终合成图，不展开和重建图层”；`src/ImageDatabase.cpp:loadPSD()` 调用其 `decode` | main 已有；不是交互式图层合成 | **必须保留**合成图快速解码路径以及色彩管理/缩略图一致性，新增图层子系统独立构建与按需惰性加载 |
| PSD/PSB 校验与兼容 | `PsdCompositeTests.h`、`test/compatibility`、`scripts/test-format-compatibility.ps1`，包含 Raw/RLE/Zip、8/16/32 位/CMYK/Lab等 | main 存在相关测试代码；非本轮实测结果 | 复用夹具，增加复杂分层及开关前后像素 oracle |
| 图层树/通道 GUI | main `OverlayLayout.h` / `ToolbarCommand.h` 不含层面板入口；用户截图的“图层与通道·临时浏览”展示候选实现 | **候选截图可见；main 未核实到实现代码** | 首先比较本地候选分支；保留其已能工作的部分，缺失部分增量补齐 |
| 图层显隐重合成 | main 合成解码器明确未实现重建；截图有“本次操作未应用，保留上一画面” | 候选存在部分交互但**不应宣称像素合成已通过** | 实施视觉状态和像素结果一致的事务重合成、失败回滚、原始合成随时恢复 |
| 工具栏/旋转镜像 | `OverlayLayout.h`、`ToolbarCommand.h`、`src/main.cpp` 已有左右旋转、水平/垂直镜像的 UI 和命令 | main 已有 | 在竖向镜像后插取色吸管，**不要复制旋转/镜像入口**；工具栏缩放布局和命中跟随测试 |
| 旋转持久化 | `RotationStore.h/.cpp`；`main.cpp` `rotationStore.save()`；README 按路径记住角度 | main 已有 | 视图旋转记忆与真实导出必须分离；不随意更改已有 DB |
| 提示 | `OperationToast.h`，`main.cpp` `showToast/drawToast` | main 已有静态文本 toast | 保留机制，可点击操作由新增轻量交互层完成；要验证绘制/命中/DPI/不阻断输入 |
| 图片取色与颜色助手 | 远端 main 的工具栏命令、图标目录未看到吸管相关代码；本地候选状态未知 | main 尚未证实；候选待核查 | 对照用户 v2.1 完整需求，已实现重用，未实现补齐 |
| 主题、多语言 | `AGENTS.md` 确认简体/英文/繁体和浅色/深色资源注意事项 | main 已有要求 | 新增图标和提示共用项目当前资源和三语机制 |
| 取色历史 | main 未发现完整历史数据子系统 | main 未证实；候选待核查 | 继续作为核心需求，持久化和迁移必须自动测试 |
| 基础测试 | `runTests.ps1`、`YeImageViewerTests/main.cpp`、图标/热区/源代码不变量脚本 | main 已有；未在本环境执行 Windows 测试 | **往现有测试体系添加用例**；不能以本需求包的 NOT_RUN 设计用例冒充现有测试 PASS |

## 2. 必须保护的代码契约

1. `AGENTS.md` / `CLAUDE.md` 是**仓库当前约定**：构建 C++23/MSVC v145、Win32/D3D11 + OpenCV；原则上不引入另一套大型 UI 框架；执行 `./runTests.ps1`。测试报告必须记录真实命令、退出码、日志。
2. 现有 `YeImageViewer.db` **前4096字节不可任意修改结构或顺序**；后续文本区使用 `ConfigFile::foreignLines` 保护其他模块的记录。历史扩展可以使用经兼容性验证的命名空间/独立可迁移库；便携模式与无权限场景另测。
3. `ShortcutConfig.h` 的 `Action` 值与持久化索引相关：只能兼容式**追加**新动作；处理版本迁移；老用户自定义热键不能被重置。**当前 `Ctrl+S` 已绑定 `ExportFrames`，不得直接覆盖为保存变换快捷键**，须先审计并设计无冲突映射。
4. `OverlayLayout.h` 的底部按钮按固定基准坐标与 DPI 计算；增加吸管必须重算 toolbar 宽度、按钮命中、低分辨率压缩与居中、热区不重叠；`ToolbarCommand.h` 同步添加命令；补齐 `runTests.ps1` SVG 检查及 `YeImageViewerTests` 对应断言。
5. 图标现有资源是 `YeImageViewer/file/icons/*.svg`，见 `runTests.ps1` `$toolbarIcons`；新吸管应来自**与已有 SVG 一致的已许可图标库**，新资源需要纳入工程构建、打包和图标回归，不得使用 emoji/系统字形临时代替。
6. 正式的 Release 包有尺寸门禁：当前 `runTests.ps1` 检查裸 EXE 上限 **28 MiB**，其它发布脚本可能还有压缩包预算，修改之前按运行脚本实际输出核对，不得只根据旧版文档猜测。
7. 原主路径 `PsdCompositeDecoder` 和 `YeThumbnailProvider` 共享对 PSD 合成图的规则。图层重合成功能应按需独立加载，不能拖慢普通 PSD 首帧或在无图层文件自动打开面板。
8. `RotationStore` 只证明角度视图记忆，不等于源文件写回；PSD/PSB 保存至少首阶段只允许**导出扁平副本**，不得意外破坏原始图层。
9. 修改前检查当前本地候选分支/未提交工作区、`git status --short`、最新 commit、`AGENTS.md`、最新候选记录；不要 `git clean/reset/checkout --` 擦掉候选未提交成果。
10. 用户说“未发布”即按照**未发布候选**管理版本与验收。不得从含“发布”字样的旧文档推断实际公开、验收完成或 GitHub Release 生效。

## 3. 状态术语（文档/Issue/PR统一）

- `MAIN_CONFIRMED`：上述远端 SHA 源码证实有相应实现，不等于可用。
- `CANDIDATE_VISIBLE`：仅从用户截图观察到交互入口，源代码需比对。
- `CANDIDATE_SOURCE_VERIFIED`：Codex 已取得本地分支对应代码，确认实现位置。
- `IMPLEMENTED_UNVERIFIED`：代码完成但未跑完相关测试。
- `VERIFIED_IN_CANDIDATE`：指定候选版本、配置和实际环境内通过验收，**仍未正式发布**。
- `NOT_IMPLEMENTED`：目标当前候选源代码确实不存在。
- `BLOCKED`：缺测试素材、平台或依赖等阻塞；需列原因与恢复计划。
- `RELEASED`：必须有明确的实际发布证据并经用户认可，本文**一律不预设**。

## 4. 还需 Codex 实际核实的信息

- 用户图 1、图 4 的本地候选程序由哪个 git commit / branch 构建，是否含未提交改动？
- 轻工具菜单、PSD 临时浏览面板、图层树与蒙版/通道的源文件具体位置；是否已提交、是否在另一目录或分支？
- 当前自动化运行结果，是否有“显示成功但画面不变”的可复现最小 PSD？
- 旋转、镜像的真实写回/导出路径和快捷键绑定；现有保存提示时机。
- 项目当前图标来源/授权与 Theme 具体枚举，必须读取最新项目规范，不以截屏臆测。

## 5. 授权

YeImageViewer 当前 main 标明 `GPL-3.0`，基于 JarkViewer；PhotoCraft (`https://github.com/storytold/photocraft`) 的仓库代码为 `MIT OR Apache-2.0`。两者不能直接简单“全库移植”：先核查 license/NOTICE/第三方资源和 Rust-C++ FFI 二进制体积、内存、异常/线程、安全，再选择解析或算法局部参考。禁止复制不兼容的品牌图、商标及未经审核的第三方素材。
