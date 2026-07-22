#include "smart_monitor_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>

#include <functional>

namespace imx6sm {

class PreviewPane : public QWidget {
public:
    explicit PreviewPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(300, 210);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setFrame(const QImage &frame)
    {
        image = frame;
        update();
    }

    void setOverlay(const QString &text)
    {
        overlay = text;
        update();
    }

    std::function<void(int, int)> focusHandler;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(QStringLiteral("#080909")));

        if (!image.isNull()) {
            const QRect target = imageRect();
            painter.drawImage(target, image);
        } else {
            painter.setPen(QColor(QStringLiteral("#7f8b8c")));
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Camera idle"));
        }

        if (!overlay.isEmpty()) {
            painter.fillRect(QRect(0, height() - 28, width(), 28), QColor(0, 0, 0, 150));
            painter.setPen(Qt::white);
            painter.drawText(QRect(8, height() - 28, width() - 16, 28),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             overlay);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (image.isNull() || !focusHandler) {
            QWidget::mousePressEvent(event);
            return;
        }

        const QRect target = imageRect();
        if (!target.contains(event->pos())) {
            QWidget::mousePressEvent(event);
            return;
        }

        const int x = qBound(0, ((event->pos().x() - target.left()) * image.width()) / target.width(), image.width() - 1);
        const int y = qBound(0, ((event->pos().y() - target.top()) * image.height()) / target.height(), image.height() - 1);
        focusHandler(x, y);
    }

private:
    QRect imageRect() const
    {
        if (image.isNull())
            return QRect();
        QSize scaled = image.size();
        scaled.scale(size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - scaled.width()) / 2,
                            (height() - scaled.height()) / 2),
                     scaled);
    }

    QImage image;
    QString overlay;
};

static QLabel *makeValueLabel(QWidget *parent)
{
    QLabel *label = new QLabel(QStringLiteral("--"), parent);
    label->setObjectName(QStringLiteral("valueLabel"));
    label->setWordWrap(true);
    return label;
}

static void addStatusRow(QGridLayout *grid, int row, const QString &name, QLabel *value, QWidget *parent)
{
    QLabel *label = new QLabel(name, parent);
    label->setObjectName(QStringLiteral("fieldLabel"));
    grid->addWidget(label, row, 0, Qt::AlignTop | Qt::AlignLeft);
    grid->addWidget(value, row, 1, Qt::AlignTop | Qt::AlignLeft);
}

