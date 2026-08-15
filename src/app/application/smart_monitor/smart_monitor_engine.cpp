#include "smart_monitor_engine.h"

namespace smartmonitor {

MonitorEngine::MonitorEngine(const MonitorPolicy &policy)
    : m_policy(policy)
{}

MonitorEngine::~MonitorEngine() = default;

const MonitorState &MonitorEngine::state() const
{
    return m_state;
}

MonitorDecision MonitorEngine::start()
{
    m_state.presenceState = PresenceState::NoPerson;
    m_state.lightState = EnvLightState::Unknown;

    return makeDecision();
}

MonitorDecision MonitorEngine::makeDecision() const
{
    MonitorDecision decision;

    // Determine if camera should be wanted
    decision.cameraWanted = (m_state.presenceState == PresenceState::ActiveMonitoring || m_state.presenceState == PresenceState::Cooldown);

    // Determine if recording should be wanted
    decision.recordingWanted = decision.cameraWanted;

    // Determine if torch should be wanted
    decision.torchWanted = decision.cameraWanted && (m_state.lightState == EnvLightState::Dark);

    return decision;
}

}