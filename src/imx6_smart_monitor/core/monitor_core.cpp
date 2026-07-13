#include "core/monitor_core.h"

namespace imx6sm {

MonitorCore::MonitorCore() = default;

void MonitorCore::setPolicy(const MonitorPolicy &newPolicy)
{
    policy = newPolicy;
    updateLightDecision();
    updateCameraDecision();
}

MonitorPolicy MonitorCore::currentPolicy() const
{
    return policy;
}

MonitorSnapshot MonitorCore::snapshot() const
{
    return current;
}

void MonitorCore::reset()
{
    current = MonitorSnapshot();
    latestPresence = false;
    latestLuxValid = false;
    latestLux = 0.0;
}

void MonitorCore::handlePresence(bool present)
{
    latestPresence = present;

    if (present) {
        if (current.presence == PresenceState::NoPerson ||
            current.presence == PresenceState::Cooldown) {
            current.presence = PresenceState::PersonPending;
            current.lastAction = QStringLiteral("presence pending");
        }
    } else {
        if (current.presence == PresenceState::ActiveMonitoring ||
            current.presence == PresenceState::PersonPending) {
            current.presence = PresenceState::Cooldown;
            current.lastAction = QStringLiteral("presence cooldown");
        }
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::handleLux(double lux)
{
    latestLuxValid = true;
    latestLux = lux;
    updateLightDecision();
}

void MonitorCore::confirmPresenceTimeout()
{
    if (current.presence == PresenceState::PersonPending && latestPresence) {
        current.presence = PresenceState::ActiveMonitoring;
        current.storage = StorageState::SessionOpen;
        current.lastAction = QStringLiteral("open session and start camera");
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::cooldownTimeout()
{
    if (current.presence == PresenceState::Cooldown && !latestPresence) {
        current.presence = PresenceState::NoPerson;
        current.storage = StorageState::Idle;
        current.lastAction = QStringLiteral("close session and stop camera");
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::setStorageWritable(bool writable)
{
    if (!writable && current.storage == StorageState::SessionOpen) {
        current.storage = StorageState::Degraded;
        current.lastAction = QStringLiteral("storage degraded");
    } else if (writable && current.storage == StorageState::Degraded) {
        current.storage = StorageState::SessionOpen;
        current.lastAction = QStringLiteral("storage recovered");
    }
}

void MonitorCore::updateLightDecision()
{
    if (!latestLuxValid) {
        current.light = LightState::Normal;
        current.torchWanted = false;
        return;
    }

    if (current.presence == PresenceState::ActiveMonitoring &&
        latestLux < policy.darkEnterLux) {
        current.light = LightState::TorchOn;
        current.torchWanted = true;
        return;
    }

    if (latestLux > policy.darkExitLux) {
        current.light = LightState::Normal;
        current.torchWanted = false;
        return;
    }

    if (latestLux < policy.darkEnterLux)
        current.light = LightState::Dark;
}

void MonitorCore::updateCameraDecision()
{
    current.cameraWanted = current.presence == PresenceState::ActiveMonitoring ||
                           current.presence == PresenceState::Cooldown;
    current.camera = current.cameraWanted ? CameraState::Streaming : CameraState::Closed;
}

} // namespace imx6sm

