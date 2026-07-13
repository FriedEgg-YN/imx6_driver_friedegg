#ifndef IMX6SMARTMONITOR_CAMERA_DEVICE_H
#define IMX6SMARTMONITOR_CAMERA_DEVICE_H

#include <QList>
#include <QString>

namespace imx6sm {

struct CameraFormat {
    QString fourcc;
    QString description;
};

struct CameraCaps {
    bool available = false;
    QString devicePath;
    QString driver;
    QString card;
    QString busInfo;
    QString error;
    QList<CameraFormat> formats;
};

class CameraDevice {
public:
    CameraCaps queryCaps(const QString &devicePath) const;
};

} // namespace imx6sm

#endif

