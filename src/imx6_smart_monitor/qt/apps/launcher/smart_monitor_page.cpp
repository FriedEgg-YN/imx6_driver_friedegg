#include "smart_monitor_page.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>
#include <QVariant>

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
    , snapshotButton(new QPushButton(QStringLiteral("Snapshot"), this))
    , torchButton(new QPushButton(QStringLiteral("Torch"), this))
    , afButton(new QPushButton(QStringLiteral("AF"), this))
    , playbackButton(new QPushButton(QStringLiteral("Playback"), this))
    , toolsButton(new QPushButton(QStringLiteral("Tools"), this))
    , modeCombo(new QComboBox(this))
    , monitoringLabel(makeValueLabel(this))
    , presenceLabel(makeValueLabel(this))
    , luxLabel(makeValueLabel(this))
    , cameraLabel(makeValueLabel(this))
    , modeLabel(makeValueLabel(this))
    , frameLabel(makeValueLabel(this))
    , afLabel(makeValueLabel(this))
    , torchLabel(makeValueLabel(this))
    , storageLabel(makeValueLabel(this))
    , sessionLabel(makeValueLabel(this))
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
        "QPushButton:checked{background:#8a6428;border-color:#c99a45;}"
        "QPushButton:disabled{background:#293230;color:#77817e;}"
        "QComboBox{font-size:12px;min-height:28px;padding:4px;border:1px solid #3c4a48;background:#202725;color:#ffffff;}"
        "QTextEdit{background:#0d0f0f;border:1px solid #2b3432;color:#b9c5c1;font-size:11px;}"
        "QListWidget{background:#0d0f0f;border:1px solid #2b3432;color:#e8eeeb;font-size:12px;}"));

    torchButton->setCheckable(true);
    snapshotButton->setEnabled(false);
    afButton->setEnabled(false);
    modeCombo->setMinimumWidth(150);

    QLabel *title = new QLabel(QStringLiteral("Smart Monitor v1"), this);
    title->setObjectName(QStringLiteral("titleLabel"));

    QHBoxLayout *top = new QHBoxLayout;
    top->setContentsMargins(8, 6, 8, 2);
    top->setSpacing(6);
    top->addWidget(title);
    top->addWidget(modeCombo, 0);
    top->addStretch();
    top->addWidget(startStopButton);
    top->addWidget(snapshotButton);
    top->addWidget(torchButton);
    top->addWidget(afButton);
    top->addWidget(playbackButton);
    top->addWidget(toolsButton);

    QGridLayout *statusGrid = new QGridLayout;
    statusGrid->setContentsMargins(8, 4, 8, 4);
    statusGrid->setHorizontalSpacing(8);
    statusGrid->setVerticalSpacing(4);
    addStatusRow(statusGrid, 0, QStringLiteral("Monitor"), monitoringLabel, this);
    addStatusRow(statusGrid, 1, QStringLiteral("Presence"), presenceLabel, this);
    addStatusRow(statusGrid, 2, QStringLiteral("Lux"), luxLabel, this);
    addStatusRow(statusGrid, 3, QStringLiteral("Camera"), cameraLabel, this);
    addStatusRow(statusGrid, 4, QStringLiteral("Mode"), modeLabel, this);
    addStatusRow(statusGrid, 5, QStringLiteral("Frames"), frameLabel, this);
    addStatusRow(statusGrid, 6, QStringLiteral("AF"), afLabel, this);
    addStatusRow(statusGrid, 7, QStringLiteral("Torch"), torchLabel, this);
    addStatusRow(statusGrid, 8, QStringLiteral("Storage"), storageLabel, this);
    addStatusRow(statusGrid, 9, QStringLiteral("Session"), sessionLabel, this);
    addStatusRow(statusGrid, 10, QStringLiteral("Error"), errorLabel, this);
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
    connect(snapshotButton, &QPushButton::clicked, controller, &MonitorController::requestManualSnapshot);
    connect(torchButton, &QPushButton::toggled, controller, &MonitorController::setManualTorch);
    connect(afButton, &QPushButton::clicked, controller, &MonitorController::requestAutoFocus);
    connect(playbackButton, &QPushButton::clicked, this, &SmartMonitorPage::playbackRequested);
    connect(toolsButton, &QPushButton::clicked, this, &SmartMonitorPage::toolsRequested);
    connect(modeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            controller, &MonitorController::setPreviewModeIndex);
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
    startStopButton->setText(snapshot.monitoringEnabled ? QStringLiteral("Stop") : QStringLiteral("Start"));
    snapshotButton->setEnabled(snapshot.camera == CameraState::Streaming);
    afButton->setEnabled(snapshot.camera == CameraState::Streaming);

    monitoringLabel->setText(snapshot.monitoringEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    const QString source = snapshot.presenceSource.isEmpty() ? QStringLiteral("--") : snapshot.presenceSource;
    presenceLabel->setText(QStringLiteral("%1  %2").arg(toString(snapshot.presence), source));
    luxLabel->setText(QStringLiteral("%1 lx").arg(snapshot.lux, 0, 'f', 1));

    QString cameraText = toString(snapshot.camera);
    if (snapshot.cameraWanted)
        cameraText += QStringLiteral(" wanted");
    cameraLabel->setText(cameraText);
    modeLabel->setText(snapshot.activeMode.isEmpty() ? QStringLiteral("--") : snapshot.activeMode);
    frameLabel->setText(QString::number(snapshot.frameCount));
    afLabel->setText(snapshot.afStatus.isEmpty() ? QStringLiteral("--") : snapshot.afStatus);

    if (torchButton->isChecked())
        torchLabel->setText(QStringLiteral("manual"));
    else
        torchLabel->setText(snapshot.torchWanted ? QStringLiteral("auto") : QStringLiteral("off"));

    QString storageText = toString(snapshot.storage);
    if (!snapshot.storageAction.isEmpty())
        storageText += QStringLiteral(" %1").arg(snapshot.storageAction);
    storageLabel->setText(storageText);
    sessionLabel->setText(snapshot.sessionId.isEmpty() ? QStringLiteral("--") : snapshot.sessionId);

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
    modeCombo->setEnabled(true);
}

