#ifndef IMX6SMARTMONITOR_SENSOR_HUB_H
#define IMX6SMARTMONITOR_SENSOR_HUB_H

#include "imx6smartmonitor/types.h"

namespace imx6sm {

class SensorHub {
public:
    SensorState latestState() const;
    void updatePresence(bool presence, const QString &source);
    void updateLux(double lux);
    void updateAp3216cRaw(qint64 proximityRaw, qint64 irRaw);

private:
    SensorState state;
};

} // namespace imx6sm

#endif

