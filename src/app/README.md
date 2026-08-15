# README

在之前开发过程中，实际发现利用AI辅助开发smart monitor项目，虽然能够快速实现功能，但实现过快反而让我难以跟上，打算先用那一版作为面试，有时间有需要自己手写一些Qt，提升一下代码能力。

## 架构

希望实现类似手机的HMI，在不同sensor基础上实现若干个app，包括：单sensor可视化app(同时充当测试工具)，复合sensor app，无sensor纯应用。积累从驱动到应用的全链路开发能力。考虑具有若干传感器，传感器和不同应用间存在一对一、多对一、一对多等多种关系。有以下问题需要明确：

1. 使用哪些c++特性？单继承？嵌套多少层？
2. 使用哪些qt模块？
3. 是否考虑实现device基类，其他继承？
4. 如何进行前期代码架构规划、边界控制？

```txt
app/
    device/
        ov5640/
        ld2410c/
        ap3216c/
    application/
        smart_monitor/
            state_machine/
            sensor_controller/
            ui/
        camera/
            state_machine/
            sensor_controller/
            ui/
        xxx_test/
    README.md
```

## SPEC

详细的设计优先级、第一版范围、架构与线程边界、驱动数据链路学习目标、Smart Monitor 迁移原则、手写实施顺序和验收标准见 [SPEC.md](SPEC.md)。本 README 只保留项目动机和概要，后续以 `SPEC.md` 作为重构规格的权威位置。

当前实施原则是先完成从 Linux 驱动 ABI 到业务状态机、Camera/媒体链路和 Qt UI 的纵向闭环，再根据真实并发或扩展需求引入 AppCatalog、lease、request ID 等平台机制。