SmartMonitorPage::SmartMonitorPage(QWidget *parent)
    : QWidget(parent)
    , controller(new MonitorController(this))
    , previewPane(new PreviewPane(this))
    , startStopButton(new QPushButton(QStringLiteral("Start"), this))
    , afButton(new QPushButton(QStringLiteral("AF"), this))
    , ld2410Button(new QPushButton(QStringLiteral("LD2410"), this))
    , toolsButton(new QPushButton(QStringLiteral("Tools"), this))
    , modeCombo(new QComboBox(this))
    , strobeCombo(new QComboBox(this))
    , monitoringLabel(makeValueLabel(this))
    , presenceLabel(makeValueLabel(this))
    , luxLabel(makeValueLabel(this))
    , occlusionLabel(makeValueLabel(this))
    , cameraLabel(makeValueLabel(this))
    , modeLabel(makeValueLabel(this))
    , frameLabel(makeValueLabel(this))
    , afLabel(makeValueLabel(this))
    , torchLabel(makeValueLabel(this))
    , storageLabel(makeValueLabel(this))
    , recordingLabel(makeValueLabel(this))
    , errorLabel(makeValueLabel(this))
    , logView(new QTextEdit(this))
{
    setMinimumSize(480, 272);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#151719;color:#eef1ef;font-family:DejaVu Sans;}"
        "QLabel#titleLabel{font-size:20px;font-weight:700;color:#ffffff;}"
        "QLabel#fieldLabel{font-size:12px;color:#9aa6a3;}"
        "QLabel#valueLabel{font-size:13px;color:#f4f6f4;}"
        "QPushButton{font-size:13px;min-height:28px;padding:5px 9px;border:1px solid #3c4a48;border-radius:3px;background:#1f5d6b;color:#ffffff;}"
        "QPushButton:pressed{background:#2a7584;}"
        "QPushButton:disabled{background:#293230;color:#77817e;}"
        "QComboBox{font-size:12px;min-height:28px;padding:4px;border:1px solid #3c4a48;background:#202725;color:#ffffff;}"
        "QTextEdit{background:#0d0f0f;border:1px solid #2b3432;color:#b9c5c1;font-size:11px;}"));

    afButton->setEnabled(false);
    modeCombo->setMinimumWidth(150);
    strobeCombo->addItem(QStringLiteral("Auto"));
    strobeCombo->addItem(QStringLiteral("Off"));
    strobeCombo->addItem(QStringLiteral("Torch"));

    QLabel *title = new QLabel(QStringLiteral("Smart Monitor v1"), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    QHBoxLayout *top = new QHBoxLayout;
    top->setContentsMargins(8, 6, 8, 2);
    top->setSpacing(6);
    top->addWidget(title);
    top->addWidget(modeCombo, 0);
    top->addWidget(strobeCombo, 0);
    top->addStretch();
    top->addWidget(startStopButton);
    top->addWidget(afButton);
    top->addWidget(ld2410Button);
    top->addWidget(toolsButton);

    QGridLayout *statusGrid = new QGridLayout;
    statusGrid->setContentsMargins(8, 4, 8, 4);
    statusGrid->setHorizontalSpacing(8);
    statusGrid->setVerticalSpacing(4);
    addStatusRow(statusGrid, 0, QStringLiteral("Monitor"), monitoringLabel, this);
    addStatusRow(statusGrid, 1, QStringLiteral("Presence"), presenceLabel, this);
    addStatusRow(statusGrid, 2, QStringLiteral("Lux"), luxLabel, this);
    addStatusRow(statusGrid, 3, QStringLiteral("Occlusion"), occlusionLabel, this);
    addStatusRow(statusGrid, 4, QStringLiteral("Camera"), cameraLabel, this);
    addStatusRow(statusGrid, 5, QStringLiteral("Mode"), modeLabel, this);
    addStatusRow(statusGrid, 6, QStringLiteral("Frames"), frameLabel, this);
    addStatusRow(statusGrid, 7, QStringLiteral("AF"), afLabel, this);
    addStatusRow(statusGrid, 8, QStringLiteral("Strobe"), torchLabel, this);
    addStatusRow(statusGrid, 9, QStringLiteral("Storage"), storageLabel, this);
    addStatusRow(statusGrid, 10, QStringLiteral("Recording"), recordingLabel, this);
    addStatusRow(statusGrid, 11, QStringLiteral("Error"), errorLabel, this);
    statusGrid->setColumnStretch(1, 1);

    QWidget *statusPane = new QWidget(this);
    statusPane->setLayout(statusGrid);
    statusPane->setMinimumWidth(220);
    statusPane->setMaximumWidth(300);

    QHBoxLayout *body = new QHBoxLayout;
    body->setContentsMargins(8, 2, 8, 2);
    body->setSpacing(8);
    body->addWidget(previewPane, 1);
    body->addWidget(statusPane, 0);

    logView->setReadOnly(true);
    logView->setMaximumHeight(78);
    logView->setMinimumHeight(54);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addLayout(body, 1);
    layout->addWidget(logView);

    connect(startStopButton, &QPushButton::clicked, this, [this]() {
        if (controller->snapshot().monitoringEnabled)
            controller->stopMonitoring();
        else
            controller->startMonitoring();
    });
    connect(afButton, &QPushButton::clicked, controller, &MonitorController::requestAutoFocus);
    connect(ld2410Button, &QPushButton::clicked, this, &SmartMonitorPage::ld2410ConfigRequested);
    connect(toolsButton, &QPushButton::clicked, this, &SmartMonitorPage::toolsRequested);
    connect(modeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            controller, &MonitorController::setPreviewModeIndex);
    connect(strobeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            controller, &MonitorController::setStrobePolicyIndex);
    connect(controller, &MonitorController::snapshotChanged, this, &SmartMonitorPage::refreshSnapshot);
    connect(controller, &MonitorController::previewFrameChanged, previewPane, &PreviewPane::setFrame);
    connect(controller, &MonitorController::modesChanged, this, &SmartMonitorPage::refreshModes);
    connect(controller, &MonitorController::logMessage, this, &SmartMonitorPage::appendLogLine);

    previewPane->focusHandler = [this](int x, int y) { controller->focusAtFramePoint(x, y); };

    refreshModes();
    refreshSnapshot(controller->snapshot());
}

