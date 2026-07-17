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
    bool torchWanted = false;
    QString presenceSource;
    double lux = 0.0;
    QString sessionId;
    QString activeMode;
    qulonglong frameCount = 0;
    QString afStatus;
    QString cameraError;
    QString storageError;
    QString cameraAction;
    QString storageAction;
    QString strobeAction;
    QString focusAction;
    QString lastAction;
};

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
    void handleStorageState(StorageState state, const QString &error = QString(),
                            const QString &sessionId = QString());
    void handlePresence(bool present);
    void handleLux(double lux);
    void confirmPresenceTimeout();
    void cooldownTimeout();
    void setStorageWritable(bool writable);

private:
    void clearActions();
    void updateLightDecision();
    void updateCameraDecision();

    MonitorPolicy policy;
    MonitorSnapshot current;
    bool latestPresence = false;
    bool latestLuxValid = false;
    double latestLux = 0.0;
};

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::MonitorSnapshot)

#endif
