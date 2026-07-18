#ifndef IMX6SMARTMONITOR_CORE_TEST_WINDOW_H
#define IMX6SMARTMONITOR_CORE_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "core/monitor_core.h"

class QLabel;

namespace imx6sm {

class CoreTestWindow : public ModuleTestWindow {
public:
    explicit CoreTestWindow(QWidget *parent = nullptr);

private:
    void refresh();

    QLabel *monitoringLabel;
    QLabel *presenceLabel;
    QLabel *lightLabel;
    QLabel *cameraLabel;
    QLabel *storageLabel;
    QLabel *torchLabel;
    QLabel *wantedLabel;
    QLabel *recordingLabel;
    QLabel *errorLabel;
    QLabel *actionLabel;
    MonitorCore core;
};

} // namespace imx6sm

#endif
