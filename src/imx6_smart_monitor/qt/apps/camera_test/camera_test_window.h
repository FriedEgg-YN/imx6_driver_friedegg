#ifndef IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H
#define IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "camera/camera_device.h"
#include "storage/storage_manager.h"

#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace imx6sm {

class PreviewWidget;

class CameraTestWindow : public ModuleTestWindow {
public:
    explicit CameraTestWindow(QWidget *parent = nullptr);
    ~CameraTestWindow() override;

private:
    enum class PendingCaptureAction {
        None,
        Snapshot,
        FlashSnapshot,
        Record,
    };

    void buildCameraLayout();
    QVBoxLayout *createMenuPage(QWidget **page);
    QPushButton *createSideButton(const QString &text);
    void showMainMenu();
    void setSelectedStrobeMode(StrobeMode mode);
    void handleStrobeStatus(const QString &status);
    void takeSnapshot();
    void startRecording();
    void toggleLog();
    void queryCaps();
    void startPreview();
    void stopPreview();
    void updateModeList(const CameraCaps &caps);
    void updateButtons();
    void updateStrobeButtons();
    void updatePreviewOverlay();
    void handlePreviewTouch(const QPoint &pos, const QSize &imageSize, const QRect &imageRect);
    bool captureActionPending() const;
    bool ensureCaptureRoot();
    bool prepareCaptureAction(PendingCaptureAction action, const QString &path, const QString &relativePath);
    void startPendingCaptureAction();
    void requestSnapshotNow(const QString &path, const QString &relativePath, bool flash);
    void requestRecordingNow(const QString &path, const QString &relativePath);
    void restorePreviewModeIfNeeded();
    bool beginFlashSnapshot(const QString &path, const QString &relativePath);
    void handleFlashSnapshotTimeout(const QString &path);
    void finishFlashSnapshotStrobe();
    bool isFlashSnapshotPath(const QString &path) const;
    void handleSnapshotStatus(const QString &status);
    void handleRecordingStatus(const QString &status);
    void appendCaptureEvent(const QString &type, const QString &relativePath, const QString &status);
    void appendCaptureIndex(const QString &relativePath, const QString &kind, const QString &status);
    QString captureRelativePath(const QString &absolutePath) const;
    QString savedPathFromStatus(const QString &status) const;
    CameraMode selectedPreviewMode() const;
    CameraMode selectedCaptureMode() const;
    QString modeListSummary(const CameraCaps &caps) const;
    QString controlSummary(const CameraCaps &caps) const;

    StorageManager storage;
    CameraDevice camera;
    QWidget *bodyWidget;
    QStackedWidget *menuStack;
    QWidget *mainMenuPage;
    QWidget *deviceMenuPage;
    QWidget *previewMenuPage;
    QWidget *strobeMenuPage;
    QWidget *captureMenuPage;
    QLineEdit *pathEdit;
    QLineEdit *saveRootEdit;
    QComboBox *modeCombo;
    QComboBox *captureModeCombo;
    PreviewWidget *previewWidget;
    QLabel *driverLabel;
    QLabel *cardLabel;
    QLabel *busLabel;
    QLabel *formatLabel;
    QLabel *controlLabel;
    QLabel *stateLabel;
    QLabel *activeModeLabel;
    QLabel *frameLabel;
    QLabel *afLabel;
    QLabel *strobeLabel;
    QLabel *snapshotLabel;
    QLabel *recordingLabel;
    QLabel *errorLabel;
    QPushButton *deviceMenuButton;
    QPushButton *previewMenuButton;
    QPushButton *strobeMenuButton;
    QPushButton *captureMenuButton;
    QPushButton *logButton;
    QPushButton *queryButton;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *afButton;
    QPushButton *strobeOffButton;
    QPushButton *torchButton;
    QPushButton *flashButton;
    QPushButton *snapshotButton;
    QPushButton *recordButton;
    QList<CameraMode> visibleModes;
    QList<CameraMode> captureModes;
    CameraMode currentActiveMode;
    PendingCaptureAction pendingCaptureAction;
    CameraMode pendingCaptureMode;
    CameraMode restorePreviewMode;
    QString pendingCapturePath;
    QString pendingCaptureRelativePath;
    StrobeMode selectedStrobeMode;
    QString captureRootPath;
    QString strobeStatusText;
    QString flashSnapshotPath;
    QString flashSnapshotRelativePath;
    bool recordingUiActive;
    bool flashSnapshotActive;
    bool restorePreviewAfterCapture;
};

} // namespace imx6sm

#endif