void SmartMonitorPage::appendLogLine(const QString &line)
{
    if (line.isEmpty())
        return;
    logView->append(QStringLiteral("%1  %2")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
}

PlaybackPage::PlaybackPage(QWidget *parent)
    : QWidget(parent)
    , storageRoot(QStringLiteral("/smart-monitor"))
    , sessionList(new QListWidget(this))
    , eventList(new QListWidget(this))
    , frameList(new QListWidget(this))
    , imageLabel(new QLabel(QStringLiteral("--"), this))
    , statusLabel(new QLabel(QStringLiteral("--"), this))
{
    setMinimumSize(480, 272);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#151719;color:#eef1ef;font-family:DejaVu Sans;}"
        "QPushButton{font-size:13px;min-height:28px;padding:5px 9px;border:1px solid #3c4a48;border-radius:3px;background:#1f5d6b;color:#ffffff;}"
        "QPushButton:pressed{background:#2a7584;}"
        "QLabel{font-size:12px;color:#dfe5e2;}"
        "QListWidget{background:#0d0f0f;border:1px solid #2b3432;color:#e8eeeb;font-size:12px;}"));

    QPushButton *back = new QPushButton(QStringLiteral("Back"), this);
    QPushButton *refresh = new QPushButton(QStringLiteral("Refresh"), this);
    QLabel *title = new QLabel(QStringLiteral("Playback"), this);
    title->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;color:#ffffff;"));

    QHBoxLayout *top = new QHBoxLayout;
    top->setContentsMargins(8, 6, 8, 2);
    top->addWidget(title);
    top->addStretch();
    top->addWidget(refresh);
    top->addWidget(back);

    sessionList->setMinimumWidth(170);
    sessionList->setMaximumWidth(230);
    eventList->setMinimumWidth(210);
    frameList->setMaximumHeight(110);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(220, 150);
    imageLabel->setStyleSheet(QStringLiteral("background:#080909;border:1px solid #2b3432;color:#7f8b8c;"));

    QVBoxLayout *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(6);
    right->addWidget(frameList);
    right->addWidget(imageLabel, 1);
    right->addWidget(statusLabel);

    QHBoxLayout *body = new QHBoxLayout;
    body->setContentsMargins(8, 2, 8, 6);
    body->setSpacing(8);
    body->addWidget(sessionList);
    body->addWidget(eventList, 1);
    body->addLayout(right, 1);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addLayout(body, 1);

    connect(back, &QPushButton::clicked, this, &PlaybackPage::backRequested);
    connect(refresh, &QPushButton::clicked, this, &PlaybackPage::refreshSessions);
    connect(sessionList, &QListWidget::currentRowChanged, this, &PlaybackPage::loadSession);
    connect(frameList, &QListWidget::itemClicked, this, &PlaybackPage::loadFrame);

    refreshSessions();
}

