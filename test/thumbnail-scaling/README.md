# 缩略图缩减解码回归素材

这些都是自动生成的色块，不含用户图片、个人信息或第三方 ICC 内容。用于替代无法公开的大型 CMYK 海报。

- `large-rgb.jpg`：8192×4096 RGB 四象限。请求 256×128 缩略图时，旧路径分配整图 BGRA，独立进程峰值提交增加超过 128 MiB。
- `large-cmyk.jpg`：同图转 CMYK，没有嵌入 ICC。验证原生缩减后的墨量通道、K 通道及基础转换，而非 WIC 默认印刷色彩转换。
- `cmyk-icc.jpg`：相同墨量数据，嵌入自建的非恒等 CMYK 输出 profile，确保忽略 ICC 会失败。
- `odd-cmyk.jpg`：3543×2362，目标必须为 256×170；不能用原生 IDCT 分别取整后的宽高反算最终比例，导致多出一行。
- `synthetic-cmyk.icc`、`synthetic-srgb.icc`：测试程序用 LittleCMS 创建的小型合成 profile，非用户文件提取；前者为三节点四维 CLUT、Lab PCS，使用非恒等墨量曲线。
- `tall-alpha.png`：8×4096 半透明纯色，最终 1×256，检查极端长宽比和预乘透明。
- `small.png`：5×3，不应放大。
- `cmyk-16.tif`：2048×1024、16 位 CMYK、ZIP 压缩，验证没有原生缩减时的完整解码与颜色转换兜底。
- `cmyk-checker.tif`：1024×1024 细密黑白点，验证完整解码兜底也使用面积缩小，不能用最近邻把高频纹理采样成纯黑/纯白。
- `*-256.png`：ImageMagick 独立解码/缩小所得参考；带 ICC 的 JPEG 使用明确的目标 sRGB profile 转换后移除 profile，避免把颜色空间标签变更当成 ICC 转换。

## 生成与运行

先运行仓库根目录 `buildRelease.ps1` 构建测试程序，再运行：

```powershell
./scripts/generate-thumbnail-scaling-fixtures.ps1
./scripts/test-thumbnail-scaling.ps1
```

生成需要本机 ImageMagick，正常 `runTests.ps1` 不需要它，直接使用已保存夹具。

内存探针每例使用独立进程，记录 GetThumbnail 前后峰值提交量差；64 MiB 闸门针对 33 MP JPEG 的整图分配缺陷，不用毫秒数作为自动化通过条件。测试同时比较实际 COM 输出与参考 PNG（预乘 BGRA，平均误差≤4、最大误差≤32），固定检查尺寸/不放大。阈值覆盖 16 位 TIFF 量化与边缘采样差异，不允许通过全空或错误尺寸冒充成功。

旧 DLL 可显式传给 `test-thumbnail-scaling.ps1 -Provider <路径>` 重现内存失败；带 ICC 夹具也可通过测试程序的 `--thumbnail-memory-probe` 单独证明旧颜色误差。旧正式 DLL 不保存在公开素材目录。
