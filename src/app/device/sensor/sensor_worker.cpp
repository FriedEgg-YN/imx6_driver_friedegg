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

    ++m_sequence;

    FakeSample sample;
    sample.updatedAtMs = QDateTime::currentMSecsSinceEpoch();

    if (m_sequence % 5 == 0)
    {
        sample.valid = false;
        sample.error = QStringLiteral("Simulated sensor error");
    }
    else
    {
        sample.valid = true;
        sample.value = m_sequence;
    }

    emit sampleProduced(sample);
}

} // namespace smartmonitor
