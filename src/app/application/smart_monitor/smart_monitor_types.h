#ifndef SMART_MONITOR_TYPES_H
#define SMART_MONITOR_TYPES_H

#include <cstdint>

namespace smartmonitor {

enum class PresenceState {
    Disabled,
    NoPerson,
    PersonPending,
    ActiveMonitoring,
    Cooldown,
};

enum class EnvLightState {
    Unknown,
    Normal,
    Dark,
};

enum class TimerCommand {
    Keep,       // Keep the current timer state
    Start,      // (re)Start the timer
    Cancel,     // Cancel and clear the timer
};

struct MonitorPolicy {
    double darkEnterLux = 20.0;
    double darkExitLux = 40.0;

    int presenceConfirmMs = 300;
    int presenceCooldownMs = 15000;

    // Stale sample thresholds, if a sample is older than this, it is considered stale and will be ignored
    int presenceStaleMs = 1500;
    int luxStaleMs = 1500;
};

struct PresenceSample {
    bool valid = false;
    bool present = false;
    std::int64_t updateAtMs = 0;
};

struct LuxSample {
    bool valid = false;
    double lux = 0.0;
    std::int64_t updateAtMs = 0;
};

struct MonitorState {
    PresenceState presenceState = PresenceState::Disabled;
    EnvLightState lightState = EnvLightState::Unknown;
};

struct MonitorDecision {
    bool cameraWanted = false;
    bool recordingWanted = false;
    bool torchWanted = false;

    TimerCommand persenceConfirmTimer = TimerCommand::Keep;
    TimerCommand presenceCooldownTimer = TimerCommand::Keep;
};

} // namespace smartmonitor

#endif