void SmartMonitorPage::refreshSnapshot(const MonitorSnapshot &snapshot)
{
    const bool recordingNow = snapshot.recordingWanted ||
        snapshot.recordingStatus.startsWith(QStringLiteral("recording:")) ||
        snapshot.recordingStatus.startsWith(QStringLiteral("stopping:"));
    startStopButton->setText(snapshot.monitoringEnabled ? QStringLiteral("Stop") : QStringLiteral("Start"));
    afButton->setEnabled(snapshot.camera == CameraState::Streaming);
    modeCombo->setEnabled(modeCombo->count() > 0 && !recordingNow);

    monitoringLabel->setText(snapshot.monitoringEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    const QString source = snapshot.presenceSource.isEmpty() ? QStringLiteral("--") : snapshot.presenceSource;
    presenceLabel->setText(QStringLiteral("%1  %2").arg(toString(snapshot.presence), source));
    luxLabel->setText(QStringLiteral("%1 lx").arg(snapshot.lux, 0, 'f', 1));
    const QString occlusionText = QStringLiteral("%1  ps:%2 lux:%3 ir:%4")
        .arg(snapshot.occlusionAlarm ? QStringLiteral("ALARM") : (snapshot.occlusionNear ? QStringLiteral("near") : QStringLiteral("clear")))
        .arg(snapshot.proximityRaw)
        .arg(snapshot.lux, 0, 'f', 1)
        .arg(snapshot.irRaw);
    occlusionLabel->setText(occlusionText);
    occlusionLabel->setStyleSheet(snapshot.occlusionAlarm
        ? QStringLiteral("QLabel#valueLabel{color:#ff756b;font-weight:700;}")
        : QString());

    QString cameraText = toString(snapshot.camera);
    if (snapshot.cameraWanted)
        cameraText += QStringLiteral(" wanted");
    cameraLabel->setText(cameraText);
    modeLabel->setText(snapshot.activeMode.isEmpty() ? QStringLiteral("--") : snapshot.activeMode);
    frameLabel->setText(QString::number(snapshot.frameCount));
    afLabel->setText(snapshot.afStatus.isEmpty() ? QStringLiteral("--") : snapshot.afStatus);

    const int strobeIndex = strobeCombo->currentIndex();
    if (strobeIndex == 0)
        torchLabel->setText(snapshot.torchWanted ? QStringLiteral("auto torch") : QStringLiteral("auto off"));
    else if (strobeIndex == 2)
        torchLabel->setText(QStringLiteral("forced torch"));
    else
        torchLabel->setText(QStringLiteral("forced off"));

    storageLabel->setText(toString(snapshot.storage));
    recordingLabel->setText(snapshot.recordingStatus.isEmpty()
        ? (snapshot.recordingWanted ? QStringLiteral("wanted") : QStringLiteral("--"))
        : snapshot.recordingStatus);

    QStringList errors;
    if (!snapshot.cameraError.isEmpty())
        errors << snapshot.cameraError;
    if (!snapshot.storageError.isEmpty())
        errors << snapshot.storageError;
    errorLabel->setText(errors.isEmpty() ? QStringLiteral("--") : errors.join(QStringLiteral(" | ")));

    QStringList overlay;
    overlay << toString(snapshot.presence) << toString(snapshot.camera);
    if (!snapshot.activeMode.isEmpty())
        overlay << snapshot.activeMode;
    if (recordingNow)
        overlay << QStringLiteral("REC");
    if (snapshot.occlusionAlarm)
        overlay << QStringLiteral("OCCLUSION");
    overlay << QStringLiteral("frames:%1").arg(snapshot.frameCount);
    previewPane->setOverlay(overlay.join(QStringLiteral("  ")));
}

void SmartMonitorPage::refreshModes()
{
    QSignalBlocker blocker(modeCombo);
    modeCombo->clear();
    const QList<CameraMode> modes = controller->previewModes();
    if (modes.isEmpty()) {
        modeCombo->addItem(QStringLiteral("No RGB565 mode"));
        modeCombo->setEnabled(false);
        return;
    }

    for (const CameraMode &mode : modes)
        modeCombo->addItem(mode.label());
    modeCombo->setCurrentIndex(controller->activeModeIndex());
    const MonitorSnapshot snapshot = controller->snapshot();
    const bool recordingNow = snapshot.recordingWanted ||
        snapshot.recordingStatus.startsWith(QStringLiteral("recording:")) ||
        snapshot.recordingStatus.startsWith(QStringLiteral("stopping:"));
    modeCombo->setEnabled(!recordingNow);
}

void SmartMonitorPage::appendLogLine(const QString &line)
{
    if (line.isEmpty())
        return;
    logView->append(QStringLiteral("%1  %2")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

} // namespace imx6sm
