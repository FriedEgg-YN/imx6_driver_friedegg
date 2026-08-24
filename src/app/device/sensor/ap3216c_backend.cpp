#include "ap3216c_backend.h"

#include <QDir>
#include <QFile>
#include <QDateTime>

namespace smartmonitor {

Ap3216cBackend::Ap3216cBackend(const QString &preferredDevicePath)
{
        findDevice(preferredDevicePath);
}

QString Ap3216cBackend::findDevice(const QString &preferred)
{
    // Check if the preferred path is provided and valid
    if (!preferred.isEmpty()) {
        if (preferred.startsWith(QLatin1Char('/')) && QFileInfo(preferred).isDir()) {
            m_deviceStatus.availability = Availability::Available;
            m_deviceStatus.sysfsPath = preferred;
            m_deviceStatus.error.clear();
            return preferred;
        }

        const QString byName = QStringLiteral("/sys/bus/iio/devices/") + preferred;
        if (QFileInfo(byName).isDir()) {
            m_deviceStatus.availability = Availability::Available;
            m_deviceStatus.sysfsPath = byName;
            m_deviceStatus.error.clear();
            return byName;
        }
    }

    QDir root(QStringLiteral("/sys/bus/iio/devices"));
    const QStringList entries = root.entryList(QStringList() << QStringLiteral("iio:device*"),
                                               QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name);
    for (const QString &entry : entries) {
        const QString path = root.absoluteFilePath(entry);
        SensorField<QString> nameField = readText(path + QStringLiteral("/name"));
        if (!nameField.valid)
            continue;
        if (nameField.value.compare(QStringLiteral("ap3216c"), Qt::CaseInsensitive) == 0) {
            m_deviceStatus.availability = Availability::Available;
            m_deviceStatus.sysfsPath = path;
            m_deviceStatus.error.clear();
            return path;
        }
    }

    m_deviceStatus.availability = Availability::Unavailable;
    m_deviceStatus.error = QStringLiteral("AP3216C device not found");
    return QString();
}

Ap3216cSample Ap3216cBackend::readSample()
{
    Ap3216cSample sample;
    if (m_deviceStatus.availability != Availability::Available) {
        sample.error = QStringLiteral("AP3216C device not available. Please find device first.");
        sample.timestamp = QDateTime::currentMSecsSinceEpoch();
        return sample;
    }

    // Read ALS raw value
    SensorField<qint64> alsRawField = readInteger(m_deviceStatus.sysfsPath + QStringLiteral("/in_illuminance_raw"));
    sample.alsRaw = alsRawField;

    // Read IR raw value
    SensorField<qint64> irRawField = readInteger(m_deviceStatus.sysfsPath + QStringLiteral("/in_intensity_ir_raw"));
    sample.irRaw = irRawField;

    // Read PS raw value
    SensorField<qint64> psRawField = readInteger(m_deviceStatus.sysfsPath + QStringLiteral("/in_proximity_raw"));
    sample.psRaw = psRawField;

    // Read lux value
    SensorField<double> luxField = readDouble(m_deviceStatus.sysfsPath + QStringLiteral("/in_illuminance_input"));
    sample.lux = luxField;

    sample.timestamp = QDateTime::currentMSecsSinceEpoch();
    return sample;
}

SensorField<QString> Ap3216cBackend::readText(const QString &path) const
{
    QFile file(path);
    SensorField<QString> result;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.valid = false;
        result.error = QStringLiteral("Failed to open file: %1").arg(file.errorString());
        return result;
    }
    result.valid = true;
    result.value = QString::fromLocal8Bit(file.readAll()).trimmed();
    return result;
}

SensorField<qint64> Ap3216cBackend::readInteger(const QString &Path) const
{
    SensorField<qint64> result;
    SensorField<QString> textField = readText(Path);
    bool ok = false;
    
    if (textField.valid) {
        result.value = textField.value.toLongLong(&ok);
        if (ok) {
            result.valid = true;
        } else {
            result.valid = false;
            result.error = QStringLiteral("Failed to convert text to integer: %1").arg(textField.value);
        }
    } else {
        result.valid = textField.valid;
        result.error = textField.error;
    }
    return result;
}
SensorField<double> Ap3216cBackend::readDouble(const QString &Path) const
{
    SensorField<double> result;
    SensorField<QString> textField = readText(Path);
    bool ok = false;

    if (textField.valid) {
        result.value = textField.value.toDouble(&ok);
        if (ok) {
            result.valid = true;
        } else {
            result.valid = false;
            result.error = QStringLiteral("Failed to convert text to double: %1").arg(textField.value);
        }
    } else {
        result.valid = textField.valid;
        result.error = textField.error;
    }
    return result;
}

} // namespace smartmonitor
