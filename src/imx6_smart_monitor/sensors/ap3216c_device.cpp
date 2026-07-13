#include "sensors/ap3216c_device.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace imx6sm {

bool Ap3216cDevice::readTextFile(const QString &path, QString *value)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    *value = QString::fromLocal8Bit(file.readAll()).trimmed();
    return true;
}

bool Ap3216cDevice::readInteger(const QString &basePath, const QStringList &names, qint64 *value)
{
    for (const QString &name : names) {
        QString text;
        if (!readTextFile(basePath + QLatin1Char('/') + name, &text))
            continue;

        bool ok = false;
        const qint64 parsed = text.toLongLong(&ok);
        if (ok) {
            *value = parsed;
            return true;
        }
    }

    return false;
}

bool Ap3216cDevice::readDouble(const QString &basePath, const QStringList &names, double *value)
{
    for (const QString &name : names) {
        QString text;
        if (!readTextFile(basePath + QLatin1Char('/') + name, &text))
            continue;

        bool ok = false;
        const double parsed = text.toDouble(&ok);
        if (ok) {
            *value = parsed;
            return true;
        }
    }

    return false;
}

QString Ap3216cDevice::findDevice(const QString &preferred) const
{
    if (!preferred.isEmpty()) {
        if (preferred.startsWith(QLatin1Char('/')) && QFileInfo(preferred).isDir())
            return preferred;

        const QString byName = QStringLiteral("/sys/bus/iio/devices/") + preferred;
        if (QFileInfo(byName).isDir())
            return byName;
    }

    QDir root(QStringLiteral("/sys/bus/iio/devices"));
    const QStringList entries = root.entryList(QStringList() << QStringLiteral("iio:device*"),
                                               QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name);
    for (const QString &entry : entries) {
        const QString path = root.absoluteFilePath(entry);
        QString name;
        if (!readTextFile(path + QStringLiteral("/name"), &name))
            continue;
        if (name.compare(QStringLiteral("ap3216c"), Qt::CaseInsensitive) == 0)
            return path;
    }

    return QString();
}

Ap3216cSample Ap3216cDevice::readSample(const QString &preferred) const
{
    Ap3216cSample sample;
    sample.sysfsPath = findDevice(preferred);
    if (sample.sysfsPath.isEmpty()) {
        sample.error = QStringLiteral("ap3216c IIO device not found");
        return sample;
    }

    sample.available = true;
    readTextFile(sample.sysfsPath + QStringLiteral("/name"), &sample.name);

    readInteger(sample.sysfsPath,
                QStringList() << QStringLiteral("in_illuminance_raw")
                              << QStringLiteral("in_illuminance0_raw"),
                &sample.alsRaw);
    readInteger(sample.sysfsPath,
                QStringList() << QStringLiteral("in_intensity_ir_raw")
                              << QStringLiteral("in_intensity_ir0_raw"),
                &sample.irRaw);
    readInteger(sample.sysfsPath,
                QStringList() << QStringLiteral("in_proximity_raw")
                              << QStringLiteral("in_proximity0_raw"),
                &sample.proximityRaw);

    double lux = 0.0;
    if (readDouble(sample.sysfsPath,
                   QStringList() << QStringLiteral("in_illuminance_input")
                                 << QStringLiteral("in_illuminance0_input"),
                   &lux)) {
        sample.lux = lux;
        sample.hasLux = true;
        return sample;
    }

    double scale = 0.0;
    if (sample.alsRaw >= 0 &&
        readDouble(sample.sysfsPath,
                   QStringList() << QStringLiteral("in_illuminance_scale")
                                 << QStringLiteral("in_illuminance0_scale"),
                   &scale)) {
        sample.lux = static_cast<double>(sample.alsRaw) * scale;
        sample.hasLux = true;
    }

    return sample;
}

} // namespace imx6sm

