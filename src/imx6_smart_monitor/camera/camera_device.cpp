#include "camera/camera_device.h"

#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace imx6sm {

static QString fourccToString(unsigned int value)
{
    char text[5];
    text[0] = static_cast<char>(value & 0xff);
    text[1] = static_cast<char>((value >> 8) & 0xff);
    text[2] = static_cast<char>((value >> 16) & 0xff);
    text[3] = static_cast<char>((value >> 24) & 0xff);
    text[4] = '\0';
    return QString::fromLatin1(text);
}

static QString cString(const unsigned char *value)
{
    return QString::fromLocal8Bit(reinterpret_cast<const char *>(value));
}

CameraCaps CameraDevice::queryCaps(const QString &devicePath) const
{
    CameraCaps caps;
    caps.devicePath = devicePath;

    if (devicePath.isEmpty()) {
        caps.error = QStringLiteral("empty video device path");
        return caps;
    }

    if (!QFileInfo::exists(devicePath)) {
        caps.error = QStringLiteral("device path does not exist");
        return caps;
    }

    const int fd = ::open(devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        caps.error = QStringLiteral("open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return caps;
    }

    v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (::ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        caps.error = QStringLiteral("VIDIOC_QUERYCAP failed: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return caps;
    }

    caps.available = true;
    caps.driver = cString(cap.driver);
    caps.card = cString(cap.card);
    caps.busInfo = cString(cap.bus_info);

    for (unsigned int index = 0;; ++index) {
        v4l2_fmtdesc desc;
        std::memset(&desc, 0, sizeof(desc));
        desc.index = index;
        desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0)
            break;

        CameraFormat format;
        format.fourcc = fourccToString(desc.pixelformat);
        format.description = cString(desc.description);
        caps.formats.append(format);
    }

    ::close(fd);
    return caps;
}

} // namespace imx6sm

