#ifndef IMX6SMARTMONITOR_LD2410_TEST_WINDOW_H
#define IMX6SMARTMONITOR_LD2410_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "sensors/ld2410_device.h"

class QLabel;
class QLineEdit;

namespace imx6sm {

class Ld2410TestWindow : public ModuleTestWindow {
public:
    explicit Ld2410TestWindow(QWidget *parent = nullptr);

private:
    void probe();

    QLineEdit *outEdit;
    QLineEdit *uartEdit;
    QLabel *hintLabel;
    QLabel *outLabel;
    QLabel *uartLabel;
    Ld2410Device device;
};

} // namespace imx6sm

#endif
