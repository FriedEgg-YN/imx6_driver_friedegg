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

/*
 * MonitorController 是 Smart Monitor 主页面的设备编排层：
 * - 周期读取 AP3216C/LD2410C，送入 MonitorCore 做纯决策；
 * - 根据 Core 输出打开/关闭 camera、启动/停止人在期间录制；
 * - 只把状态和预览帧发给 UI，UI 不直接访问 V4L2/IIO/UART/storage。
 */
class MonitorController : public QObject {
    Q_OBJECT

public:
    enum class StrobePolicy {
        Auto,
        Off,
        Torch,
    };

    explicit MonitorController(QObject *parent = nullptr);
    ~MonitorController() override;

    MonitorSnapshot snapshot() const;
    QList<CameraMode> previewModes() const;
    int activeModeIndex() const;
    StrobePolicy strobePolicy() const;

public slots:
    void startMonitoring();
    void stopMonitoring();
    void setPreviewModeIndex(int index);
    void setStrobePolicyIndex(int index);
    void requestAutoFocus();
    void focusAtFramePoint(int x, int y);

signals:
    void snapshotChanged(const imx6sm::MonitorSnapshot &snapshot);
    void previewFrameChanged(const QImage &image);
    void modesChanged();
    void logMessage(const QString &line);

private slots:
    void pollSensors();
    void confirmPresence();
    void finishCooldown();
    void retryCameraOpen();

private:
    void refreshCameraModes();
    void updateModesFromCaps(const CameraCaps &caps);
    void emitSnapshot();
    void applyDecisions();
    bool ensureStorageReady();
    bool ensureCameraRunning();
    void stopCamera();
    bool ensureRecordingStarted();
    void stopRecording();
    void syncTorch();
    void handleRecordingStatus(const QString &status);
    void updateCoreCameraState(CameraState state = CameraState::Closed);
    CameraMode selectedPreviewMode() const;
    QString statusPathFromText(const QString &status) const;
    QString rootRelativePath(const QString &absolutePath) const;
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
    QTimer retryTimer;
    QList<CameraMode> modes;
    QString devicePath;
    QString storageRoot;
    CameraMode activeCameraMode;
    QString cameraError;
    QString afStatus;
    QString recordingPath;
    qulonglong frameCount = 0;
    double frameFps = 0.0;
    int modeIndex = 0;
    StrobePolicy currentStrobePolicy = StrobePolicy::Auto;
    StrobeMode appliedStrobeMode = StrobeMode::None;
    bool strobeModeApplied = false;
    bool recordingActive = false;
    QString lastSensorError;
};

} // namespace imx6sm

#endif
