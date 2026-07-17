#include "monitor_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaType>

namespace imx6sm {

MonitorController::MonitorController(QObject *parent)
    : QObject(parent)
    , camera(this)
    , devicePath(QStringLiteral("/dev/video1"))
    , storageRoot(policy.storageRoot)
{
    qRegisterMetaType<MonitorSnapshot>("imx6sm::MonitorSnapshot");
    qRegisterMetaType<MonitorSessionInfo>("imx6sm::MonitorSessionInfo");
    qRegisterMetaType<QList<MonitorSessionInfo> >("QList<imx6sm::MonitorSessionInfo>");

    confirmTimer.setSingleShot(true);
    confirmTimer.setInterval(policy.presenceStartConfirmMs);
    cooldownTimer.setSingleShot(true);
    cooldownTimer.setInterval(policy.presenceEndCooldownMs);
    sensorTimer.setInterval(500);
    snapshotTimer.setInterval(policy.snapshotIntervalMs);
    retryTimer.setSingleShot(true);
    retryTimer.setInterval(3000);

    connect(&sensorTimer, &QTimer::timeout, this, &MonitorController::pollSensors);
    connect(&confirmTimer, &QTimer::timeout, this, &MonitorController::confirmPresence);
    connect(&cooldownTimer, &QTimer::timeout, this, &MonitorController::finishCooldown);
    connect(&snapshotTimer, &QTimer::timeout, this, &MonitorController::takePeriodicSnapshot);
    connect(&retryTimer, &QTimer::timeout, this, &MonitorController::retryCameraOpen);

    connect(&camera, &CameraDevice::frameReady, this, [this](const QImage &image) {
        emit previewFrameChanged(image);
    });
    connect(&camera, &CameraDevice::stateChanged, this, [this](CameraState state) {
        updateCoreCameraState(state);
        if (state == CameraState::Streaming)
            syncTorch();
        if (state == CameraState::Error && core.snapshot().cameraWanted && !retryTimer.isActive())
            retryTimer.start();
        emitSnapshot();
    });
    connect(&camera, &CameraDevice::activeModeChanged, this, [this](const CameraMode &mode) {
        activeCameraMode = mode;
        updateCoreCameraState(camera.state());
        emitSnapshot();
    });
    connect(&camera, &CameraDevice::frameStatsChanged, this, [this](qulonglong count, double fps) {
        frameCount = count;
        frameFps = fps;
        Q_UNUSED(frameFps)
        updateCoreCameraState(camera.state());
        emitSnapshot();
    });
    connect(&camera, &CameraDevice::afStatusChanged, this, [this](const QString &status) {
        afStatus = status;
        updateCoreCameraState(camera.state());
        emitSnapshot();
    });
    connect(&camera, &CameraDevice::snapshotStatusChanged, this, &MonitorController::handleSnapshotStatus);
    connect(&camera, &CameraDevice::errorChanged, this, [this](const QString &error) {
        cameraError = error;
        if (!error.isEmpty())
            emit logMessage(QStringLiteral("camera: %1").arg(error));
        updateCoreCameraState(camera.state());
        emitSnapshot();
    });
    connect(&camera, &CameraDevice::strobeStatusChanged, this, [this](const QString &status) {
        emit logMessage(QStringLiteral("strobe: %1").arg(status));
    });
    connect(&camera, &CameraDevice::logMessage, this, &MonitorController::logMessage);

    refreshCameraModes();
    refreshSessions();
    emitSnapshot();
}

MonitorController::~MonitorController()
{
    stopMonitoring();
}

MonitorSnapshot MonitorController::snapshot() const
{
    return core.snapshot();
}

QList<CameraMode> MonitorController::previewModes() const
{
    return modes;
}

QList<MonitorSessionInfo> MonitorController::monitorSessions() const
{
    return sessions;
}

int MonitorController::activeModeIndex() const
{
    return modeIndex;
}

void MonitorController::startMonitoring()
{
    core.startMonitoring();
    if (!sensorTimer.isActive())
        sensorTimer.start();
    if (!snapshotTimer.isActive())
        snapshotTimer.start();
    emit logMessage(QStringLiteral("monitoring started"));
    pollSensors();
    applyDecisions();
    emitSnapshot();
}

void MonitorController::stopMonitoring()
{
    sensorTimer.stop();
    confirmTimer.stop();
    cooldownTimer.stop();
    snapshotTimer.stop();
    retryTimer.stop();

    core.stopMonitoring();
    manualTorch = false;
    stopCamera();
    closeSession(QStringLiteral("stopped"));
    emit logMessage(QStringLiteral("monitoring stopped"));
    emitSnapshot();
}

void MonitorController::setPreviewModeIndex(int index)
{
    if (index < 0 || index >= modes.size())
        return;

    modeIndex = index;
    const CameraMode mode = selectedPreviewMode();
    if (camera.isStreaming() && !camera.setMode(mode)) {
        cameraError = camera.lastError();
        core.handleCameraState(CameraState::Error, cameraError,
                               activeCameraMode.width > 0 ? activeCameraMode.label() : QString(),
                               frameCount, afStatus);
        emit logMessage(QStringLiteral("mode switch failed: %1").arg(cameraError));
    }
    emit modesChanged();
    emitSnapshot();
}

void MonitorController::requestManualSnapshot()
{
    queueSnapshot(QStringLiteral("manual"));
}

void MonitorController::setManualTorch(bool enabled)
{
    manualTorch = enabled;
    syncTorch();
    emitSnapshot();
}

void MonitorController::requestAutoFocus()
{
    if (!camera.isStreaming()) {
        afStatus = QStringLiteral("camera idle");
    } else if (!camera.supportsAutoFocus()) {
        afStatus = QStringLiteral("unsupported");
    } else if (camera.startAutoFocus()) {
        afStatus = QStringLiteral("queued");
    } else {
        afStatus = camera.lastError();
    }
    updateCoreCameraState(camera.state());
    emitSnapshot();
}

void MonitorController::focusAtFramePoint(int x, int y)
{
    if (!camera.isStreaming())
        return;

    if (camera.supportsTouchFocus() && camera.focusTouch(x, y)) {
        afStatus = QStringLiteral("touch queued (%1,%2)").arg(x).arg(y);
    } else if (camera.supportsAutoFocus() && camera.startAutoFocus()) {
        afStatus = QStringLiteral("queued");
    } else {
        afStatus = camera.supportsTouchFocus() ? camera.lastError() : QStringLiteral("unsupported");
    }
    updateCoreCameraState(camera.state());
    emitSnapshot();
}

void MonitorController::refreshSessions()
{
    QString error;
    sessions = storage.listMonitorSessions(storageRoot, &error);
    if (!error.isEmpty() && QFileInfo::exists(storageRoot))
        emit logMessage(QStringLiteral("playback list: %1").arg(error));
    emit sessionsChanged();
}

void MonitorController::pollSensors()
{
    QStringList errors;

    const Ap3216cSample ap = ap3216c.readSample();
    if (ap.available) {
        if (ap.hasLux)
            sensorHub.updateLux(ap.lux);
        sensorHub.updateAp3216cRaw(ap.proximityRaw, ap.irRaw);
    } else if (!ap.error.isEmpty()) {
        errors << ap.error;
    }

    Ld2410State ld;
    QString ldError;
    if (ld2410.readState(&ld, &ldError)) {
        sensorHub.updateLd2410State(ld);
    } else {
        sensorHub.updatePresence(false, QStringLiteral("ld2410c"));
        if (!ldError.isEmpty())
            errors << ldError;
    }

    const QString errorText = errors.join(QStringLiteral("; "));
    if (errorText != lastSensorError) {
        lastSensorError = errorText;
        if (!errorText.isEmpty())
            emit logMessage(QStringLiteral("sensor: %1").arg(errorText));
    }

    core.handleSensorState(sensorHub.latestState());
    const MonitorSnapshot state = core.snapshot();
    if (state.monitoringEnabled) {
        if (state.presence == PresenceState::PersonPending) {
            if (!confirmTimer.isActive())
                confirmTimer.start();
        } else {
            confirmTimer.stop();
        }

        if (state.presence == PresenceState::Cooldown) {
            if (!cooldownTimer.isActive())
                cooldownTimer.start();
        } else {
            cooldownTimer.stop();
        }
    }

    applyDecisions();
    emitSnapshot();
}

void MonitorController::confirmPresence()
{
    core.confirmPresenceTimeout();
    applyDecisions();
    emitSnapshot();
}

void MonitorController::finishCooldown()
{
    core.cooldownTimeout();
    applyDecisions();
    emitSnapshot();
}

void MonitorController::takePeriodicSnapshot()
{
    const MonitorSnapshot state = core.snapshot();
    if (!state.monitoringEnabled || state.presence != PresenceState::ActiveMonitoring)
        return;
    queueSnapshot(QStringLiteral("auto"));
}

void MonitorController::retryCameraOpen()
{
    const MonitorSnapshot state = core.snapshot();
    if (!state.monitoringEnabled || !state.cameraWanted)
        return;
    ensureCameraRunning();
}

void MonitorController::refreshCameraModes()
{
    const CameraCaps caps = camera.queryCaps(devicePath);
    updateModesFromCaps(caps);
    if (!caps.available) {
        cameraError = caps.error;
        core.handleCameraState(CameraState::Closed, cameraError);
    }
    emit modesChanged();
}

void MonitorController::updateModesFromCaps(const CameraCaps &caps)
{
    modes.clear();
    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Mode(mode))
            modes.append(mode);
    }

    modeIndex = 0;
    const CameraMode preferred = CameraDevice::preferredMode(caps);
    for (int i = 0; i < modes.size(); ++i) {
        if (modes.at(i).fourcc == preferred.fourcc &&
            modes.at(i).width == preferred.width &&
            modes.at(i).height == preferred.height &&
            modes.at(i).fpsNum == preferred.fpsNum &&
            modes.at(i).fpsDen == preferred.fpsDen) {
            modeIndex = i;
            break;
        }
    }
}

