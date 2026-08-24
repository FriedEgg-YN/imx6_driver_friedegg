#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace smartmonitor {

enum class Availability
{
    Unknown,
    Available,
    Unavailable
};

struct DeviceStatus
{
    Availability availability = Availability::Unknown;
    QString sysfsPath;
    QString error;
};

template <typename T>
struct SensorField
{
    T value{};
    bool valid = false;
    QString error;
};

/**
 * @brief 一次模拟传感器采样结果。
 *
 * 该类型按值跨线程传递，不持有 QObject、设备句柄或外部缓冲区。
 * valid 为 false 时，value 不表示业务值，调用方应展示不可用状态和 error。
 */
struct FakeSample
{
    bool valid = false;
    int value = 0;
    qint64 updatedAtMs = 0;
    QString error;
};

/** @brief Service 对外公开的采样生命周期状态。 */
enum class SamplingState
{
    Idle,
    Starting,
    Running,
    Stopping
};

/** @brief 同步命令受理结果或异步操作结果的状态码。 */
enum class OperationCode
{
    Accepted,
    Succeeded,
    Busy,
    IoError,
    Unavailable
};

/**
 * @brief Service public API 的同步返回值。
 *
 * Accepted 只表示命令已受理并准备投递，不表示 Worker 已完成操作。
 * 最终状态由 Service 的类型化信号异步发布。
 */
struct OperationResult
{
    OperationResult(OperationCode resultCode = OperationCode::Accepted,
                    const QString &errorText = QString())
        : code(resultCode), error(errorText)
    {
    }

    OperationCode code = OperationCode::Accepted;
    QString error;
};

} // namespace smartmonitor

Q_DECLARE_METATYPE(smartmonitor::FakeSample)

#endif // SENSOR_TYPES_H
