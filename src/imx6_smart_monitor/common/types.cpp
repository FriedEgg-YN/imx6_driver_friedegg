#include "imx6smartmonitor/types.h"

namespace imx6sm {

QString CameraMode::label() const
{
    const int fps = fpsDen != 0 ? fpsNum / fpsDen : 0;
    return QStringLiteral("%1x%2 %3 %4fps")
        .arg(width)
        .arg(height)
        .arg(fourcc)
        .arg(fps);
}

QString toString(PresenceState state)
{
    switch (state) {
    case PresenceState::NoPerson:
        return QStringLiteral("NoPerson");
    case PresenceState::PersonPending:
        return QStringLiteral("PersonPending");
    case PresenceState::ActiveMonitoring:
        return QStringLiteral("ActiveMonitoring");
    case PresenceState::Cooldown:
        return QStringLiteral("Cooldown");
    }

    return QStringLiteral("Unknown");
}

QString toString(LightState state)
{
    switch (state) {
    case LightState::Normal:
        return QStringLiteral("Normal");
    case LightState::Dark:
        return QStringLiteral("Dark");
    case LightState::TorchOn:
        return QStringLiteral("TorchOn");
    case LightState::Recovering:
        return QStringLiteral("Recovering");
    }

    return QStringLiteral("Unknown");
}

QString toString(CameraState state)
{
    switch (state) {
    case CameraState::Closed:
        return QStringLiteral("Closed");
    case CameraState::Opened:
        return QStringLiteral("Opened");
    case CameraState::Configured:
        return QStringLiteral("Configured");
    case CameraState::Streaming:
        return QStringLiteral("Streaming");
    case CameraState::Reconfiguring:
        return QStringLiteral("Reconfiguring");
    case CameraState::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Unknown");
}

QString toString(StorageState state)
{
    switch (state) {
    case StorageState::Idle:
        return QStringLiteral("Idle");
    case StorageState::Ready:
        return QStringLiteral("Ready");
    case StorageState::Degraded:
        return QStringLiteral("Degraded");
    }

    return QStringLiteral("Unknown");
}

QString toString(StrobeMode mode)
{
    switch (mode) {
    case StrobeMode::None:
        return QStringLiteral("none");
    case StrobeMode::Flash:
        return QStringLiteral("flash");
    case StrobeMode::Torch:
        return QStringLiteral("torch");
    }

    return QStringLiteral("unknown");
}

} // namespace imx6sm
