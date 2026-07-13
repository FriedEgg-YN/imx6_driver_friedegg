#ifndef IMX6SMARTMONITOR_AP3216C_TEST_WINDOW_H
#define IMX6SMARTMONITOR_AP3216C_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "sensors/ap3216c_device.h"

class QLabel;
class QLineEdit;
class QTimer;

namespace imx6sm {

class Ap3216cTestWindow : public ModuleTestWindow {
public:
    explicit Ap3216cTestWindow(QWidget *parent = nullptr);

private:
    QString preferredPath() const;
    void readOnce();

    QLineEdit *pathEdit;
    QLabel *deviceLabel;
    QLabel *luxLabel;
    QLabel *alsLabel;
    QLabel *irLabel;
    QLabel *psLabel;
    QTimer *timer;
    Ap3216cDevice device;
};

} // namespace imx6sm

#endif
