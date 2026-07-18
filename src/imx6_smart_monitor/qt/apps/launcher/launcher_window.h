#ifndef IMX6SMARTMONITOR_LAUNCHER_WINDOW_H
#define IMX6SMARTMONITOR_LAUNCHER_WINDOW_H

#include <QString>
#include <QWidget>

#include <functional>

class QGridLayout;
class QStackedWidget;
class QToolButton;

namespace imx6sm {

class ModuleTestWindow;
class SmartMonitorPage;

class SmartMonitorLauncher : public QWidget {
public:
    explicit SmartMonitorLauncher(QWidget *parent = nullptr);

private:
    struct AppEntry {
        QString title;
        QString subtitle;
        QString tag;
        QString colorName;
        std::function<ModuleTestWindow *()> factory;
    };

    QWidget *createHomePage();
    void showMonitor();
    void showLd2410Config();
    QToolButton *createAppButton(const AppEntry &entry);
    void addApp(QGridLayout *grid, int row, int column, const AppEntry &entry);
    void openApp(const AppEntry &entry);
    void showHome();

    QStackedWidget *stack;
    SmartMonitorPage *monitorPage;
    QWidget *homePage;
    QWidget *currentAppPage = nullptr;
};

} // namespace imx6sm

#endif
