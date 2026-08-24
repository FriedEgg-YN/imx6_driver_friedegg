#include "sensor_worker.h"

#include <QDateTime>
#include <QThread>
#include <QTimer>

namespace smartmonitor {

SensorWorker::SensorWorker(QObject *parent)
    : QObject(parent)
{
}

void SensorWorker::startSampling()
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (!m_timer)
    {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &SensorWorker::sampleOnce);
    }

    if (!m_timer->isActive())
    {
        m_timer->start(500);
    }

    if (!m_ap3216cBackend)
    {
        m_ap3216cBackend.reset(new Ap3216cBackend());
    }

    emit samplingStarted();
}

void SensorWorker::stopSampling()
{
    Q_ASSERT(QThread::currentThread() == thread());

    if (m_timer && m_timer->isActive())
    {
        m_timer->stop();
    }

    emit samplingStopped();
}

void SensorWorker::sampleOnce()
{
    Q_ASSERT(QThread::currentThread() == thread());

    Ap3216cSample sample;

    if (!m_ap3216cBackend)
    {
        sample.error = QStringLiteral("Ap3216c Backend not initialized");
    }

    sample = m_ap3216cBackend->readSample();
    sample.timestamp = QDateTime::currentMSecsSinceEpoch();

    emit sampleProduced(sample);
}

} // namespace smartmonitor
