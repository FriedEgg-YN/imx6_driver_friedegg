#include "main/composition_root.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    smartmonitor::CompositionRoot root;
    root.start();
    return app.exec();
}