void MonitorController::emitSnapshot()
{
    emit snapshotChanged(core.snapshot());
}

void MonitorController::applyDecisions()
{
    const MonitorSnapshot state = core.snapshot();

    if (state.storageAction == QStringLiteral("open"))
        ensureSessionOpen();
    else if (state.storageAction == QStringLiteral("close"))
        closeSession(QStringLiteral("cooldown"));

    if (state.cameraAction == QStringLiteral("open") || state.cameraAction == QStringLiteral("retry"))
        ensureCameraRunning();
    else if (state.cameraAction == QStringLiteral("close"))
        stopCamera();

    if (!state.strobeAction.isEmpty())
        syncTorch();
}

bool MonitorController::ensureSessionOpen()
{
    if (activeSession.ok)
        return true;

    activeSession = storage.openMonitorSession(storageRoot, devicePath, selectedPreviewMode());
    frameSequence = 0;
    if (!activeSession.ok) {
        core.handleStorageState(StorageState::Degraded, activeSession.error);
        emit logMessage(QStringLiteral("storage degraded: %1").arg(activeSession.error));
        return false;
    }

    core.handleStorageState(StorageState::SessionOpen, QString(), activeSession.sessionId);
    emit logMessage(QStringLiteral("monitor session opened %1").arg(activeSession.sessionPath));
    refreshSessions();
    return true;
}

