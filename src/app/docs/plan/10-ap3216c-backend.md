# 10 AP3216C Backend：从 IIO sysfs 到类型化样本

> 以 [SPEC](../../SPEC.md) 的 AP3216C 范围为准。先完成 backend 单次读取，再接 [11 AP3216C App](11-ap3216c-app.md)。

## 目标与前置

目标：发现 AP3216C IIO device，读取 lux、ALS raw、IR raw、proximity raw，正确传播属性缺失、解析失败和 I/O 错误。

前置：完成 [09 Worker 线程](09-qt-worker-thread.md)；会使用 `QFile/QDir` 和 Linux sysfs。先在板端用 `ap3216c_test` 确认驱动 ABI。

## 最低必懂模型

- `/sys/bus/iio/devices/iio:deviceX` 的编号不稳定；应读取 `name` 并匹配 `ap3216c`，也允许显式传入路径用于调试。
- sysfs 是文本 ABI：每次 open/read/trim/parse；读到文本 `0` 是成功的零值，打不开或解析失败是错误，二者不可合并。
- lux 优先读 `in_illuminance_input`；若 ABI 只有 raw 与 scale，可在两者都有效时计算 `raw * scale`。
- 每个字段需要 validity。设备目录存在不代表每个属性都有效；`available`、字段 valid 和 error 是不同层次。
- timestamp 只在本次成功采样时更新；stale 是消费者按当前时间与 timestamp 判断，不是悄悄复用旧值。
- backend 是普通 C++/QtCore 对象，不是 UI；只由 SensorWorker 所在线程调用。它不创建 timer、不生成“Unavailable”这类展示文案。

## 数据流、线程与所有权

```text
AP3216C -> IIO core -> sysfs text -> Ap3216cBackend::readSample()
                                   -> Ap3216cSample -> SensorWorker signal
```

backend 由 SensorWorker 独占，在 Sensor I/O 线程同步执行一次短读取。它临时拥有 `QFile`；不长期保存 sysfs fd，不把引用或指针跨线程传出。`Ap3216cSample` 按值拥有 QString 和数值。

## 分步手写任务

1. 在板端列出 IIO 设备并记录 AP3216C 实际属性名。
2. 定义字段状态，至少包含 `valid`；样本包含 `DeviceStatus`、路径、四个字段、`updatedAtMs`。
3. 实现 `readText(path)`，返回“成功文本”或 typed error，不用空字符串同时表示所有失败。
4. 实现 `findDevice(preferred)`：显式目录优先，否则遍历 `iio:device*` 并匹配 `name`。
5. 实现整数和 double 解析，保留失败属性名与原因。
6. 依次读取 ALS/IR/PS raw；lux 走 input，必要时走 raw × scale fallback。
7. 明确整次结果策略：目录找不到为 unavailable；目录存在但字段读取失败为 available + 字段 invalid，并汇总 error。
8. 写纯解析练习：`"0\n"`、`"12.500000\n"`、空文本、`"abc"`、溢出值。

## 关键 Qt/C++ 片段

```cpp
template <typename T>
struct SensorField {
    SensorField() = default;
    SensorField(bool isValid, const T &fieldValue, const QString &fieldError)
        : valid(isValid), value(fieldValue), error(fieldError) {}

    bool valid = false;
    T value = T();
    QString error;
};

struct Ap3216cSample {
    DeviceStatus status;
    QString sysfsPath;
    SensorField<double> lux;
    SensorField<qint64> alsRaw;
    SensorField<qint64> irRaw;
    SensorField<qint64> proximityRaw;
    qint64 updatedAtMs = 0;
};
Q_DECLARE_METATYPE(Ap3216cSample)
```

关键解析必须检查 `ok`：

```cpp
bool ok = false;
const qint64 value = text.trimmed().toLongLong(&ok);
if (!ok)
    return SensorField<qint64>{false, 0, QStringLiteral("invalid integer")};
return SensorField<qint64>{true, value, QString()};
```

这里失败对象里的 `0` 只是未使用的存储默认值，消费者必须先检查 `valid`，不得展示或参与业务判断。

## 旧实现：可参考与不能照搬

可参考 `../../../imx6_smart_monitor/sensors/ap3216c_device.cpp` 的 IIO name 扫描、兼容属性名和 lux fallback 机制。

不能照搬：旧样本以 `-1` 作为多个 raw 字段的无效哨兵，字段错误信息不足；旧测试页在 GUI `QTimer` timeout 中直接读 sysfs。新 backend 必须由 Worker 调用，并对字段有效性、错误和 timestamp 建模。

## 检查点

- IIO 编号变化后仍能按 name 找到设备。
- 文本 `0` 得到 `valid=true,value=0`；缺文件得到 `valid=false,error!=empty`。
- lux input 缺失时只有 raw 和 scale 均成功才 fallback。
- error 不包含私有主机路径、板卡 IP 或用户目录。
- backend 不 include QtWidgets，不持有 Worker/Page 指针。

## 常见错误

- 固定使用 `iio:device0`。
- `QFile::readAll()` 失败和空属性都当成 0。
- 忽略 `toDouble(&ok)` 的 `ok`。
- 某字段失败后继续发布上次值并刷新 timestamp，制造“新鲜假数据”。
- 将 proximity raw 直接解释成 presence；AP3216C PS raw 不是 LD2410C presence ABI。
- 在 backend 内遍历 UI、弹窗或拼展示字符串。

## 交叉构建与板端最窄验证

先验证驱动 ABI：

```bash
ap3216c_test scan
ap3216c_test test1 auto
```

交叉编译 backend harness 或阶段 App：

```bash
"${BUILDROOT_DIR}/output/host/bin/qmake" "${AP3216C_PROJECT}.pro"
make -j2
file "${APP_BINARY}"
```

板端最窄验证：

```bash
"${APP_BINARY}" --once
"${APP_BINARY}" --once --device "${DEVICE}"
```

若需要部署驱动包：`bash buildscripts/build_and_deploy.sh drv ap3216c`。应用 package 尚未确定时保留 `<package>`，不借用旧 `imx6-smart-monitor` 名称。

## 正常、缺失与失败验收

| 场景 | 预期 |
| --- | --- |
| 正常 | 四类字段按实际 ABI发布，valid 与 timestamp 正确 |
| 设备缺失 | status=Unavailable、路径为空、字段 invalid，不出现全 0 正常样本 |
| 属性/读取失败 | 设备与字段状态分开；错误指出属性，其他成功字段仍可用 |

## 完成标准

- 有一张实际 ABI 表：属性、单位/语义、输入、失败形式。
- backend 单次读取可在板端独立验证，不需要 QApplication。
- 能解释 0、unknown、unavailable、stale 四种语义。
- fd/文件对象不跨线程，sample 是可 queued 传递的自有数据值类型。

## 复盘问题

1. 为什么不能固定 `iio:device0`？
2. lux input 与 raw × scale 的可信条件分别是什么？
3. 设备 available 但 lux invalid 时 UI 和 Smart Monitor 应如何处理？
4. stale 为什么不能靠重复发布旧值解决？
5. 旧实现哪些 Linux ABI 机制值得保留，哪些错误模型必须重写？
