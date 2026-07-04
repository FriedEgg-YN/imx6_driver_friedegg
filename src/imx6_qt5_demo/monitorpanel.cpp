#include "monitorpanel.h"
#include "ui_monitorpanel.h"

#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QStringList>

MonitorPanel::MonitorPanel(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MonitorPanel)
    , manager(new QNetworkAccessManager(this))
    , monitor(new QProcess(this))
{
    ui->setupUi(this);

    setStyleSheet(QStringLiteral(
        "QWidget{background:#111820;color:#eef3f5;font-family:DejaVu Sans;}"
        "QLabel{font-size:16px;}"
        "QPushButton{font-size:18px;padding:14px 10px;border-radius:6px;background:#25313b;color:#eef3f5;}"
        "QPushButton:checked{background:#1f8f64;color:white;}"
        "QPushButton:pressed{background:#2f4555;}"
        "#title{font-size:24px;font-weight:700;}"
        "#previewLabel{background:#050607;border:2px solid #2a3944;}"
        "#stateLabel,#sensorLabel,#videoLabel{font-size:14px;color:#b7c5cd;}"));

    connect(monitor, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) { updateStatusSoon(); });
    connect(&statusTimer, &QTimer::timeout, this, [this]() { refreshStatus(); });
    connect(&previewTimer, &QTimer::timeout, this, &MonitorPanel::refreshPreview);
    connect(ui->monitorButton, &QPushButton::clicked, this, &MonitorPanel::toggleMonitor);
    connect(ui->cameraButton, &QPushButton::clicked, this, &MonitorPanel::toggleCamera);
    connect(ui->previewButton, &QPushButton::clicked, this, &MonitorPanel::togglePreview);
    connect(ui->cameraPublicButton, &QPushButton::clicked, this, &MonitorPanel::toggleCameraPublic);
    connect(ui->sensorButton, &QPushButton::clicked, this, &MonitorPanel::toggleSensor);
    connect(ui->sensorPublicButton, &QPushButton::clicked, this, &MonitorPanel::toggleSensorPublic);

    statusTimer.start(800);
    updateButtons();
    QTimer::singleShot(0, this, &MonitorPanel::ensureMonitorRunning);
}

MonitorPanel::~MonitorPanel()
{
    if (ownMonitor && monitor->state() != QProcess::NotRunning) {
        monitor->terminate();
        if (!monitor->waitForFinished(1200))
            monitor->kill();
    }

    delete ui;
}

QUrl MonitorPanel::apiUrl(const QString &path) const
{
    return QUrl(QStringLiteral("http://127.0.0.1:8080%1").arg(path));
}

void MonitorPanel::updateStatusSoon()
{
    monitorRunning = false;
    ownMonitor = false;
    updateButtons();
    QTimer::singleShot(200, this, [this]() { refreshStatus(); });
}

void MonitorPanel::ensureMonitorRunning()
{
    refreshStatus(true);
}

void MonitorPanel::startMonitor()
{
    if (monitor->state() != QProcess::NotRunning)
        return;

    QStringList args;
    args << QStringLiteral("-n")
         << QStringLiteral("-s") << QStringLiteral("auto")
         << QStringLiteral("-p") << QStringLiteral("8080")
         << QStringLiteral("-W") << QStringLiteral("800")
         << QStringLiteral("-H") << QStringLiteral("480")
         << QStringLiteral("-r") << QStringLiteral("10")
         << QStringLiteral("-q") << QStringLiteral("75")
         << QStringLiteral("--camera-off")
         << QStringLiteral("--sensor-off")
         << QStringLiteral("--public-off");
    monitor->setProcessChannelMode(QProcess::ForwardedChannels);
    monitor->start(QStringLiteral("imx6-monitor"), args);
    ownMonitor = true;
    ui->stateLabel->setText(QStringLiteral("monitor: launching"));
}

void MonitorPanel::stopMonitor()
{
    if (!ownMonitor) {
        ui->stateLabel->setText(QStringLiteral("monitor: external process not stopped"));
        refreshStatus();
        return;
    }
    if (monitor->state() != QProcess::NotRunning) {
        monitor->terminate();
        if (!monitor->waitForFinished(1200))
            monitor->kill();
    }
    monitorRunning = false;
    ownMonitor = false;
    cameraEnabled = false;
    sensorEnabled = false;
    cameraPublicEnabled = false;
    sensorPublicEnabled = false;
    previewEnabled = false;
    previewTimer.stop();
    ui->previewLabel->setText(QStringLiteral("preview off"));
    ui->stateLabel->setText(QStringLiteral("monitor: stopped"));
    updateButtons();
}

