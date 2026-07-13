#ifndef IMX6SMARTMONITOR_LD2410_DEVICE_H
#define IMX6SMARTMONITOR_LD2410_DEVICE_H

#include <QString>

namespace imx6sm {

struct Ld2410Probe {
    QString outPath;
    QString uartPath;
    QString inputHint;
    bool outAvailable = false;
    bool uartAvailable = false;
    QString error;
};

class Ld2410Device {
public:
    Ld2410Probe probe(const QString &outPath, const QString &uartPath) const;
    QString findInputHint() const;
};

} // namespace imx6sm

#endif

