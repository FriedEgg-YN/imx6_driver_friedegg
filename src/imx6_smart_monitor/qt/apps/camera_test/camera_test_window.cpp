#include "camera_test_window.h"

#include <QComboBox>
#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace imx6sm {

static bool isRgb565Mode(const CameraMode &mode)
{
    return mode.fourcc == QStringLiteral("RGBP") && mode.width > 0 && mode.height > 0;
}

static bool sameUiMode(const CameraMode &a, const CameraMode &b)
{
    return a.fourcc == b.fourcc && a.width == b.width && a.height == b.height &&
           a.fpsNum == b.fpsNum && a.fpsDen == b.fpsDen;
}

static QString statusPathFromText(const QString &status)
{
    QString text = status.trimmed();
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon >= 0)
        text = text.mid(colon + 1).trimmed();
    const int suffix = text.indexOf(QStringLiteral(" ("));
    if (suffix >= 0)
        text = text.left(suffix).trimmed();
    return text;
}

class PreviewWidget : public QWidget {
public:
    explicit PreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(320, 240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setFrame(const QImage &frame)
    {
        image = frame;
        update();
    }

    void clearFrame()
    {
        image = QImage();
        update();
    }

    void setOverlayText(const QString &text)
    {
        overlayText = text;
        update();
    }

    std::function<void(const QPoint &, const QSize &, const QRect &)> touchHandler;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#05080b")));
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

        const QRect target = targetRect();
        if (image.isNull()) {
            painter.setPen(QColor(QStringLiteral("#7c8990")));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No preview"));
        } else {
            painter.drawImage(target, image);
        }

        if (!overlayText.isEmpty()) {
            const int overlayHeight = 26;
            const QRect overlayRect = rect().adjusted(8, height() - overlayHeight - 8, -8, -8);
            painter.fillRect(overlayRect, QColor(0, 0, 0, 170));
            painter.setPen(QColor(QStringLiteral("#edf3f5")));
            const QString text = QFontMetrics(painter.font()).elidedText(overlayText, Qt::ElideRight, overlayRect.width() - 12);
            painter.drawText(overlayRect.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        }

        painter.setPen(QColor(QStringLiteral("#344550")));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!image.isNull()) {
            const QRect target = targetRect();
            if (target.contains(event->pos()) && touchHandler)
                touchHandler(event->pos(), image.size(), target);
        }
        QWidget::mousePressEvent(event);
    }

private:
    QRect targetRect() const
    {
        if (image.isNull())
            return rect();

        QSize scaled = image.size();
        scaled.scale(size(), Qt::KeepAspectRatio);
        const int x = (width() - scaled.width()) / 2;
        const int y = (height() - scaled.height()) / 2;
        return QRect(QPoint(x, y), scaled);
    }

    QImage image;
    QString overlayText;
};

