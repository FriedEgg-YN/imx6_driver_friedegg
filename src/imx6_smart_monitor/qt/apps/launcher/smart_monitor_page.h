#ifndef IMX6SMARTMONITOR_SMART_MONITOR_PAGE_H
#define IMX6SMARTMONITOR_SMART_MONITOR_PAGE_H

#include "monitor_controller.h"
#include "storage/storage_manager.h"

#include <QList>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextEdit;

namespace imx6sm {

class PreviewPane;

class SmartMonitorPage : public QWidget {
    Q_OBJECT

public:
    explicit SmartMonitorPage(QWidget *parent = nullptr);

signals:
    void toolsRequested();
    void playbackRequested();

private:
    void refreshSnapshot(const MonitorSnapshot &snapshot);
    void refreshModes();
    void appendLogLine(const QString &line);

    MonitorController *controller;
    PreviewPane *previewPane;
    QPushButton *startStopButton;
    QPushButton *snapshotButton;
    QPushButton *torchButton;
    QPushButton *afButton;
    QPushButton *playbackButton;
    QPushButton *toolsButton;
    QComboBox *modeCombo;
    QLabel *monitoringLabel;
    QLabel *presenceLabel;
    QLabel *luxLabel;
    QLabel *cameraLabel;
    QLabel *modeLabel;
    QLabel *frameLabel;
    QLabel *afLabel;
    QLabel *torchLabel;
    QLabel *storageLabel;
    QLabel *sessionLabel;
    QLabel *errorLabel;
    QTextEdit *logView;
};

class PlaybackPage : public QWidget {
    Q_OBJECT

public:
    explicit PlaybackPage(QWidget *parent = nullptr);

signals:
    void backRequested();

private slots:
    void refreshSessions();
    void loadSession(int row);
    void loadFrame(QListWidgetItem *item);

private:
    void loadEvents(const QString &sessionPath);
    void loadFrames(const QString &sessionPath);
    QString itemPath(QListWidgetItem *item) const;

    StorageManager storage;
    QString storageRoot;
    QList<MonitorSessionInfo> sessions;
    QListWidget *sessionList;
    QListWidget *eventList;
    QListWidget *frameList;
    QLabel *imageLabel;
    QLabel *statusLabel;
};

} // namespace imx6sm

#endif
