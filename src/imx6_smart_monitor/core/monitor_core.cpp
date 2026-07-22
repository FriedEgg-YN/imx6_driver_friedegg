#include "core/monitor_core.h"

namespace imx6sm {

MonitorCore::MonitorCore() = default;

void MonitorCore::setPolicy(const MonitorPolicy &newPolicy)
{
    policy = newPolicy;
    updateLightDecision();
    updateCameraDecision();
    updateRecordingDecision();
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
    occlusionEnterSamples = 0;
    occlusionExitSamples = 0;
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
    updateRecordingDecision();
}

void MonitorCore::stopMonitoring()
{
    clearActions();
    current.monitoringEnabled = false;
    current.presence = PresenceState::NoPerson;
    current.light = LightState::Normal;
    current.torchWanted = false;
    current.recordingWanted = false;
    current.cameraWanted = false;
    current.recordingAction = QStringLiteral("stop");
    current.cameraAction = QStringLiteral("close");
    current.strobeAction = QStringLiteral("off");
    current.occlusionAlarm = false;
    current.occlusionNear = false;
    occlusionEnterSamples = 0;
    occlusionExitSamples = 0;
    current.storage = StorageState::Idle;
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
    current.proximityRaw = state.proximityRaw;
    current.irRaw = state.irRaw;

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
    updateRecordingDecision();
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
    updateRecordingDecision();
}

void MonitorCore::handleStorageState(StorageState state, const QString &error)
{
    clearActions();
    current.storage = state;
    current.storageError = error;

    if (state == StorageState::Degraded) {
        current.lastAction = error.isEmpty()
            ? QStringLiteral("storage degraded")
            : QStringLiteral("storage degraded: %1").arg(error);
    } else if (state == StorageState::Ready) {
        current.lastAction = QStringLiteral("storage ready");
    } else if (state == StorageState::Idle) {
        current.lastAction = QStringLiteral("storage idle");
    }
}

void MonitorCore::handleRecordingState(const QString &path, const QString &status)
{
    clearActions();
    current.recordingPath = path;
    current.recordingStatus = status;
    if (!status.isEmpty())
        current.lastAction = QStringLiteral("recording %1").arg(status);
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

void MonitorCore::handleOcclusionInput(const OcclusionInput &input)
{
    clearActions();

    if (input.hasLux) {
        latestLuxValid = true;
        latestLux = input.lux;
        current.lux = input.lux;
    }
    current.proximityRaw = input.proximityRaw;

    const bool nearObject = input.proximityRaw >= (current.occlusionAlarm
        ? policy.occlusionExitProximityRaw
        : policy.occlusionProximityRaw);
    const bool lowLux = input.hasLux && input.lux < (current.occlusionAlarm
        ? policy.occlusionExitLux
        : policy.occlusionEnterLux);
    const bool luxRecovered = !input.hasLux || input.lux > policy.occlusionExitLux;
    const bool proximityRecovered = input.proximityRaw < policy.occlusionExitProximityRaw;
    const bool candidate = current.monitoringEnabled && nearObject && lowLux;
    const bool clearCandidate = !current.monitoringEnabled || proximityRecovered || luxRecovered;

    current.occlusionNear = nearObject;

    if (candidate) {
        ++occlusionEnterSamples;
        occlusionExitSamples = 0;
    } else if (clearCandidate) {
        ++occlusionExitSamples;
        occlusionEnterSamples = 0;
    }

    if (!current.occlusionAlarm && occlusionEnterSamples >= policy.occlusionEnterCount) {
        current.occlusionAlarm = true;
        current.lastAction = QStringLiteral("camera occlusion alarm");
    } else if (current.occlusionAlarm && occlusionExitSamples >= policy.occlusionExitCount) {
        current.occlusionAlarm = false;
        current.lastAction = QStringLiteral("camera occlusion cleared");
    }

    updateCameraDecision();
    updateRecordingDecision();
    updateLightDecision();
}

void MonitorCore::confirmPresenceTimeout()
{
    clearActions();
    if (current.monitoringEnabled &&
        current.presence == PresenceState::PersonPending && latestPresence) {
        current.presence = PresenceState::ActiveMonitoring;
        current.lastAction = QStringLiteral("start camera and recording");
    }

    updateCameraDecision();
    updateRecordingDecision();
    updateLightDecision();
}

void MonitorCore::cooldownTimeout()
{
    clearActions();
    if (current.presence == PresenceState::Cooldown && !latestPresence) {
        current.presence = PresenceState::NoPerson;
        current.lastAction = QStringLiteral("stop camera after cooldown");
    }

    updateCameraDecision();
    updateRecordingDecision();
    updateLightDecision();
}

void MonitorCore::setStorageWritable(bool writable)
{
    clearActions();
    if (!writable && current.storage == StorageState::Ready) {
        current.storage = StorageState::Degraded;
        current.storageError = QStringLiteral("storage is not writable");
        current.lastAction = QStringLiteral("storage degraded");
    } else if (writable && current.storage == StorageState::Degraded) {
        current.storage = StorageState::Ready;
        current.storageError.clear();
        current.lastAction = QStringLiteral("storage recovered");
    }
}

void MonitorCore::clearActions()
{
    current.cameraAction.clear();
    current.recordingAction.clear();
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

void MonitorCore::updateRecordingDecision()
{
    const bool previousWanted = current.recordingWanted;
    current.recordingWanted = current.monitoringEnabled &&
                              (current.presence == PresenceState::ActiveMonitoring ||
                               current.presence == PresenceState::Cooldown);

    if (previousWanted != current.recordingWanted)
        current.recordingAction = current.recordingWanted ? QStringLiteral("start") : QStringLiteral("stop");
}

} // namespace imx6sm
