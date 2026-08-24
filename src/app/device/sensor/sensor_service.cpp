#include "sensor_service.h"

namespace smartmonitor {

SensorService::SensorService(QObject *parent)
    : QObject(parent)
{
}

OperationResult SensorService::requestStartSampling()
{
    if (m_samplingState != SamplingState::Idle)
    {
        return {OperationCode::Busy,
                QStringLiteral("Sampling is already active")};
    }

    m_samplingState = SamplingState::Starting;
    emit workerStartRequested();
    return {OperationCode::Accepted, QString()};
}

OperationResult SensorService::requestStopSampling()
{
    if (m_samplingState == SamplingState::Idle)
    {
        return {OperationCode::Accepted, QString()};
    }

    if (m_samplingState == SamplingState::Stopping)
    {
        return {OperationCode::Busy,
                QStringLiteral("Sampling stop is already pending")};
    }

    m_samplingState = SamplingState::Stopping;
    emit workerStopRequested();
    return {OperationCode::Accepted, QString()};
}

void SensorService::handleWorkerStarted()
{
    if (m_samplingState != SamplingState::Starting)
    {
        return;
    }

    m_samplingState = SamplingState::Running;
    emit samplingStarted();
}

void SensorService::handleWorkerSample(const Ap3216cSample &sample)
{
    if (m_samplingState == SamplingState::Running)
    {
        emit sampleUpdated(sample);
    }
}

void SensorService::handleWorkerStopped()
{
    if (m_samplingState == SamplingState::Idle)
    {
        return;
    }

    m_samplingState = SamplingState::Idle;
    emit samplingStopped();
}

} // namespace smartmonitor
