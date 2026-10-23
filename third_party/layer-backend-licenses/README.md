# 图层后端许可补充

Cargo 发布包遗漏许可文件时，使用该发布包 `.cargo_vcs_info.json` 所指的**同一源码提交**补齐，不能只保留 SPDX 名称。Zune 旧版选用其包元数据及源文件头允许的 Zlib 许可，版权声明另保存在对应 `.txt` 中。

| 文件前缀 | 上游源码提交 | 仓库内来源 | SHA-256 |
|---|---|---|---|
| alloc-stdlib-0.2.4.txt | `dropbox/rust-alloc-no-stdlib@ae42d22078b98549e987d2f03d12df7b984fde47` | `LICENSE` | `C0C56F26D9C051CAC4D200C34C84E7AE9AAA853E01A982A1DF08B09931E518AE` |
| pulp-wasm-simd-flag-0.1.1.txt | `sarah-quinones/pulp@5eb07fd7b68edf0a5e19f71737d315f72a510295` | `LICENSE` | `D64F878C89BD5F1E5ADA7E4AAD57690B122727CF5229B7B87EA1980C188DA1D9` |
| zune-core-0.4.12.txt | `etemesi254/zune-image@f8fbb123d5ed04441e8324a555bfcda0cb1bd28f` | `LICENSE.md` | `C6DFF146A9F31848AC296FAA5A08A4253CAF2C384C86F906DC99E7FC0A39CC8C` |
| zune-jpeg-0.4.21.txt | `etemesi254/zune-image@fa2c767a01d7d9373911d0bf63e0588553d67e0e` | `LICENSE.md` | `C6DFF146A9F31848AC296FAA5A08A4253CAF2C384C86F906DC99E7FC0A39CC8C` |
| zune-inflate-0.2.54.txt | `etemesi254/zune-image@69502ce83fdfecdd0beefd677e2abb3781b29d98` | `LICENSE.md` | `E870D7FD7819A9F0453C62798CB52AE95D148A8C91DC2DE1D983F997143C72DE` |
| 上述三份 Zune 的 `-LICENSE-ZLIB.txt` | 各自同一提交 | `LICENSE-ZLIB` | `7FA429541E55B1509909E058F2D21A37467E4958EC713B357F6E0CF9DC4EE352` |

原始 URL 规则：`https://raw.githubusercontent.com/<仓库>/<提交>/<仓库内来源>`。后端构建从实际 Windows 依赖图提取归属，生成 `LAYER_BACKEND_NOTICES.txt` 随绿色包交付。未带入 PhotoCraft 商标、Logo 或整个编辑器界面资源。