void MonitorPanel::refreshStatus(bool autoStart)
{
    if (statusInFlight)
        return;
    statusInFlight = true;
    QNetworkReply *reply = manager->get(QNetworkRequest(apiUrl(QStringLiteral("/api/status"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, autoStart]() {
        statusInFlight = false;
        if (reply->error() != QNetworkReply::NoError) {
            monitorRunning = false;
            ui->stateLabel->setText(QStringLiteral("monitor: offline"));
            reply->deleteLater();
            updateButtons();
            if (autoStart)
                startMonitor();
            return;
        }

        monitorRunning = true;
        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonObject root = doc.object();
        const QJsonObject controls = root.value(QStringLiteral("controls")).toObject();
        const QJsonObject video = root.value(QStringLiteral("video")).toObject();
        const QJsonObject sensor = root.value(QStringLiteral("sensor")).toObject();

        cameraEnabled = controls.value(QStringLiteral("camera_enabled")).toBool(video.value(QStringLiteral("enabled")).toBool(false));
        sensorEnabled = controls.value(QStringLiteral("sensor_enabled")).toBool(sensor.value(QStringLiteral("enabled")).toBool(false));
        cameraPublicEnabled = controls.value(QStringLiteral("camera_public_enabled")).toBool(video.value(QStringLiteral("public")).toBool(false));
        sensorPublicEnabled = controls.value(QStringLiteral("sensor_public_enabled")).toBool(sensor.value(QStringLiteral("public")).toBool(false));

        ui->stateLabel->setText(QStringLiteral("monitor: running  uptime %1s").arg(root.value(QStringLiteral("uptime_sec")).toInt()));
        ui->videoLabel->setText(QStringLiteral("video: %1  frames %2  errors %3")
                                    .arg(cameraEnabled ? QStringLiteral("on") : QStringLiteral("off"))
                                    .arg(video.value(QStringLiteral("captured_frames")).toInt())
                                    .arg(video.value(QStringLiteral("errors")).toInt()));
        ui->sensorLabel->setText(QStringLiteral("AP3216C: %1  IR %2  ALS %3  lux %4  PS %5  %6")
                                     .arg(sensorEnabled ? QStringLiteral("on") : QStringLiteral("off"))
                                     .arg(sensor.value(QStringLiteral("ir")).toInt())
                                     .arg(sensor.value(QStringLiteral("als")).toInt())
                                     .arg(sensor.value(QStringLiteral("als_input")).toString(QStringLiteral("-")))
                                     .arg(sensor.value(QStringLiteral("ps")).toInt())
                                     .arg(sensor.value(QStringLiteral("state")).toString(QStringLiteral("unknown"))));
        reply->deleteLater();
        updateButtons();
    });
}

void MonitorPanel::sendControl(const QString &query)
{
    if (!monitorRunning)
        return;
    QNetworkReply *reply = manager->get(QNetworkRequest(apiUrl(QStringLiteral("/api/control?%1").arg(query))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        refreshStatus();
    });
}

void MonitorPanel::refreshPreview()
{
    if (!previewEnabled || !monitorRunning || !cameraEnabled || previewInFlight)
        return;
    previewInFlight = true;
    QNetworkReply *reply = manager->get(QNetworkRequest(apiUrl(QStringLiteral("/frame.rgb565"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        previewInFlight = false;
        if (reply->error() == QNetworkReply::NoError) {
            bool widthOk = false;
            bool heightOk = false;
            const int width = reply->rawHeader("X-Width").toInt(&widthOk);
            const int height = reply->rawHeader("X-Height").toInt(&heightOk);
            const QByteArray frame = reply->readAll();

            if (widthOk && heightOk && width > 0 && height > 0 &&
                frame.size() == width * height * 2) {
                const QImage image(reinterpret_cast<const uchar *>(frame.constData()),
                                   width, height, width * 2, QImage::Format_RGB16);
                ui->previewLabel->setPixmap(QPixmap::fromImage(
                    image.scaled(ui->previewLabel->size(), Qt::KeepAspectRatio,
                                 Qt::FastTransformation)));
            }
        }
        reply->deleteLater();
    });
}

void MonitorPanel::updateButtons()
{
    ui->monitorButton->setChecked(monitorRunning);
    ui->cameraButton->setChecked(cameraEnabled);
    ui->previewButton->setChecked(previewEnabled);
    ui->cameraPublicButton->setChecked(cameraPublicEnabled);
    ui->sensorButton->setChecked(sensorEnabled);
    ui->sensorPublicButton->setChecked(sensorPublicEnabled);
    ui->cameraButton->setEnabled(monitorRunning);
    ui->previewButton->setEnabled(monitorRunning && cameraEnabled);
    ui->cameraPublicButton->setEnabled(monitorRunning);
    ui->sensorButton->setEnabled(monitorRunning);
    ui->sensorPublicButton->setEnabled(monitorRunning);
    if (previewEnabled && !previewTimer.isActive())
        previewTimer.start(200);
    if ((!previewEnabled || !cameraEnabled || !monitorRunning) && previewTimer.isActive())
        previewTimer.stop();
    if (!previewEnabled)
        ui->previewLabel->setText(QStringLiteral("preview off"));
}

void MonitorPanel::toggleMonitor()
{
    if (monitorRunning)
        stopMonitor();
    else
        startMonitor();
}

void MonitorPanel::toggleCamera()
{
    sendControl(QStringLiteral("camera=%1").arg(cameraEnabled ? QStringLiteral("off") : QStringLiteral("on")));
}

void MonitorPanel::togglePreview()
{
    previewEnabled = !previewEnabled;
    updateButtons();
    if (previewEnabled)
        refreshPreview();
}

void MonitorPanel::toggleCameraPublic()
{
    sendControl(QStringLiteral("camera_public=%1").arg(cameraPublicEnabled ? QStringLiteral("off") : QStringLiteral("on")));
}

void MonitorPanel::toggleSensor()
{
    sendControl(QStringLiteral("sensor=%1").arg(sensorEnabled ? QStringLiteral("off") : QStringLiteral("on")));
}

void MonitorPanel::toggleSensorPublic()
{
    sendControl(QStringLiteral("sensor_public=%1").arg(sensorPublicEnabled ? QStringLiteral("off") : QStringLiteral("on")));
}