CameraTestWindow::CameraTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("OV5640 Camera Test"), parent)
    , camera(this)
    , bodyWidget(new QWidget(this))
    , menuStack(new QStackedWidget(this))
    , mainMenuPage(nullptr)
    , deviceMenuPage(nullptr)
    , previewMenuPage(nullptr)
    , strobeMenuPage(nullptr)
    , captureMenuPage(nullptr)
    , pathEdit(new QLineEdit(QStringLiteral("/dev/video1"), this))
    , saveRootEdit(new QLineEdit(QStringLiteral("/smart-monitor"), this))
    , modeCombo(new QComboBox(this))
    , previewWidget(new PreviewWidget(this))
    , driverLabel(new QLabel(QStringLiteral("--"), this))
    , cardLabel(new QLabel(QStringLiteral("--"), this))
    , busLabel(new QLabel(QStringLiteral("--"), this))
    , formatLabel(new QLabel(QStringLiteral("--"), this))
    , controlLabel(new QLabel(QStringLiteral("--"), this))
    , stateLabel(new QLabel(QStringLiteral("--"), this))
    , activeModeLabel(new QLabel(QStringLiteral("--"), this))
    , frameLabel(new QLabel(QStringLiteral("0  0.0 fps"), this))
    , afLabel(new QLabel(QStringLiteral("--"), this))
    , strobeLabel(new QLabel(QStringLiteral("off"), this))
    , snapshotLabel(new QLabel(QStringLiteral("planned"), this))
    , recordingLabel(new QLabel(QStringLiteral("planned"), this))
    , errorLabel(new QLabel(QStringLiteral("--"), this))
    , deviceMenuButton(nullptr)
    , previewMenuButton(nullptr)
    , strobeMenuButton(nullptr)
    , captureMenuButton(nullptr)
    , logButton(nullptr)
    , queryButton(nullptr)
    , startButton(nullptr)
    , stopButton(nullptr)
    , afButton(nullptr)
    , strobeOffButton(nullptr)
    , torchButton(nullptr)
    , flashButton(nullptr)
    , snapshotButton(nullptr)
    , recordButton(nullptr)
    , selectedStrobeMode(StrobeMode::None)
    , strobeStatusText(QStringLiteral("off"))
    , captureSequence(0)
    , recordingUiActive(false)
    , flashSnapshotActive(false)
{
    resize(800, 480);
    buildCameraLayout();

    formatLabel->setWordWrap(true);
    controlLabel->setWordWrap(true);
    activeModeLabel->setWordWrap(true);
    errorLabel->setWordWrap(true);

    connect(queryButton, &QPushButton::clicked, this, [this]() { queryCaps(); });
    connect(startButton, &QPushButton::clicked, this, [this]() { startPreview(); });
    connect(stopButton, &QPushButton::clicked, this, [this]() { stopPreview(); });
    connect(afButton, &QPushButton::clicked, this, [this]() {
        if (camera.startAutoFocus())
            afLabel->setText(QStringLiteral("DEFAULT queued"));
        updatePreviewOverlay();
    });
    connect(strobeOffButton, &QPushButton::clicked, this, [this]() { setSelectedStrobeMode(StrobeMode::None); });
    connect(torchButton, &QPushButton::clicked, this, [this]() { setSelectedStrobeMode(StrobeMode::Torch); });
    connect(flashButton, &QPushButton::clicked, this, [this]() { setSelectedStrobeMode(StrobeMode::Flash); });
    connect(snapshotButton, &QPushButton::clicked, this, [this]() { takeSnapshot(); });
    connect(recordButton, &QPushButton::clicked, this, [this]() { startRecording(); });
    connect(modeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (camera.isStreaming())
                    camera.setMode(selectedMode());
                updatePreviewOverlay();
            });

    connect(&camera, &CameraDevice::frameReady, this, [this](const QImage &image) {
        previewWidget->setFrame(image);
    });
    connect(&camera, &CameraDevice::stateChanged, this, [this](CameraState state) {
        const QString stateText = toString(state);
        stateLabel->setText(stateText);
        setStatus(stateText);
        updateButtons();
        if (state == CameraState::Streaming) {
            if (selectedStrobeMode == StrobeMode::Torch && camera.supportsStrobeMode())
                camera.setStrobeMode(StrobeMode::Torch);
        } else if (flashSnapshotActive) {
            finishFlashSnapshotStrobe();
        }
        updatePreviewOverlay();
    });
    connect(&camera, &CameraDevice::activeModeChanged, this, [this](const CameraMode &mode) {
        currentActiveMode = mode;
        activeModeLabel->setText(mode.width > 0 ? mode.label() : QStringLiteral("--"));
        updatePreviewOverlay();
    });
    connect(&camera, &CameraDevice::frameStatsChanged, this,
            [this](qulonglong frameCount, double fps) {
                frameLabel->setText(QStringLiteral("%1  %2 fps")
                                        .arg(frameCount)
                                        .arg(fps, 0, char(102), 1));
                updatePreviewOverlay();
            });
    connect(&camera, &CameraDevice::afStatusChanged, this, [this](const QString &status) {
        afLabel->setText(status);
        updatePreviewOverlay();
    });
    connect(&camera, &CameraDevice::strobeStatusChanged, this, [this](const QString &status) {
        handleStrobeStatus(status);
    });
    connect(&camera, &CameraDevice::snapshotStatusChanged, this, [this](const QString &status) {
        handleSnapshotStatus(status);
    });
    connect(&camera, &CameraDevice::recordingStatusChanged, this, [this](const QString &status) {
        handleRecordingStatus(status);
    });
    connect(&camera, &CameraDevice::errorChanged, this, [this](const QString &error) {
        errorLabel->setText(error.isEmpty() ? QStringLiteral("--") : error);
        updateButtons();
        updatePreviewOverlay();
    });
    connect(&camera, &CameraDevice::logMessage, this, [this](const QString &line) {
        appendLog(line);
    });

    previewWidget->touchHandler = [this](const QPoint &pos, const QSize &imageSize, const QRect &imageRect) {
        handlePreviewTouch(pos, imageSize, imageRect);
    };

    stateLabel->setText(toString(CameraState::Closed));
    updateButtons();
    updatePreviewOverlay();
    queryCaps();
}


QVBoxLayout *CameraTestWindow::createMenuPage(QWidget **page)
{
    QWidget *newPage = new QWidget(menuStack);
    QVBoxLayout *layout = new QVBoxLayout(newPage);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    *page = newPage;
    menuStack->addWidget(newPage);
    return layout;
}

QPushButton *CameraTestWindow::createSideButton(const QString &text)
{
    QPushButton *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("sideButton"));
    button->setMinimumHeight(42);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

