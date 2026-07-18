#ifndef IMX6SMARTMONITOR_LD2410_DEVICE_H
#define IMX6SMARTMONITOR_LD2410_DEVICE_H

#include <QString>
#include <QtGlobal>

#include <friedegg/ld2410c.h>

namespace imx6sm {

/*
 * LD2410C 节点探测结果。misc 是主应用读取 presence/config 的首选路径，
 * input 只作为 OUT 事件提示，UART 用于绑定 line discipline 和配置雷达。
 */
struct Ld2410Probe {
    QString miscPath;
    QString outPath;
    QString uartPath;
    QString inputHint;
    bool miscAvailable = false;
    bool outAvailable = false;
    bool uartAvailable = false;
    bool attached = false;
    QString error;
};

/*
 * LD2410C 实时状态快照。presence 是主闭环唯一依赖的 gate 信号，距离、能量、
 * 工程模式 gate 数据主要供 LD2410 Test 展示和调参，不直接耦合录制策略。
 */
struct Ld2410State {
    bool available = false;
    bool reportValid = false;
    bool engineering = false;
    bool outValid = false;
    bool outLevel = false;
    bool presence = false;
    quint8 targetState = 0;
    quint16 movingDistanceCm = 0;
    quint16 staticDistanceCm = 0;
    quint16 detectDistanceCm = 0;
    quint8 movingEnergy = 0;
    quint8 staticEnergy = 0;
    quint8 maxMovingGate = 0;
    quint8 maxStaticGate = 0;
    quint8 movingGateEnergy[LD2410C_MAX_GATES] = {};
    quint8 staticGateEnergy[LD2410C_MAX_GATES] = {};
    quint8 light = 0;
    quint32 frameCount = 0;
    quint32 errorCount = 0;
    quint64 sequence = 0;
    QString source;
    QString error;
};

/*
 * LD2410C 可配置参数镜像。主页面只提供基础入口，完整读写、工程模式、
 * 灵敏度和距离门配置仍在 LD2410 Test 中操作，避免主链路 UI 过重。
 */
struct Ld2410Config {
    bool available = false;
    quint8 maxGate = 0;
    quint8 motionGate = 0;
    quint8 staticGate = 0;
    quint8 motionSensitivity[LD2410C_MAX_GATES] = {};
    quint8 staticSensitivity[LD2410C_MAX_GATES] = {};
    quint16 idleTimeS = 0;
    quint8 resolutionIndex = 0;
    quint8 auxMode = 0;
    quint8 auxThreshold = 0;
    bool outDefaultHigh = false;
    quint32 baud = LD2410C_DEFAULT_BAUD;
    QString error;
};

/*
 * LD2410Device 是对内核 friedegg/ld2410c misc ioctl 与 UART 绑定的轻量封装。
 * 推荐顺序：probe() 找节点，openDevice() 打开 /dev/ld2410c0，readState() 周期读取；
 * 需要配置时 attachUart() 后调用 readConfig()/writeConfig()。本类不拥有线程，
 * 调用侧应避免在 UI 线程里做长时间阻塞配置或校准操作。
 */
class Ld2410Device {
public:
    Ld2410Device();
    ~Ld2410Device();

    Ld2410Device(const Ld2410Device &) = delete;
    Ld2410Device &operator=(const Ld2410Device &) = delete;

    Ld2410Probe probe(const QString &miscPath, const QString &outPath, const QString &uartPath) const;
    QString findMiscPath() const;
    QString findInputHint() const;
    QString defaultUartPath() const;

    bool openDevice(const QString &miscPath, QString *error = nullptr);
    void closeDevice();
    bool attachUart(const QString &uartPath, quint32 baud, QString *error = nullptr);
    bool readState(Ld2410State *state, QString *error = nullptr);
    bool pollEvent(int timeoutMs, Ld2410State *state, QString *error = nullptr);
    bool readConfig(Ld2410Config *config, QString *error = nullptr);
    bool writeConfig(const Ld2410Config &config, QString *error = nullptr);
    bool setEngineeringMode(bool enable, QString *error = nullptr);
    bool getVersion(QString *version, QString *error = nullptr);
    bool setBaud(quint32 baud, QString *error = nullptr);
    bool setResolution(quint8 index, QString *error = nullptr);
    bool setAuxControl(quint8 mode, quint8 threshold, bool outDefaultHigh, QString *error = nullptr);
    bool startNoiseCalibration(quint16 durationS, quint16 *status = nullptr, QString *error = nullptr);
    bool getNoiseStatus(quint16 *status, QString *error = nullptr);
    bool factoryReset(QString *error = nullptr);
    bool reboot(QString *error = nullptr);

private:
    bool ensureOpen(QString *error);
    static Ld2410State convertState(const struct ld2410c_state &raw, const QString &source);
    static Ld2410Config convertConfig(const struct ld2410c_config &raw);
    static void setError(QString *target, const QString &message);

    int miscFd;
    int uartFd;
    QString openedMiscPath;
};

} // namespace imx6sm

#endif
