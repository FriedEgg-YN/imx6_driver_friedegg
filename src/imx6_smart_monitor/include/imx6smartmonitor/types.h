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
    bool hasLux = false;
    double lux = 0.0;
    qint64 proximityRaw = -1;
    qint64 irRaw = -1;
    int movingDistanceMm = -1;
    int staticDistanceMm = -1;
    int movingEnergy = -1;
    int staticEnergy = -1;
};

struct OcclusionInput {
    bool hasLux = false;
    double lux = 0.0;
    qint64 proximityRaw = -1;
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
    double occlusionEnterLux = 8.0;
    double occlusionExitLux = 18.0;
    qint64 occlusionProximityRaw = 800;
    qint64 occlusionExitProximityRaw = 500;
    int occlusionEnterCount = 4;
    int occlusionExitCount = 4;
    int presenceStartConfirmMs = 300;
    int presenceEndCooldownMs = 5000;
    QString storageRoot = QStringLiteral("/smart-monitor");
};

enum class PresenceState {
    NoPerson,
    PersonPending,
    ActiveMonitoring,
    Cooldown,
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
    Ready,
    Degraded,
};

enum class StrobeMode {
    None,
    Flash,
    Torch,
};

QString toString(PresenceState state);
QString toString(CameraState state);
QString toString(StorageState state);
QString toString(StrobeMode mode);

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::CameraMode)
Q_DECLARE_METATYPE(imx6sm::CameraState)
Q_DECLARE_METATYPE(imx6sm::StrobeMode)

#endif
