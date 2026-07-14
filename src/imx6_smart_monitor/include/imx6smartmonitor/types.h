#ifndef IMX6SMARTMONITOR_TYPES_H
#define IMX6SMARTMONITOR_TYPES_H

#include <QtGlobal>
#include <QMetaType>
#include <QString>

namespace imx6sm {

struct DeviceStatus {
    bool available = false;
    bool active = false;
    QString lastError;
    qint64 lastUpdateNs = 0;
};

struct SensorState {
    bool presence = false;
    QString presenceSource;
    bool hasLux = false;
    double lux = 0.0;
    qint64 proximityRaw = -1;
    qint64 irRaw = -1;
    int movingDistanceMm = -1;
    int staticDistanceMm = -1;
    int movingEnergy = -1;
    int staticEnergy = -1;
};

struct CameraMode {
    QString fourcc;
    int width = 0;
    int height = 0;
    int fpsNum = 0;
    int fpsDen = 1;
    int bytesPerLine = 0;
    int sizeImage = 0;

    QString label() const;
};

struct MonitorPolicy {
    double darkEnterLux = 20.0;
    double darkExitLux = 40.0;
    int presenceStartConfirmMs = 300;
    int presenceEndCooldownMs = 15000;
    int snapshotIntervalMs = 1000;
    QString storageRoot = QStringLiteral("/tmp/smart-monitor");
};

enum class PresenceState {
    NoPerson,
    PersonPending,
    ActiveMonitoring,
    Cooldown,
};

enum class LightState {
    Normal,
    Dark,
    TorchOn,
    Recovering,
};

enum class CameraState {
    Closed,
    Opened,
    Configured,
    Streaming,
    Reconfiguring,
    Error,
};

enum class StorageState {
    Idle,
    SessionOpen,
    Degraded,
};

enum class StrobeMode {
    None,
    Flash,
    Torch,
};

QString toString(PresenceState state);
QString toString(LightState state);
QString toString(CameraState state);
QString toString(StorageState state);
QString toString(StrobeMode mode);

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::CameraMode)
Q_DECLARE_METATYPE(imx6sm::CameraState)
Q_DECLARE_METATYPE(imx6sm::StrobeMode)

#endif

