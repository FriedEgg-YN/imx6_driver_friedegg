# LD2410C

本目录提供 LD2410C 雷达的内核模块、UAPI 头文件和两个用户态工具：

- `ld2410c.ko`：OUT GPIO/input、TTY line discipline、misc ioctl。
- `ld2410c_attach`：把 UART 切到 `LD2410C_LDISC`。
- `ld2410c_rawdump`：抓取 UART 裸字节，排查 baud 和帧格式。

## 构建

```bash
make -C src/ld2410c app
make -C src/ld2410c kernel_modules KERNELDIR=<kernel-tree> ARCH=arm CROSS_COMPILE=<toolchain-prefix>
```

## 文档

- [`docs/ld2410c-driver-and-attach.md`](docs/ld2410c-driver-and-attach.md)：驱动、attach、misc UAPI 和板端验证说明。