void PlaybackPage::refreshSessions()
{
    QString error;
    sessions = storage.listMonitorSessions(storageRoot, &error);
    sessionList->clear();
    eventList->clear();
    frameList->clear();
    imageLabel->setText(QStringLiteral("--"));
    imageLabel->setPixmap(QPixmap());

    for (const MonitorSessionInfo &session : sessions) {
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1\n%2 frame(s)").arg(session.sessionId).arg(session.frameCount));
        item->setData(Qt::UserRole, session.sessionPath);
        sessionList->addItem(item);
    }

    if (!error.isEmpty())
        statusLabel->setText(error);
    else
        statusLabel->setText(QStringLiteral("%1 session(s)").arg(sessions.size()));

    if (!sessions.isEmpty())
        sessionList->setCurrentRow(0);
}

void PlaybackPage::loadSession(int row)
{
    if (row < 0 || row >= sessions.size())
        return;

    const MonitorSessionInfo session = sessions.at(row);
    loadEvents(session.sessionPath);
    loadFrames(session.sessionPath);
    statusLabel->setText(QStringLiteral("%1  %2 event(s)  %3 frame(s)")
                         .arg(session.sessionId)
                         .arg(session.eventCount)
                         .arg(session.frameCount));

    if (frameList->count() > 0) {
        frameList->setCurrentRow(0);
        loadFrame(frameList->item(0));
    }
}

void PlaybackPage::loadFrame(QListWidgetItem *item)
{
    const QString path = itemPath(item);
    if (path.isEmpty())
        return;

    QImage image(path);
    if (image.isNull()) {
        imageLabel->setPixmap(QPixmap());
        imageLabel->setText(QStringLiteral("cannot load image"));
        return;
    }

    imageLabel->setText(QString());
    imageLabel->setPixmap(QPixmap::fromImage(image).scaled(imageLabel->size(),
                                                           Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation));
}

void PlaybackPage::loadEvents(const QString &sessionPath)
{
    eventList->clear();
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("events.jsonl")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            eventList->addItem(QStringLiteral("%1  %2  %3")
                               .arg(obj.value(QStringLiteral("ts")).toString(),
                                    obj.value(QStringLiteral("type")).toString(),
                                    obj.value(QStringLiteral("status")).toString()));
        } else {
            eventList->addItem(line);
        }
    }
}

void PlaybackPage::loadFrames(const QString &sessionPath)
{
    frameList->clear();
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("index.jsonl")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        const QString relPath = obj.value(QStringLiteral("path")).toString();
        if (relPath.isEmpty())
            continue;
        QListWidgetItem *item = new QListWidgetItem(
            QStringLiteral("%1  %2").arg(obj.value(QStringLiteral("seq")).toInt()).arg(relPath));
        item->setData(Qt::UserRole, QDir(sessionPath).absoluteFilePath(relPath));
        frameList->addItem(item);
    }
}

QString PlaybackPage::itemPath(QListWidgetItem *item) const
{
    if (!item)
        return QString();
    return item->data(Qt::UserRole).toString();
}

} // namespace imx6sm
