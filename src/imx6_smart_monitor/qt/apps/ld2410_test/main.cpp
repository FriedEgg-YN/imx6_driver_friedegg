#include "qt/common/app_runner.h"
#include "ld2410_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-ld2410-test"),
                        []() { return new Ld2410TestWindow; });
}
