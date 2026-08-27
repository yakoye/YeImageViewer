# 回归测试素材

此目录保存曾触发真实缺陷的原始文件。新增素材时，应同时在根目录的 `runTests.ps1` 中加入自动化回归步骤，并记录缺陷现象和预期行为。

## Image crash/dji_export_photo_20260809221510044.jpg

- 来源：用户提供的 DJI MotionPhoto 原图。
- SHA-256：`00E55EF88EF3897D52D950839C676508D21F3D26AA0D549F8B4CFA96630FFC72`
- 历史缺陷：内嵌视频的 H.264 像素格式无法识别，FFmpeg `libswscale` 断言使程序以 `0xC0000409` 退出。
- 预期行为：无法解码内嵌视频时安全降级为静态 JPEG，窗口保持打开并正常响应。

## HDR 色彩通道回归

- 上游问题：[`jark006/JarkViewer#45`](https://github.com/jark006/JarkViewer/issues/45)。
- 历史缺陷：Radiance HDR 的 RGB 数据被当作 BGRA 显示，表现为红色与蓝色互换。
- 对比图：`HDR color error/HDR_compared.png`，SHA-256 为 `8010D5E627002BEE7591B86EF4BE965872C66C397F402F99F59EBF5D52C8F49D`。
- 原始文件：`HDR color error/HDR.hdr`，2560×1600，SHA-256 为 `1A1A661E0A22BECBE019B6C095004315351F28600D9BD7600BD933BEB351E5D5`。
- 自动化方式：`YeImageViewerTests` 先用内存中的纯红、纯蓝最小 Radiance HDR 验证精确 BGRA 通道顺序，再解码真实原图并验证尺寸和红/蓝通道统计关系；`runTests.ps1` 会先校验真实原图哈希，防止夹具被意外替换。

## 背景选择回归

- 复用透明矢量图 `SVG Blurring/SittingHuman.svg` 验证右键菜单中的“透明、白色、黑色、毛玻璃”四种背景。
- 预期行为：透明模式显示棋盘格，白/黑模式精确合成透明像素，毛玻璃模式将预乘 Alpha 图像交给 Windows DWM 桌面亚克力合成；不支持的系统回退为主题底色。
- 自动化方式：`YeImageViewerTests` 对四种背景和半透明像素做精确像素断言；`runTests.ps1` 打开真实 SVG 并依次切换四种菜单命令，验证窗口始终存活且可响应。
