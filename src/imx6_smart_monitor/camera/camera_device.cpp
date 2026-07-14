#include "camera/camera_device.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#ifndef V4L2_CTRL_FLAG_NEXT_CTRL
#define V4L2_CTRL_FLAG_NEXT_CTRL 0x80000000U
#endif

#ifndef V4L2_CTRL_FLAG_WRITE_ONLY
#define V4L2_CTRL_FLAG_WRITE_ONLY 0x0040
#endif

#ifndef V4L2_CID_AUTO_FOCUS_START
#define V4L2_CID_AUTO_FOCUS_START  (V4L2_CID_CAMERA_CLASS_BASE + 28)
#define V4L2_CID_AUTO_FOCUS_STOP   (V4L2_CID_CAMERA_CLASS_BASE + 29)
#define V4L2_CID_AUTO_FOCUS_STATUS (V4L2_CID_CAMERA_CLASS_BASE + 30)
#endif

#ifndef V4L2_AUTO_FOCUS_STATUS_BUSY
#define V4L2_AUTO_FOCUS_STATUS_IDLE    (0 << 0)
#define V4L2_AUTO_FOCUS_STATUS_BUSY    (1 << 0)
#define V4L2_AUTO_FOCUS_STATUS_REACHED (1 << 1)
#define V4L2_AUTO_FOCUS_STATUS_FAILED  (1 << 2)
#endif

namespace imx6sm {

static const quint32 Ov5640CidAfBase = (V4L2_CID_USER_BASE | 0xf000);
static const quint32 Ov5640CidAfZoneMode = Ov5640CidAfBase + 0;
static const quint32 Ov5640CidAfTouchX = Ov5640CidAfBase + 1;
static const quint32 Ov5640CidAfTouchY = Ov5640CidAfBase + 2;
static const quint32 Ov5640CidAfZoneResult = Ov5640CidAfBase + 3;
static const int Ov5640AfZoneModeDefault = 0;
static const int Ov5640AfZoneModeTouch = 1;

static int xioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

static QString errnoString(const char *operation)
{
    return QStringLiteral("%1 failed: %2")
        .arg(QString::fromLatin1(operation), QString::fromLocal8Bit(std::strerror(errno)));
}

static QString fourccToString(quint32 value)
{
    char text[5];
    text[0] = static_cast<char>(value & 0xff);
    text[1] = static_cast<char>((value >> 8) & 0xff);
    text[2] = static_cast<char>((value >> 16) & 0xff);
    text[3] = static_cast<char>((value >> 24) & 0xff);
    text[4] = '\0';
    return QString::fromLatin1(text);
}

static quint32 fourccFromString(const QString &value)
{
    const QByteArray bytes = value.toLatin1();
    if (bytes.size() != 4)
        return 0;

    return v4l2_fourcc(bytes.at(0), bytes.at(1), bytes.at(2), bytes.at(3));
}

static QString cString(const unsigned char *value)
{
    return QString::fromLocal8Bit(reinterpret_cast<const char *>(value));
}

static QString normalizedControlName(const QString &value)
{
    QString out;
    const QString lower = value.toLower();
    for (int i = 0; i < lower.size(); ++i) {
        const QChar ch = lower.at(i);
        if (ch.isLetterOrNumber())
            out.append(ch);
    }
    return out;
}

static bool isRgb565Fourcc(const QString &fourcc)
{
    return fourcc == QStringLiteral("RGBP");
}

static bool sameMode(const CameraMode &a, const CameraMode &b)
{
    return a.fourcc == b.fourcc && a.width == b.width && a.height == b.height &&
           a.fpsNum == b.fpsNum && a.fpsDen == b.fpsDen;
}

static bool modeIsValid(const CameraMode &mode)
{
    return !mode.fourcc.isEmpty() && mode.width > 0 && mode.height > 0;
}

static QString controlTypeName(quint32 type)
{
    switch (type) {
    case V4L2_CTRL_TYPE_INTEGER:
        return QStringLiteral("integer");
    case V4L2_CTRL_TYPE_BOOLEAN:
        return QStringLiteral("boolean");
    case V4L2_CTRL_TYPE_MENU:
        return QStringLiteral("menu");
    case V4L2_CTRL_TYPE_BUTTON:
        return QStringLiteral("button");
    case V4L2_CTRL_TYPE_INTEGER64:
        return QStringLiteral("integer64");
    case V4L2_CTRL_TYPE_CTRL_CLASS:
        return QStringLiteral("class");
    case V4L2_CTRL_TYPE_STRING:
        return QStringLiteral("string");
    case V4L2_CTRL_TYPE_BITMASK:
        return QStringLiteral("bitmask");
#ifdef V4L2_CTRL_TYPE_INTEGER_MENU
    case V4L2_CTRL_TYPE_INTEGER_MENU:
        return QStringLiteral("integer-menu");
#endif
    default:
        return QStringLiteral("type-%1").arg(type);
    }
}

static bool controlIsMenu(quint32 type)
{
    if (type == V4L2_CTRL_TYPE_MENU)
        return true;
#ifdef V4L2_CTRL_TYPE_INTEGER_MENU
    if (type == V4L2_CTRL_TYPE_INTEGER_MENU)
        return true;
#endif
    return false;
}

static QString afStatusName(int status)
{
    if (status & V4L2_AUTO_FOCUS_STATUS_REACHED)
        return QStringLiteral("REACHED");
    if (status & V4L2_AUTO_FOCUS_STATUS_FAILED)
        return QStringLiteral("FAILED");
    if (status & V4L2_AUTO_FOCUS_STATUS_BUSY)
        return QStringLiteral("BUSY");
    return QStringLiteral("IDLE");
}

static bool appendModeIfMissing(QList<CameraMode> *modes, const CameraMode &mode)
{
    for (const CameraMode &existing : *modes) {
        if (sameMode(existing, mode))
            return false;
    }
    modes->append(mode);
    return true;
}

static void appendMode(QList<CameraMode> *modes, quint32 pixelFormat, int width, int height,
                       int intervalNumerator, int intervalDenominator)
{
    CameraMode mode;
    mode.fourcc = fourccToString(pixelFormat);
    mode.width = width;
    mode.height = height;
    if (intervalNumerator > 0 && intervalDenominator > 0) {
        mode.fpsNum = intervalDenominator;
        mode.fpsDen = intervalNumerator;
    }
    appendModeIfMissing(modes, mode);
}

static void appendFrameIntervals(int fd, quint32 pixelFormat, int width, int height,
                                 QList<CameraMode> *modes)
{
    bool found = false;
    for (unsigned int index = 0;; ++index) {
        v4l2_frmivalenum interval;
        std::memset(&interval, 0, sizeof(interval));
        interval.index = index;
        interval.pixel_format = pixelFormat;
        interval.width = width;
        interval.height = height;

        if (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) < 0)
            break;

        found = true;
        if (interval.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            appendMode(modes, pixelFormat, width, height,
                       interval.discrete.numerator, interval.discrete.denominator);
        } else {
            appendMode(modes, pixelFormat, width, height,
                       interval.stepwise.min.numerator, interval.stepwise.min.denominator);
            appendMode(modes, pixelFormat, width, height,
                       interval.stepwise.max.numerator, interval.stepwise.max.denominator);
            break;
        }
    }

