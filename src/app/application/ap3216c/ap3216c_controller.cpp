#include "ap3216c_controller.h"

#include <QtGlobal>

namespace smartmonitor {

Ap3216cController::Ap3216cController(SensorService *sensorService,
                                     QObject *parent)
    : QObject(parent), m_sensorService(sensorService)
{
    Q_ASSERT(m_sensorService);

    connect(m_sensorService, &SensorService::samplingStarted,
            this, &Ap3216cController::handleSamplingStarted);
    connect(m_sensorService, &SensorService::sampleUpdated,
            this, &Ap3216cController::handleSampleUpdated);
    connect(m_sensorService, &SensorService::samplingStopped,
            this, &Ap3216cController::handleSamplingStopped);
}

void Ap3216cController::publishCurrentViewState()
{
    publishViewState();
}

void Ap3216cController::requestStart()
{
    const OperationResult result = m_sensorService->requestStartSampling();

    if (result.code == OperationCode::Accepted)
    {
        m_viewState.samplingState = SamplingState::Starting;
        m_viewState.status = QStringLiteral("Starting");
    }
    else
    {
        m_viewState.status = result.error;
    }

    publishViewState();
}

void Ap3216cController::requestStop()
{
    const OperationResult result = m_sensorService->requestStopSampling();

    if (result.code == OperationCode::Accepted)
    {
        if (m_viewState.samplingState != SamplingState::Idle)
        {
            m_viewState.samplingState = SamplingState::Stopping;
            m_viewState.status = QStringLiteral("Stopping");
        }
    }
    else
    {
        m_viewState.status = result.error;
    }

    publishViewState();
}

void Ap3216cController::handleSamplingStarted()
{
    m_viewState.samplingState = SamplingState::Running;
    m_viewState.status = QStringLiteral("Running");
    publishViewState();
}

void Ap3216cController::handleSampleUpdated(const FakeSample &sample)
{
    m_viewState.hasSample = true;
    m_viewState.sample = sample;

    if (sample.valid)
    {
        m_viewState.status = QStringLiteral("Available");
    }
    else
    {
        m_viewState.status = QStringLiteral("Unavailable: ") + sample.error;
    }

    publishViewState();
}

void Ap3216cController::handleSamplingStopped()
{
    m_viewState.samplingState = SamplingState::Idle;
    m_viewState.status = QStringLiteral("Stopped");
    publishViewState();
}

void Ap3216cController::publishViewState()
{
    emit viewStateChanged(m_viewState);
}

} // namespace smartmonitor
