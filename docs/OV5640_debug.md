# OV5640移植debug过程

## TODO

- [ ] 分析`mdev`引起`ov5640` bug原理，精细修改，而不是一刀切关闭`autoload`

## 参考资料

-  [移植ov5640摄像头到imx6ull开发板（一）_imx6ul摄像头-CSDN博客](https://blog.csdn.net/qq_55389904/article/details/132538449)
- [移植ov5640摄像头到imx6ull开发板（二）_linux移植ov5640驱动-CSDN博客](https://blog.csdn.net/qq_55389904/article/details/132568689?spm=1001.2014.3001.5502)
- [移植ov5640摄像头到imx6ull开发板（三）_ov5640驱动移植-CSDN博客](https://blog.csdn.net/qq_55389904/article/details/132561962?spm=1001.2014.3001.5502)

## debug过程


- 移植后一直显示显示 `reg write/read error`
	- `datasheet` 检查对应 `regaddr` 含义，根据 `probe` 定位 `error` 发生位置，无果
	- 多次重复发现 `error` 地址总在变换，与单个寄存器无关，而是 `i2c` 通信存在问题
	- `i2cdetect` `OV5640` 地址 `0x3c` 显示 `UU`
	- 测量各个引脚电压
	- 修改 `i2c` 频率
- `mdev`默认 `autoload`
	- 注意到 `autoload` 过程中显示 `camera ov5640 is found`
	- 随后先 `rmmod mx6s_capture` 显示 `send error`、`camera ov5640 is not found`，再 `rmmod ov5640`
	- 再次先后 `insmod mx6s_capture`、`insmod ov5640`显示 `camera ov5640 not found`，认为自启动可能有问题
- 利用 `overlay` 关闭 `mdev` `autoload` 功能，测试 `module` 多次卸载安装稳定运行；重新打开 `autoload`，发现再次出现该 bug，确认是由 `mdev` 自动加载引起