    if (!found)
        appendMode(modes, pixelFormat, width, height, 0, 0);
}

static void appendFrameSizes(int fd, quint32 pixelFormat, QList<CameraMode> *modes)
{
    for (unsigned int index = 0;; ++index) {
        v4l2_frmsizeenum size;
        std::memset(&size, 0, sizeof(size));
        size.index = index;
        size.pixel_format = pixelFormat;

        if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0)
            break;

        if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
            appendFrameIntervals(fd, pixelFormat, size.discrete.width, size.discrete.height, modes);
        } else {
            appendFrameIntervals(fd, pixelFormat, size.stepwise.min_width, size.stepwise.min_height, modes);
            appendFrameIntervals(fd, pixelFormat, size.stepwise.max_width, size.stepwise.max_height, modes);
            break;
        }
    }
}

static void appendControl(int fd, const v4l2_queryctrl &query, QList<CameraControl> *controls)
{
    CameraControl control;
    control.id = query.id;
    control.name = cString(query.name);
    control.typeName = controlTypeName(query.type);
    control.minimum = query.minimum;
    control.maximum = query.maximum;
    control.step = query.step;
    control.defaultValue = query.default_value;
    control.currentValue = query.default_value;
    control.flags = query.flags;

    if (!(query.flags & V4L2_CTRL_FLAG_WRITE_ONLY) && query.type != V4L2_CTRL_TYPE_BUTTON) {
        v4l2_control value;
        std::memset(&value, 0, sizeof(value));
        value.id = query.id;
        if (xioctl(fd, VIDIOC_G_CTRL, &value) == 0)
            control.currentValue = value.value;
    }

    if (controlIsMenu(query.type)) {
        for (int value = query.minimum; value <= query.maximum; ++value) {
            v4l2_querymenu menu;
            std::memset(&menu, 0, sizeof(menu));
            menu.id = query.id;
            menu.index = static_cast<unsigned int>(value);
            if (xioctl(fd, VIDIOC_QUERYMENU, &menu) == 0) {
                CameraControlMenuItem item;
                item.value = value;
                item.name = QString::fromLocal8Bit(reinterpret_cast<const char *>(menu.name));
                control.menuItems.append(item);
            }
        }
    }

    for (const CameraControl &existing : *controls) {
        if (existing.id == control.id)
            return;
    }
    controls->append(control);
}

static void queryControlById(int fd, quint32 id, QList<CameraControl> *controls)
{
    v4l2_queryctrl query;
    std::memset(&query, 0, sizeof(query));
    query.id = id;
    if (xioctl(fd, VIDIOC_QUERYCTRL, &query) == 0)
        appendControl(fd, query, controls);
}

