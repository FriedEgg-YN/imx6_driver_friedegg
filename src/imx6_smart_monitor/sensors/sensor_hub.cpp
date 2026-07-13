#include "sensors/sensor_hub.h"

namespace imx6sm {

SensorState SensorHub::latestState() const
{
    return state;
}

void SensorHub::updatePresence(bool presence, const QString &source)
{
    state.presence = presence;
    state.presenceSource = source;
}

void SensorHub::updateLux(double lux)
{
    state.hasLux = true;
    state.lux = lux;
}

void SensorHub::updateAp3216cRaw(qint64 proximityRaw, qint64 irRaw)
{
    state.proximityRaw = proximityRaw;
    state.irRaw = irRaw;
}

} // namespace imx6sm

