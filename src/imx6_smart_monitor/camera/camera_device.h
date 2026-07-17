#ifndef IMX6SMARTMONITOR_CAMERA_DEVICE_H
#define IMX6SMARTMONITOR_CAMERA_DEVICE_H

#include "imx6smartmonitor/types.h"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

namespace imx6sm {

struct CameraFrameInterval {
    int fpsNum = 0;
    int fpsDen = 1;

    QString label() const;
};

struct CameraFormat {
    quint32 pixelFormat = 0;
    QString fourcc;
    QString description;
};

struct CameraControlMenuItem {
    int value = 0;
    QString name;
};

struct CameraControl {
    quint32 id = 0;
    QString name;
    QString typeName;
    int minimum = 0;
    int maximum = 0;
    int step = 0;
    int defaultValue = 0;
    int currentValue = 0;
    quint32 flags = 0;
    QList<CameraControlMenuItem> menuItems;

    bool isDisabled() const;
    bool isReadOnly() const;
    QString label() const;
};

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

    bool hasControl(quint32 id) const;
    int controlIndex(quint32 id) const;
    int controlIndexByName(const QString &name) const;
};

class CameraCaptureThread;
class CameraSaveWorker;

class CameraDevice : public QObject {
    Q_OBJECT

public:
    enum class ActionResult {
        Ok,
        NotImplemented,
        Failed,
    };

    explicit CameraDevice(QObject *parent = nullptr);
    ~CameraDevice() override;

    CameraCaps queryCaps(const QString &devicePath) const;

    bool openDevice(const QString &devicePath);
    void closeDevice();

    bool startPreview(const CameraMode &mode = CameraMode());
    void stopPreview();
    bool setMode(const CameraMode &mode);

    bool setStrobeMode(StrobeMode mode);
    bool triggerFlash();
    bool stopFlash();
    bool startAutoFocus();
    bool focusTouch(int activeFrameX, int activeFrameY);

    ActionResult requestSnapshot(const QString &path, int frameDelay = 0);
    ActionResult startRecording(const QString &path);
    ActionResult stopRecording();

    CameraCaps capabilities() const;
    CameraMode activeMode() const;
    CameraState state() const;
    QString lastError() const;
    bool isStreaming() const;
    void setPreviewDisplayEnabled(bool enabled);
    bool isPreviewDisplayEnabled() const;

    bool supportsStrobeMode() const;
    bool supportsFlashPulse() const;
    bool supportsAutoFocus() const;
    bool supportsTouchFocus() const;

    static CameraMode preferredMode(const CameraCaps &caps);

signals:
    void frameReady(const QImage &image);
    void stateChanged(CameraState state);
    void activeModeChanged(const CameraMode &mode);
    void frameStatsChanged(qulonglong frameCount, double fps);
    void afStatusChanged(const QString &status);
    void strobeStatusChanged(const QString &status);
    void snapshotStatusChanged(const QString &status);
    void recordingStatusChanged(const QString &status);
    void errorChanged(const QString &error);
    void logMessage(const QString &line);

private slots:
    void deliverFrame(const QImage &image);
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
    void setLocalState(CameraState state);
    void setLocalError(const QString &error);
    bool requirePreviewThread(const QString &operation);
    bool refreshCaps(const QString &devicePath);
    ActionResult queueSnapshotImage(const QString &path, const QImage &image);

    CameraCaptureThread *captureThread;
    CameraSaveWorker *saveWorker;
    CameraCaps currentCaps;
    CameraMode currentMode;
    CameraState currentState;
    QString currentDevicePath;
    QString currentLastError;
    QImage latestFrame;
    QString pendingSnapshotPath;
    int pendingSnapshotFrames;
    QElapsedTimer previewFrameTimer;
    qint64 lastPreviewFrameMs;
    bool recordingActive;
    bool previewDisplayEnabled;
    QString recordingPath;
    int recordingGeneration;
};

} // namespace imx6sm

Q_DECLARE_METATYPE(imx6sm::CameraControl)
Q_DECLARE_METATYPE(imx6sm::CameraCaps)

#endif
