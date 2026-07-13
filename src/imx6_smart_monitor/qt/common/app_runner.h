#ifndef IMX6SMARTMONITOR_APP_RUNNER_H
#define IMX6SMARTMONITOR_APP_RUNNER_H

#include <QString>

#include <functional>

class QWidget;

namespace imx6sm {

int runWidgetApp(int argc, char **argv, const QString &name,
                 const std::function<QWidget *()> &factory);

} // namespace imx6sm

#endif