void CameraTestWindow::buildCameraLayout()
{
    setStandardBodyVisible(false);
    setLogVisible(false);
    logWidget()->setMinimumHeight(82);
    logWidget()->setMaximumHeight(104);
    contentLayout()->setContentsMargins(6, 6, 6, 6);
    contentLayout()->setSpacing(6);
    setStyleSheet(styleSheet() + QStringLiteral(
        "QPushButton#sideButton{font-size:14px;min-height:30px;padding:8px 4px;border-radius:3px;background:#1e637c;color:#ffffff;}"
        "QPushButton#sideButton:pressed{background:#2c7f9d;}"
        "QPushButton#sideButton:checked{background:#2f8f62;}"
        "QPushButton#sideButton:disabled{background:#31414a;color:#7c8990;}"
        "QLineEdit#sideEdit,QComboBox#sideCombo{font-size:13px;min-height:30px;padding:6px 4px;}"));

    menuStack->setFixedWidth(118);
    pathEdit->setObjectName(QStringLiteral("sideEdit"));
    pathEdit->setMinimumHeight(40);
    saveRootEdit->setObjectName(QStringLiteral("sideEdit"));
    saveRootEdit->setMinimumHeight(40);
    saveRootEdit->setPlaceholderText(QStringLiteral("/smart-monitor"));
    modeCombo->setObjectName(QStringLiteral("sideCombo"));
    modeCombo->setMinimumHeight(40);

    QVBoxLayout *mainLayout = createMenuPage(&mainMenuPage);
    deviceMenuButton = createSideButton(QStringLiteral("Device"));
    previewMenuButton = createSideButton(QStringLiteral("Preview"));
    strobeMenuButton = createSideButton(QStringLiteral("Strobe"));
    captureMenuButton = createSideButton(QStringLiteral("Capture"));
    logButton = createSideButton(QStringLiteral("Log"));
    logButton->setCheckable(true);
    mainLayout->addWidget(deviceMenuButton);
    mainLayout->addWidget(previewMenuButton);
    mainLayout->addWidget(strobeMenuButton);
    mainLayout->addWidget(captureMenuButton);
    mainLayout->addWidget(logButton);
    mainLayout->addStretch();

    QVBoxLayout *deviceLayout = createMenuPage(&deviceMenuPage);
    QPushButton *deviceBackButton = createSideButton(QStringLiteral("Back"));
    queryButton = createSideButton(QStringLiteral("Query"));
    deviceLayout->addWidget(deviceBackButton);
    deviceLayout->addWidget(pathEdit);
    deviceLayout->addWidget(saveRootEdit);
    deviceLayout->addWidget(queryButton);
    deviceLayout->addStretch();

    QVBoxLayout *previewLayout = createMenuPage(&previewMenuPage);
    QPushButton *previewBackButton = createSideButton(QStringLiteral("Back"));
    startButton = createSideButton(QStringLiteral("Start"));
    stopButton = createSideButton(QStringLiteral("Stop"));
    previewLayout->addWidget(previewBackButton);
    previewLayout->addWidget(modeCombo);
    previewLayout->addWidget(startButton);
    previewLayout->addWidget(stopButton);
    previewLayout->addStretch();

    QVBoxLayout *strobeLayout = createMenuPage(&strobeMenuPage);
    QPushButton *strobeBackButton = createSideButton(QStringLiteral("Back"));
    strobeOffButton = createSideButton(QStringLiteral("Off"));
    flashButton = createSideButton(QStringLiteral("Flash"));
    torchButton = createSideButton(QStringLiteral("Torch"));
    strobeOffButton->setCheckable(true);
    flashButton->setCheckable(true);
    torchButton->setCheckable(true);
    strobeLayout->addWidget(strobeBackButton);
    strobeLayout->addWidget(strobeOffButton);
    strobeLayout->addWidget(flashButton);
    strobeLayout->addWidget(torchButton);
    strobeLayout->addStretch();

    QVBoxLayout *captureLayout = createMenuPage(&captureMenuPage);
    QPushButton *captureBackButton = createSideButton(QStringLiteral("Back"));
    snapshotButton = createSideButton(QStringLiteral("Snapshot"));
    afButton = createSideButton(QStringLiteral("AF"));
    recordButton = createSideButton(QStringLiteral("Record"));
    captureLayout->addWidget(captureBackButton);
    captureLayout->addWidget(snapshotButton);
    captureLayout->addWidget(afButton);
    captureLayout->addWidget(recordButton);
    captureLayout->addStretch();

    connect(deviceMenuButton, &QPushButton::clicked, this, [this]() { menuStack->setCurrentWidget(deviceMenuPage); });
    connect(previewMenuButton, &QPushButton::clicked, this, [this]() { menuStack->setCurrentWidget(previewMenuPage); });
    connect(strobeMenuButton, &QPushButton::clicked, this, [this]() { menuStack->setCurrentWidget(strobeMenuPage); });
    connect(captureMenuButton, &QPushButton::clicked, this, [this]() { menuStack->setCurrentWidget(captureMenuPage); });
    connect(logButton, &QPushButton::clicked, this, [this]() { toggleLog(); });
    connect(deviceBackButton, &QPushButton::clicked, this, [this]() { showMainMenu(); });
    connect(previewBackButton, &QPushButton::clicked, this, [this]() { showMainMenu(); });
    connect(strobeBackButton, &QPushButton::clicked, this, [this]() { showMainMenu(); });
    connect(captureBackButton, &QPushButton::clicked, this, [this]() { showMainMenu(); });

    QHBoxLayout *bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(6);
    bodyLayout->addWidget(menuStack);
    bodyLayout->addWidget(previewWidget, 1);
    contentLayout()->insertWidget(1, bodyWidget, 1);
    showMainMenu();
}

