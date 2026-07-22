#ifndef IMX6SMARTMONITOR_SMART_MONITOR_PAGE_H
#define IMX6SMARTMONITOR_SMART_MONITOR_PAGE_H

#include "monitor_controller.h"

#include <QWidget>

class QComboBox;
class QLabel;
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
    void ld2410ConfigRequested();

private:
    void refreshSnapshot(const MonitorSnapshot &snapshot);
    void refreshModes();
    void appendLogLine(const QString &line);

    MonitorController *controller;
    PreviewPane *previewPane;
    QPushButton *startStopButton;
    QPushButton *afButton;
    QPushButton *ld2410Button;
    QPushButton *toolsButton;
    QComboBox *modeCombo;
    QComboBox *strobeCombo;
    QLabel *monitoringLabel;
    QLabel *presenceLabel;
    QLabel *luxLabel;
    QLabel *occlusionLabel;
    QLabel *cameraLabel;
    QLabel *modeLabel;
    QLabel *frameLabel;
    QLabel *afLabel;
    QLabel *torchLabel;
    QLabel *storageLabel;
    QLabel *recordingLabel;
    QLabel *errorLabel;
    QTextEdit *logView;
};

} // namespace imx6sm

#endif
