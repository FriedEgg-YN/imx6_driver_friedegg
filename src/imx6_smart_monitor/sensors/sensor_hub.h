#ifndef IMX6SMARTMONITOR_SENSOR_HUB_H
#define IMX6SMARTMONITOR_SENSOR_HUB_H

#include "imx6smartmonitor/types.h"
#include "sensors/ld2410_device.h"

namespace imx6sm {

/*
 * SensorHub 是 controller 内部的传感器状态聚合器。
 * AP3216C 只更新 lux/raw，LD2410C 更新 presence、距离和能量；latestState()
 * 返回一份简单快照交给 MonitorCore。这里不做硬件访问、不启动线程，方便主
 * 闭环把“采样”和“决策”分开，也便于 Core Test 构造输入。
 */
class SensorHub {
public:
    SensorState latestState() const;
    void updatePresence(bool presence);
    void updateLux(double lux);
    void updateAp3216cRaw(qint64 proximityRaw, qint64 irRaw);
    void updateLd2410State(const Ld2410State &state);
    void updateLd2410Config(const Ld2410Config &config);

private:
    SensorState state;
};

} // namespace imx6sm

#endif
