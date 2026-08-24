#ifndef AP3216C_BACKEND_H
#define AP3216C_BACKEND_H

#include "sensor_types.h"

namespace smartmonitor {

// 是否要放设备状态
struct Ap3216cSample
{
    SensorField<double> lux;
    SensorField<qint64> alsRaw;
    SensorField<qint64> irRaw;
    SensorField<qint64> psRaw;

    QString error;
    qint64 timestamp = 0;
};

class Ap3216cBackend
{
public:
    Ap3216cBackend(const QString &preferredDevicePath = QString());
    /* 查找AP3216C设备，返回路径，如果没找到为空，维护 m_deviceStatus */
    QString findDevice(const QString &preferred = QString());
    /* 依赖m_deviceStatus中设备路径，读取设备数据 */
    Ap3216cSample readSample();
private:
    SensorField<QString> readText(const QString &path) const;
    SensorField<qint64> readInteger(const QString &Path) const;
    SensorField<double> readDouble(const QString &Path) const;

    DeviceStatus m_deviceStatus;
};

} // namespace smartmonitor

Q_DECLARE_METATYPE(smartmonitor::Ap3216cSample)

#endif // AP3216C_BACKEND_H
