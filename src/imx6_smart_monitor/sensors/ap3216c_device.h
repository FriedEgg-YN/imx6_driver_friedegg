#ifndef IMX6SMARTMONITOR_AP3216C_DEVICE_H
#define IMX6SMARTMONITOR_AP3216C_DEVICE_H

#include <QString>

namespace imx6sm {

struct Ap3216cSample {
    bool available = false;
    QString sysfsPath;
    QString name;
    QString error;
    bool hasLux = false;
    double lux = 0.0;
    qint64 alsRaw = -1;
    qint64 irRaw = -1;
    qint64 proximityRaw = -1;
};

class Ap3216cDevice {
public:
    QString findDevice(const QString &preferred = QString()) const;
    Ap3216cSample readSample(const QString &preferred = QString()) const;

private:
    static bool readTextFile(const QString &path, QString *value);
    static bool readInteger(const QString &basePath, const QStringList &names, qint64 *value);
    static bool readDouble(const QString &basePath, const QStringList &names, double *value);
};

} // namespace imx6sm

#endif

