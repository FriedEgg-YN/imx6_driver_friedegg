#ifndef IMX6SMARTMONITOR_MONITOR_CORE_H
#define IMX6SMARTMONITOR_MONITOR_CORE_H

#include "imx6smartmonitor/types.h"

namespace imx6sm {

struct MonitorSnapshot {
    bool monitoringEnabled = false;
    PresenceState presence = PresenceState::NoPerson;
    LightState light = LightState::Normal;
    CameraState camera = CameraState::Closed;
    StorageState storage = StorageState::Idle;
    bool cameraWanted = false;
    bool recordingWanted = false;
    bool torchWanted = false;
    QString presenceSource;
    double lux = 0.0;
    QString activeMode;
    qulonglong frameCount = 0;
    QString afStatus;
    QString recordingPath;
    QString recordingStatus;
    QString cameraError;
    QString storageError;
    QString cameraAction;
    QString recordingAction;
    QString strobeAction;
    QString focusAction;
    QString lastAction;
};

/*
 * MonitorCore 是 Smart Monitor 的纯状态机：输入传感器/camera/storage/recording
 * 状态，输出 cameraAction/recordingAction/strobeAction。它不访问 Qt 控件、设备
 * 节点或文件系统，便于用 Core Test 单独验证“有人 -> 录制”的决策链。
 */
class MonitorCore {
public:
    MonitorCore();

    void setPolicy(const MonitorPolicy &policy);
    MonitorPolicy currentPolicy() const;

    MonitorSnapshot snapshot() const;
    void reset();
    void startMonitoring();
    void stopMonitoring();
    void handleSensorState(const SensorState &state);
    void handleCameraState(CameraState state, const QString &error = QString(),
                           const QString &activeMode = QString(),
                           qulonglong frameCount = 0,
                           const QString &afStatus = QString());
    void handleStorageState(StorageState state, const QString &error = QString());
    void handleRecordingState(const QString &path, const QString &status);
    void handlePresence(bool present);
    void handleLux(double lux);
    void confirmPresenceTimeout();
    void cooldownTimeout();
    void setStorageWritable(bool writable);

private:
    void clearActions();
    void updateLightDecision();
    void updateCameraDecision();
    void updateRecordingDecision();

    MonitorPolicy policy;
    MonitorSnapshot current;
    bool latestPresence = false;
    bool latestLuxValid = false;
    double latestLux = 0.0;
};

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::MonitorSnapshot)

#endif
