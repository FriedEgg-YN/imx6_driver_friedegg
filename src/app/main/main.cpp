#include "application/ap3216c/ap3216c_controller.h"
#include "application/ap3216c/ap3216c_page.h"
#include "device/sensor/sensor_service.h"
#include "device/sensor/sensor_worker.h"

#include <QApplication>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

using smartmonitor::Ap3216cController;
using smartmonitor::Ap3216cPage;
using smartmonitor::FakeSample;
using smartmonitor::SensorService;
using smartmonitor::SensorWorker;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 必须在第一次跨线程 queued signal 发射前注册自定义值类型。
    qRegisterMetaType<FakeSample>("smartmonitor::FakeSample");

    SensorService sensorService;
    QThread sensorThread;
    SensorWorker *sensorWorker = new SensorWorker;
    Ap3216cController controller(&sensorService);
    Ap3216cPage page;

    sensorWorker->moveToThread(&sensorThread);

    QObject::connect(&sensorService, &SensorService::workerStartRequested,
                     sensorWorker, &SensorWorker::startSampling,
                     Qt::QueuedConnection);
    QObject::connect(&sensorService, &SensorService::workerStopRequested,
                     sensorWorker, &SensorWorker::stopSampling,
                     Qt::QueuedConnection);
    QObject::connect(sensorWorker, &SensorWorker::samplingStarted,
                     &sensorService, &SensorService::handleWorkerStarted,
                     Qt::QueuedConnection);
    QObject::connect(sensorWorker, &SensorWorker::sampleProduced,
                     &sensorService, &SensorService::handleWorkerSample,
                     Qt::QueuedConnection);
    QObject::connect(sensorWorker, &SensorWorker::samplingStopped,
                     &sensorService, &SensorService::handleWorkerStopped,
                     Qt::QueuedConnection);
    QObject::connect(&sensorThread, &QThread::finished,
                     sensorWorker, &QObject::deleteLater);

    QObject::connect(&page, &Ap3216cPage::startRequested,
                     &controller, &Ap3216cController::requestStart);
    QObject::connect(&page, &Ap3216cPage::stopRequested,
                     &controller, &Ap3216cController::requestStop);
    QObject::connect(&controller, &Ap3216cController::viewStateChanged,
                     &page, &Ap3216cPage::renderViewState);

    controller.publishCurrentViewState();

    page.resize(800, 480);
    page.setWindowTitle(QStringLiteral("AP3216C Worker Lab"));
    page.show();
    sensorThread.start();

    const int exitCode = app.exec();

    // Fake Worker 的槽函数有界且不阻塞。接入真实设备后，应由 CompositionRoot
    // 在窗口关闭前异步协调 stop，并为 I/O 和线程等待分别设置 deadline。
    if (sensorThread.isRunning())
    {
        QMetaObject::invokeMethod(sensorWorker, "stopSampling",
                                  Qt::BlockingQueuedConnection);
        sensorThread.quit();
        if (!sensorThread.wait(1000))
        {
            qWarning() << "Sensor thread did not stop within 1000 ms";
        }
    }

    return exitCode;
}
