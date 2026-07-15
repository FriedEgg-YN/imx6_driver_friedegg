#include "sensors/ld2410_device.h"

#include <cerrno>
#include <cstring>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <asm/termbits.h>

namespace imx6sm {

Ld2410Device::Ld2410Device()
    : miscFd(-1)
    , uartFd(-1)
{
}

Ld2410Device::~Ld2410Device()
{
    closeDevice();
    if (uartFd >= 0)
        ::close(uartFd);
}

void Ld2410Device::setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString Ld2410Device::findMiscPath() const
{
    const QString path = QStringLiteral("/dev/ld2410c0");
    return QFileInfo::exists(path) ? path : QString();
}

QString Ld2410Device::findInputHint() const
{
    QFile file(QStringLiteral("/proc/bus/input/devices"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    QString currentName;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith(QStringLiteral("N:"))) {
            currentName = line;
            continue;
        }

        if (!line.startsWith(QStringLiteral("H:")))
            continue;

        const QString lower = (currentName + QLatin1Char(' ') + line).toLower();
        if (!lower.contains(QStringLiteral("ld2410")) && !lower.contains(QStringLiteral("presence")))
            continue;

        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.startsWith(QStringLiteral("event")))
                return QStringLiteral("/dev/input/") + part;
        }
    }

    return QString();
}

QString Ld2410Device::defaultUartPath() const
{
    return QStringLiteral("/dev/ttymxc2");
}

Ld2410Probe Ld2410Device::probe(const QString &miscPath, const QString &outPath, const QString &uartPath) const
{
    Ld2410Probe result;
    result.miscPath = miscPath == QStringLiteral("auto") ? findMiscPath() : miscPath;
    result.inputHint = findInputHint();
    result.outPath = outPath == QStringLiteral("auto") ? result.inputHint : outPath;
    result.uartPath = uartPath == QStringLiteral("auto") ? defaultUartPath() : uartPath;

    if (!result.miscPath.isEmpty())
        result.miscAvailable = QFileInfo::exists(result.miscPath);
    if (!result.outPath.isEmpty())
        result.outAvailable = QFileInfo::exists(result.outPath);
    if (!result.uartPath.isEmpty())
        result.uartAvailable = QFileInfo::exists(result.uartPath);

    result.attached = miscFd >= 0;

    if (!result.miscAvailable && !result.outAvailable && !result.uartAvailable)
        result.error = QStringLiteral("no LD2410C misc/input or UART node found");

    return result;
}

