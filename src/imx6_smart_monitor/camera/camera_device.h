#ifndef IMX6SMARTMONITOR_CAMERA_DEVICE_H
#define IMX6SMARTMONITOR_CAMERA_DEVICE_H

#include "imx6smartmonitor/types.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

namespace imx6sm {

/*
 * CameraFrameInterval / CameraMode 使用 fpsNum / fpsDen 表示帧率：
 * 30/1 表示 30 fps，15/1 表示 15 fps。V4L2 底层实际使用
 * timeperframe(numerator/denominator)，实现中会在枚举或配置时做一次转换。
 */
struct CameraFrameInterval {
    int fpsNum = 0;
    int fpsDen = 1;

    /* 返回适合 UI 显示的帧率文本，例如 "30 fps"；非法帧率返回 "--"。 */
    QString label() const;
};

/* 单个 V4L2 pixel format 描述，来自 VIDIOC_ENUM_FMT。 */
struct CameraFormat {
    /* V4L2 fourcc 的整数值，例如 v4l2_fourcc('R', 'G', 'B', 'P')。 */
    quint32 pixelFormat = 0;
    /* 4 字符格式名，例如 "RGBP"、"JPEG"、"MJPG"。 */
    QString fourcc;
    /* 驱动返回的格式描述字符串。 */
    QString description;
};

/* V4L2 menu / integer-menu control 的一个候选值。 */
struct CameraControlMenuItem {
    int value = 0;
    QString name;
};

/* 单个 V4L2 control 描述，来自 VIDIOC_QUERYCTRL / VIDIOC_QUERYMENU。 */
struct CameraControl {
    quint32 id = 0;
    QString name;
    QString typeName;
    /* minimum/maximum/step/defaultValue/currentValue 直接对应 V4L2 control 元数据。 */
    int minimum = 0;
    int maximum = 0;
    int step = 0;
    int defaultValue = 0;
    int currentValue = 0;
    /* V4L2_CTRL_FLAG_* 原始标志。调用前应检查 disabled/read-only 等限制。 */
    quint32 flags = 0;
    QList<CameraControlMenuItem> menuItems;

    /* control 被驱动标记为 disabled 时，不应展示为可操作能力。 */
    bool isDisabled() const;
    /* read-only control 只能读状态，不能通过 VIDIOC_S_CTRL 修改。 */
    bool isReadOnly() const;
    /* 返回 "0x<id> <name> (<type>)"，主要用于调试列表。 */
    QString label() const;
};

/*
 * 摄像头能力快照。
 *
 * queryCaps() / openDevice() 会临时打开 video device，读取 QUERYCAP、格式、
 * frame size / interval 和 controls。available=false 时 error 给出失败原因。
 *
 * 注意：formats 记录驱动枚举到的所有格式；当前采集实现只真正支持
 * RGB565/RGBP 和 JPEG/MJPG。modes 中可能包含其它 fourcc，调用 startPreview()
 * 或 setMode() 前要让 preferredMode() 或 UI 过滤可采集格式。
 */
struct CameraCaps {
    bool available = false;
    QString devicePath;
    QString driver;
    QString card;
    QString busInfo;
    QString error;
    QList<CameraFormat> formats;
    QList<CameraMode> modes;
    QList<CameraControl> controls;

    /* 只有 control 存在且未 disabled 时返回 true。 */
    bool hasControl(quint32 id) const;
    /* 按 V4L2 control id 查找 controls 下标，未找到返回 -1。 */
    int controlIndex(quint32 id) const;
    /* 按归一化后的名字查找 control，下划线/空格/大小写差异会被忽略。 */
    int controlIndexByName(const QString &name) const;
};

/*
 * 实际采集线程和保存线程的前置声明。
 *
 * CameraDevice 是 UI / controller 侧使用的 QObject facade：
 * - CameraCaptureThread 在后台线程中执行 open/ioctl/select/MMAP 采集。
 * - CameraSaveWorker 在后台线程中执行 JPEG 编码、snapshot 写入和短录制写入。
 * - worker 通过 Qt::QueuedConnection 回到 CameraDevice 的 deliver* 槽，再发出 public signals。
 */
class CameraCaptureThread;
class CameraSaveWorker;

/*
 * V4L2 摄像头高层封装。
 *
 * 推荐调用顺序：
 * 1. openDevice("/dev/videoX") 查询并缓存能力。
 * 2. 从 capabilities().modes 选择 RGBP 预览模式，或用 preferredMode(caps)。
 * 3. startPreview(mode) 启动采集线程。
 * 4. 通过 signals 接收 frame/state/fps/error/status。
 * 5. 用 requestSnapshot()/startRecording()/setStrobeMode()/startAutoFocus() 发异步命令。
 * 6. stopPreview() 或 closeDevice() 释放采集资源。
 *
 * 线程注意事项：
 * - public 方法按 QObject 使用习惯从 CameraDevice 所在线程调用。
 * - 采集和保存是异步的，返回 true/Ok 只代表请求已接受；最终结果看 status/error signals。
 * - frameReady() 为 UI 预览做了约 100 ms 节流；snapshot/recording 使用最新原始帧，
 *   不依赖 frameReady() 的显示频率。
 */
class CameraDevice : public QObject {
    Q_OBJECT

public:
    enum class ActionResult {
        /* 请求已接受，后续状态通过 snapshotStatusChanged()/recordingStatusChanged() 返回。 */
        Ok,
        /* 预留值；当前实现的 snapshot/recording 路径主要返回 Ok 或 Failed。 */
        NotImplemented,
        /* 参数错误、未启动 preview、后台 worker 不可用或请求入队失败。 */
        Failed,
    };

