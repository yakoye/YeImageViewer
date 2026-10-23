# v2.2 接入：本地候选基线审计

日期：2026-10-20。本报告只记录本轮需求接入与现有候选检查，不声称已实现v2.2功能。

## 来源、改动保护与版本

- 当前仓库：`D:/work/code/code_o/YeImageViewer`，分支 `codex/pan-settings-audit`，HEAD `bd6b56973597`。
- 当前EXE：`v1.38.3-rc6`，28,404,224字节（27.09MiB），SHA-256 `6CBCDAD32654C05AB6BDF89FD9DE6E1E7FF8C2D97D512F249E0272FD0E2A6BE3`。本轮重新编译后仍与已有rc6交付字节相同。
- HEAD之外存在前序未提交开发，不能以HEAD冒充完整候选快照。已复制并逐文件核对210个已修改/未跟踪文件，见 [基线清单](../../artifacts/candidate-baselines/v2.2-intake-20261020/dirty-file-hashes.csv)、[工作区状态](../../artifacts/candidate-baselines/v2.2-intake-20261020/git-status.txt)；备份不含忽略的依赖库、artifacts和个人配置，不是完整机器备份。
- 对比本地对象 `bd6b56973597`，重点跟踪文件 `PsdCompositeDecoder.h`、`OverlayLayout.h`、`ToolbarCommand.h`、`ShortcutConfig.h`、`src/ImageDatabase.cpp` 共80增行/19删行；`OperationToast.h`、`RotationStore.h`无已跟踪差异。此外PsdDock、PsdLayers、PsdRecompose、ScreenPickerSession等为未跟踪成果，不能遗漏。完整差异统计和原字节文件已保存。
- 本轮不查询或变更YeImageViewer远端；附件的main审计只作为固定SHA的外部材料，不称远端最新。本轮未改产品源码、运行配置或版本，也未push、tag或公开发布。另只读核对PhotoCraft根许可证及PSD库说明，不等于完成模块/第三方许可审计或POC。

## 实际调用链与差距

| 能力 | 本地实现定位 | 当前结论 |
|---|---|---|
| 底部吸管 | `OverlayLayout::screenPickerRect` → `ToolbarCommand::ScreenPicker` → `main.cpp::openScreenPicker` → `ScreenPickerSession::open` | 已有屏幕快照，不是缺少取色代码；仍在最右侧，移动到垂直镜像后为新任务 |
| 菜单 | `D3D11App.cpp::CreatePopupMenu`中的toolsMenu → `main.cpp`的screenColorPicker/colorEditor/psdLayers/saveImageCopy命令 | 当前仍有“轻工具”；可保留原命令ID并改为直达，不能删除其功能 |
| 图层与通道 | `main.cpp`的psdLayers命令 → `PsdDock` → `PsdLayers`、`PsdRecompose` | 已在主窗口停靠，不需要重新做一个独立对话框；复杂语义/原生通道未完整支持 |
| 合成事务 | `PsdDock`的desired/accepted、generation及poll交付 → `PsdRecompose::render` | 存在接受状态与失败回退基础；新矩阵需核对待处理眼睛状态、空层、快速操作与像素一致性，不能凭源码存在就标PASS |
| 原合成 | `ImageDatabase.cpp`调用共享`PsdCompositeDecoder`，缩略图共享同一规则 | 保留默认可信合成与首帧；新图层路径按需加载，不替换其基本功能 |
| 导出副本 | `main.cpp::saveCurrentImageCopy` → `ImageExportJob` / `ImageExport`，完成后提示 | 已有PNG/JPEG副本；可点击保存/撤销toast、扩展格式及元数据策略仍待实施 |
| 旋转记忆/提示 | `RotationStore`、`OperationToast`、`main.cpp::showToast/drawToast` | 前者是浏览方向记忆，后者现有静态提示；两者都不是原图自动写回 |
| 快捷动作/配置 | `ShortcutConfig::STORAGE_VERSION=6`、`ScreenPicker`追加动作；Ctrl+S仍为ExportFrames | 不抢键、不改4096字节布局；后续扩展必须有迁移测试 |

## 需求与测试登记

- 原包58项清单校验一致；单独23号文件与ZIP中原文相同。
- 总457条唯一设计ID，原403条和新增54条的全部字段与总表逐行一致；原259表字节不变。模板保持NOT_RUN，未覆盖旧rc6执行表。
- 新旧范围、冲突及C0–C6实施顺序见 [增量合并评估](v2.2-增量需求评估.md)；原始需求保存在 [baseline-v2.2](baseline-v2.2/README.md)。参考插件/素材仅保存为需求输入，未执行、集成进程序或判定可再分发。

## 本轮真实回归

- 命令：`pwsh -NoProfile -ExecutionPolicy Bypass -File ./runTests.ps1`，包含Release x64构建。
- 开始：`2026-10-20T14:06:57.2773236+08:00`。
- 原始日志：[本轮基线回归](../../artifacts/color-assistant-v22-intake-baseline-20261020.log)，与此前失败日志分开保存。
- 结束：`2026-10-20T14:19:29.8239511+08:00`，**退出码1，完整回归未通过**。
- 环境：Windows 11专业版 `10.0.26300`，窗口状态记录96DPI；多显示器实机项明确SKIPPED，不能视为Win10 4K200%或混合DPI验收。
- 已通过：1403项单元断言、45个真实格式夹具、143个PNG逐字节比对、色彩管理自检；基础观看80项、拖拽54项、键盘方向56项、连续平移6项、单图滚轮11项、阅读独立滚轮104项、二十档滑杆57项。以上不是457条设计用例的执行结果。
- 桌面让行实际发生一次：检测到沙枣`e2e_tabs.py`，等它退出并连续空闲4秒后恢复；没有关闭其他项目。
- **BASELINE-V22-001（未归因）**：惯性1–20档的启动/终点曲线断言已逐档通过；随后工具栏悬停检查失败，实际鼠标命中句柄`57477276`，被测主窗`27265230`。最后状态仍报工具栏隐藏。只凭这组句柄不能确定是其他窗口遮挡、测试路由还是产品问题；没有记录到失败时命中窗口的进程/类名，不擅自归咎于沙枣或用户，也没有放宽断言或改产品。
- 独立保存 [证据目录快照说明](../../artifacts/color-assistant-v22-intake-inertia-evidence/说明.md)、最后可读状态与二十档位移。目录包含历史文件，按时间和原日志区分，旧截图不当本轮成功证据。
- 惯性余项、之后的视觉/DPI/实际大小/常规设置与快捷键持久化完整审计/PSD主窗口/取色专项/最终格式兼容性阶段没有由本次入口跑完，保持未完成。后续先定位该异常，再重跑完整入口；单独复测通过不能替代或覆盖原失败。
- 回归结束后确认没有残留`YeImageViewer`进程；相对备份的非docs开发文件哈希变化数为0。
- 另已核对原包哈希、设计ID/字段保留与本次Markdown链接。没有把这些文档检查算作457条功能通过。

本轮只接入计划与核查既有候选，不以相同产品字节换新候选号，也不新建一个假称完成v2.2的绿色包。后续每批完成验证的产品变更仍须独立候选打包、解压复验及双归档。
