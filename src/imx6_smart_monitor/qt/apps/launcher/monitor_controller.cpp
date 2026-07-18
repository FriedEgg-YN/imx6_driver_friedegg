#include "monitor_controller.h"

#include <QDir>
#include <QMetaType>
#include <QStringList>

namespace imx6sm {

MonitorController::MonitorController(QObject *parent)
    : QObject(parent)
    , camera(this)
    , devicePath(QStringLiteral("/dev/video1"))
    , storageRoot(policy.storageRoot)
{
    qRegisterMetaType<MonitorSnapshot>("imx6sm::MonitorSnapshot");

    confirmTimer.setSingleShot(true);
    confirmTimer.setInterval(policy.presenceStartConfirmMs);
    cooldownTimer.setSingleShot(true);
    cooldownTimer.setInterval(policy.presenceEndCooldownMs);
    sensorTimer.setInterval(500);
    retryTimer.setSingleShot(true);
    retryTimer.setInterval(3000);

    connect(&sensorTimer, &QTimer::timeout, this, &MonitorController::pollSensors);
    connect(&confirmTimer, &QTimer::timeout, this, &MonitorController::confirmPresence);
    connect(&cooldownTimer, &QTimer::timeout, this, &MonitorController::finishCooldown);
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
    connect(&camera, &CameraDevice::recordingStatusChanged, this, &MonitorController::handleRecordingStatus);
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

int MonitorController::activeModeIndex() const
{
    return modeIndex;
}

MonitorController::StrobePolicy MonitorController::strobePolicy() const
{
    return currentStrobePolicy;
}

void MonitorController::startMonitoring()
{
    core.startMonitoring();
    ensureStorageReady();
    if (!sensorTimer.isActive())
        sensorTimer.start();
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
    retryTimer.stop();

    core.stopMonitoring();
    stopRecording();
    stopCamera();
    emit logMessage(QStringLiteral("monitoring stopped"));
    emitSnapshot();
}

void MonitorController::setPreviewModeIndex(int index)
{
    if (index < 0 || index >= modes.size())
        return;

    if (recordingActive || core.snapshot().recordingWanted) {
        emit logMessage(QStringLiteral("mode switch ignored during recording"));
        return;
    }

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
    queueSnapshot();
}

void MonitorController::setStrobePolicyIndex(int index)
{
    switch (index) {
    case 1:
        currentStrobePolicy = StrobePolicy::Off;
        break;
    case 2:
        currentStrobePolicy = StrobePolicy::Torch;
        break;
    default:
        currentStrobePolicy = StrobePolicy::Auto;
        break;
    }
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

void MonitorController::retryCameraOpen()
{
    const MonitorSnapshot state = core.snapshot();
    if (!state.monitoringEnabled || !state.cameraWanted)
        return;
    ensureCameraRunning();
    if (state.recordingWanted && !recordingActive)
        ensureRecordingStarted();
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

    if (state.recordingAction == QStringLiteral("stop") || (!state.recordingWanted && recordingActive))
        stopRecording();

    if (state.cameraAction == QStringLiteral("open") || state.cameraAction == QStringLiteral("retry"))
        ensureCameraRunning();

    if (state.recordingWanted && !recordingActive)
        ensureRecordingStarted();

    if (state.cameraAction == QStringLiteral("close"))
        stopCamera();

    if (!state.strobeAction.isEmpty() || currentStrobePolicy != StrobePolicy::Auto)
        syncTorch();
}

bool MonitorController::ensureStorageReady()
{
    const MonitorSnapshot before = core.snapshot();
    const StorageCheckResult check = storage.checkRoot(storageRoot);
    if (!check.ok) {
        if (before.storage != StorageState::Degraded || before.storageError != check.error)
            emit logMessage(QStringLiteral("storage degraded: %1").arg(check.error));
        core.handleStorageState(StorageState::Degraded, check.error);
        return false;
    }

    storageRoot = check.rootPath;
    if (before.storage != StorageState::Ready)
        emit logMessage(QStringLiteral("storage ready %1").arg(storageRoot));
    core.handleStorageState(StorageState::Ready);
    return true;
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
    stopRecording();
    if (camera.isStreaming()) {
        if (camera.supportsStrobeMode())
            camera.setStrobeMode(StrobeMode::None);
        camera.stopPreview();
        emit previewFrameChanged(QImage());
    }
    updateCoreCameraState(camera.state());
}

bool MonitorController::ensureRecordingStarted()
{
    if (recordingActive)
        return true;
    if (!ensureCameraRunning())
        return false;
    if (!ensureStorageReady())
        return false;

    const StoragePathResult path = storage.makeVideoPath(storageRoot, QStringLiteral("presence"));
    if (!path.ok) {
        core.handleStorageState(StorageState::Degraded, path.error);
        emit logMessage(QStringLiteral("recording path failed: %1").arg(path.error));
        return false;
    }

    if (camera.startRecording(path.path, 0) != CameraDevice::ActionResult::Ok) {
        const QString status = QStringLiteral("failed: request rejected");
        core.handleRecordingState(path.path, status);
        emit logMessage(QStringLiteral("recording request rejected"));
        return false;
    }

    recordingActive = true;
    recordingPath = path.path;
    core.handleRecordingState(recordingPath,
                              QStringLiteral("recording: %1").arg(path.relativePath));
    emit logMessage(QStringLiteral("presence recording started %1").arg(path.relativePath));
    emitSnapshot();
    return true;
}

void MonitorController::stopRecording()
{
    if (!recordingActive)
        return;

    recordingActive = false;
    camera.stopRecording();
    const QString text = recordingPath.isEmpty()
        ? QStringLiteral("stopping")
        : QStringLiteral("stopping: %1").arg(rootRelativePath(recordingPath));
    core.handleRecordingState(recordingPath, text);
    emit logMessage(QStringLiteral("presence recording stop requested"));
}

void MonitorController::syncTorch()
{
    if (!camera.isStreaming() || !camera.supportsStrobeMode())
        return;

    bool torchWanted = false;
    switch (currentStrobePolicy) {
    case StrobePolicy::Auto:
        torchWanted = core.snapshot().torchWanted;
        break;
    case StrobePolicy::Off:
        torchWanted = false;
        break;
    case StrobePolicy::Torch:
        torchWanted = true;
        break;
    }

    camera.setStrobeMode(torchWanted ? StrobeMode::Torch : StrobeMode::None);
}

void MonitorController::queueSnapshot()
{
    if (snapshotPending)
        return;
    if (!camera.isStreaming()) {
        emit logMessage(QStringLiteral("snapshot ignored: camera idle"));
        return;
    }
    if (!ensureStorageReady())
        return;

    const StoragePathResult path = storage.makeFramePath(storageRoot, QStringLiteral("snapshot"));
    if (!path.ok) {
        core.handleStorageState(StorageState::Degraded, path.error);
        emitSnapshot();
        return;
    }

    const CameraDevice::ActionResult result = camera.requestSnapshot(path.path, 1);
    if (result != CameraDevice::ActionResult::Ok) {
        emit logMessage(QStringLiteral("snapshot request rejected"));
        return;
    }

    snapshotPending = true;
    snapshotPendingPath = path.path;
    emit logMessage(QStringLiteral("snapshot requested %1").arg(path.relativePath));
}

void MonitorController::handleSnapshotStatus(const QString &status)
{
    if (status.startsWith(QStringLiteral("saved:"))) {
        const QString path = statusPathFromText(status);
        emit logMessage(QStringLiteral("snapshot saved %1").arg(rootRelativePath(path)));
        snapshotPending = false;
        snapshotPendingPath.clear();
    } else if (status.startsWith(QStringLiteral("failed:"))) {
        emit logMessage(QStringLiteral("snapshot %1").arg(status));
        snapshotPending = false;
        snapshotPendingPath.clear();
    }

    emitSnapshot();
}

void MonitorController::handleRecordingStatus(const QString &status)
{
    const QString path = statusPathFromText(status);
    if (!path.isEmpty())
        recordingPath = path;

    QString display = status;
    if (!path.isEmpty())
        display.replace(path, rootRelativePath(path));

    if (status.startsWith(QStringLiteral("recording:"))) {
        recordingActive = true;
    } else if (status.startsWith(QStringLiteral("saved:")) ||
               status.startsWith(QStringLiteral("failed:")) ||
               status.startsWith(QStringLiteral("stopped:"))) {
        recordingActive = false;
    }

    core.handleRecordingState(recordingPath, display);
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
    const int colon = text.indexOf(QStringLiteral(":"));
    if (colon >= 0)
        text = text.mid(colon + 1).trimmed();
    const int suffix = text.indexOf(QStringLiteral(" ("));
    if (suffix >= 0)
        text = text.left(suffix).trimmed();
    if (!text.startsWith(QStringLiteral("/")))
        return QString();
    return text;
}

QString MonitorController::rootRelativePath(const QString &absolutePath) const
{
    if (absolutePath.isEmpty())
        return absolutePath;
    return QDir(storageRoot).relativeFilePath(absolutePath);
}

bool MonitorController::isRgb565Mode(const CameraMode &mode)
{
    return mode.fourcc == QStringLiteral("RGBP") && mode.width > 0 && mode.height > 0;
}

} // namespace imx6sm
