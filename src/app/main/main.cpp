#include <QApplication>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.resize(800, 480);
    window.setWindowTitle("Qt Application");
    window.show();

    return app.exec();
}