void MonitorController::closeSession(const QString &status)
{
    if (!activeSession.ok) {
        core.handleStorageState(StorageState::Idle);
        return;
    }

    QString error;
    if (!storage.closeMonitorSession(activeSession, status, &error)) {
        core.handleStorageState(StorageState::Degraded, error, activeSession.sessionId);
        emit logMessage(QStringLiteral("session close failed: %1").arg(error));
    } else {
        emit logMessage(QStringLiteral("monitor session closed %1").arg(activeSession.sessionId));
        core.handleStorageState(StorageState::Idle);
    }
    activeSession = MonitorSessionResult();
    refreshSessions();
}

bool MonitorController::ensureCameraRunning()
{
    if (camera.isStreaming()) {
        syncTorch();
        return true;
    }

    if (modes.isEmpty())
        refreshCameraModes();

    if (!camera.openDevice(devicePath)) {
        cameraError = camera.lastError();
        core.handleCameraState(CameraState::Error, cameraError);
        if (activeSession.ok) {
            QString error;
            storage.appendMonitorEvent(activeSession, QStringLiteral("camera_error"),
                                       QStringLiteral("open_failed"), cameraError, &error);
        }
        if (!retryTimer.isActive())
            retryTimer.start();
        emit logMessage(QStringLiteral("camera open failed: %1").arg(cameraError));
        return false;
    }

    updateModesFromCaps(camera.capabilities());
    emit modesChanged();

    const CameraMode mode = selectedPreviewMode();
    if (!isRgb565Mode(mode)) {
        cameraError = QStringLiteral("no RGB565 preview mode");
        core.handleCameraState(CameraState::Error, cameraError);
        emit logMessage(cameraError);
        return false;
    }

    if (!camera.startPreview(mode)) {
        cameraError = camera.lastError();
        core.handleCameraState(CameraState::Error, cameraError);
        if (!retryTimer.isActive())
            retryTimer.start();
        emit logMessage(QStringLiteral("camera preview failed: %1").arg(cameraError));
        return false;
    }

    cameraError.clear();
    activeCameraMode = mode;
    updateCoreCameraState(camera.state());
    return true;
}

