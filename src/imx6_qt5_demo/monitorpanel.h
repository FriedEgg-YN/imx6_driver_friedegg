#ifndef MONITORPANEL_H
#define MONITORPANEL_H

#include <QTimer>
#include <QUrl>
#include <QWidget>

class QNetworkAccessManager;
class QProcess;
class QString;

namespace Ui {
class MonitorPanel;
}

class MonitorPanel : public QWidget {
    Q_OBJECT

public:
    explicit MonitorPanel(QWidget *parent = nullptr);
    ~MonitorPanel() override;

signals:
    void homeRequested();

private:
    Ui::MonitorPanel *ui;
    QNetworkAccessManager *manager;
    QProcess *monitor;
    QTimer statusTimer;
    QTimer previewTimer;
    bool monitorRunning = false;
    bool ownMonitor = false;
    bool cameraEnabled = false;
    bool sensorEnabled = false;
    bool cameraPublicEnabled = false;
    bool sensorPublicEnabled = false;
    bool previewEnabled = false;
    bool statusInFlight = false;
    bool previewInFlight = false;

    QUrl apiUrl(const QString &path) const;
    void updateStatusSoon();
    void ensureMonitorRunning();
    void startMonitor();
    void stopMonitor();
    void refreshStatus(bool autoStart = false);
    void sendControl(const QString &query);
    void refreshPreview();
    void updateButtons();
    void toggleMonitor();
    void toggleCamera();
    void togglePreview();
    void toggleCameraPublic();
    void toggleSensor();
    void toggleSensorPublic();
};

#endif
