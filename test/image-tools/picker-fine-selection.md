# COLOR-004 最小复现夹具

`picker-fine-selection.svg` 保存与原生专项相同的240×180像素图案：底色#12ABEF，在(81,70)有一个蓝色#0000FF像素。无需用户私有图片。

自动测试不依赖SVG插值或查看器缩放：`ScreenPickerSession::selfTest` 用GDI在真实桌面上按上述整数物理坐标绘制相同图案；所有取样走生产抓屏/覆盖层/消息/剪贴板路径。

复现动作：鼠标停在(80,70)，方向键向右微调1px（系统鼠标不动），再左键确认。

- 正确：复制微调选中的蓝色#0000FF。
- 修复前：左键重新获取未移动的鼠标，退回底色#12ABEF。
- 同一用例修前构建63通过/1失败，修后64通过/0失败。原始证据分别为 `artifacts/color-picker-rc6-fine-negative.txt` 与 `artifacts/color-picker-rc6-screen-final.log`。

执行入口：`pwsh -NoProfile -File scripts/test-screen-picker.ps1`。测试必须使用隔离运行目录并在其他项目桌面自动化结束后运行，不修改原图。
