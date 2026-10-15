# 格式兼容性最小夹具

全部由本目录 `palette.ppm` 与 `scripts/generate-compatibility-fixtures.ps1` 生成，不包含用户作品、相机照片或原始 EXIF。

- 48 个 PSD／PSB：两种容器、RGB／CMYK／灰度／Lab、8／16 位、未压缩／RLE／ZIP，均为 4×2 像素。
- `alpha/` 另有 12 个透明 PSD／PSB，覆盖 8／16 位及三种压缩；合计 60 组独立参考逐像素比较。
- 同名 PNG 为独立 ImageMagick 参考解码，专项脚本逐像素比较 RGBA，量化允许最大 3 级误差；不能用仅尺寸正确作为颜色验收。
- `metadata-large.tif`：4×2 RGB 像素加 128 KiB 全零 Photoshop 二进制标签，复现大型元数据被展开成数字字符串的问题；要求输出保留字节数与省略说明，而非巨型文本。
- 单元代码另构造 8／16／32 位 ZIP 预测与透明通道、CRC 损坏、PackBits 越界、截断和内存预算夹具。
- 旧 16 位 RLE／透明 PSD 像素回归仍保留；新增 CMYK PSD／PSB 纳入真实 COM 缩略图调用链测试。

再生成需要 ImageMagick；日常 `runTests.ps1` 直接使用已保存的夹具，无需现场下载或生成。原始六份用户文件只在本机 ignored `artifacts/compatibility-v1382` 记录路径、哈希和诊断结果，不复制进公开仓库。
