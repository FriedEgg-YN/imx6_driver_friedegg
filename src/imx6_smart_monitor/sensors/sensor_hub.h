#ifndef IMX6SMARTMONITOR_SENSOR_HUB_H
#define IMX6SMARTMONITOR_SENSOR_HUB_H

#include "imx6smartmonitor/types.h"
#include "sensors/ld2410_device.h"

namespace imx6sm {

class SensorHub {
public:
    SensorState latestState() const;
    void updatePresence(bool presence, const QString &source);
    void updateLux(double lux);
    void updateAp3216cRaw(qint64 proximityRaw, qint64 irRaw);
    void updateLd2410State(const Ld2410State &state);
    void updateLd2410Config(const Ld2410Config &config);

private:
    SensorState state;
};

} // namespace imx6sm

#endif
