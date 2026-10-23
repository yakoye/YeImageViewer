# PhotoCraft 图层后端

## 目的及来源

直接链接 `storytold/photocraft@9b18f57344ff70d6f5aa640a0db7bc55596d5324` 的 PSD 导入、文档模型、像素及矢量蒙版、调整、效果与 CPU 合成模块。代码按 MIT 许可使用，不携带整个 egui 编辑器或品牌资源。具体归属见根目录 `THIRD_PARTY_NOTICES.md` 与构建生成的 `LAYER_BACKEND_NOTICES.txt`。

`scripts/photocraft-psd-entry.patch` 只将原有 `import_psd` 函数改为 `pub`，导入算法不变。它包括合成图损坏恢复、警告和文字缓存准备；不能退回仅解析格式层。构建校验上游提交与唯一允许的一行差异，有其他改动时拒绝覆盖。

## 构建

```powershell
./scripts/build-layer-backend.ps1 -PrepareToolchain
./buildRelease.ps1
./runTests.ps1
```

首次准备可下载固定 SHA-256 的 Rust 1.95 Windows MSVC 便携工具链，只放在项目 `artifacts/rust-toolchain`；不修改用户或系统 PATH。正常构建使用 Cargo.lock 和固定源码。输出 `x64/Release/YeLayerBackend.dll` 及完整许可文本，打包时两者必须一起保留。主程序仍由原有 v145/C++23 构建。

## C ABI v1

调用方通过绝对 EXE 目录、受限 DLL 搜索规则惰性加载，不在普通看图/取色首帧加载后端。句柄只在单个 PSD 会话的串行后台任务中使用。

- `yl_open / yl_close`：只读输入字节，独立拥有导入文档。输入及回调在调用期间必须有效。
- `yl_info / yl_row / yl_channel_name / yl_warnings`：有长度的 UTF-8 与固定布局标量，不跨模块传 Rust/C++ 容器。
- `yl_render / yl_free_pixels`：完整状态覆盖、COW 文档快照、返回 BGRA 显示预览。返回的像素由原 DLL 释放；C++ RAII 保证异常路径也释放。
- 所有句柄和指针必须来自本模块或调用方声明的有效缓冲，不能跨线程同时渲染和释放。空指针与尺寸检查不等于支持任意伪造地址。
- 取消回调不得抛异常、不得改 UI，只有读原子会话代数；导入分层与合成分带检查取消。旧代结果在 UI 端再次丢弃。
- 状态码：0成功、1参数/数据错误、2限额、3取消、4捕获到内部panic、5为C++侧缺少/不兼容DLL。panic不越过ABI；这不是防止操作系统内存耗尽的沙箱。

上限：文件256MiB、画布16Mi像素、图层记录10,000、树深64、预计解码平面总量1GiB。普通原合成图路径与这些互动限额独立。模块无原图写入/导出接口，不会因旋转、显隐或关闭而写回PSD。

## 验证及边界

```powershell
./x64/Release/YeImageViewerTests.exe --photocraft-selftest
./x64/Release/YeImageViewer.exe --psd-layer-audit <文件> <诊断目录>
```

独立给定RGB/蒙版像素、CMYK旧路径失败对照、PSD/PSB 8/16/32位四压缩、取消/无效值恢复以及29个公开Photoshop合成参考已登记进核心回归。溶解只检查上游同样的随机密度标准，不能声称随机纹理逐像素一样。完整窗口回归仍是单独的发布闸门。

`examples/mask_fixture.rs` 生成最小四像素公开蒙版夹具，最终合成值独立指定；`examples/merged_reference.rs` 通过独立解析器读取保存合成图，仅用于离屏诊断，不是对输入文件的保存。

直接复用不等于完整Photoshop兼容。`43.psd` 的重建与文件已存合成仍有明显局部差异，`42.psb` 亦非逐像素完全相同；两边均只读核验。原始状态保留已存合成图，临时浏览不自动禁用，但应显示差异提示。完整面板键盘/多选/拖动及复杂CMYK仍需后续开发与同状态外部验收。
