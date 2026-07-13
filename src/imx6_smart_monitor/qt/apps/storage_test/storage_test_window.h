#ifndef IMX6SMARTMONITOR_STORAGE_TEST_WINDOW_H
#define IMX6SMARTMONITOR_STORAGE_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "storage/storage_manager.h"

class QLabel;
class QLineEdit;

namespace imx6sm {

class StorageTestWindow : public ModuleTestWindow {
public:
    explicit StorageTestWindow(QWidget *parent = nullptr);

private:
    void checkRoot();

    QLineEdit *rootEdit;
    QLabel *resultLabel;
    QLabel *sessionLabel;
    StorageManager storage;
};

} // namespace imx6sm

#endif
