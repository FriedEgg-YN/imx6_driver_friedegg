#include "ap3216c_controller.h"
#include "ap3216c_page.h"

#include <QApplication>
#include <QThread>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    Ap3216cPage page;
    Ap3216cController controller(&page); // Set the page as the parent of the controller

    QObject::connect(&page, &Ap3216cPage::startRequested, &controller, &Ap3216cController::start);
    QObject::connect(&page, &Ap3216cPage::stopRequested, &controller, &Ap3216cController::stop);
    QObject::connect(&controller, &Ap3216cController::viewStateChanged, &page, &Ap3216cPage::render);

    Q_ASSERT(page.thread() == QThread::currentThread());
    Q_ASSERT(controller.thread() == QThread::currentThread());

    controller.refresh(); // Initialize the view state

    page.resize(800, 480);
    page.setWindowTitle(QStringLiteral("AP3216C Application"));
    page.show();
    
    return app.exec();
}