static void appendControls(int fd, QList<CameraControl> *controls)
{
    v4l2_queryctrl query;
    std::memset(&query, 0, sizeof(query));
    query.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    while (xioctl(fd, VIDIOC_QUERYCTRL, &query) == 0) {
        appendControl(fd, query, controls);
        query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }

    static const quint32 knownControls[] = {
        V4L2_CID_FLASH_LED_MODE,
        V4L2_CID_FLASH_STROBE,
        V4L2_CID_FLASH_STROBE_STOP,
        V4L2_CID_AUTO_FOCUS_START,
        V4L2_CID_AUTO_FOCUS_STATUS,
        Ov5640CidAfZoneMode,
        Ov5640CidAfTouchX,
        Ov5640CidAfTouchY,
        Ov5640CidAfZoneResult,
    };

    for (unsigned int i = 0; i < sizeof(knownControls) / sizeof(knownControls[0]); ++i)
        queryControlById(fd, knownControls[i], controls);
}

QString CameraFrameInterval::label() const
{
    if (fpsNum <= 0 || fpsDen <= 0)
        return QStringLiteral("--");

    const double fps = static_cast<double>(fpsNum) / static_cast<double>(fpsDen);
    if (qAbs(fps - static_cast<int>(fps)) < 0.01)
        return QStringLiteral("%1 fps").arg(static_cast<int>(fps));
    return QStringLiteral("%1 fps").arg(fps, 0, 'f', 2);
}

bool CameraControl::isDisabled() const
{
    return (flags & V4L2_CTRL_FLAG_DISABLED) != 0;
}

bool CameraControl::isReadOnly() const
{
    return (flags & V4L2_CTRL_FLAG_READ_ONLY) != 0;
}

QString CameraControl::label() const
{
    return QStringLiteral("0x%1 %2 (%3)")
        .arg(QString::number(id, 16), name, typeName);
}

int CameraCaps::controlIndex(quint32 id) const
{
    for (int i = 0; i < controls.size(); ++i) {
        if (controls.at(i).id == id)
            return i;
    }
    return -1;
}

bool CameraCaps::hasControl(quint32 id) const
{
    const int index = controlIndex(id);
    return index >= 0 && !controls.at(index).isDisabled();
}

int CameraCaps::controlIndexByName(const QString &name) const
{
    const QString normalized = normalizedControlName(name);
    for (int i = 0; i < controls.size(); ++i) {
        if (normalizedControlName(controls.at(i).name) == normalized)
            return i;
    }
    return -1;
}

class CameraCaptureThread : public QThread {
public:
    CameraCaptureThread(CameraDevice *facade, const QString &devicePath, const CameraMode &mode)
        : QThread(facade)
        , facade(facade)
        , devicePath(devicePath)
        , requestedMode(mode)
    {
    }

    ~CameraCaptureThread() override
    {
        requestAbort();
        wait();
    }

    bool requestMode(const CameraMode &mode)
    {
        Command command;
        command.type = Command::SetMode;
        command.mode = mode;
        return enqueue(command);
    }

    bool requestStrobeMode(StrobeMode mode)
    {
        Command command;
        command.type = Command::SetStrobe;
        command.strobeMode = mode;
        return enqueue(command);
    }

    bool requestFlashPulse()
    {
        Command command;
        command.type = Command::TriggerFlash;
        return enqueue(command);
    }

    bool requestAutoFocus()
    {
        Command command;
        command.type = Command::AutoFocus;
        return enqueue(command);
    }

    bool requestTouchFocus(int x, int y)
    {
        Command command;
        command.type = Command::TouchFocus;
        command.x = x;
        command.y = y;
        return enqueue(command);
    }

    void requestAbort()
    {
        QMutexLocker locker(&commandMutex);
        abortRequested = true;
        commandCond.wakeAll();
    }

protected:
    void run() override
    {
        frameCount = 0;
        lastFpsFrameCount = 0;
        lastFpsMs = 0;
        fpsTimer.start();
        afPollTimer.invalidate();

        postState(CameraState::Opened);
        if (!openFd()) {
            postState(CameraState::Error);
            return;
        }

        refreshControlSupport();
        postFrameStats(0, 0.0);

        postState(CameraState::Reconfiguring);
        if (!configureMode(requestedMode)) {
            postState(CameraState::Error);
            closeFd();
            return;
        }

        while (!isAbortRequested()) {
            processCommands();
            if (isAbortRequested())
                break;

            if (!streaming) {
                waitForCommand(100);
                continue;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);

            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;

            const int ret = ::select(fd + 1, &readfds, nullptr, nullptr, &timeout);
            if (ret < 0) {
                if (errno == EINTR)
                    continue;
                postError(errnoString("select"));
                postState(CameraState::Error);
                break;
            }

            if (ret == 0) {
                pollAutoFocusStatus();
                continue;
            }

            if (FD_ISSET(fd, &readfds) && !readFrame()) {
                postState(CameraState::Error);
                break;
            }
            pollAutoFocusStatus();
        }

        teardownStreaming();
        closeFd();
        postState(CameraState::Closed);
    }

private:
    struct MappedBuffer {
        void *start = nullptr;
        size_t length = 0;
    };

    struct Command {
        enum Type {
            SetMode,
            SetStrobe,
            TriggerFlash,
            AutoFocus,
            TouchFocus,
        } type = SetMode;

        CameraMode mode;
        StrobeMode strobeMode = StrobeMode::None;
        int x = 0;
        int y = 0;
    };