    explicit CameraDevice(QObject *parent = nullptr);
    /*
     * 析构时会停止录制、停止预览，并等待保存线程退出。
     * 调用侧不需要手动 join 后台线程，但长时间写文件仍可能让析构短暂阻塞。
     */
    ~CameraDevice() override;

    /*
     * 只查询指定 video device 的能力，不改变当前已打开设备状态。
     * 实现会 open(O_RDWR | O_NONBLOCK)，查询完立即 close。
     */
    CameraCaps queryCaps(const QString &devicePath) const;

    /*
     * 查询并缓存 devicePath 的能力，选择 preferredMode() 作为 currentMode。
     * 不会启动 streaming；切换到不同 path 时会先停止已有 preview。
     */
    bool openDevice(const QString &devicePath);
    /* 停止 preview 并清空当前设备、模式、最近帧和 pending snapshot 状态。 */
    void closeDevice();

    /*
     * 启动采集线程并配置 mode；mode 为空时使用 preferredMode(currentCaps)。
     * 当前实现支持 RGB565/RGBP 预览帧，也支持 JPEG/MJPG 原始 JPEG 采集。
     * JPEG 模式不会产生 QImage frameReady()，但可用于 snapshot/recording 直接保存 JPEG。
     */
    bool startPreview(const CameraMode &mode = CameraMode());
    /*
     * 停止底层 streaming 和采集线程；如果正在 recording，会先请求停止录制。
     * 如果只是想隐藏 UI 预览、保持采集供 snapshot 使用，用 setPreviewDisplayEnabled(false)。
     */
    void stopPreview();
    /*
     * 在 preview 已启动时切换采集模式。
     * 请求进入后台线程后异步执行；切换结果通过 activeModeChanged()/stateChanged()/errorChanged() 观察。
     */
    bool setMode(const CameraMode &mode);

    /*
     * 设置闪光灯模式：
     * - None: 关闭
     * - Torch: 常亮补光
     * - Flash: 预备闪光模式
     * 需要 preview 运行，且设备暴露 V4L2_CID_FLASH_LED_MODE。
     */
    bool setStrobeMode(StrobeMode mode);
    /* 触发一次 flash pulse；调用侧通常先检查 supportsFlashPulse()。 */
    bool triggerFlash();
    /* 停止 flash pulse 并尽量把 LED mode 复位为 None。 */
    bool stopFlash();
    /* 使用默认对焦区域启动一次 AF；需要 streaming 和 V4L2_CID_AUTO_FOCUS_START。 */
    bool startAutoFocus();
    /*
     * 触摸对焦：activeFrameX/Y 使用当前 activeMode 的图像坐标。
     * 实现会 clamp 到有效图像范围，再写 OV5640 自定义 touch AF controls。
     */
    bool focusTouch(int activeFrameX, int activeFrameY);

    /*
     * 请求 snapshot 写入 path。
     * - frameDelay=0：立即使用最新帧；尚未采到帧会 Failed。
     * - frameDelay>0：等待后续若干帧后保存，适合切换模式或触发闪光后避开旧帧。
     * 同一时间只允许一个 pending snapshot。RGBP 会编码 JPEG，JPEG/MJPG 会写原始 JPEG bytes。
     */
    ActionResult requestSnapshot(const QString &path, int frameDelay = 0);
    /*
     * 开始录制到 path。
     * 当前实现写入连续 JPEG bytes，保存线程约 200 ms 接收一帧。
     * maxDurationMs=0 表示由调用侧显式 stopRecording()；大于 0 时到期自动停止。
     * 文件扩展名由调用侧决定；它不是标准容器格式视频。
     */
    ActionResult startRecording(const QString &path, int maxDurationMs = 0);
    /* 请求停止录制；未录制时返回 Ok。最终保存/失败状态看 recordingStatusChanged()。 */
    ActionResult stopRecording();

    /* 最近一次 openDevice()/refreshCaps() 缓存的能力。 */
    CameraCaps capabilities() const;
    /* 当前配置成功的模式；openDevice() 后为 preferredMode()，streaming 后为驱动实际接受的模式。 */
    CameraMode activeMode() const;
    /* CameraDevice facade 维护的最新状态。 */
    CameraState state() const;
    /* 最近一次本地或后台线程上报的错误文本。 */
    QString lastError() const;
    /* Reconfiguring 和 Streaming 都视为采集线程已在工作。 */
    bool isStreaming() const;
    /*
     * 只控制 frameReady() 是否继续发给 UI。
     * false 不会停止 V4L2 streaming，snapshot/recording 仍可继续使用最新帧。
     */
    void setPreviewDisplayEnabled(bool enabled);
    bool isPreviewDisplayEnabled() const;

