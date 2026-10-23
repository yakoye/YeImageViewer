# YeImageViewer Color Picker & Color Assistant — 完整开发需求包 v2.0

> **重要：这是完整需求，不是只做三个功能的精简版。** 2026-10-20修订；请以本包`docs/01-Full-PRD.md`为准。

## 用户最新确认的范围

1. **先实现**全屏快捷取色（图1）、取色后颜色调整（图2/3）、永久取色历史。
2. **继续全部实现**原截图与讨论的UI色卡、图片色卡、传统色、渐变、收藏、图片原像素取色、配色推荐、主题、色值导入导出、各种复杂显示器边界等。
3. AI/自然语言配色放到**最后P4**，基础离线配色必须独立工作；用户未来明确说取消AI才能调整范围。
4. **不允许测试用例缩水**：v1.0正式100条保持不变 + 159条全新补充 = 本版 **259个唯一正式测试ID**；v1.1的81条完整留档用于交叉验证。所有测试默认`NOT_RUN`，需Codex实际执行。

## 推荐阅读顺序

| 文件 | 用途 |
|---|---|
| `docs/08-Codex-Master-Prompt.md` | 交给Codex直接执行的完整开发指令 |
| `docs/01-Full-PRD.md` | 全范围需求、优先级与取色历史增强 |
| `docs/02-UX-Interaction.md` | 用户7张截图的布局、操作与键位 |
| `docs/03-Windows-Architecture.md` | Windows取屏/多屏/DPI/源图采样、线程与颜色算法 |
| `docs/04-History-and-Data.md` | 历史事务、坐标隐私、收藏/方案、导入导出及迁移 |
| `docs/05-Complete-Test-Plan.md` | **259条测试用例**和环境矩阵 |
| `docs/06-Traceability-Matrix.md` | 功能到测试的追溯矩阵 |
| `docs/07-Implementation-Roadmap.md` | 从P0到P4乃至最终发布的完整阶段计划 |
| `docs/09-Test-Report-Template.md` | 仅模板，待真机填写 |
| `docs/10-Release-Checklist.md` | 发布检查清单 |
| `docs/11-Reference-Review.md` | 参考插件检查与版权注意 |
| `docs/12-Decision-Log.md` | 解决v1.0/v1.1冲突的明确决策 |
| `templates/Test-Case-Registry.csv` | **259条测试的UTF-8 BOM CSV**，导入测试工具可直接填状态 |

## 参考文件

- `reference/screenshots/`：原用户7张截图（PNG），对应图1～7。
- `reference/utools-color-helper-5.0.1/`：从用户上传的参考运行包解压的只读版本；未知许可证，**不要直接复用代码与专有数据**。
- `archive/`：v1.0全部正式文档（含100条测试）与v1.1完整文档（含81条），均保留供核对，不作为新的删减范围指令。

## Codex使用方法

1. 将本ZIP解压到YeImageViewer工程的`docs/color-picker/`；具体路径可结合已有仓库`docs/`组织。
2. 让Codex先读`docs/08-Codex-Master-Prompt.md`和所有现行docs，核对参考截图与实际项目源码。
3. 按M0→M1(P0)→M2(P1a)→M3(P1b)→M4(P2)→M5(P3)→M6(P4)→M7(全部验收)完成；不得只交付前3项便结束整个任务。
4. 每阶段把真实构建/测试报告、缺陷和源代码变更纳入原项目正式`docs/`，未跑的测试继续写NOT_RUN。

## 交付状态

**仅提供开发需求、参考材料、测试用例与指令；不包含新的YeImageViewer可执行源码、不宣称已实现或通过真实测试。**