    bool enqueue(const Command &command)
    {
        if (!isRunning())
            return false;
        QMutexLocker locker(&commandMutex);
        if (abortRequested)
            return false;
        commandQueue.append(command);
        commandCond.wakeAll();
        return true;
    }

    bool isAbortRequested()
    {
        QMutexLocker locker(&commandMutex);
        return abortRequested;
    }

    void waitForCommand(unsigned long ms)
    {
        QMutexLocker locker(&commandMutex);
        if (!abortRequested && commandQueue.isEmpty())
            commandCond.wait(&commandMutex, ms);
    }

    QList<Command> takeCommands()
    {
        QMutexLocker locker(&commandMutex);
        const QList<Command> result = commandQueue;
        commandQueue.clear();
        return result;
    }

    void processCommands()
    {
        const QList<Command> commands = takeCommands();
        for (const Command &command : commands) {
            if (isAbortRequested())
                return;

            switch (command.type) {
            case Command::SetMode:
                postState(CameraState::Reconfiguring);
                if (!configureMode(command.mode))
                    postState(CameraState::Error);
                break;
            case Command::SetStrobe:
                applyStrobeMode(command.strobeMode);
                break;
            case Command::TriggerFlash:
                triggerFlashPulse();
                break;
            case Command::AutoFocus:
                runAutoFocus();
                break;
            case Command::TouchFocus:
                runTouchFocus(command.x, command.y);
                break;
            }
        }
    }

    bool openFd()
    {
        fd = ::open(devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            postError(errnoString("open"));
            return false;
        }
        postLog(QStringLiteral("opened %1").arg(devicePath));
        return true;
    }

