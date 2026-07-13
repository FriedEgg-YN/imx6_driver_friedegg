#include "qt/common/app_runner.h"
#include "camera_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-camera-test"),
                        []() { return new CameraTestWindow; });
}
