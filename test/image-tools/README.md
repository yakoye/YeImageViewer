# 轻工具最小复现夹具

由单元测试程序 `--generate-tools-fixtures test/image-tools` 生成，生成器在 `YeImageViewerTests/ImageToolsTests.h`。

24 个小 PSD/PSB：8/16/32 位 × RAW/RLE/ZIP/ZIP预测 × PSD/PSB。单图层包含不透明红和半透明绿两个像素，中文名称“测试”、隐藏状态、不透明度128，以及负坐标。合成图独立存在；主解码器不依赖图层查看器。

orientation-alpha.png 为3×2非对称透明图，用于旋转/镜像方向、保存和源文件不变验证。均为合成数据，不包含用户图片或隐私。

two-layers.psd 包含两层，不同的红/绿顺序用于验证真实列表选择第二层时确实更新了预览。