    void closeFd()
    {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
            postLog(QStringLiteral("closed %1").arg(devicePath));
        }
    }

    bool configureMode(const CameraMode &mode)
    {
        if (!modeIsValid(mode)) {
            postError(QStringLiteral("invalid camera mode"));
            return false;
        }
        if (!isRgb565Fourcc(mode.fourcc)) {
            postError(QStringLiteral("preview supports RGB565/RGBP only, got %1").arg(mode.fourcc));
            return false;
        }

        teardownStreaming();

        v4l2_format format;
        std::memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = mode.width;
        format.fmt.pix.height = mode.height;
        format.fmt.pix.pixelformat = fourccFromString(mode.fourcc);
        format.fmt.pix.field = V4L2_FIELD_NONE;

        if (xioctl(fd, VIDIOC_S_FMT, &format) < 0) {
            postError(errnoString("VIDIOC_S_FMT"));
            return false;
        }

        CameraMode configured;
        configured.fourcc = fourccToString(format.fmt.pix.pixelformat);
        configured.width = static_cast<int>(format.fmt.pix.width);
        configured.height = static_cast<int>(format.fmt.pix.height);
        configured.bytesPerLine = static_cast<int>(format.fmt.pix.bytesperline);
        configured.sizeImage = static_cast<int>(format.fmt.pix.sizeimage);
        configured.fpsNum = mode.fpsNum;
        configured.fpsDen = mode.fpsDen > 0 ? mode.fpsDen : 1;

        if (mode.fpsNum > 0 && mode.fpsDen > 0) {
            v4l2_streamparm parm;
            std::memset(&parm, 0, sizeof(parm));
            parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            parm.parm.capture.timeperframe.numerator = mode.fpsDen;
            parm.parm.capture.timeperframe.denominator = mode.fpsNum;
            if (xioctl(fd, VIDIOC_S_PARM, &parm) < 0) {
                postLog(errnoString("VIDIOC_S_PARM"));
            } else if (parm.parm.capture.timeperframe.numerator > 0 &&
                       parm.parm.capture.timeperframe.denominator > 0) {
                configured.fpsNum = parm.parm.capture.timeperframe.denominator;
                configured.fpsDen = parm.parm.capture.timeperframe.numerator;
            }
        }

        if (!requestBuffers())
            return false;
        if (!queueBuffers())
            return false;
        if (!startStreaming())
            return false;

        activeMode = configured;
        refreshControlSupport();
        postActiveMode(activeMode);
        postState(CameraState::Streaming);
        postLog(QStringLiteral("preview streaming %1").arg(activeMode.label()));
        return true;
    }

    bool requestBuffers()
    {
        v4l2_requestbuffers request;
        std::memset(&request, 0, sizeof(request));
        request.count = 4;
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd, VIDIOC_REQBUFS, &request) < 0) {
            postError(errnoString("VIDIOC_REQBUFS"));
            return false;
        }
        if (request.count < 2) {
            postError(QStringLiteral("VIDIOC_REQBUFS returned less than 2 buffers"));
            return false;
        }

        buffers.clear();
        for (unsigned int i = 0; i < request.count; ++i) {
            v4l2_buffer buffer;
            std::memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = i;

            if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
                postError(errnoString("VIDIOC_QUERYBUF"));
                teardownStreaming();
                return false;
            }

            void *start = ::mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffer.m.offset);
            if (start == MAP_FAILED) {
                postError(errnoString("mmap"));
                teardownStreaming();
                return false;
            }

            MappedBuffer mapped;
            mapped.start = start;
            mapped.length = buffer.length;
            buffers.append(mapped);
        }

        return true;
    }

    bool queueBuffers()
    {
        for (int i = 0; i < buffers.size(); ++i) {
            v4l2_buffer buffer;
            std::memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = static_cast<unsigned int>(i);

            if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
                postError(errnoString("VIDIOC_QBUF"));
                teardownStreaming();
                return false;
            }
        }
        return true;
    }

    bool startStreaming()
    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            postError(errnoString("VIDIOC_STREAMON"));
            teardownStreaming();
            return false;
        }
        streaming = true;
        return true;
    }

    void teardownStreaming()
    {
        if (fd >= 0 && streaming) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0)
                postLog(errnoString("VIDIOC_STREAMOFF"));
            streaming = false;
        }

        for (const MappedBuffer &buffer : buffers) {
            if (buffer.start && buffer.length > 0)
                ::munmap(buffer.start, buffer.length);
        }
        buffers.clear();

        if (fd >= 0) {
            v4l2_requestbuffers request;
            std::memset(&request, 0, sizeof(request));
            request.count = 0;
            request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            request.memory = V4L2_MEMORY_MMAP;
            xioctl(fd, VIDIOC_REQBUFS, &request);
        }
    }

    bool readFrame()
    {
        v4l2_buffer buffer;
        std::memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd, VIDIOC_DQBUF, &buffer) < 0) {
            if (errno == EAGAIN)
                return true;
            postError(errnoString("VIDIOC_DQBUF"));
            return false;
        }

        if (buffer.index >= static_cast<unsigned int>(buffers.size())) {
            postError(QStringLiteral("VIDIOC_DQBUF returned invalid buffer index %1").arg(buffer.index));
            return false;
        }

        const MappedBuffer &mapped = buffers.at(static_cast<int>(buffer.index));
        const int bytesPerLine = activeMode.bytesPerLine > 0 ? activeMode.bytesPerLine : activeMode.width * 2;
        bool copied = false;
        QImage image(activeMode.width, activeMode.height, QImage::Format_RGB16);
        if (!image.isNull()) {
            const uchar *src = static_cast<const uchar *>(mapped.start);
            const int copyBytes = qMin(image.bytesPerLine(), bytesPerLine);
            const size_t needed = static_cast<size_t>(bytesPerLine) * static_cast<size_t>(activeMode.height);
            if (needed <= mapped.length) {
                for (int y = 0; y < activeMode.height; ++y)
                    std::memcpy(image.scanLine(y), src + y * bytesPerLine, static_cast<size_t>(copyBytes));
                copied = true;
            } else {
                postError(QStringLiteral("captured buffer is smaller than expected image size"));
            }
        }

        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            postError(errnoString("VIDIOC_QBUF"));
            return false;
        }

        if (copied)
            postFrame(image);
        else
            return false;

        ++frameCount;
        const qint64 elapsed = fpsTimer.elapsed();
        if (elapsed - lastFpsMs >= 1000) {
            const qint64 deltaMs = elapsed - lastFpsMs;
            const qulonglong deltaFrames = frameCount - lastFpsFrameCount;
            const double fps = deltaMs > 0 ? (static_cast<double>(deltaFrames) * 1000.0) / deltaMs : 0.0;
            postFrameStats(frameCount, fps);
            lastFpsMs = elapsed;
            lastFpsFrameCount = frameCount;
        }

        return true;
    }

    bool queryControl(quint32 id) const
    {
        if (fd < 0)
            return false;
        v4l2_queryctrl query;
        std::memset(&query, 0, sizeof(query));
        query.id = id;
        if (xioctl(fd, VIDIOC_QUERYCTRL, &query) < 0)
            return false;
        return (query.flags & V4L2_CTRL_FLAG_DISABLED) == 0;
    }

    void refreshControlSupport()
    {
        hasFlashLedMode = queryControl(V4L2_CID_FLASH_LED_MODE);
        hasFlashStrobe = queryControl(V4L2_CID_FLASH_STROBE);
        hasFlashStop = queryControl(V4L2_CID_FLASH_STROBE_STOP);
        hasAutoFocusStart = queryControl(V4L2_CID_AUTO_FOCUS_START);
        hasAutoFocusStatus = queryControl(V4L2_CID_AUTO_FOCUS_STATUS);
        hasAfZoneMode = queryControl(Ov5640CidAfZoneMode);
        hasAfTouchX = queryControl(Ov5640CidAfTouchX);
        hasAfTouchY = queryControl(Ov5640CidAfTouchY);
        hasAfZoneResult = queryControl(Ov5640CidAfZoneResult);
    }

    bool setCtrl(quint32 id, int value)
    {
        v4l2_control control;
        std::memset(&control, 0, sizeof(control));
        control.id = id;
        control.value = value;
        if (xioctl(fd, VIDIOC_S_CTRL, &control) < 0) {
            postError(QStringLiteral("VIDIOC_S_CTRL 0x%1 value %2 failed: %3")
                          .arg(QString::number(id, 16), QString::number(value),
                               QString::fromLocal8Bit(std::strerror(errno))));
            return false;
        }
        return true;
    }

    bool getCtrl(quint32 id, int *value)
    {
        v4l2_control control;
        std::memset(&control, 0, sizeof(control));
        control.id = id;
        if (xioctl(fd, VIDIOC_G_CTRL, &control) < 0)
            return false;
        *value = control.value;
        return true;
    }

    void applyStrobeMode(StrobeMode mode)
    {
        if (!hasFlashLedMode) {
            postError(QStringLiteral("V4L2_CID_FLASH_LED_MODE is not available"));
            return;
        }

        const int value = mode == StrobeMode::Torch ? V4L2_FLASH_LED_MODE_TORCH : V4L2_FLASH_LED_MODE_NONE;
        if (!setCtrl(V4L2_CID_FLASH_LED_MODE, value))
            return;

        const QString status = mode == StrobeMode::Torch ? QStringLiteral("torch") : QStringLiteral("off");
        postStrobeStatus(status);
        postLog(QStringLiteral("strobe %1").arg(status));
    }

    void triggerFlashPulse()
    {
        if (!hasFlashLedMode) {
            postError(QStringLiteral("V4L2_CID_FLASH_LED_MODE is not available"));
            return;
        }

        if (!setCtrl(V4L2_CID_FLASH_LED_MODE, V4L2_FLASH_LED_MODE_NONE))
            return;

        postStrobeStatus(QStringLiteral("flash skipped"));
        postLog(QStringLiteral("flash pulse skipped to keep OV5640 LED strobe off"));
    }

    void runAutoFocus()
    {
        if (!streaming) {
            postError(QStringLiteral("auto focus requires active streaming"));
            return;
        }
        if (!hasAutoFocusStart) {
            postError(QStringLiteral("V4L2_CID_AUTO_FOCUS_START is not available"));
            return;
        }

        if (!setCtrl(V4L2_CID_AUTO_FOCUS_START, 0))
            return;
        postAfStatus(QStringLiteral("STARTED"));
        lastAfStatusText.clear();
        afPollTimer.start();
        postLog(QStringLiteral("auto focus started"));
    }

    void runTouchFocus(int x, int y)
    {
        if (!hasAfZoneMode || !hasAfTouchX || !hasAfTouchY) {
            postError(QStringLiteral("touch AF controls af_touch_x/y and af_zone_mode are not available"));
            return;
        }

        const int clampedX = activeMode.width > 0 ? qBound(0, x, activeMode.width - 1) : qMax(0, x);
        const int clampedY = activeMode.height > 0 ? qBound(0, y, activeMode.height - 1) : qMax(0, y);
        if (!setCtrl(Ov5640CidAfTouchX, clampedX) ||
            !setCtrl(Ov5640CidAfTouchY, clampedY) ||
            !setCtrl(Ov5640CidAfZoneMode, Ov5640AfZoneModeTouch)) {
            return;
        }

        postLog(QStringLiteral("touch AF zone center=(%1,%2)").arg(clampedX).arg(clampedY));
        runAutoFocus();
    }

    void pollAutoFocusStatus()
    {
        if (!hasAutoFocusStatus)
            return;
        if (!afPollTimer.isValid())
            afPollTimer.start();
        if (afPollTimer.elapsed() < 250)
            return;
        afPollTimer.restart();

        int status = V4L2_AUTO_FOCUS_STATUS_IDLE;
        if (!getCtrl(V4L2_CID_AUTO_FOCUS_STATUS, &status))
            return;

        QString text = afStatusName(status);
        if (hasAfZoneResult) {
            int zoneResult = 0;
            if (getCtrl(Ov5640CidAfZoneResult, &zoneResult))
                text += QStringLiteral(" zone=0x%1").arg(QString::number(zoneResult, 16));
        }

        if (text != lastAfStatusText) {
            lastAfStatusText = text;
            postAfStatus(text);
        }
    }

    void postFrame(const QImage &image)
    {
        QMetaObject::invokeMethod(facade, "deliverFrame", Qt::QueuedConnection,
                                  Q_ARG(QImage, image));
    }

    void postState(CameraState state)
    {
        QMetaObject::invokeMethod(facade, "deliverState", Qt::QueuedConnection,
                                  Q_ARG(CameraState, state));
    }

    void postActiveMode(const CameraMode &mode)
    {
        QMetaObject::invokeMethod(facade, "deliverActiveMode", Qt::QueuedConnection,
                                  Q_ARG(CameraMode, mode));
    }

    void postFrameStats(qulonglong frames, double fps)
    {
        QMetaObject::invokeMethod(facade, "deliverFrameStats", Qt::QueuedConnection,
                                  Q_ARG(qulonglong, frames), Q_ARG(double, fps));
    }

    void postAfStatus(const QString &status)
    {
        QMetaObject::invokeMethod(facade, "deliverAfStatus", Qt::QueuedConnection,
                                  Q_ARG(QString, status));
    }

    void postStrobeStatus(const QString &status)
    {
        QMetaObject::invokeMethod(facade, "deliverStrobeStatus", Qt::QueuedConnection,
                                  Q_ARG(QString, status));
    }

    void postError(const QString &error)
    {
        QMetaObject::invokeMethod(facade, "deliverError", Qt::QueuedConnection,
                                  Q_ARG(QString, error));
    }

    void postLog(const QString &line)
    {
        QMetaObject::invokeMethod(facade, "deliverLog", Qt::QueuedConnection,
                                  Q_ARG(QString, line));
    }

    CameraDevice *facade;
    QString devicePath;
    CameraMode requestedMode;
    int fd = -1;
    QList<MappedBuffer> buffers;
    bool streaming = false;
    CameraMode activeMode;
    qulonglong frameCount = 0;
    QElapsedTimer fpsTimer;
    qint64 lastFpsMs = 0;
    qulonglong lastFpsFrameCount = 0;
    QElapsedTimer afPollTimer;
    QString lastAfStatusText;

    bool hasFlashLedMode = false;
    bool hasFlashStrobe = false;
    bool hasFlashStop = false;
    bool hasAutoFocusStart = false;
    bool hasAutoFocusStatus = false;
    bool hasAfZoneMode = false;
    bool hasAfTouchX = false;
    bool hasAfTouchY = false;
    bool hasAfZoneResult = false;

    QMutex commandMutex;
    QWaitCondition commandCond;
    QList<Command> commandQueue;
    bool abortRequested = false;
};

