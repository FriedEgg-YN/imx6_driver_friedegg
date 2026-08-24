#include "composition_root.h"

#include "application/ap3216c/ap3216c_controller.h"
#include "application/ap3216c/ap3216c_page.h"
#include "device/sensor/sensor_service.h"
#include "device/sensor/sensor_worker.h"

#include <QMetaObject>
#include <QMetaType>
#include <QThread>

namespace smartmonitor {

CompositionRoot::CompositionRoot(QObject *parent)
    : QObject(parent),
      m_sensorService(new SensorService(this)),
      m_sensorThread(new QThread(this)),
      m_sensorWorker(new SensorWorker),
      m_controller(new Ap3216cController(m_sensorService, this)),
      m_page(new Ap3216cPage)
{
    qRegisterMetaType<Ap3216cSample>("smartmonitor::Ap3216cSample");

    m_sensorWorker->moveToThread(m_sensorThread);

    connect(m_sensorService, &SensorService::workerStartRequested,
            m_sensorWorker, &SensorWorker::startSampling,
            Qt::QueuedConnection);
    connect(m_sensorService, &SensorService::workerStopRequested,
            m_sensorWorker, &SensorWorker::stopSampling,
            Qt::QueuedConnection);
    connect(m_sensorWorker, &SensorWorker::samplingStarted,
            m_sensorService, &SensorService::handleWorkerStarted,
            Qt::QueuedConnection);
    connect(m_sensorWorker, &SensorWorker::sampleProduced,
            m_sensorService, &SensorService::handleWorkerSample,
            Qt::QueuedConnection);
    connect(m_sensorWorker, &SensorWorker::samplingStopped,
            m_sensorService, &SensorService::handleWorkerStopped,
            Qt::QueuedConnection);
    connect(m_page, &Ap3216cPage::startRequested,
            m_controller, &Ap3216cController::requestStart);
    connect(m_page, &Ap3216cPage::stopRequested,
            m_controller, &Ap3216cController::requestStop);
    connect(m_controller, &Ap3216cController::viewStateChanged,
            m_page, &Ap3216cPage::renderViewState);
}

CompositionRoot::~CompositionRoot()
{
    if (m_sensorThread->isRunning()) {
        QMetaObject::invokeMethod(m_sensorWorker, "stopSampling",
                                  Qt::BlockingQueuedConnection);
        m_sensorThread->quit();
        m_sensorThread->wait(1000);
    }

    delete m_sensorWorker;
    delete m_page;
}

void CompositionRoot::start()
{
    m_controller->publishCurrentViewState();
    m_page->resize(800, 480);
    m_page->setWindowTitle(QStringLiteral("AP3216C Worker Lab"));
    m_page->show();
    m_sensorThread->start();
}

} // namespace smartmonitor