void CameraTestWindow::showMainMenu()
{
    menuStack->setCurrentWidget(mainMenuPage);
}

void CameraTestWindow::setSelectedStrobeMode(StrobeMode mode)
{
    const StrobeMode previousMode = selectedStrobeMode;
    selectedStrobeMode = mode;

    if (mode == StrobeMode::Torch) {
        strobeStatusText = QStringLiteral("torch");
        if (camera.isStreaming() && camera.supportsStrobeMode())
            camera.setStrobeMode(StrobeMode::Torch);
    } else if (mode == StrobeMode::Flash) {
        strobeStatusText = QStringLiteral("flash armed");
        if (previousMode == StrobeMode::Torch && camera.isStreaming() && camera.supportsStrobeMode())
            camera.setStrobeMode(StrobeMode::None);
        appendLog(QStringLiteral("flash armed for next snapshot"));
    } else {
        strobeStatusText = QStringLiteral("off");
        finishFlashSnapshotStrobe();
        if (camera.isStreaming() && camera.supportsStrobeMode())
            camera.setStrobeMode(StrobeMode::None);
    }

    strobeLabel->setText(strobeStatusText);
    updateStrobeButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::handleStrobeStatus(const QString &status)
{
    strobeStatusText = status;
    strobeLabel->setText(strobeStatusText);

    if (flashSnapshotActive && status == QStringLiteral("flash triggered")) {
        QString error;
        if (camera.requestSnapshot(flashSnapshotPath, 3) != CameraDevice::ActionResult::Ok) {
            snapshotLabel->setText(QStringLiteral("failed: flash snapshot request rejected"));
            appendCaptureEvent(QStringLiteral("flash_snapshot_failed"), flashSnapshotRelativePath, QStringLiteral("request rejected"));
            if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_failed"), flashSnapshotRelativePath, &error) && !error.isEmpty())
                appendLog(QStringLiteral("latest status write failed: %1").arg(error));
            finishFlashSnapshotStrobe();
        } else {
            snapshotLabel->setText(QStringLiteral("flash: waiting %1").arg(flashSnapshotRelativePath));
            appendCaptureEvent(QStringLiteral("flash_snapshot_triggered"), flashSnapshotRelativePath, QStringLiteral("waiting third frame"));
            if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_flash_waiting"), flashSnapshotRelativePath, &error) && !error.isEmpty())
                appendLog(QStringLiteral("latest status write failed: %1").arg(error));
        }
    }

    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::takeSnapshot()
{
    if (!camera.isStreaming()) {
        appendLog(QStringLiteral("snapshot requires active capture streaming"));
        snapshotLabel->setText(QStringLiteral("failed: capture inactive"));
        updatePreviewOverlay();
        return;
    }

    if (!ensureCaptureSession())
        return;

    const CameraTestPathResult path = storage.makeCameraSnapshotPath(captureSession);
    if (!path.ok) {
        snapshotLabel->setText(QStringLiteral("failed: %1").arg(path.error));
        appendLog(QStringLiteral("snapshot path failed: %1").arg(path.error));
        return;
    }

    const QString relativePath = captureRelativePath(path.path);
    if (selectedStrobeMode == StrobeMode::Flash) {
        if (beginFlashSnapshot(path.path, relativePath))
            updatePreviewOverlay();
        return;
    }

    QString error;
    if (camera.requestSnapshot(path.path) != CameraDevice::ActionResult::Ok) {
        snapshotLabel->setText(QStringLiteral("failed: request rejected"));
        appendLog(QStringLiteral("snapshot request rejected"));
        return;
    }

    appendCaptureEvent(QStringLiteral("snapshot_requested"), relativePath, QStringLiteral("requested"));
    if (!storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_saving"), relativePath, &error) && !error.isEmpty())
        appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    snapshotLabel->setText(QStringLiteral("saving: %1").arg(relativePath));
    updatePreviewOverlay();
}

void CameraTestWindow::startRecording()
{
    if (!camera.isStreaming()) {
        appendLog(QStringLiteral("recording requires active capture streaming"));
        recordingLabel->setText(QStringLiteral("failed: capture inactive"));
        updatePreviewOverlay();
        return;
    }

    if (recordingUiActive) {
        appendLog(QStringLiteral("recording already active"));
        return;
    }

    if (!ensureCaptureSession())
        return;

    const CameraTestPathResult path = storage.makeCameraRecordingPath(captureSession);
    if (!path.ok) {
        recordingLabel->setText(QStringLiteral("failed: %1").arg(path.error));
        appendLog(QStringLiteral("recording path failed: %1").arg(path.error));
        return;
    }

    const QString relativePath = captureRelativePath(path.path);
    QString error;
    if (camera.startRecording(path.path) != CameraDevice::ActionResult::Ok) {
        recordingLabel->setText(QStringLiteral("failed: request rejected"));
        appendLog(QStringLiteral("recording request rejected"));
        return;
    }

    recordingUiActive = true;
    appendCaptureEvent(QStringLiteral("recording_requested"), relativePath, QStringLiteral("requested"));
    if (!storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("recording_saving"), relativePath, &error) && !error.isEmpty())
        appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    recordingLabel->setText(QStringLiteral("recording: %1").arg(relativePath));
    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::toggleLog()
{
    setLogVisible(!isLogVisible());
    logButton->setChecked(isLogVisible());
    logButton->setText(isLogVisible() ? QStringLiteral("Hide Log") : QStringLiteral("Log"));
}


CameraTestWindow::~CameraTestWindow()
{
    camera.stopPreview();
}

void CameraTestWindow::queryCaps()
{
    const QString path = pathEdit->text().trimmed();
    if (!camera.openDevice(path)) {
        const CameraCaps caps = camera.capabilities();
        setStatus(QStringLiteral("Unavailable"));
        driverLabel->setText(caps.error.isEmpty() ? camera.lastError() : caps.error);
        cardLabel->setText(QStringLiteral("--"));
        busLabel->setText(QStringLiteral("--"));
        formatLabel->setText(QStringLiteral("--"));
        controlLabel->setText(QStringLiteral("--"));
        visibleModes.clear();
        modeCombo->clear();
        modeCombo->addItem(QStringLiteral("No RGB565/RGBP mode"));
        updateButtons();
        updatePreviewOverlay();
        return;
    }

    const CameraCaps caps = camera.capabilities();
    driverLabel->setText(caps.driver);
    cardLabel->setText(caps.card);
    busLabel->setText(caps.busInfo);
    formatLabel->setText(modeListSummary(caps));
    controlLabel->setText(controlSummary(caps));
    errorLabel->setText(QStringLiteral("--"));

    updateModeList(caps);
    if ((selectedStrobeMode == StrobeMode::Flash && !camera.supportsFlashPulse()) ||
        (selectedStrobeMode == StrobeMode::Torch && !camera.supportsStrobeMode())) {
        selectedStrobeMode = StrobeMode::None;
        strobeStatusText = QStringLiteral("off");
        strobeLabel->setText(strobeStatusText);
    }
    setStatus(QStringLiteral("Ready"));
    appendLog(QStringLiteral("VIDIOC_QUERYCAP ok, %1 RGB565 mode(s), %2 control(s)")
                  .arg(visibleModes.size())
                  .arg(caps.controls.size()));
    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::startPreview()
{
    if (camera.isStreaming()) {
        camera.setPreviewDisplayEnabled(true);
        appendLog(QStringLiteral("preview display resumed"));
        updateButtons();
        updatePreviewOverlay();
        return;
    }

    const QString path = pathEdit->text().trimmed();
    if (camera.capabilities().devicePath != path || !camera.capabilities().available) {
        queryCaps();
        if (!camera.capabilities().available)
            return;
    }

    const CameraMode mode = selectedMode();
    if (!isRgb565Mode(mode)) {
        appendLog(QStringLiteral("no RGB565/RGBP mode selected"));
        return;
    }

    if (camera.startPreview(mode)) {
        setStatus(QStringLiteral("Starting"));
        frameLabel->setText(QStringLiteral("0  0.0 fps"));
    }
    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::stopPreview()
{
    if (camera.isStreaming()) {
        camera.setPreviewDisplayEnabled(false);
        previewWidget->clearFrame();
        appendLog(QStringLiteral("preview display paused; capture stream remains active"));
    }
    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::updateModeList(const CameraCaps &caps)
{
    QSignalBlocker blocker(modeCombo);
    modeCombo->clear();
    visibleModes.clear();

    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Mode(mode))
            visibleModes.append(mode);
    }

    if (visibleModes.isEmpty()) {
        modeCombo->addItem(QStringLiteral("No RGB565/RGBP mode"));
        return;
    }

    const CameraMode preferred = CameraDevice::preferredMode(caps);
    int preferredIndex = 0;
    for (int i = 0; i < visibleModes.size(); ++i) {
        const CameraMode mode = visibleModes.at(i);
        modeCombo->addItem(mode.label());
        if (sameUiMode(mode, preferred))
            preferredIndex = i;
    }
    modeCombo->setCurrentIndex(preferredIndex);
    currentActiveMode = visibleModes.at(preferredIndex);
    activeModeLabel->setText(currentActiveMode.label());
    updatePreviewOverlay();
}

void CameraTestWindow::updateButtons()
{
    const bool running = camera.isStreaming();
    const bool previewDisplay = camera.isPreviewDisplayEnabled();
    const bool haveMode = !visibleModes.isEmpty();
    const bool capsAvailable = camera.capabilities().available;
    const bool haveStrobe = camera.supportsStrobeMode();
    const bool haveFlash = camera.supportsFlashPulse();

    pathEdit->setEnabled(!running);
    saveRootEdit->setEnabled(!running && !recordingUiActive);
    queryButton->setEnabled(!running);
    modeCombo->setEnabled(haveMode);
    startButton->setText(running ? QStringLiteral("Show") : QStringLiteral("Start"));
    stopButton->setText(QStringLiteral("Hide"));
    startButton->setEnabled(haveMode && (!running || !previewDisplay));
    stopButton->setEnabled(running && previewDisplay);
    afButton->setEnabled(running && camera.supportsAutoFocus());
    strobeOffButton->setEnabled(capsAvailable || selectedStrobeMode != StrobeMode::None);
    torchButton->setEnabled(running && haveStrobe);
    flashButton->setEnabled(capsAvailable && haveFlash);
    snapshotButton->setEnabled(running);
    recordButton->setEnabled(running && !recordingUiActive);

    deviceMenuButton->setEnabled(true);
    previewMenuButton->setEnabled(true);
    strobeMenuButton->setEnabled(capsAvailable && (haveStrobe || haveFlash));
    captureMenuButton->setEnabled(true);
    logButton->setEnabled(true);

    updateStrobeButtons();
}

void CameraTestWindow::updateStrobeButtons()
{
    if (!strobeOffButton || !flashButton || !torchButton)
        return;

    strobeOffButton->setChecked(selectedStrobeMode == StrobeMode::None);
    flashButton->setChecked(selectedStrobeMode == StrobeMode::Flash);
    torchButton->setChecked(selectedStrobeMode == StrobeMode::Torch);
}

void CameraTestWindow::updatePreviewOverlay()
{
    QStringList parts;
    const QString state = stateLabel->text();
    const QString active = activeModeLabel->text();
    const QString frames = frameLabel->text();
    const QString error = errorLabel->text();

    if (!state.isEmpty())
        parts << state;
    if (!active.isEmpty() && active != QStringLiteral("--"))
        parts << active;
    if (!frames.isEmpty())
        parts << frames;
    if (camera.isStreaming() && !camera.isPreviewDisplayEnabled())
        parts << QStringLiteral("preview:off");
    parts << QStringLiteral("strobe:%1").arg(strobeStatusText);
    if (!snapshotLabel->text().isEmpty() && snapshotLabel->text() != QStringLiteral("planned"))
        parts << QStringLiteral("snap:%1").arg(snapshotLabel->text());
    if (!recordingLabel->text().isEmpty() && recordingLabel->text() != QStringLiteral("planned"))
        parts << QStringLiteral("rec:%1").arg(recordingLabel->text());
    if (!error.isEmpty() && error != QStringLiteral("--"))
        parts << QStringLiteral("err:%1").arg(error);

    previewWidget->setOverlayText(parts.join(QStringLiteral("  |  ")));
}


void CameraTestWindow::handlePreviewTouch(const QPoint &pos, const QSize &imageSize, const QRect &imageRect)
{
    if (!camera.isStreaming())
        return;
    if (!camera.supportsTouchFocus()) {
        appendLog(QStringLiteral("touch AF unavailable: missing af_touch_x/y or af_zone_mode control"));
        return;
    }

    const int frameWidth = currentActiveMode.width > 0 ? currentActiveMode.width : imageSize.width();
    const int frameHeight = currentActiveMode.height > 0 ? currentActiveMode.height : imageSize.height();
    if (frameWidth <= 0 || frameHeight <= 0 || imageRect.width() <= 0 || imageRect.height() <= 0)
        return;

    const int x = qBound(0, ((pos.x() - imageRect.left()) * frameWidth) / imageRect.width(), frameWidth - 1);
    const int y = qBound(0, ((pos.y() - imageRect.top()) * frameHeight) / imageRect.height(), frameHeight - 1);
    if (camera.focusTouch(x, y)) {
        afLabel->setText(QStringLiteral("TOUCH queued (%1,%2)").arg(x).arg(y));
        updatePreviewOverlay();
    }
}

QString CameraTestWindow::captureRelativePath(const QString &absolutePath) const
{
    if (!captureSession.ok || absolutePath.isEmpty())
        return absolutePath;
    return QDir(captureSession.sessionPath).relativeFilePath(absolutePath);
}

QString CameraTestWindow::savedPathFromStatus(const QString &status) const
{
    return statusPathFromText(status);
}

bool CameraTestWindow::ensureCaptureSession()
{
    const QString root = saveRootEdit->text().trimmed().isEmpty()
        ? QStringLiteral("/smart-monitor")
        : saveRootEdit->text().trimmed();
    const QString device = pathEdit->text().trimmed();
    CameraMode mode = currentActiveMode;
    if (!isRgb565Mode(mode))
        mode = camera.activeMode();
    if (!isRgb565Mode(mode))
        mode = selectedMode();

    if (captureSession.ok && captureSession.rootPath == root && captureSession.devicePath == device &&
        sameUiMode(captureSession.mode, mode)) {
        return true;
    }

    captureSession = storage.openCameraTestSession(root, device, mode);
    captureSequence = 0;
    if (!captureSession.ok) {
        appendLog(QStringLiteral("capture session failed: %1").arg(captureSession.error));
        snapshotLabel->setText(QStringLiteral("failed: %1").arg(captureSession.error));
        recordingLabel->setText(QStringLiteral("failed: %1").arg(captureSession.error));
        updatePreviewOverlay();
        return false;
    }

    appendLog(QStringLiteral("capture session opened %1").arg(captureSession.sessionPath));
    return true;
}

void CameraTestWindow::appendCaptureEvent(const QString &type, const QString &relativePath, const QString &status)
{
    if (!captureSession.ok)
        return;

    QString error;
    if (!storage.appendCameraEvent(captureSession, type, relativePath, status, &error))
        appendLog(QStringLiteral("capture event write failed: %1").arg(error));
}

void CameraTestWindow::appendCaptureIndex(const QString &relativePath, const QString &kind, const QString &status)
{
    if (!captureSession.ok)
        return;

    QString error;
    if (!storage.appendCameraIndex(captureSession, ++captureSequence, relativePath, kind, status, &error))
        appendLog(QStringLiteral("capture index write failed: %1").arg(error));
}


bool CameraTestWindow::beginFlashSnapshot(const QString &path, const QString &relativePath)
{
    if (!camera.supportsFlashPulse()) {
        snapshotLabel->setText(QStringLiteral("failed: flash controls unavailable"));
        appendLog(QStringLiteral("flash snapshot unavailable: missing flash controls"));
        return false;
    }
    if (flashSnapshotActive) {
        snapshotLabel->setText(QStringLiteral("failed: flash snapshot already active"));
        appendLog(QStringLiteral("flash snapshot already active"));
        return false;
    }

    flashSnapshotActive = true;
    flashSnapshotPath = path;
    flashSnapshotRelativePath = relativePath;
    snapshotLabel->setText(QStringLiteral("flash: arming %1").arg(relativePath));
    appendCaptureEvent(QStringLiteral("flash_snapshot_requested"), relativePath, QStringLiteral("arming"));

    QString error;
    if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_flash_arming"), relativePath, &error) && !error.isEmpty())
        appendLog(QStringLiteral("latest status write failed: %1").arg(error));

    if (!camera.setStrobeMode(StrobeMode::Flash) || !camera.triggerFlash()) {
        snapshotLabel->setText(QStringLiteral("failed: flash trigger rejected"));
        appendCaptureEvent(QStringLiteral("flash_snapshot_failed"), relativePath, QStringLiteral("trigger rejected"));
        finishFlashSnapshotStrobe();
        return false;
    }

    QTimer::singleShot(2000, this, [this, path]() { handleFlashSnapshotTimeout(path); });
    return true;
}

void CameraTestWindow::handleFlashSnapshotTimeout(const QString &path)
{
    if (!flashSnapshotActive || flashSnapshotPath != path)
        return;

    const QString relativePath = flashSnapshotRelativePath;
    QString error;
    snapshotLabel->setText(QStringLiteral("failed: flash snapshot timeout"));
    appendCaptureEvent(QStringLiteral("flash_snapshot_failed"), relativePath, QStringLiteral("timeout"));
    if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_failed"), relativePath, &error) && !error.isEmpty())
        appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    finishFlashSnapshotStrobe();
}

void CameraTestWindow::finishFlashSnapshotStrobe()
{
    const bool wasActive = flashSnapshotActive;
    flashSnapshotActive = false;
    flashSnapshotPath.clear();
    flashSnapshotRelativePath.clear();

    if (wasActive && camera.isStreaming() && camera.supportsFlashPulse())
        camera.stopFlash();
    if (selectedStrobeMode == StrobeMode::Flash)
        selectedStrobeMode = StrobeMode::None;
    if (wasActive)
        appendLog(QStringLiteral("flash snapshot strobe stop requested"));
    updateStrobeButtons();
}

bool CameraTestWindow::isFlashSnapshotPath(const QString &path) const
{
    return flashSnapshotActive && !flashSnapshotPath.isEmpty() && path == flashSnapshotPath;
}

void CameraTestWindow::handleSnapshotStatus(const QString &status)
{
    QString path;
    QString relativePath;
    QString error;

    if (status.startsWith(QStringLiteral("waiting:"))) {
        path = savedPathFromStatus(status);
        relativePath = captureRelativePath(path);
        snapshotLabel->setText(QStringLiteral("waiting: %1").arg(relativePath));
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_waiting"), relativePath, &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    } else if (status.startsWith(QStringLiteral("saving:"))) {
        path = savedPathFromStatus(status);
        const bool flashPath = isFlashSnapshotPath(path);
        relativePath = captureRelativePath(path);
        snapshotLabel->setText(QStringLiteral("saving: %1").arg(relativePath));
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_saving"), relativePath, &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
        if (flashPath)
            finishFlashSnapshotStrobe();
    } else if (status.startsWith(QStringLiteral("saved:"))) {
        path = savedPathFromStatus(status);
        relativePath = captureRelativePath(path);
        snapshotLabel->setText(QStringLiteral("saved: %1").arg(relativePath));
        appendCaptureEvent(QStringLiteral("snapshot_saved"), relativePath, status);
        appendCaptureIndex(relativePath, QStringLiteral("snapshot"), QStringLiteral("saved"));
        if (captureSession.ok && !storage.updateLatestImage(captureSession, path, QStringLiteral("snapshot_saved"), &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest image update failed: %1").arg(error));
    } else if (status.startsWith(QStringLiteral("failed:"))) {
        snapshotLabel->setText(status);
        appendCaptureEvent(QStringLiteral("snapshot_failed"), flashSnapshotRelativePath, status);
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("snapshot_failed"), flashSnapshotRelativePath, &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
        if (flashSnapshotActive)
            finishFlashSnapshotStrobe();
    } else {
        snapshotLabel->setText(status);
    }

    updateButtons();
    updatePreviewOverlay();
}

void CameraTestWindow::handleRecordingStatus(const QString &status)
{
    QString path;
    QString relativePath;
    QString error;

    if (status.startsWith(QStringLiteral("recording:"))) {
        recordingUiActive = true;
        path = savedPathFromStatus(status);
        relativePath = captureRelativePath(path);
        recordingLabel->setText(QStringLiteral("recording: %1").arg(relativePath));
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("recording_saving"), relativePath, &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    } else if (status.startsWith(QStringLiteral("saved:"))) {
        recordingUiActive = false;
        path = savedPathFromStatus(status);
        relativePath = captureRelativePath(path);
        recordingLabel->setText(QStringLiteral("saved: %1").arg(relativePath));
        appendCaptureEvent(QStringLiteral("recording_saved"), relativePath, status);
        appendCaptureIndex(relativePath, QStringLiteral("recording"), QStringLiteral("saved"));
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("recording_saved"), relativePath, &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    } else if (status.startsWith(QStringLiteral("failed:"))) {
        recordingUiActive = false;
        recordingLabel->setText(status);
        appendCaptureEvent(QStringLiteral("recording_failed"), QString(), status);
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("recording_failed"), QString(), &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    } else if (status.startsWith(QStringLiteral("stopped:"))) {
        recordingUiActive = false;
        recordingLabel->setText(status);
        appendCaptureEvent(QStringLiteral("recording_stopped"), QString(), status);
        if (captureSession.ok && !storage.writeLatestStatus(captureSession.rootPath, QStringLiteral("recording_stopped"), QString(), &error) && !error.isEmpty())
            appendLog(QStringLiteral("latest status write failed: %1").arg(error));
    } else {
        recordingLabel->setText(status);
    }

    updateButtons();
    updatePreviewOverlay();
}

CameraMode CameraTestWindow::selectedMode() const
{
    const int index = modeCombo->currentIndex();
    if (index < 0 || index >= visibleModes.size())
        return CameraMode();
    return visibleModes.at(index);
}

QString CameraTestWindow::modeListSummary(const CameraCaps &caps) const
{
    QStringList formats;
    for (const CameraFormat &format : caps.formats)
        formats << QStringLiteral("%1 %2").arg(format.fourcc, format.description);

    QStringList modes;
    for (const CameraMode &mode : caps.modes) {
        if (isRgb565Mode(mode))
            modes << mode.label();
        if (modes.size() >= 6)
            break;
    }

    const QString formatText = formats.isEmpty() ? QStringLiteral("--") : formats.join(QStringLiteral(", "));
    const QString modeText = modes.isEmpty() ? QStringLiteral("no RGB565") : modes.join(QStringLiteral(", "));
    return QStringLiteral("%1 | modes: %2").arg(formatText, modeText);
}

QString CameraTestWindow::controlSummary(const CameraCaps &caps) const
{
    QStringList interesting;
    for (const CameraControl &control : caps.controls) {
        const QString name = control.name.toLower();
        if (name.contains(QStringLiteral("flash")) || name.contains(QStringLiteral("strobe")) ||
            name.contains(QStringLiteral("focus")) || name.contains(QStringLiteral("af_"))) {
            interesting << control.name;
        }
    }

    if (interesting.isEmpty())
        return QStringLiteral("%1 total").arg(caps.controls.size());

    return QStringLiteral("%1 total: %2").arg(caps.controls.size()).arg(interesting.join(QStringLiteral(", ")));
}

} // namespace imx6sm
