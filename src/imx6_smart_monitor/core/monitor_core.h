#ifndef IMX6SMARTMONITOR_MONITOR_CORE_H
#define IMX6SMARTMONITOR_MONITOR_CORE_H

#include "imx6smartmonitor/types.h"

namespace imx6sm {

struct MonitorSnapshot {
    PresenceState presence = PresenceState::NoPerson;
    LightState light = LightState::Normal;
    CameraState camera = CameraState::Closed;
    StorageState storage = StorageState::Idle;
    bool cameraWanted = false;
    bool torchWanted = false;
    QString lastAction;
};

class MonitorCore {
public:
    MonitorCore();

    void setPolicy(const MonitorPolicy &policy);
    MonitorPolicy currentPolicy() const;

    MonitorSnapshot snapshot() const;
    void reset();
    void handlePresence(bool present);
    void handleLux(double lux);
    void confirmPresenceTimeout();
    void cooldownTimeout();
    void setStorageWritable(bool writable);

private:
    void updateLightDecision();
    void updateCameraDecision();

    MonitorPolicy policy;
    MonitorSnapshot current;
    bool latestPresence = false;
    bool latestLuxValid = false;
    double latestLux = 0.0;
};

} // namespace imx6sm

#endif

