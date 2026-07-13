#include "qt/common/app_runner.h"
#include "storage_test_window.h"

using namespace imx6sm;

int main(int argc, char **argv)
{
    return runWidgetApp(argc, argv, QStringLiteral("imx6-sm-storage-test"),
                        []() { return new StorageTestWindow; });
}
