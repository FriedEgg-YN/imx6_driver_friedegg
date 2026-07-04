#include "monitorpanel.h"

#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>

static bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QString::fromLatin1(name))
            return true;
    }

    return false;
}

static int durationMs(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--duration-ms")) {
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
            return ok ? value : 0;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (hasArg(argc, argv, "--self-test")) {
        QCoreApplication app(argc, argv);
        std::printf("imx6-qt5-demo Qt %s\n", qVersion());
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "linuxfb");

    QApplication app(argc, argv);
    MonitorPanel panel;
    panel.showFullScreen();

    const int timeout = durationMs(argc, argv);
    if (timeout > 0)
        QTimer::singleShot(timeout, &app, &QCoreApplication::quit);

    return app.exec();
}