bool Ld2410Device::openDevice(const QString &miscPath, QString *error)
{
    const QString path = miscPath.isEmpty() || miscPath == QStringLiteral("auto") ? findMiscPath() : miscPath;
    if (path.isEmpty()) {
        setError(error, QStringLiteral("/dev/ld2410c0 not found"));
        return false;
    }

    if (miscFd >= 0 && openedMiscPath == path)
        return true;

    if (miscFd >= 0)
        ::close(miscFd);

    const QByteArray pathBytes = path.toLocal8Bit();
    miscFd = ::open(pathBytes.constData(), O_RDWR | O_CLOEXEC);
    if (miscFd < 0) {
        setError(error, QStringLiteral("open %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
        openedMiscPath.clear();
        return false;
    }

    openedMiscPath = path;
    return true;
}

void Ld2410Device::closeDevice()
{
    if (miscFd >= 0) {
        ::close(miscFd);
        miscFd = -1;
    }
    openedMiscPath.clear();
}

bool Ld2410Device::attachUart(const QString &uartPath, quint32 baud, QString *error)
{
    const QString path = uartPath.isEmpty() || uartPath == QStringLiteral("auto") ? defaultUartPath() : uartPath;
    struct termios2 tio;
    int ldisc = LD2410C_LDISC;

    if (uartFd >= 0) {
        ::close(uartFd);
        uartFd = -1;
    }

    const QByteArray pathBytes = path.toLocal8Bit();
    uartFd = ::open(pathBytes.constData(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (uartFd < 0) {
        setError(error, QStringLiteral("open %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    if (::ioctl(uartFd, TCGETS2, &tio) < 0) {
        setError(error, QStringLiteral("TCGETS2 %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(uartFd);
        uartFd = -1;
        return false;
    }

    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag |= BOTHER | CS8 | CLOCAL | CREAD;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    if (::ioctl(uartFd, TCSETS2, &tio) < 0) {
        setError(error, QStringLiteral("TCSETS2 %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(uartFd);
        uartFd = -1;
        return false;
    }

    if (::ioctl(uartFd, TIOCSETD, &ldisc) < 0) {
        setError(error, QStringLiteral("TIOCSETD %1: %2").arg(path, QString::fromLocal8Bit(std::strerror(errno))));
        ::close(uartFd);
        uartFd = -1;
        return false;
    }

    return true;
}

bool Ld2410Device::ensureOpen(QString *error)
{
    if (miscFd >= 0)
        return true;
    return openDevice(QStringLiteral("auto"), error);
}

Ld2410State Ld2410Device::convertState(const struct ld2410c_state &raw, const QString &source)
{
    Ld2410State state;
    state.available = true;
    state.reportValid = raw.flags & LD2410C_STATE_F_REPORT_VALID;
    state.engineering = raw.flags & LD2410C_STATE_F_ENGINEERING;
    state.outValid = raw.flags & LD2410C_STATE_F_OUT_VALID;
    state.outLevel = raw.out_level != 0;
    state.presence = raw.flags & LD2410C_STATE_F_OUT_ACTIVE;
    if (!state.presence)
        state.presence = raw.target_state == LD2410C_TARGET_MOVING ||
                         raw.target_state == LD2410C_TARGET_STATIC ||
                         raw.target_state == LD2410C_TARGET_MOVING_STATIC;
    state.targetState = raw.target_state;
    state.movingDistanceCm = raw.motion_distance_cm;
    state.staticDistanceCm = raw.static_distance_cm;
    state.detectDistanceCm = raw.detect_distance_cm;
    state.movingEnergy = raw.motion_energy;
    state.staticEnergy = raw.static_energy;
    state.maxMovingGate = raw.max_motion_gate;
    state.maxStaticGate = raw.max_static_gate;
    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        state.movingGateEnergy[i] = raw.motion_gate_energy[i];
        state.staticGateEnergy[i] = raw.static_gate_energy[i];
    }
    state.light = raw.light;
    state.frameCount = raw.frame_count;
    state.errorCount = raw.error_count;
    state.sequence = raw.sequence;
    state.source = source;
    return state;
}

Ld2410Config Ld2410Device::convertConfig(const struct ld2410c_config &raw)
{
    Ld2410Config config;
    config.available = raw.flags != 0;
    config.maxGate = raw.max_gate;
    config.motionGate = raw.motion_gate;
    config.staticGate = raw.static_gate;
    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        config.motionSensitivity[i] = raw.motion_sensitivity[i];
        config.staticSensitivity[i] = raw.static_sensitivity[i];
    }
    config.idleTimeS = raw.idle_time_s;
    return config;
}

bool Ld2410Device::readState(Ld2410State *state, QString *error)
{
    struct ld2410c_state raw;

    if (!ensureOpen(error))
        return false;

    if (::ioctl(miscFd, LD2410C_IOC_GET_STATE, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_GET_STATE: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    if (state)
        *state = convertState(raw, openedMiscPath);
    return true;
}

bool Ld2410Device::pollEvent(int timeoutMs, Ld2410State *state, QString *error)
{
    struct pollfd pfd;
    struct ld2410c_state raw;
    int ret;

    if (!ensureOpen(error))
        return false;

    pfd.fd = miscFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = ::poll(&pfd, 1, timeoutMs);
    if (ret < 0) {
        setError(error, QStringLiteral("poll LD2410C: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (ret == 0) {
        setError(error, QStringLiteral("poll timeout"));
        return false;
    }

    ret = ::read(miscFd, &raw, sizeof(raw));
    if (ret != static_cast<int>(sizeof(raw))) {
        setError(error, QStringLiteral("read LD2410C: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    if (state)
        *state = convertState(raw, openedMiscPath);
    return true;
}

bool Ld2410Device::readConfig(Ld2410Config *config, QString *error)
{
    struct ld2410c_config raw;

    if (!ensureOpen(error))
        return false;
    if (::ioctl(miscFd, LD2410C_IOC_READ_CONFIG, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_READ_CONFIG: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (config)
        *config = convertConfig(raw);
    return true;
}

bool Ld2410Device::writeConfig(const Ld2410Config &config, QString *error)
{
    struct ld2410c_gate_config gates;

    if (!ensureOpen(error))
        return false;

    std::memset(&gates, 0, sizeof(gates));
    gates.motion_gate = config.motionGate;
    gates.static_gate = config.staticGate;
    gates.idle_time_s = config.idleTimeS;
    if (::ioctl(miscFd, LD2410C_IOC_SET_MAX_GATE, &gates) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_SET_MAX_GATE: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        struct ld2410c_gate_sensitivity sens;
        std::memset(&sens, 0, sizeof(sens));
        sens.gate = i;
        sens.motion_sensitivity = config.motionSensitivity[i];
        sens.static_sensitivity = config.staticSensitivity[i];
        if (::ioctl(miscFd, LD2410C_IOC_SET_GATE_SENSITIVITY, &sens) < 0) {
            setError(error, QStringLiteral("LD2410C_IOC_SET_GATE_SENSITIVITY gate %1: %2")
                                .arg(i)
                                .arg(QString::fromLocal8Bit(std::strerror(errno))));
            return false;
        }
    }

    return true;
}

bool Ld2410Device::setEngineeringMode(bool enable, QString *error)
{
    struct ld2410c_mode mode;

    if (!ensureOpen(error))
        return false;
    std::memset(&mode, 0, sizeof(mode));
    mode.enable = enable ? 1 : 0;
    if (::ioctl(miscFd, LD2410C_IOC_SET_ENGINEERING_MODE, &mode) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_SET_ENGINEERING_MODE: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

bool Ld2410Device::getVersion(QString *version, QString *error)
{
    struct ld2410c_version raw;

    if (!ensureOpen(error))
        return false;
    if (::ioctl(miscFd, LD2410C_IOC_GET_VERSION, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_GET_VERSION: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (version)
        *version = QString::fromLatin1(raw.text);
    return true;
}

bool Ld2410Device::setBaud(quint32 baud, QString *error)
{
    struct ld2410c_baud raw;

    if (!ensureOpen(error))
        return false;
    raw.baud = baud;
    if (::ioctl(miscFd, LD2410C_IOC_SET_BAUD, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_SET_BAUD: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

bool Ld2410Device::setResolution(quint8 index, QString *error)
{
    struct ld2410c_resolution raw;

    if (!ensureOpen(error))
        return false;
    std::memset(&raw, 0, sizeof(raw));
    raw.index = index;
    if (::ioctl(miscFd, LD2410C_IOC_SET_RESOLUTION, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_SET_RESOLUTION: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

bool Ld2410Device::setAuxControl(quint8 mode, quint8 threshold, bool outDefaultHigh, QString *error)
{
    struct ld2410c_aux_control raw;

    if (!ensureOpen(error))
        return false;
    std::memset(&raw, 0, sizeof(raw));
    raw.mode = mode;
    raw.threshold = threshold;
    raw.out_default_high = outDefaultHigh ? 1 : 0;
    if (::ioctl(miscFd, LD2410C_IOC_SET_AUX_CONTROL, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_SET_AUX_CONTROL: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

bool Ld2410Device::startNoiseCalibration(quint16 durationS, quint16 *status, QString *error)
{
    struct ld2410c_noise raw;

    if (!ensureOpen(error))
        return false;
    std::memset(&raw, 0, sizeof(raw));
    raw.duration_s = durationS;
    if (::ioctl(miscFd, LD2410C_IOC_START_NOISE_CALIBRATION, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_START_NOISE_CALIBRATION: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (status)
        *status = raw.status;
    return true;
}

bool Ld2410Device::getNoiseStatus(quint16 *status, QString *error)
{
    struct ld2410c_noise raw;

    if (!ensureOpen(error))
        return false;
    std::memset(&raw, 0, sizeof(raw));
    if (::ioctl(miscFd, LD2410C_IOC_GET_NOISE_STATUS, &raw) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_GET_NOISE_STATUS: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    if (status)
        *status = raw.status;
    return true;
}

bool Ld2410Device::factoryReset(QString *error)
{
    if (!ensureOpen(error))
        return false;
    if (::ioctl(miscFd, LD2410C_IOC_FACTORY_RESET) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_FACTORY_RESET: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

bool Ld2410Device::reboot(QString *error)
{
    if (!ensureOpen(error))
        return false;
    if (::ioctl(miscFd, LD2410C_IOC_REBOOT) < 0) {
        setError(error, QStringLiteral("LD2410C_IOC_REBOOT: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    return true;
}

} // namespace imx6sm
