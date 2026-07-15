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

void SensorHub::updateLd2410State(const Ld2410State &ld2410)
{
    state.presence = ld2410.presence;
    state.presenceSource = ld2410.source.isEmpty() ? QStringLiteral("ld2410c") : ld2410.source;
    state.movingDistanceMm = static_cast<int>(ld2410.movingDistanceCm) * 10;
    state.staticDistanceMm = static_cast<int>(ld2410.staticDistanceCm) * 10;
    state.movingEnergy = ld2410.movingEnergy;
    state.staticEnergy = ld2410.staticEnergy;
}

void SensorHub::updateLd2410Config(const Ld2410Config &config)
{
    Q_UNUSED(config)
}

} // namespace imx6sm
