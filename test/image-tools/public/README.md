# 独立 PSD 分组夹具

`ag-psd-group-blend.psd` 来源于 [ag-psd](https://github.com/Agamnentzar/ag-psd/blob/04d927df87130ad6d765bc63d51ade0a98d66c14/test/read-write/group-blend/src.psd)，固定提交 `04d927df87130ad6d765bc63d51ade0a98d66c14`。MIT 许可见同目录 `LICENSE-ag-psd.txt`。

文件包含穿透组和正片叠底组，保留上游保存的最终合成图作为独立参考；不使用本项目合成器输出制作金标准。回归同时检查物理记录顺序、分组树、Alpha 和预乘颜色，两个8位图层的合成舍入容限为2 LSB。不包含用户私人 PSD。
