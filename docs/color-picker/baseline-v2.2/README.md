# YeImageViewer 完整开发需求包 v2.2（候选版现状适配）

> 日期 2026-10-20；**需求包版本号不是软件发布版本号。用户明确说明截图/当前开发实现均是尚未发布的候选。**

## 最重要的结论

- 完整继承 v2.0/v2.1 所有颜色助手、取色器、图层/通道、旋转镜像、UI、测试需求；**没有删掉任何功能**。
- GitHub 远端 `main` 静态审计 SHA：`bd6b56973597e9ab23b1a2b8699b2b055cf81e8b`（UTC 2026-10-15 18:45:11）；截图候选可能未推送，**远端 main 不代表用户当前运行的本地版本**。
- v2.1 的 **403 条**设计测试全部原样保留；v2.2 **新增 54 条**候选审计、回归与操作测试；正式设计总数 **457 条**，均非本次真实执行结果。
- 不重复重写现有 `PsdCompositeDecoder`、`RotationStore`、`OperationToast`、底部旋转镜像和已有测试框架。Codex 先找到候选中实现了多少，再增量修补。
- 用户优先级：全屏快捷取色、取色后调整、历史；随后全部颜色助手页面、PSD/PSB 图层/通道、真合成和统一入口；AI 最后。

## Codex 直接阅读入口

**先阅读 `docs/23-Codex-Candidate-Incremental-Master-Prompt.md`。**

1. `docs/21-Current-GitHub-Baseline-and-Status.md`：远端 main 实际源码与正确的候选/未发布状态区别；固定 SHA、保留的现有功能、关键契约。
2. `docs/22-Candidate-UX-and-Gap-Plan.md`：按截图对比候选与目标；轻工具清理、工具栏吸管、图层通道、旋转镜像保存、完整路线。
3. `docs/23-Codex-Candidate-Incremental-Master-Prompt.md`：最高优先级、不会覆盖候选未提交改动的 Codex 执行指令。
4. `docs/24-Candidate-Regression-Additions.md`：54 条增量测试和实施说明。
5. `templates/Test-Case-Registry-v2.2-Total.csv`：**457 条**唯一测试 ID；`templates/Test-Cases-v2.2-Additional.csv` 为本次新增。
6. 旧 `docs/01`～`20`、`archive/`、`reference/`、其他 templates **原封保留**，旧文档与新状态有冲突时以最新用户确认的候选状态和增量指令为准。

## 来源与交付状态

- 来源：只读检查 https://github.com/yakoye/YeImageViewer 的 main、用户四张新截图、之前的 uTools 七张截图、用户上传 uTools 参考包，以及旧开发包。
- 当前未获得截图候选源码/二进制或现场 Windows 桌面测试；没有编译或执行候选。因此**不能断言这些功能已通过，也不能称 v1.38.2 已经正式公开发布**。
- PhotoCraft 参考仓库 https://github.com/storytold/photocraft 需经过许可证/第三方归属与性能评估；不得整套移植造成功能臃肿。
