#include "qt/common/app_runner.h"
#include "launcher_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-smart-monitor"),
                        []() { return new SmartMonitorLauncher; });
}
