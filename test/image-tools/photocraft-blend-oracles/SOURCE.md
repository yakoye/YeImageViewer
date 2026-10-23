# 独立混合模式参考

来源：[psd-tools 测试素材](https://github.com/psd-tools/psd-tools/tree/1ffd8950eecada389899978b76c118b0cac3c8f0/tests/psd_files/blend-modes)，固定提交 `1ffd8950eecada389899978b76c118b0cac3c8f0`。仅取该目录小于50,000字节的29份 PSD，保留上游 MIT 许可 `LICENSE-psd-tools.txt`。无用户私有文件。

测试重新导入各图层并调用实际 PhotoCraft DLL，拿文件由 Photoshop 保存的最终合成图做独立金标准；不是用本后端自己的输出生成金标准。普通混合比较 Alpha 与预乘 RGB，每分量误差不超过2 LSB。

溶解的 Photoshop 随机数发生器未公开，上游自身也不声称随机纹理逐像素相同；使用上游已公布的16×16块密度容限0.1，溶解层边界外仍逐像素2 LSB。测试输出单独标明“块密度”，不能在报告中写成所有27种模式像素完全相同。

`group-divider-blend-mode.psd` 的最终合成图是上游登记的损坏数据；本例只验证图层恢复，不用于像素一致性断言。
