#ifndef IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H
#define IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "camera/camera_device.h"

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
    void buildCameraLayout();
    QVBoxLayout *createMenuPage(QWidget **page);
    QPushButton *createSideButton(const QString &text);
    void showMainMenu();
    void setSelectedStrobeMode(StrobeMode mode);
    void takeSnapshot();
    void toggleLog();
    void queryCaps();
    void startPreview();
    void stopPreview();
    void updateModeList(const CameraCaps &caps);
    void updateButtons();
    void updateStrobeButtons();
    void updatePreviewOverlay();
    void handlePreviewTouch(const QPoint &pos, const QSize &imageSize, const QRect &imageRect);
    CameraMode selectedMode() const;
    QString modeListSummary(const CameraCaps &caps) const;
    QString controlSummary(const CameraCaps &caps) const;

    CameraDevice camera;
    QWidget *bodyWidget;
    QStackedWidget *menuStack;
    QWidget *mainMenuPage;
    QWidget *deviceMenuPage;
    QWidget *previewMenuPage;
    QWidget *strobeMenuPage;
    QWidget *captureMenuPage;
    QLineEdit *pathEdit;
    QComboBox *modeCombo;
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
    CameraMode currentActiveMode;
    StrobeMode selectedStrobeMode;
    QString strobeStatusText;
};

} // namespace imx6sm

#endif