    /* 以下能力判断基于最近一次 capabilities() 中的 V4L2 controls。 */
    bool supportsStrobeMode() const;
    bool supportsFlashPulse() const;
    bool supportsAutoFocus() const;
    bool supportsTouchFocus() const;

    /*
     * 从能力列表中选择默认模式：
     * 优先 800x480 RGBP 30fps，其次 800x480 RGBP、任意 RGBP、任意可采集 RGBP/JPEG/MJPG。
     */
    static CameraMode preferredMode(const CameraCaps &caps);

signals:
    /* RGB565/RGBP 预览帧；为了 UI 负载，CameraDevice 层约 100 ms 最多发一次。 */
    void frameReady(const QImage &image);
    /* CameraState 变化：Opened/Reconfiguring/Streaming/Error/Closed 等。 */
    void stateChanged(CameraState state);
    /* 驱动实际接受的新模式；setMode()/startPreview() 成功配置后触发。 */
    void activeModeChanged(const CameraMode &mode);
    /* 底层采集线程统计的累计帧数和最近一秒 fps。 */
    void frameStatsChanged(qulonglong frameCount, double fps);
    /* AF 状态文本，例如 STARTED/BUSY/REACHED/FAILED/TIMEOUT。 */
    void afStatusChanged(const QString &status);
    /* 闪光/补光状态文本，例如 off/torch/flash triggered。 */
    void strobeStatusChanged(const QString &status);
    /* snapshot 生命周期状态：waiting/saving/saved/failed。 */
    void snapshotStatusChanged(const QString &status);
    /* recording 生命周期状态：recording/saved/failed/stopped。 */
    void recordingStatusChanged(const QString &status);
    /* 错误状态变化；空字符串表示 openDevice()/refreshCaps() 清除了旧错误。 */
    void errorChanged(const QString &error);
    /* 给 UI 或日志面板展示的诊断文本。 */
    void logMessage(const QString &line);

private slots:
    /*
     * 以下 deliver* 槽由后台线程通过 QueuedConnection 调用。
     * 它们把后台事件同步回 facade 状态，再向 public signals 转发。
     */
    void deliverFrame(const QImage &image);
    void deliverJpegFrame(const QByteArray &jpeg);
    void deliverState(CameraState state);
    void deliverActiveMode(const CameraMode &mode);
    void deliverFrameStats(qulonglong frameCount, double fps);
    void deliverAfStatus(const QString &status);
    void deliverStrobeStatus(const QString &status);
    void deliverSnapshotStatus(const QString &status);
    void deliverRecordingStatus(const QString &status);
    void deliverError(const QString &error);
    void deliverLog(const QString &line);

private:
    /* 更新本地状态并只在变化时发 stateChanged()。 */
    void setLocalState(CameraState state);
    /* 记录最后错误，同时发 errorChanged() 和 logMessage()。 */
    void setLocalError(const QString &error);
    /* 检查采集线程是否正在运行；需要 streaming 的 API 都走这里。 */
    bool requirePreviewThread(const QString &operation);
    /* 重新 queryCaps() 并缓存结果；失败时进入 Error 状态。 */
    bool refreshCaps(const QString &devicePath);
    /* 把 RGB QImage snapshot 入队到保存线程，由保存线程编码 JPEG。 */
    ActionResult queueSnapshotImage(const QString &path, const QImage &image);
    /* 把 JPEG/MJPG 原始帧入队到保存线程，直接写文件。 */
    ActionResult queueSnapshotJpeg(const QString &path, const QByteArray &jpeg);

    /* captureThread/saveWorker 由 CameraDevice 拥有，析构时统一停止和释放。 */
    CameraCaptureThread *captureThread;
    CameraSaveWorker *saveWorker;
    /* openDevice()/refreshCaps() 得到的能力缓存和当前模式/状态。 */
    CameraCaps currentCaps;
    CameraMode currentMode;
    CameraState currentState;
    QString currentDevicePath;
    QString currentLastError;
    /* 最近一次采到的 RGB 帧或 JPEG 原始帧，供 snapshot/recording 使用。 */
    QImage latestFrame;
    QByteArray latestJpegFrame;
    /* frameDelay snapshot 的等待状态；同一时间只允许一个 pending snapshot。 */
    QString pendingSnapshotPath;
    int pendingSnapshotFrames;
    /* frameReady() 的 UI 节流计时器，不影响实际采集和保存。 */
    QElapsedTimer previewFrameTimer;
    qint64 lastPreviewFrameMs;
    /* recordingGeneration 用来让旧的 singleShot 自动停止回调失效。 */
    bool recordingActive;
    bool previewDisplayEnabled;
    QString recordingPath;
    int recordingGeneration;
};

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::CameraControl)
Q_DECLARE_METATYPE(imx6sm::CameraCaps)

#endif
