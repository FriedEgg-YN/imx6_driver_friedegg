#include "qt/common/app_runner.h"
#include "touch_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-touch-test"),
                        []() { return new TouchTestWindow; });
}
