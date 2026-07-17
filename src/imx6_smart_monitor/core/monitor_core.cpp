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

void MonitorCore::startMonitoring()
{
    clearActions();
    if (!current.monitoringEnabled) {
        current.monitoringEnabled = true;
        current.lastAction = QStringLiteral("monitoring enabled");
    }
    updateLightDecision();
    updateCameraDecision();
}

void MonitorCore::stopMonitoring()
{
    clearActions();
    current.monitoringEnabled = false;
    current.presence = PresenceState::NoPerson;
    current.light = LightState::Normal;
    current.torchWanted = false;
    current.cameraWanted = false;
    current.cameraAction = QStringLiteral("close");
    current.storageAction = QStringLiteral("close");
    current.strobeAction = QStringLiteral("off");
    current.storage = StorageState::Idle;
    current.sessionId.clear();
    current.lastAction = QStringLiteral("monitoring disabled");
}

void MonitorCore::handleSensorState(const SensorState &state)
{
    clearActions();
    current.presenceSource = state.presenceSource;
    latestPresence = state.presence;

    if (state.hasLux) {
        latestLuxValid = true;
        latestLux = state.lux;
        current.lux = state.lux;
    }

    if (current.monitoringEnabled) {
        if (state.presence) {
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
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::handleCameraState(CameraState state, const QString &error,
                                    const QString &activeMode,
                                    qulonglong frameCount,
                                    const QString &afStatus)
{
    clearActions();
    current.camera = state;
    current.activeMode = activeMode;
    current.frameCount = frameCount;
    current.afStatus = afStatus;
    current.cameraError = error;

    if (state == CameraState::Error) {
        current.lastAction = error.isEmpty()
            ? QStringLiteral("camera error")
            : QStringLiteral("camera error: %1").arg(error);
        if (current.cameraWanted)
            current.cameraAction = QStringLiteral("retry");
    }

    updateCameraDecision();
}

void MonitorCore::handleStorageState(StorageState state, const QString &error,
                                     const QString &sessionId)
{
    clearActions();
    current.storage = state;
    current.storageError = error;
    current.sessionId = sessionId;

    if (state == StorageState::Degraded) {
        current.lastAction = error.isEmpty()
            ? QStringLiteral("storage degraded")
            : QStringLiteral("storage degraded: %1").arg(error);
    } else if (state == StorageState::SessionOpen) {
        current.lastAction = QStringLiteral("storage session open");
    } else if (state == StorageState::Idle) {
        current.lastAction = QStringLiteral("storage idle");
    }
}

void MonitorCore::handlePresence(bool present)
{
    SensorState state;
    state.presence = present;
    state.presenceSource = QStringLiteral("manual");
    handleSensorState(state);
}

void MonitorCore::handleLux(double lux)
{
    clearActions();
    latestLuxValid = true;
    latestLux = lux;
    current.lux = lux;
    updateLightDecision();
}

void MonitorCore::confirmPresenceTimeout()
{
    clearActions();
    if (current.monitoringEnabled &&
        current.presence == PresenceState::PersonPending && latestPresence) {
        current.presence = PresenceState::ActiveMonitoring;
        current.storage = StorageState::SessionOpen;
        current.storageAction = QStringLiteral("open");
        current.lastAction = QStringLiteral("open session and start camera");
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::cooldownTimeout()
{
    clearActions();
    if (current.presence == PresenceState::Cooldown && !latestPresence) {
        current.presence = PresenceState::NoPerson;
        current.storage = StorageState::Idle;
        current.storageAction = QStringLiteral("close");
        current.lastAction = QStringLiteral("close session and stop camera");
    }

    updateCameraDecision();
    updateLightDecision();
}

void MonitorCore::setStorageWritable(bool writable)
{
    clearActions();
    if (!writable && current.storage == StorageState::SessionOpen) {
        current.storage = StorageState::Degraded;
        current.storageError = QStringLiteral("storage is not writable");
        current.lastAction = QStringLiteral("storage degraded");
    } else if (writable && current.storage == StorageState::Degraded) {
        current.storage = StorageState::SessionOpen;
        current.storageError.clear();
        current.lastAction = QStringLiteral("storage recovered");
    }
}

void MonitorCore::clearActions()
{
    current.cameraAction.clear();
    current.storageAction.clear();
    current.strobeAction.clear();
    current.focusAction.clear();
}

void MonitorCore::updateLightDecision()
{
    const bool previousTorchWanted = current.torchWanted;

    if (!current.monitoringEnabled || !latestLuxValid) {
        current.light = LightState::Normal;
        current.torchWanted = false;
    } else if (current.presence == PresenceState::ActiveMonitoring) {
        if (latestLux < policy.darkEnterLux ||
            (current.torchWanted && latestLux < policy.darkExitLux)) {
            current.light = LightState::TorchOn;
            current.torchWanted = true;
        } else if (latestLux > policy.darkExitLux) {
            current.light = LightState::Normal;
            current.torchWanted = false;
        } else {
            current.light = LightState::Dark;
            current.torchWanted = false;
        }
    } else if (latestLux < policy.darkEnterLux) {
        current.light = LightState::Dark;
        current.torchWanted = false;
    } else if (latestLux > policy.darkExitLux) {
        current.light = LightState::Normal;
        current.torchWanted = false;
    }

    if (previousTorchWanted != current.torchWanted)
        current.strobeAction = current.torchWanted ? QStringLiteral("torch") : QStringLiteral("off");
}

void MonitorCore::updateCameraDecision()
{
    const bool previousWanted = current.cameraWanted;
    current.cameraWanted = current.monitoringEnabled &&
                           (current.presence == PresenceState::ActiveMonitoring ||
                            current.presence == PresenceState::Cooldown);

    if (previousWanted != current.cameraWanted)
        current.cameraAction = current.cameraWanted ? QStringLiteral("open") : QStringLiteral("close");

    if (current.camera == CameraState::Closed && current.cameraWanted)
        current.cameraAction = QStringLiteral("open");
    if (current.camera == CameraState::Error && current.cameraWanted)
        current.cameraAction = QStringLiteral("retry");
}

} // namespace imx6sm
