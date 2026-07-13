#include "qt/common/app_runner.h"
#include "core_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-core-test"),
                        []() { return new CoreTestWindow; });
}
