#ifndef IMX6SMARTMONITOR_MONITOR_CONTROLLER_H
#define IMX6SMARTMONITOR_MONITOR_CONTROLLER_H

#include "camera/camera_device.h"
#include "core/monitor_core.h"
#include "sensors/ap3216c_device.h"
#include "sensors/ld2410_device.h"
#include "sensors/sensor_hub.h"
#include "storage/storage_manager.h"

#include <QImage>
#include <QList>
#include <QObject>
#include <QTimer>

namespace imx6sm {

class MonitorController : public QObject {
    Q_OBJECT

public:
    explicit MonitorController(QObject *parent = nullptr);
    ~MonitorController() override;

    MonitorSnapshot snapshot() const;
    QList<CameraMode> previewModes() const;
    QList<MonitorSessionInfo> monitorSessions() const;
    int activeModeIndex() const;

public slots:
    void startMonitoring();
    void stopMonitoring();
    void setPreviewModeIndex(int index);
    void requestManualSnapshot();
    void setManualTorch(bool enabled);
    void requestAutoFocus();
    void focusAtFramePoint(int x, int y);
    void refreshSessions();

signals:
    void snapshotChanged(const imx6sm::MonitorSnapshot &snapshot);
    void previewFrameChanged(const QImage &image);
    void modesChanged();
    void sessionsChanged();
    void logMessage(const QString &line);

private slots:
    void pollSensors();
    void confirmPresence();
    void finishCooldown();
    void takePeriodicSnapshot();
    void retryCameraOpen();

private:
    void refreshCameraModes();
    void updateModesFromCaps(const CameraCaps &caps);
    void emitSnapshot();
    void applyDecisions();
    bool ensureSessionOpen();
    void closeSession(const QString &status);
    bool ensureCameraRunning();
    void stopCamera();
    void syncTorch();
    void queueSnapshot(const QString &kind);
    void handleSnapshotStatus(const QString &status);
    void updateCoreCameraState(CameraState state = CameraState::Closed);
    CameraMode selectedPreviewMode() const;
    QString statusPathFromText(const QString &status) const;
    QString sessionRelativePath(const QString &absolutePath) const;
    static bool isRgb565Mode(const CameraMode &mode);

    MonitorPolicy policy;
    MonitorCore core;
    SensorHub sensorHub;
    Ap3216cDevice ap3216c;
    Ld2410Device ld2410;
    StorageManager storage;
    CameraDevice camera;
    QTimer sensorTimer;
    QTimer confirmTimer;
    QTimer cooldownTimer;
    QTimer snapshotTimer;
    QTimer retryTimer;
    QList<CameraMode> modes;
    QList<MonitorSessionInfo> sessions;
    MonitorSessionResult activeSession;
    QString devicePath;
    QString storageRoot;
    CameraMode activeCameraMode;
    QString cameraError;
    QString afStatus;
    qulonglong frameCount = 0;
    double frameFps = 0.0;
    int modeIndex = 0;
    int frameSequence = 0;
    bool manualTorch = false;
    bool snapshotPending = false;
    QString snapshotPendingPath;
    QString snapshotPendingKind;
    QString lastSensorError;
};

} // namespace imx6sm

#endif