CameraDevice::CameraDevice(QObject *parent)
    : QObject(parent)
    , captureThread(nullptr)
    , currentState(CameraState::Closed)
{
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<CameraMode>("CameraMode");
    qRegisterMetaType<CameraMode>("imx6sm::CameraMode");
    qRegisterMetaType<CameraState>("CameraState");
    qRegisterMetaType<CameraState>("imx6sm::CameraState");
}

CameraDevice::~CameraDevice()
{
    stopPreview();
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
        caps.error = errnoString("open");
        return caps;
    }

    v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        caps.error = errnoString("VIDIOC_QUERYCAP");
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
        if (xioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0)
            break;

        CameraFormat format;
        format.pixelFormat = desc.pixelformat;
        format.fourcc = fourccToString(desc.pixelformat);
        format.description = cString(desc.description);
        caps.formats.append(format);
        appendFrameSizes(fd, desc.pixelformat, &caps.modes);
    }

    appendControls(fd, &caps.controls);

    ::close(fd);
    return caps;
}

bool CameraDevice::openDevice(const QString &devicePath)
{
    const QString path = devicePath.trimmed();
    if (captureThread && captureThread->isRunning() && path != currentDevicePath)
        stopPreview();

    if (!refreshCaps(path))
        return false;

    currentDevicePath = path;
    currentMode = preferredMode(currentCaps);
    setLocalState(CameraState::Opened);
    emit activeModeChanged(currentMode);
    emit logMessage(QStringLiteral("query %1: %2 mode(s), %3 control(s)")
                        .arg(path)
                        .arg(currentCaps.modes.size())
                        .arg(currentCaps.controls.size()));
    return true;
}

