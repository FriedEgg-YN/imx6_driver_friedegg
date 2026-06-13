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

## 尝试复现autoload bug发现此bug消失

1. 修改 nfs 的rootfs，采用 mdev 尝试复现自启动
	1. 删除 S90imx6-monitor，避免其自动 insmod
	2. 恢复默认 S10mdev
	3. 恢复默认 mdev. conf
	4. 如果 mdev 自启动没问题，去除 S90部分. ko 相关功能；如果有问题，尝试定位、解决

现象如下：
1. 临时禁用S90imx6-monitor 后，手动加载 mx6s_capture. ko 及 ov5640. ko，可以运行 test 显示摄像头内容
2. 恢复默认 S10mdev 后，启动关键日志如下；lsmod 发现除了上述摄像头驱动，还有 evbug；i2cdetect 发现0x3c 标志位为 UU，没问题；经验证采用默认 S10mdev 没问题
3. 恢复默认 mdev. conf，经过ov5640_test也没问题

```txt
Saving 2048 bits of non-creditable seed for next boot
Starting syslogd: OK
Starting klogd: OK
Running sysctl: OK
Starting mdev... OK
1-003c supply DOVDD not found, using dummy regulator
1-003c supply DVDD not found, using dummy regulator
1-003c supply AVDD not found, using dummy regulator
camera ov5640, is found
CSI: Registered sensor subdevice: ov5640 1-003c
Starting network: ip: RTNETLINK answers: File exists
FAIL
Starting crond: OK

# i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- -- -- -- -- -- -- -- -- 
10: -- -- -- -- 14 -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- UU -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- -- 

```
