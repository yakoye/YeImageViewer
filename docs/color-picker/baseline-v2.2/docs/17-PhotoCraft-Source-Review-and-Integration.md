# 17 · PhotoCraft 源码调查和移植取舍（2026-10-20 检查 GitHub main）

## 已核实资源
- 源码：https://github.com/storytold/photocraft
- `crates/psd/README.md`: https://github.com/storytold/photocraft/blob/main/crates/psd/README.md
- `crates/psd/src/{file,layer,tree,pixels,compression}.rs` PSD/PSB 的读取、延迟通道解码、图层树和元数据。
- `crates/compose/src/{lib,psblend,masks,advanced,multichannel}.rs` 合成与特殊混合支持。
- `crates/ui-egui/src/{channels_panel,channel_view,eyedropper_ui}.rs` 等界面参考。
- `Cargo.toml` 显示 Rust 2024 edition、workspace 引用了 doc/compose/io/gpu/ui 等独立 crates。
- 许可证：仓库根部 `LICENSE-MIT` 与 `LICENSE-APACHE`，二者择一；第三方资源/字体/图标还需要各自授权；**不要移植产品名称、Logo、品牌设计资产**。

## 功能事实（区分库与软件）
- `photocraft-psd` 是可独立引用的读取/写入 Rust crate，可解析 PSD/PSB 元数据、层、组、mask 和 channels，并能读取 merged image；**这个独立 crate 明确声明 No compositing**。
- PhotoCraft 整体软件还有 `compose` / document / UI 模块执行图层效果、合成及复杂工作流。因此只“拷贝 crate/psd + UI”不能得到 Photoshop 一样的图层开关视觉效果。
- PhotoCraft README 中对大型 PSD/PSB、复杂颜色模式、分层、颜色管理、图层效果有较广覆盖，但其项目自述仍处于早期 alpha。不要将 README 中的覆盖统计视为 YeImageViewer 迁移后测试结果。

## 当前 YeImageViewer 的已有基础
- 原生 C++/Win32/D3D11 看图架构；`YeImageViewer/include/PsdCompositeDecoder.h` 注释明确：**“只读取 Photoshop 保存的最终合成图，不展开和重建图层”**；具备 ICC 色彩流程相关代码。
- `YeImageViewer/include/PsdMemoryFile.h` 使用已有的 psd_sdk 只读内存桥接，说明工程已内置部分 PSD 生态接口（需审计其许可证和用法）。
- `OverlayLayout.h` 和 `ToolbarCommand.h` 已用于按钮排布与命中逻辑；`OperationToast.h` 已有 toast 的布局与文案基础。

## 推荐路线（先 POC 不盲目移植）
A. **最低风险首选**：保留现有 C++ PSD merged renderer，复用已集成 psd_sdk 解析层树/通道，补统一数据模型；再按能力逐步实现 C++ 重建合成，可优先支持普通像素层、组和简单 alpha。
B. **可选 Rust FFI POC**：构建 PhotoCraft `psd` 子 crate + 自己写的最薄 `extern "C"` ABI 封装 (Rust `cdylib` 或静态库)，传递文件映射只读视图/句柄、层树和按需解码像素。**Rust 错误不得跨 C 边界 panic；显式内存释放、资源限额、版本管理，测量二进制体积/冷启动/加载速度**。如果还要真实混合模式，需单独整合 `compose` 及 doc/color 依赖，不得假设可直接链接单个 psd crate。
C. **不要**为了面板引入 PhotoCraft 的整个 egui/Rust 应用作为查看器 UI；这与 YeImageViewer 的小巧快速定位相冲突。

## POC 决策门槛
实现样例：简单 PSD、带组/蒙版/混合模式、16bpc、PSB、特殊层、坏文件各一；对比原合成基线、Photoshop 同状态输出；记录解析/合成正确率、内存、启动时间、体积、许可证和依赖。实测报告后由维护者决定选 A/B，而非先强制移植。

## 第三方合规
MIT/Apache-2.0 可在满足许可条件下商业/非商业使用，但需要保留许可证及版权/NOTICE/第三方归属，并核实子模块实际许可。明确代码贡献来源；不得复制仓库中商标、品牌设计，除非其品牌协议允许。
