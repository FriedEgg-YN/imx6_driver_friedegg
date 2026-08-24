#ifndef SMART_MONITOR_ENGINE_H
#define SMART_MONITOR_ENGINE_H

#include "smart_monitor_types.h"

namespace smartmonitor {

class MonitorEngine {
public:
    explicit MonitorEngine(const MonitorPolicy &policy = MonitorPolicy());
    ~MonitorEngine();

    const MonitorState &state() const;

    MonitorDecision start();
    MonitorDecision stop();

    MonitorDecision onPresenceChanged(const PresenceSample &sample, std::int64_t nowMs);
    MonitorDecision onLuxChanged(const LuxSample &sample, std::int64_t nowMs);

    MonitorDecision onConfirmTimerExpired(std::int64_t nowMs);
    MonitorDecision onCooldownTimerExpired(std::int64_t nowMs);

private:
    // make decision based on current state
    MonitorDecision makeDecision(
        TimerCommand confirmTimerCmd = TimerCommand::Keep,
        TimerCommand cooldownTimerCmd = TimerCommand::Keep
    ) const;

    MonitorPolicy m_policy;
    MonitorState m_state;
    PresenceSample m_latestPresenceSample;
    LuxSample m_latestLuxSample;
};


} // namespace smartmonitor

#endif // SMART_MONITOR_ENGINE_H