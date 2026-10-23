# 第三方颜色助手5.0.1参考与许可注意

## 输入资料

- 用户上传：`颜色助手-5.0.1.zip`，本包解压副本路径为 `reference/utools-color-helper-5.0.1/`（所有压缩包内部路径均为英文ASCII）。
- 用户提供的七张截图：`reference/screenshots/fig-01-picker.png` 到 `fig-07-favorites.png`。

## 已观察到的打包内容

- `plugin.json`：uTools插件元信息及功能入口。
- `preload/preload.js`：依赖uTools/Node桥接。
- `assets/index-CnOXSPuO.js`：压缩构建产物而非可读开发源项目。
- `assets/index-DxxDyN9L.css`：打包样式；以及模板jpg和logo。
- `index.html`/`package.json`：运行容器与配置。
- 使用uTools提供的`window.utools.screenColorPick`等函数，不能直接移植到独立Windows进程。

## 可以参考

- 图2/3字段信息架构：HEX/RGB/HSV/HSB/HSL/CMYK/HSI/Lab，色轮、亮度滑块、色组、复制按钮。
- 图4 UI色卡分类；图5中国24节气及日本传统色分类；图6渐变卡片；图7收藏的空状态和添加入口。
- 模块概念和标准颜色换算算法可重新实现并对照公开标准测试。

## 不允许未经授权直接搬运

- 未附可确认复用许可证，不能自动假定可分发/修改打包JS/CSS/logo/模板/专有色名数据。
- 如果Codex为理解行为静态阅读，只能整理思路；交付代码需独立原生实现；每种UI色卡数据必须核实官方授权并在第三方NOTICE登记。
- 不得用utools依赖、嵌入其插件环境或直接复制内部访问密钥绕过API限制。

## 取色历史差异

此前评估表明原插件部分界面含临时最近色状态，但不能证明可用的持久历史设计。YeImageViewer需独立设计正式历史模块；不要复刻一个仅保留6色的临时数组。