void MonitorController::stopCamera()
{
    if (camera.isStreaming()) {
        if (camera.supportsStrobeMode())
            camera.setStrobeMode(StrobeMode::None);
        camera.stopPreview();
        emit previewFrameChanged(QImage());
    }
    updateCoreCameraState(camera.state());
}

void MonitorController::syncTorch()
{
    if (!camera.isStreaming() || !camera.supportsStrobeMode())
        return;

    const bool torchWanted = manualTorch || core.snapshot().torchWanted;
    camera.setStrobeMode(torchWanted ? StrobeMode::Torch : StrobeMode::None);
}

void MonitorController::queueSnapshot(const QString &kind)
{
    if (snapshotPending)
        return;
    if (!camera.isStreaming()) {
        emit logMessage(QStringLiteral("snapshot ignored: camera idle"));
        return;
    }
    if (!ensureSessionOpen())
        return;

    const CameraTestPathResult path = storage.makeMonitorSnapshotPath(activeSession);
    if (!path.ok) {
        core.handleStorageState(StorageState::Degraded, path.error, activeSession.sessionId);
        emitSnapshot();
        return;
    }

    const CameraDevice::ActionResult result = camera.requestSnapshot(path.path, 1);
    if (result != CameraDevice::ActionResult::Ok) {
        QString error;
        storage.appendMonitorEvent(activeSession, QStringLiteral("snapshot_failed"),
                                   QStringLiteral("request_rejected"), path.relativePath, &error);
        emit logMessage(QStringLiteral("snapshot request rejected"));
        return;
    }

    snapshotPending = true;
    snapshotPendingPath = path.path;
    snapshotPendingKind = kind;
    QString error;
    if (!storage.appendMonitorEvent(activeSession, QStringLiteral("snapshot_requested"),
                                    kind, path.relativePath, &error)) {
        core.handleStorageState(StorageState::Degraded, error, activeSession.sessionId);
        emitSnapshot();
    }
}

void MonitorController::handleSnapshotStatus(const QString &status)
{
    if (!activeSession.ok) {
        snapshotPending = false;
        return;
    }

    QString error;
    if (status.startsWith(QStringLiteral("saved:"))) {
        const QString path = statusPathFromText(status);
        const QString relativePath = sessionRelativePath(path);
        const QString kind = snapshotPendingKind.isEmpty() ? QStringLiteral("snapshot") : snapshotPendingKind;
        if (!storage.appendMonitorEvent(activeSession, QStringLiteral("snapshot_saved"), kind, relativePath, &error) ||
            !storage.appendMonitorIndex(activeSession, ++frameSequence, relativePath, kind, QStringLiteral("saved"), &error) ||
            !storage.updateLatestImage(activeSession, path, QStringLiteral("snapshot_saved"), &error)) {
            core.handleStorageState(StorageState::Degraded, error, activeSession.sessionId);
            emit logMessage(QStringLiteral("snapshot storage failed: %1").arg(error));
        }
        snapshotPending = false;
        snapshotPendingPath.clear();
        snapshotPendingKind.clear();
        refreshSessions();
    } else if (status.startsWith(QStringLiteral("failed:"))) {
        storage.appendMonitorEvent(activeSession, QStringLiteral("snapshot_failed"),
                                   snapshotPendingKind, status, &error);
        snapshotPending = false;
        snapshotPendingPath.clear();
        snapshotPendingKind.clear();
    }

    emitSnapshot();
}

void MonitorController::updateCoreCameraState(CameraState state)
{
    const QString modeText = activeCameraMode.width > 0 ? activeCameraMode.label() : QString();
    core.handleCameraState(state, cameraError, modeText, frameCount, afStatus);
}

CameraMode MonitorController::selectedPreviewMode() const
{
    if (modeIndex >= 0 && modeIndex < modes.size())
        return modes.at(modeIndex);
    return CameraMode();
}

QString MonitorController::statusPathFromText(const QString &status) const
{
    QString text = status.trimmed();
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        text = text.mid(colon + 1).trimmed();
    const int suffix = text.indexOf(QStringLiteral(" ("));
    if (suffix >= 0)
        text = text.left(suffix).trimmed();
    return text;
}

QString MonitorController::sessionRelativePath(const QString &absolutePath) const
{
    if (!activeSession.ok || absolutePath.isEmpty())
        return absolutePath;
    return QDir(activeSession.sessionPath).relativeFilePath(absolutePath);
}

bool MonitorController::isRgb565Mode(const CameraMode &mode)
{
    return mode.fourcc == QStringLiteral("RGBP") && mode.width > 0 && mode.height > 0;
}

} // namespace imx6sm
