#include "smart_monitor_engine.h"

namespace smartmonitor {

namespace {

bool isFresh(bool valid,
             std::int64_t updatedAtMs,
             std::int64_t nowMs,
             int staleMs)
{
    return valid
        && staleMs >= 0
        && updatedAtMs <= nowMs
        && nowMs - updatedAtMs <= staleMs;
}

} // namespace

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
    m_latestPresenceSample = PresenceSample();
    m_latestLuxSample = LuxSample();

    return makeDecision(TimerCommand::Cancel, TimerCommand::Cancel);
}

MonitorDecision MonitorEngine::stop()
{
    m_state.presenceState = PresenceState::Disabled;
    m_state.lightState = EnvLightState::Unknown;
    m_latestPresenceSample = PresenceSample();
    m_latestLuxSample = LuxSample();

    return makeDecision(TimerCommand::Cancel, TimerCommand::Cancel);
}

MonitorDecision MonitorEngine::onPresenceChanged(const PresenceSample &sample, std::int64_t nowMs)
{
    m_latestPresenceSample = sample;
    const bool fresh = isFresh(sample.valid,
                               sample.updateAtMs,
                               nowMs,
                               m_policy.presenceStaleMs);

    switch (m_state.presenceState) {
    case PresenceState::Disabled:
        break;

    case PresenceState::NoPerson:
        if (fresh && sample.present) {
            m_state.presenceState = PresenceState::PersonPending;
            return makeDecision(TimerCommand::Start, TimerCommand::Keep);
        }
        break;

    case PresenceState::PersonPending:
        if (!fresh || !sample.present) {
            m_state.presenceState = PresenceState::NoPerson;
            return makeDecision(TimerCommand::Cancel, TimerCommand::Keep);
        }
        break;

    case PresenceState::ActiveMonitoring:
        if (!fresh || !sample.present) {
            m_state.presenceState = PresenceState::Cooldown;
            return makeDecision(TimerCommand::Cancel, TimerCommand::Start);
        }
        break;

    case PresenceState::Cooldown:
        if (fresh && sample.present) {
            m_state.presenceState = PresenceState::ActiveMonitoring;
            return makeDecision(TimerCommand::Cancel, TimerCommand::Cancel);
        }
        break;
    }

    return makeDecision();
}

MonitorDecision MonitorEngine::onLuxChanged(const LuxSample &sample, std::int64_t nowMs)
{
    m_latestLuxSample = sample;
    const bool fresh = isFresh(sample.valid,
                               sample.updateAtMs,
                               nowMs,
                               m_policy.luxStaleMs);

    if (!fresh) {
        m_state.lightState = EnvLightState::Unknown;
        return makeDecision();
    }

    switch (m_state.lightState) {
    case EnvLightState::Unknown:
        m_state.lightState = sample.lux <= m_policy.darkEnterLux
            ? EnvLightState::Dark
            : EnvLightState::Normal;
        break;

    case EnvLightState::Normal:
        if (sample.lux <= m_policy.darkEnterLux) {
            m_state.lightState = EnvLightState::Dark;
        }
        break;

    case EnvLightState::Dark:
        if (sample.lux >= m_policy.darkExitLux) {
            m_state.lightState = EnvLightState::Normal;
        }
        break;
    }

    return makeDecision();
}

MonitorDecision MonitorEngine::onConfirmTimerExpired(std::int64_t nowMs)
{
    if (m_state.presenceState != PresenceState::PersonPending) {
        return makeDecision(TimerCommand::Cancel, TimerCommand::Keep);
    }

    const bool fresh = isFresh(m_latestPresenceSample.valid,
                               m_latestPresenceSample.updateAtMs,
                               nowMs,
                               m_policy.presenceStaleMs);
    if (fresh && m_latestPresenceSample.present) {
        m_state.presenceState = PresenceState::ActiveMonitoring;
    } else {
        m_state.presenceState = PresenceState::NoPerson;
    }

    return makeDecision(TimerCommand::Cancel, TimerCommand::Keep);
}

MonitorDecision MonitorEngine::onCooldownTimerExpired(std::int64_t nowMs)
{
    if (m_state.presenceState != PresenceState::Cooldown) {
        return makeDecision(TimerCommand::Keep, TimerCommand::Cancel);
    }

    const bool fresh = isFresh(m_latestPresenceSample.valid,
                               m_latestPresenceSample.updateAtMs,
                               nowMs,
                               m_policy.presenceStaleMs);
    if (fresh && m_latestPresenceSample.present) {
        m_state.presenceState = PresenceState::ActiveMonitoring;
    } else {
        m_state.presenceState = PresenceState::NoPerson;
    }

    return makeDecision(TimerCommand::Keep, TimerCommand::Cancel);
}

MonitorDecision MonitorEngine::makeDecision(TimerCommand confirmTimerCmd,
    TimerCommand cooldownTimerCmd
) const
{
    MonitorDecision decision;

    decision.persenceConfirmTimer = confirmTimerCmd;
    decision.presenceCooldownTimer = cooldownTimerCmd;
    // Determine if camera should be wanted
    decision.cameraWanted = (m_state.presenceState == PresenceState::ActiveMonitoring || m_state.presenceState == PresenceState::Cooldown);

    // Determine if recording should be wanted
    decision.recordingWanted = decision.cameraWanted;

    // Determine if torch should be wanted
    decision.torchWanted = decision.cameraWanted && (m_state.lightState == EnvLightState::Dark);

    return decision;
}

}
