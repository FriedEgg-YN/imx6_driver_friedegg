#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>

class MonitorPanel : public QWidget {
public:
    MonitorPanel()
        : manager(new QNetworkAccessManager(this))
        , monitor(new QProcess(this))
    {
        setWindowTitle(QStringLiteral("i.MX6 Monitor HMI"));
        setMinimumSize(480, 272);
        buildUi();
        connect(monitor, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) { updateStatusSoon(); });
        connect(&statusTimer, &QTimer::timeout, this, [this]() { refreshStatus(); });
        connect(&previewTimer, &QTimer::timeout, this, &MonitorPanel::refreshPreview);
        statusTimer.start(800);
        QTimer::singleShot(0, this, &MonitorPanel::ensureMonitorRunning);
    }

    ~MonitorPanel() override
    {
        if (ownMonitor && monitor->state() != QProcess::NotRunning) {
            monitor->terminate();
            if (!monitor->waitForFinished(1200))
                monitor->kill();
        }
    }

private:
    QNetworkAccessManager *manager;
    QProcess *monitor;
    QTimer statusTimer;
    QTimer previewTimer;
    QLabel *stateLabel;
    QLabel *sensorLabel;
    QLabel *videoLabel;
    QLabel *previewLabel;
    QPushButton *monitorButton;
    QPushButton *cameraButton;
    QPushButton *previewButton;
    QPushButton *cameraPublicButton;
    QPushButton *sensorButton;
    QPushButton *sensorPublicButton;
    bool monitorRunning = false;
    bool ownMonitor = false;
    bool cameraEnabled = false;
    bool sensorEnabled = false;
    bool cameraPublicEnabled = false;
    bool sensorPublicEnabled = false;
    bool previewEnabled = false;
    bool statusInFlight = false;
    bool previewInFlight = false;

    void buildUi()
    {
        setStyleSheet(QStringLiteral(
            "QWidget{background:#111820;color:#eef3f5;font-family:DejaVu Sans;}"
            "QLabel{font-size:16px;}"
            "QPushButton{font-size:18px;padding:14px 10px;border-radius:6px;background:#25313b;color:#eef3f5;}"
            "QPushButton:checked{background:#1f8f64;color:white;}"
            "QPushButton:pressed{background:#2f4555;}"
            "#title{font-size:24px;font-weight:700;}"
            "#preview{background:#050607;border:2px solid #2a3944;}"
            "#small{font-size:14px;color:#b7c5cd;}"));

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 10, 14, 10);
        root->setSpacing(10);

        QLabel *title = new QLabel(QStringLiteral("i.MX6ULL Monitor HMI"), this);
        title->setObjectName(QStringLiteral("title"));
        stateLabel = new QLabel(QStringLiteral("monitor: starting"), this);
        stateLabel->setObjectName(QStringLiteral("small"));
        root->addWidget(title);
        root->addWidget(stateLabel);

        QHBoxLayout *body = new QHBoxLayout;
        body->setSpacing(12);
        QVBoxLayout *left = new QVBoxLayout;
        left->setSpacing(8);
        QVBoxLayout *right = new QVBoxLayout;
        right->setSpacing(8);

        monitorButton = makeButton(QStringLiteral("Monitor"));
        cameraButton = makeButton(QStringLiteral("Camera"));
        previewButton = makeButton(QStringLiteral("LCD Preview"));
        cameraPublicButton = makeButton(QStringLiteral("Camera Net"));
        sensorButton = makeButton(QStringLiteral("AP3216C"));
        sensorPublicButton = makeButton(QStringLiteral("Sensor Net"));

        connect(monitorButton, &QPushButton::clicked, this, &MonitorPanel::toggleMonitor);
        connect(cameraButton, &QPushButton::clicked, this, &MonitorPanel::toggleCamera);
        connect(previewButton, &QPushButton::clicked, this, &MonitorPanel::togglePreview);
        connect(cameraPublicButton, &QPushButton::clicked, this, &MonitorPanel::toggleCameraPublic);
        connect(sensorButton, &QPushButton::clicked, this, &MonitorPanel::toggleSensor);
        connect(sensorPublicButton, &QPushButton::clicked, this, &MonitorPanel::toggleSensorPublic);

        left->addWidget(monitorButton);
        left->addWidget(cameraButton);
        left->addWidget(previewButton);
        left->addWidget(cameraPublicButton);
        left->addWidget(sensorButton);
        left->addWidget(sensorPublicButton);
        left->addStretch(1);

        previewLabel = new QLabel(QStringLiteral("preview off"), this);
        previewLabel->setObjectName(QStringLiteral("preview"));
        previewLabel->setAlignment(Qt::AlignCenter);
        previewLabel->setMinimumSize(260, 150);
        previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sensorLabel = new QLabel(QStringLiteral("AP3216C: off"), this);
        sensorLabel->setObjectName(QStringLiteral("small"));
        videoLabel = new QLabel(QStringLiteral("video: off"), this);
        videoLabel->setObjectName(QStringLiteral("small"));
        sensorLabel->setWordWrap(true);
        videoLabel->setWordWrap(true);

        right->addWidget(previewLabel, 1);
        right->addWidget(videoLabel);
        right->addWidget(sensorLabel);

        body->addLayout(left, 0);
        body->addLayout(right, 1);
        root->addLayout(body, 1);
        updateButtons();
    }

    QPushButton *makeButton(const QString &text)
    {
        QPushButton *button = new QPushButton(text, this);
        button->setCheckable(true);
        button->setMinimumHeight(52);
        button->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        return button;
    }

    QUrl apiUrl(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:8080%1").arg(path));
    }

    void updateStatusSoon()
    {
        monitorRunning = false;
        ownMonitor = false;
        updateButtons();
        QTimer::singleShot(200, this, [this]() { refreshStatus(); });
    }

    void ensureMonitorRunning()
    {
        refreshStatus(true);
    }

    void startMonitor()
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
        stateLabel->setText(QStringLiteral("monitor: launching"));
    }

    void stopMonitor()
    {
        if (!ownMonitor) {
            stateLabel->setText(QStringLiteral("monitor: external process not stopped"));
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
        previewLabel->setText(QStringLiteral("preview off"));
        stateLabel->setText(QStringLiteral("monitor: stopped"));
        updateButtons();
    }

    void refreshStatus(bool autoStart = false)
    {
        if (statusInFlight)
            return;
        statusInFlight = true;
        QNetworkReply *reply = manager->get(QNetworkRequest(apiUrl(QStringLiteral("/api/status"))));
        connect(reply, &QNetworkReply::finished, this, [this, reply, autoStart]() {
            statusInFlight = false;
            if (reply->error() != QNetworkReply::NoError) {
                monitorRunning = false;
                stateLabel->setText(QStringLiteral("monitor: offline"));
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

            stateLabel->setText(QStringLiteral("monitor: running  uptime %1s").arg(root.value(QStringLiteral("uptime_sec")).toInt()));
            videoLabel->setText(QStringLiteral("video: %1  frames %2  errors %3")
                                    .arg(cameraEnabled ? QStringLiteral("on") : QStringLiteral("off"))
                                    .arg(video.value(QStringLiteral("captured_frames")).toInt())
                                    .arg(video.value(QStringLiteral("errors")).toInt()));
            sensorLabel->setText(QStringLiteral("AP3216C: %1  IR %2  ALS %3  lux %4  PS %5  %6")
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

    void sendControl(const QString &query)
    {
        if (!monitorRunning)
            return;
        QNetworkReply *reply = manager->get(QNetworkRequest(apiUrl(QStringLiteral("/api/control?%1").arg(query))));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            refreshStatus();
        });
    }

    void refreshPreview()
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
                    previewLabel->setPixmap(QPixmap::fromImage(
                        image.scaled(previewLabel->size(), Qt::KeepAspectRatio,
                                     Qt::FastTransformation)));
                }
            }
            reply->deleteLater();
        });
    }

    void updateButtons()
    {
        monitorButton->setChecked(monitorRunning);
        cameraButton->setChecked(cameraEnabled);
        previewButton->setChecked(previewEnabled);
        cameraPublicButton->setChecked(cameraPublicEnabled);
        sensorButton->setChecked(sensorEnabled);
        sensorPublicButton->setChecked(sensorPublicEnabled);
        cameraButton->setEnabled(monitorRunning);
        previewButton->setEnabled(monitorRunning && cameraEnabled);
        cameraPublicButton->setEnabled(monitorRunning);
        sensorButton->setEnabled(monitorRunning);
        sensorPublicButton->setEnabled(monitorRunning);
        if (previewEnabled && !previewTimer.isActive())
            previewTimer.start(200);
        if ((!previewEnabled || !cameraEnabled || !monitorRunning) && previewTimer.isActive())
            previewTimer.stop();
        if (!previewEnabled)
            previewLabel->setText(QStringLiteral("preview off"));
    }

    void toggleMonitor()
    {
        if (monitorRunning)
            stopMonitor();
        else
            startMonitor();
    }

    void toggleCamera()
    {
        sendControl(QStringLiteral("camera=%1").arg(cameraEnabled ? QStringLiteral("off") : QStringLiteral("on")));
    }

    void togglePreview()
    {
        previewEnabled = !previewEnabled;
        updateButtons();
        if (previewEnabled)
            refreshPreview();
    }

    void toggleCameraPublic()
    {
        sendControl(QStringLiteral("camera_public=%1").arg(cameraPublicEnabled ? QStringLiteral("off") : QStringLiteral("on")));
    }

    void toggleSensor()
    {
        sendControl(QStringLiteral("sensor=%1").arg(sensorEnabled ? QStringLiteral("off") : QStringLiteral("on")));
    }

    void toggleSensorPublic()
    {
        sendControl(QStringLiteral("sensor_public=%1").arg(sensorPublicEnabled ? QStringLiteral("off") : QStringLiteral("on")));
    }
};

static bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QString::fromLatin1(name))
            return true;
    }

    return false;
}

static int durationMs(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--duration-ms")) {
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
            return ok ? value : 0;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (hasArg(argc, argv, "--self-test")) {
        QCoreApplication app(argc, argv);
        std::printf("imx6-qt5-demo Qt %s\n", qVersion());
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "linuxfb");

    QApplication app(argc, argv);
    MonitorPanel panel;
    panel.showFullScreen();

    const int timeout = durationMs(argc, argv);
    if (timeout > 0)
        QTimer::singleShot(timeout, &app, &QCoreApplication::quit);

    return app.exec();
}