void CameraDevice::closeDevice()
{
    stopPreview();
    currentDevicePath.clear();
    currentMode = CameraMode();
    setLocalState(CameraState::Closed);
}

bool CameraDevice::startPreview(const CameraMode &mode)
{
    if (captureThread && !captureThread->isRunning()) {
        captureThread->wait();
        delete captureThread;
        captureThread = nullptr;
    }

    if (captureThread && captureThread->isRunning())
        return setMode(modeIsValid(mode) ? mode : currentMode);

    if (currentDevicePath.isEmpty()) {
        setLocalError(QStringLiteral("open/query a video device before starting preview"));
        return false;
    }

    CameraMode startMode = modeIsValid(mode) ? mode : preferredMode(currentCaps);
    if (!modeIsValid(startMode)) {
        setLocalError(QStringLiteral("no RGB565/RGBP preview mode is available"));
        return false;
    }

    captureThread = new CameraCaptureThread(this, currentDevicePath, startMode);
    setLocalState(CameraState::Reconfiguring);
    captureThread->start();
    return true;
}

void CameraDevice::stopPreview()
{
    if (!captureThread)
        return;

    captureThread->requestAbort();
    captureThread->wait();
    delete captureThread;
    captureThread = nullptr;
    setLocalState(CameraState::Closed);
}

bool CameraDevice::setMode(const CameraMode &mode)
{
    if (!modeIsValid(mode)) {
        setLocalError(QStringLiteral("invalid camera mode"));
        return false;
    }
    if (!requirePreviewThread(QStringLiteral("setMode")))
        return false;
    if (!captureThread->requestMode(mode)) {
        setLocalError(QStringLiteral("failed to queue mode switch"));
        return false;
    }
    return true;
}

bool CameraDevice::setStrobeMode(StrobeMode mode)
{
    if (!requirePreviewThread(QStringLiteral("setStrobeMode")))
        return false;
    if (!captureThread->requestStrobeMode(mode)) {
        setLocalError(QStringLiteral("failed to queue strobe mode"));
        return false;
    }
    return true;
}

bool CameraDevice::triggerFlash()
{
    if (!requirePreviewThread(QStringLiteral("triggerFlash")))
        return false;
    if (!captureThread->requestFlashPulse()) {
        setLocalError(QStringLiteral("failed to queue flash pulse"));
        return false;
    }
    return true;
}

