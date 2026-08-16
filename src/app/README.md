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

## 手写学习计划

学习计划位于 [`docs/plan/`](docs/plan/)，从阶段 0 的旧实现与驱动 ABI 调查开始，按纯状态机、Qt 对象和线程模型、单设备 App、Camera/媒体、Smart Monitor 集成、Shell 与交付的顺序推进。

| 入口 | 内容 |
| --- | --- |
| [`00-learning-roadmap.md`](docs/plan/00-learning-roadmap.md) | 阶段 0-8 总路线、01-21 实验导航、学习纪律和验证主线。 |
| [`01-current-code-review.md`](docs/plan/01-current-code-review.md) | 将当前半成品作为审查练习，识别构建、接口和状态机问题。 |
| [`04-value-types-and-state-model.md`](docs/plan/04-value-types-and-state-model.md) | 从类型化状态开始重写 `MonitorEngine`。 |
| [`08-qt-object-model.md`](docs/plan/08-qt-object-model.md) | QObject、signal/slot、event loop 和 ownership 基础。 |
| [`10-ap3216c-backend.md`](docs/plan/10-ap3216c-backend.md) | AP3216C 起始设备实验，随后按总路线进入 LD2410C 和 Camera。 |
| [`19-smart-monitor-integration.md`](docs/plan/19-smart-monitor-integration.md) | Smart Monitor desired/actual/pending 异步收敛。 |
| [`21-failure-delivery-review.md`](docs/plan/21-failure-delivery-review.md) | 异常矩阵、Buildroot 接入、交付清单和总复盘。 |

这些文档提供关键接口、局部示例、练习和验收条件，不提供整套可直接复制的参考实现。实现过程中以本文档索引、`SPEC.md` 和已完成阶段的板端证据共同控制范围。
