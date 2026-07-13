#include "qt/common/app_runner.h"
#include "ap3216c_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-ap3216c-test"),
                        []() { return new Ap3216cTestWindow; });
}
