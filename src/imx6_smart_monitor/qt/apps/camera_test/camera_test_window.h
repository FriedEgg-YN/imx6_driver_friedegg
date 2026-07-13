#ifndef IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H
#define IMX6SMARTMONITOR_CAMERA_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "camera/camera_device.h"

class QLabel;
class QLineEdit;

namespace imx6sm {

class CameraTestWindow : public ModuleTestWindow {
public:
    explicit CameraTestWindow(QWidget *parent = nullptr);

private:
    void queryCaps();

    QLineEdit *pathEdit;
    QLabel *driverLabel;
    QLabel *cardLabel;
    QLabel *busLabel;
    QLabel *formatLabel;
    CameraDevice camera;
};

} // namespace imx6sm

#endif
