# Third-party notices

## Phosphor eyedropper / palette

`YeImageViewer/file/icons/color-picker.svg`、`color-palette.svg`采用Phosphor Icons regular eyedropper / palette路径，仅将currentColor固定为本项目图标色。palette来源：https://github.com/phosphor-icons/core/blob/main/assets/regular/palette.svg 。
来源：https://github.com/phosphor-icons/core/blob/main/assets/regular/eyedropper.svg

MIT License

Copyright (c) 2023 Phosphor Icons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## IconPark

The five overlay toolbar icons in `YeImageViewer/file/icons` adapt path geometry
from ByteDance IconPark. They have been restyled and embedded as local SVG
resources.

- Source: https://github.com/bytedance/IconPark
- License: Apache License 2.0
- Copyright: ByteDance and IconPark contributors

IconPark is provided under the Apache License 2.0. A copy of the license is
available at https://www.apache.org/licenses/LICENSE-2.0.

## LZMA SDK installer SFX

The one-click Windows installer uses `tools/installer/7zSD.sfx` from the
official LZMA SDK 26.02 distribution.

- Source: https://www.7-zip.org/sdk.html
- Copyright: Igor Pavlov
- License: Public domain

The official LZMA SDK page states that the SDK, including its SFX modules, may
be copied, modified, published, used, compiled, sold, or distributed for any
commercial or non-commercial purpose.

<!-- COLOR-CATALOG-NOTICES -->
# 色库来源与MIT许可

色值为各上游提供的sRGB近似，不是传统染料的测量标准；没有导入参考插件数据、描述、图片、音频。

## https://github.com/yeun/open-color

固定提交：3a716ee1f5ff5456db33cb8a6e964afdca1e7bc3

MIT License

Copyright (c) 2016 heeyeun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## https://github.com/zerosoul/chinese-colors

固定提交：a3e09ddc79c349473768e714d3180a04781a4e39

MIT License

Copyright (c) 2019 Tristan Yang

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## https://github.com/sen-ltd/japanese-colors

固定提交：2cc2a57dad6119c7995e6498ed6c5e73196e2a87

MIT License

Copyright (c) 2026 SEN LLC (SEN 合同会社)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


数据SHA-256：0D581435E6FEED442132E1A5C0202CAC840EC2CF07E77E95E967E5B81EB9E19C

## 用户提供的颜色助手5.0.1参考包数据

本次用户明确同意提取参考数据库。仅提取静态色名/色值/分类/渐变端点，不执行脚本、不复制应用代码、图像或品牌。来源uTools团队颜色助手5.0.1，参考脚本SHA-256：BD2C89EEFB0115498DBF6912F641D6F7D818446DDE04D30F478116F8842A118A。此部分不宣称属于上述MIT授权，不据此声称第三方已授予公开再分发权；v1.39.0按用户明确要求保留当前数据；本记录不构成对第三方授权范围的独立确认。渐变仅导入2～4色标数据，其他复杂条目不截断。
# PhotoCraft 图层与通道后端

`YeLayerBackend.dll` 直接使用 [PhotoCraft](https://github.com/storytold/photocraft) 的 PSD 解析、文档模型、CPU 合成及其依赖模块；固定源码版本为 `9b18f57344ff70d6f5aa640a0db7bc55596d5324`。本项目新增的 C ABI 适配层位于 `tools/layer-backend`，Win32 停靠界面保留本项目实现。`PsdBlendModes.h` 的混合菜单键及顺序改编自 `crates/color/src/blend.rs`。未复制品牌资源。

PhotoCraft 代码按 MIT 许可使用。完整上游 NOTICE、许可与实际依赖归属由构建脚本生成到 `LAYER_BACKEND_NOTICES.txt`，必须随 DLL 一起分发。这里只说明来源，不将上游自述的兼容率当成本项目验收结果。

rc17 的有界图层/蒙版缩略图采样移植自同一固定提交 `crates/ui-egui/src/lib.rs` 的 `layer_thumb`、`mask_thumb` 和 `thumb_image`；适配实现位于 `tools/layer-backend/src/lib.rs`。列表展示原层像素及独立蒙版，不冒称每个缩略图包含最终重合成的全部效果。

```text
MIT License

Copyright (c) 2026 ArtCraft Team and the PhotoCraft contributors
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
