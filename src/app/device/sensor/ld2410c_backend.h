#ifndef LD2410C_BACKEND_H
#define LD2410C_BACKEND_H

#include <array>

#include <QString>
#include <QtGlobal>

#include "src/ld2410c/include/friedegg/ld2410c.h"
#include "sensor_types.h"

namespace smartmonitor {

struct Ld2410cInterfaces
{
    DeviceStatus misc;
    DeviceStatus input;
    DeviceStatus uart;
};

struct Ld2410cSample
{
    /*
     * reportValid controls common radar report fields.
     * engineering controls engineering-only realtime fields.
     * outValid controls OUT-derived level/presence interpretation.
     */
    bool reportValid = false;
    bool engineering = false;
    bool outValid = false;
    bool outLevel = false;

    /*
     * presence is meaningful only if presenceValid is true.
     *
     * false + valid  => confirmed Absent
     * false + invalid => Unknown, never Absent
     */
    bool presenceValid = false;
    bool presence = false;

    SensorField<quint8> targetState;
    SensorField<quint16> movingDistanceCm;
    SensorField<quint16> staticDistanceCm;
    SensorField<quint16> detectDistanceCm;
    SensorField<quint8> movingEnergy;
    SensorField<quint8> staticEnergy;

    /*
     * These values are meaningful only for a valid engineering report.
     * Each field has its own valid flag so a ViewState cannot accidentally
     * display zero as a real gate-energy value.
     */
    SensorField<quint8> maxMovingGate;
    SensorField<quint8> maxStaticGate;
    std::array<SensorField<quint8>, LD2410C_MAX_GATES> movingGateEnergy;
    std::array<SensorField<quint8>, LD2410C_MAX_GATES> staticGateEnergy;
    SensorField<quint8> light;

    quint32 errorCount = 0;
    quint64 sequence = 0;
    qint64 updatedAtMs = 0;

    QString source;
    QString error;
};

/*
 * A read-back configuration snapshot. valid is true only when READ_CONFIG
 * succeeded; it is not inferred from raw config flags, whose application
 * semantics are not currently defined in the shared UAPI header.
 */
struct Ld2410cConfig
{
    DeviceStatus deviceStatus;
    bool valid = false;

    quint8 maxGate = 0;
    quint8 motionGate = 0;
    quint8 staticGate = 0;
    std::array<quint8, LD2410C_MAX_GATES> motionSensitivity = {};
    std::array<quint8, LD2410C_MAX_GATES> staticSensitivity = {};
    quint16 idleTimeS = 0;

    qint64 updatedAtMs = 0;
    QString error;
};

/*
 * A non-copyable RAII wrapper for LD2410C misc ioctl access.
 *
 * This object must be constructed, used, and destroyed only in the Sensor I/O
 * thread. It is not a QObject because its task is synchronous Linux resource
 * ownership, not Qt event dispatch.
 */
class Ld2410cBackend
{
public:
    explicit Ld2410cBackend(
        const QString &preferredMiscPath = QStringLiteral("/dev/ld2410c0"));
    ~Ld2410cBackend();

    Ld2410cBackend(const Ld2410cBackend &) = delete;
    Ld2410cBackend &operator=(const Ld2410cBackend &) = delete;

    /*
     * Probe each interface independently. Calling this does not establish that
     * GET_STATE will work; only openDevice() + readSample() can do that.
     */
    Ld2410cInterfaces probeInterfaces(const QString &inputPath,
                                      const QString &uartPath);

    /*
     * Idempotent: when the requested misc path is already open, it succeeds
     * without opening a second fd.
     */
    bool openDevice(const QString &miscPath = QString());

    void closeDevice();

    /*
     * This always returns a sample object. Failure is represented by
     * sample.error, invalid fields, and a copied DeviceStatus—not by falsely
     * publishing presence=false.
     */
    Ld2410cSample readSample();

    /*
     * Configuration must be read back after a write operation. This method
     * provides the typed read-back result used by future write workflows.
     */
    Ld2410cConfig readConfig();

    /*
     * Enables/disables richer realtime reports. This is not a replacement for
     * saving configuration; configuration is read/written through its own ABI.
     */
    OperationResult setEngineeringMode(bool enable);

    const DeviceStatus &deviceStatus() const;

private:
    QString resolveMiscPath(const QString &requestedPath) const;
    bool ensureOpen();

    Ld2410cSample makeUnavailableSample(const QString &error) const;
    Ld2410cSample makeIoErrorSample(const QString &operation, int errorNumber) const;

    static QString errnoMessage(const QString &operation, int errorNumber);
    static bool decodeReportPresence(quint8 targetState);

    DeviceStatus m_deviceStatus;
    QString m_preferredMiscPath;
    int m_miscFd = -1;
};

}   // namespace smartmonitor

Q_DECLARE_METATYPE(smartmonitor::Ld2410cInterfaces)
Q_DECLARE_METATYPE(smartmonitor::Ld2410cSample)
Q_DECLARE_METATYPE(smartmonitor::Ld2410cConfig)

#endif  // LD2410C_BACKEND_H
