#include "qt/common/app_runner.h"
#include "qt/common/module_test_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>

namespace imx6sm {

static bool hasArg(int argc, char **argv, const QString &name)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == name)
            return true;
    }

    return false;
}

static int durationMs(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) != QStringLiteral("--duration-ms"))
            continue;

        bool ok = false;
        const int value = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
        return ok ? value : 0;
    }

    return 0;
}

int runWidgetApp(int argc, char **argv, const QString &name,
                 const std::function<QWidget *()> &factory)
{
    if (hasArg(argc, argv, QStringLiteral("--self-test"))) {
        QCoreApplication app(argc, argv);
        std::printf("%s Qt %s\n", name.toLocal8Bit().constData(), qVersion());
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "linuxfb");

    QApplication app(argc, argv);
    QWidget *window = factory();
    if (ModuleTestWindow *testWindow = dynamic_cast<ModuleTestWindow *>(window)) {
        QPushButton *exitButton = testWindow->addHeaderButton(QStringLiteral("Exit"));
        QObject::connect(exitButton, &QPushButton::clicked, &app, []() {
            QCoreApplication::quit();
        });
    }
    window->showFullScreen();

    const int timeout = durationMs(argc, argv);
    if (timeout > 0)
        QTimer::singleShot(timeout, &app, &QCoreApplication::quit);

    return app.exec();
}

} // namespace imx6sm

