# AP3216C 日志分级说明（当前实现）

本文档汇总当前驱动日志分级所使用的内核日志 API，重点说明每个函数的作用、输出效果、使用方法和最佳实践。

## 0. 说明

本文只说明日志分级原理与实践；App 具体测试命令统一见 `APP_TESTING_GUIDE.md`。

## 1. 当前日志体系概览

当前代码采用 Linux 内核最经典的两类日志接口：

- 模块级日志：`pr_info`、`pr_err`
- 设备级日志：`dev_info`、`dev_warn`、`dev_err`、`dev_dbg`

同时在文件头使用了：

- `#define pr_fmt(fmt) "ap3216c: " fmt`

它会给当前源文件里的 `pr_*` 日志统一添加前缀，便于在 `dmesg` 中检索。

## 2. 各日志函数详解

### 2.1 pr_info

作用：
- 打印模块级普通信息，表示关键流程进入或成功完成。

效果：
- 输出到内核日志缓冲区，可通过 `dmesg` 查看。
- 受内核日志等级影响，通常默认可见。

使用方法：
```c
pr_info("register i2c driver\n");
```

当前典型场景：
- 驱动注册开始、注册成功、注销流程等模块生命周期日志。

最佳实践：
- 只在低频关键节点打印，不要在高频路径使用。
- 消息内容简洁明确，包含动作和对象。

### 2.2 pr_err

作用：
- 打印模块级错误，表示模块初始化/退出路径出现失败。

效果：
- 错误级日志，在排障时优先关注。

使用方法：
```c
pr_err("i2c_add_driver failed: ret=%d\n", ret);
```

当前典型场景：
- `i2c_add_driver` 失败。

最佳实践：
- 一定要带返回码 `ret`。
- 失败日志与返回路径保持一致，避免“打印错误但返回成功”。

### 2.3 dev_info

作用：
- 打印设备级普通信息，自动带设备上下文（设备名、总线信息）。

效果：
- 相比 `pr_info`，定位到具体设备更直接。

使用方法：
```c
dev_info(&client->dev, "probe success: i2c addr=0x%02x\n", client->addr);
```

当前典型场景：
- probe 成功、remove、open 成功等低频事件。

最佳实践：
- 仅用于里程碑事件，避免刷屏。
- 优先在有 `struct device` 上下文时使用 `dev_*` 而不是 `pr_*`。

### 2.4 dev_warn

作用：
- 打印设备级警告，表示非致命异常。

效果：
- 不一定导致流程失败，但提示调用方或上层逻辑不规范。

使用方法：
```c
dev_warn(&client->dev, "read buffer too small: cnt=%zu need=%zu\n", cnt, sizeof(data));
```

当前典型场景：
- 读接口参数不符合预期（缓冲区太小）。
- 单寄存器读取失败但函数继续返回默认值。

最佳实践：
- 用于“可继续运行但值得关注”的情况。
- 警告必须描述“现值 vs 期望值”。

### 2.5 dev_err

作用：
- 打印设备级错误，表示当前操作失败，通常会返回错误码。

效果：
- 排障优先级最高；建议所有失败路径都补充。

使用方法：
```c
dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n", ret, reg, len);
```

当前典型场景：
- I2C 读写失败。
- `copy_to_user` 失败。
- 复位/使能传感器失败。
- probe 过程 cdev/class/device 创建失败。

最佳实践：
- 附带最小可复现上下文：返回值、寄存器地址、长度、关键参数。
- 一条错误日志对应一个明确失败点。

### 2.6 dev_dbg

作用：
- 打印设备级调试信息，用于开发期深度排查。

效果：
- 默认可能不可见；开启动态调试后可见。
- 适合记录高频细节（如寄存器读写成功、传感器原始值）。

使用方法：
```c
dev_dbg(&client->dev, "sensor raw: ir=%u als=%u ps=%u\n", dev->ir, dev->als, dev->ps);
```

当前典型场景：
- I2C 读写成功日志。
- 每次采样后的原始数据日志。

最佳实践：
- 高频路径只放 `dev_dbg`，不要用 `dev_info`。
- 发布版本默认保持关闭，排障时按需打开。

## 3. 日志级别与场景映射建议

推荐按下面规则选级别：

- `dev_err` / `pr_err`：本次操作失败，返回负错误码。
- `dev_warn`：异常但可继续运行，不立即失败。
- `dev_info` / `pr_info`：低频关键路径状态（probe/open/remove/init/exit）。
- `dev_dbg`：高频细节、调试过程数据。

## 4. 动态调试（dev_dbg）使用方法

`dev_dbg` 通常配合 dynamic debug 使用。

查看是否支持：
```sh
ls /sys/kernel/debug/dynamic_debug/control
```

按文件开启调试：
```sh
echo 'file ap3216c.c +p' > /sys/kernel/debug/dynamic_debug/control
```

按文件关闭调试：
```sh
echo 'file ap3216c.c -p' > /sys/kernel/debug/dynamic_debug/control
```

查看日志：
```sh
dmesg | tail -n 100
```

说明：
- 若 `control` 文件不存在，通常是内核未开启 `CONFIG_DYNAMIC_DEBUG` 或 debugfs 未挂载。

## 5. 常见错误与规避

- 错误：在高频路径使用 `dev_info`。
- 后果：日志洪泛，性能下降，关键错误被淹没。
- 规避：高频路径仅用 `dev_dbg`。

- 错误：日志不带上下文（只打印“failed”）。
- 后果：无法复现或定位失败点。
- 规避：至少包含 `ret` 和关键参数（如 `reg/len/cnt`）。

- 错误：有错误日志但返回成功。
- 后果：上层误判，隐藏真实故障。
- 规避：日志与返回路径保持一致。

- 错误：设备上下文存在却使用 `pr_*`。
- 后果：丢失设备定位信息。
- 规避：优先使用 `dev_*`。

## 6. 推荐模板（可复用）

I2C 读失败模板：
```c
ret = i2c_transfer(client->adapter, msg, 2);
if (ret != 2) {
    dev_err(&client->dev, "i2c read failed: ret=%d reg=0x%02x len=%d\n", ret, reg, len);
    return -EREMOTEIO;
}
```

用户拷贝失败模板：
```c
err = copy_to_user(buf, data, sizeof(data));
if (err) {
    dev_err(&client->dev, "copy_to_user failed: uncopied=%ld\n", err);
    return -EFAULT;
}
```

模块生命周期模板：
```c
pr_info("register i2c driver\n");
ret = i2c_add_driver(&ap3216c_driver);
if (ret)
    pr_err("i2c_add_driver failed: ret=%d\n", ret);
```

## 7. 一句话总结

当前日志方案是“模块用 `pr_*`、设备用 `dev_*`、高频细节用 `dev_dbg`、错误路径必须带上下文”的经典内核写法，代码体积小、可读性高、排障效率好。