bool CameraDevice::startAutoFocus()
{
    if (!requirePreviewThread(QStringLiteral("startAutoFocus")))
        return false;
    if (!captureThread->requestAutoFocus()) {
        setLocalError(QStringLiteral("failed to queue auto focus"));
        return false;
    }
    return true;
}

bool CameraDevice::focusTouch(int activeFrameX, int activeFrameY)
{
    if (!requirePreviewThread(QStringLiteral("focusTouch")))
        return false;
    if (!captureThread->requestTouchFocus(activeFrameX, activeFrameY)) {
        setLocalError(QStringLiteral("failed to queue touch focus"));
        return false;
    }
    return true;
}

CameraDevice::ActionResult CameraDevice::requestSnapshot(const QString &path)
{
    Q_UNUSED(path);
    const QString status = QStringLiteral("planned: framework only, encoder not implemented");
    emit snapshotStatusChanged(status);
    emit logMessage(QStringLiteral("snapshot %1").arg(status));
    return ActionResult::NotImplemented;
}

CameraDevice::ActionResult CameraDevice::startRecording(const QString &path)
{
    Q_UNUSED(path);
    const QString status = QStringLiteral("planned: framework only, encoder not implemented");
    emit recordingStatusChanged(status);
    emit logMessage(QStringLiteral("recording %1").arg(status));
    return ActionResult::NotImplemented;
}

CameraDevice::ActionResult CameraDevice::stopRecording()
{
    const QString status = QStringLiteral("not recording: encoder not implemented");
    emit recordingStatusChanged(status);
    emit logMessage(status);
    return ActionResult::NotImplemented;
}

CameraCaps CameraDevice::capabilities() const
{
    return currentCaps;
}

CameraMode CameraDevice::activeMode() const
{
    return currentMode;
}

CameraState CameraDevice::state() const
{
    return currentState;
}

QString CameraDevice::lastError() const
{
    return currentLastError;
}

bool CameraDevice::isStreaming() const
{
    return currentState == CameraState::Streaming || currentState == CameraState::Reconfiguring;
}

bool CameraDevice::supportsStrobeMode() const
{
    return currentCaps.hasControl(V4L2_CID_FLASH_LED_MODE);
}

bool CameraDevice::supportsFlashPulse() const
{
    return currentCaps.hasControl(V4L2_CID_FLASH_LED_MODE) &&
           currentCaps.hasControl(V4L2_CID_FLASH_STROBE) &&
           currentCaps.hasControl(V4L2_CID_FLASH_STROBE_STOP);
}

bool CameraDevice::supportsAutoFocus() const
{
    return currentCaps.hasControl(V4L2_CID_AUTO_FOCUS_START);
}

bool CameraDevice::supportsTouchFocus() const
{
    return supportsAutoFocus() && currentCaps.hasControl(Ov5640CidAfZoneMode) &&
           currentCaps.hasControl(Ov5640CidAfTouchX) && currentCaps.hasControl(Ov5640CidAfTouchY);
}

CameraMode CameraDevice::preferredMode(const CameraCaps &caps)
{
    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Fourcc(mode.fourcc) && mode.width == 800 && mode.height == 480 &&
            mode.fpsNum == 30 && mode.fpsDen == 1) {
            return mode;
        }
    }

    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Fourcc(mode.fourcc) && mode.width == 800 && mode.height == 480)
            return mode;
    }

    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Fourcc(mode.fourcc))
            return mode;
    }

    return CameraMode();
}

void CameraDevice::deliverFrame(const QImage &image)
{
    emit frameReady(image);
}

void CameraDevice::deliverState(CameraState state)
{
    setLocalState(state);
}

void CameraDevice::deliverActiveMode(const CameraMode &mode)
{
    currentMode = mode;
    emit activeModeChanged(mode);
}

void CameraDevice::deliverFrameStats(qulonglong frameCount, double fps)
{
    emit frameStatsChanged(frameCount, fps);
}

void CameraDevice::deliverAfStatus(const QString &status)
{
    emit afStatusChanged(status);
}

void CameraDevice::deliverStrobeStatus(const QString &status)
{
    emit strobeStatusChanged(status);
}

void CameraDevice::deliverError(const QString &error)
{
    setLocalError(error);
}

void CameraDevice::deliverLog(const QString &line)
{
    emit logMessage(line);
}

void CameraDevice::setLocalState(CameraState state)
{
    if (currentState == state)
        return;
    currentState = state;
    emit stateChanged(state);
}

void CameraDevice::setLocalError(const QString &error)
{
    currentLastError = error;
    emit errorChanged(error);
    emit logMessage(error);
}

bool CameraDevice::requirePreviewThread(const QString &operation)
{
    if (!captureThread || !captureThread->isRunning()) {
        setLocalError(QStringLiteral("%1 requires active preview streaming").arg(operation));
        return false;
    }
    return true;
}

bool CameraDevice::refreshCaps(const QString &devicePath)
{
    currentCaps = queryCaps(devicePath);
    if (!currentCaps.available) {
        setLocalError(currentCaps.error);
        setLocalState(CameraState::Error);
        return false;
    }
    currentLastError.clear();
    emit errorChanged(QString());
    return true;
}

} // namespace imx6sm
