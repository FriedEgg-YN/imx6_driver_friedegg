#include "smart_monitor_engine.h"

#include <iostream>

using namespace smartmonitor;

namespace {

int failures = 0;

#define CHECK(expression)                                                     \
    do {                                                                      \
        if (!(expression)) {                                                  \
            std::cerr << __func__ << ':' << __LINE__                         \
                      << ": check failed: " #expression << '\n';             \
            ++failures;                                                       \
            return;                                                           \
        }                                                                     \
    } while (false)

MonitorPolicy testPolicy()
{
    MonitorPolicy policy;
    policy.darkEnterLux = 20.0;
    policy.darkExitLux = 40.0;
    policy.presenceConfirmMs = 300;
    policy.presenceCooldownMs = 15000;
    policy.presenceStaleMs = 1500;
    policy.luxStaleMs = 1500;
    return policy;
}

PresenceSample presenceAt(bool present, std::int64_t updatedAtMs)
{
    PresenceSample sample;
    sample.valid = true;
    sample.present = present;
    sample.updateAtMs = updatedAtMs;
    return sample;
}

PresenceSample invalidPresenceAt(std::int64_t updatedAtMs)
{
    PresenceSample sample;
    sample.valid = false;
    sample.updateAtMs = updatedAtMs;
    return sample;
}

LuxSample luxAt(double lux, std::int64_t updatedAtMs)
{
    LuxSample sample;
    sample.valid = true;
    sample.lux = lux;
    sample.updateAtMs = updatedAtMs;
    return sample;
}

LuxSample invalidLuxAt(std::int64_t updatedAtMs)
{
    LuxSample sample;
    sample.valid = false;
    sample.updateAtMs = updatedAtMs;
    return sample;
}

MonitorDecision enterActive(MonitorEngine &engine,
                             const MonitorPolicy &policy,
                             std::int64_t sampleTimeMs)
{
    engine.start();
    engine.onPresenceChanged(presenceAt(true, sampleTimeMs), sampleTimeMs);
    return engine.onConfirmTimerExpired(sampleTimeMs + policy.presenceConfirmMs);
}

void default_and_start_are_idle()
{
    MonitorEngine engine{testPolicy()};
    CHECK(engine.state().presenceState == PresenceState::Disabled);
    CHECK(engine.state().lightState == EnvLightState::Unknown);

    const MonitorDecision decision = engine.start();
    CHECK(engine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);
    CHECK(!decision.cameraWanted);
    CHECK(!decision.recordingWanted);
    CHECK(!decision.torchWanted);
}

void pending_is_cancelled_by_absence()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();

    MonitorDecision decision =
        engine.onPresenceChanged(presenceAt(true, 100), 100);
    CHECK(engine.state().presenceState == PresenceState::PersonPending);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Start);
    CHECK(!decision.cameraWanted);

    decision = engine.onPresenceChanged(presenceAt(false, 200), 200);
    CHECK(engine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(!decision.cameraWanted);
}

void repeated_presence_does_not_restart_confirm()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();
    engine.onPresenceChanged(presenceAt(true, 100), 100);

    const MonitorDecision decision =
        engine.onPresenceChanged(presenceAt(true, 200), 200);
    CHECK(engine.state().presenceState == PresenceState::PersonPending);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Keep);
}

void confirm_requires_fresh_present_sample()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();
    engine.onPresenceChanged(presenceAt(true, 100), 100);

    MonitorDecision decision =
        engine.onConfirmTimerExpired(100 + policy.presenceConfirmMs);
    CHECK(engine.state().presenceState == PresenceState::ActiveMonitoring);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(decision.cameraWanted);
    CHECK(decision.recordingWanted);

    MonitorEngine staleEngine{policy};
    staleEngine.start();
    staleEngine.onPresenceChanged(presenceAt(true, 100), 100);
    decision = staleEngine.onConfirmTimerExpired(
        100 + policy.presenceStaleMs + 1);
    CHECK(staleEngine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(!decision.cameraWanted);
}

void active_absence_enters_cooldown()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    enterActive(engine, policy, 100);

    const MonitorDecision decision =
        engine.onPresenceChanged(presenceAt(false, 500), 500);
    CHECK(engine.state().presenceState == PresenceState::Cooldown);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Start);
    CHECK(decision.cameraWanted);
    CHECK(decision.recordingWanted);
}

void cooldown_return_keeps_camera_and_recording()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    enterActive(engine, policy, 100);
    engine.onPresenceChanged(presenceAt(false, 500), 500);

    MonitorDecision decision =
        engine.onPresenceChanged(presenceAt(false, 600), 600);
    CHECK(engine.state().presenceState == PresenceState::Cooldown);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Keep);

    decision = engine.onPresenceChanged(presenceAt(true, 700), 700);
    CHECK(engine.state().presenceState == PresenceState::ActiveMonitoring);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);
    CHECK(decision.cameraWanted);
    CHECK(decision.recordingWanted);
}

void cooldown_timeout_returns_to_no_person()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    enterActive(engine, policy, 100);
    engine.onPresenceChanged(presenceAt(false, 500), 500);

    engine.onPresenceChanged(
        presenceAt(false, 500 + policy.presenceCooldownMs - 1),
        500 + policy.presenceCooldownMs - 1);
    const MonitorDecision decision = engine.onCooldownTimerExpired(
        500 + policy.presenceCooldownMs);
    CHECK(engine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);
    CHECK(!decision.cameraWanted);
    CHECK(!decision.recordingWanted);
}

void unknown_presence_uses_fail_safe_transitions()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine pendingEngine{policy};
    pendingEngine.start();
    pendingEngine.onPresenceChanged(presenceAt(true, 100), 100);
    MonitorDecision decision =
        pendingEngine.onPresenceChanged(invalidPresenceAt(200), 200);
    CHECK(pendingEngine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);

    MonitorEngine activeEngine{policy};
    enterActive(activeEngine, policy, 100);
    decision = activeEngine.onPresenceChanged(invalidPresenceAt(500), 500);
    CHECK(activeEngine.state().presenceState == PresenceState::Cooldown);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Start);

    MonitorEngine staleEngine{policy};
    enterActive(staleEngine, policy, 100);
    decision = staleEngine.onPresenceChanged(
        presenceAt(true, 100), 100 + policy.presenceStaleMs + 1);
    CHECK(staleEngine.state().presenceState == PresenceState::Cooldown);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Start);
}

void freshness_boundaries_are_explicit()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine boundaryEngine{policy};
    boundaryEngine.start();
    MonitorDecision decision =
        boundaryEngine.onPresenceChanged(
            presenceAt(true, 100), 100 + policy.presenceStaleMs);
    CHECK(boundaryEngine.state().presenceState == PresenceState::PersonPending);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Start);

    MonitorEngine futureEngine{policy};
    futureEngine.start();
    decision = futureEngine.onPresenceChanged(presenceAt(true, 101), 100);
    CHECK(futureEngine.state().presenceState == PresenceState::NoPerson);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Keep);
}

void light_thresholds_are_hysteretic()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();

    engine.onLuxChanged(luxAt(30.0, 100), 100);
    CHECK(engine.state().lightState == EnvLightState::Normal);

    engine.onLuxChanged(luxAt(20.0, 200), 200);
    CHECK(engine.state().lightState == EnvLightState::Dark);

    engine.onLuxChanged(luxAt(30.0, 300), 300);
    CHECK(engine.state().lightState == EnvLightState::Dark);

    engine.onLuxChanged(luxAt(40.0, 400), 400);
    CHECK(engine.state().lightState == EnvLightState::Normal);

    engine.onLuxChanged(invalidLuxAt(500), 500);
    CHECK(engine.state().lightState == EnvLightState::Unknown);
}

void torch_requires_dark_active_window()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();

    MonitorDecision decision = engine.onLuxChanged(luxAt(10.0, 100), 100);
    CHECK(engine.state().lightState == EnvLightState::Dark);
    CHECK(!decision.torchWanted);

    engine.onPresenceChanged(presenceAt(true, 200), 200);
    decision = engine.onConfirmTimerExpired(200 + policy.presenceConfirmMs);
    CHECK(engine.state().presenceState == PresenceState::ActiveMonitoring);
    CHECK(decision.torchWanted);

    decision = engine.onPresenceChanged(presenceAt(false, 600), 600);
    CHECK(engine.state().presenceState == PresenceState::Cooldown);
    CHECK(decision.torchWanted);

    decision = engine.stop();
    CHECK(!decision.cameraWanted);
    CHECK(!decision.recordingWanted);
    CHECK(!decision.torchWanted);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);
}

void stop_clears_every_presence_state()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine disabled{policy};
    MonitorDecision decision = disabled.stop();
    CHECK(disabled.state().presenceState == PresenceState::Disabled);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);

    MonitorEngine noPerson{policy};
    noPerson.start();
    decision = noPerson.stop();
    CHECK(noPerson.state().presenceState == PresenceState::Disabled);

    MonitorEngine pending{policy};
    pending.start();
    pending.onPresenceChanged(presenceAt(true, 100), 100);
    decision = pending.stop();
    CHECK(pending.state().presenceState == PresenceState::Disabled);
    CHECK(decision.persenceConfirmTimer == TimerCommand::Cancel);

    MonitorEngine active{policy};
    enterActive(active, policy, 100);
    decision = active.stop();
    CHECK(active.state().presenceState == PresenceState::Disabled);
    CHECK(!decision.cameraWanted);
    CHECK(!decision.recordingWanted);

    MonitorEngine cooldown{policy};
    enterActive(cooldown, policy, 100);
    cooldown.onPresenceChanged(presenceAt(false, 500), 500);
    decision = cooldown.stop();
    CHECK(cooldown.state().presenceState == PresenceState::Disabled);
    CHECK(decision.presenceCooldownTimer == TimerCommand::Cancel);
    CHECK(!decision.cameraWanted);
}

void lux_at_stale_edge()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();

    MonitorDecision decision = engine.onPresenceChanged(presenceAt(true, 1000), 1000);
    CHECK(engine.state().presenceState == PresenceState::PersonPending);
    engine.onConfirmTimerExpired(1000 + policy.presenceConfirmMs);
    decision = engine.onLuxChanged(
        luxAt(41.0, 1000), 1000 + policy.luxStaleMs);
    CHECK(engine.state().lightState == EnvLightState::Normal);
    CHECK(decision.torchWanted == false);
    decision = engine.onLuxChanged(
        luxAt(19.0, 1001), 1001 + policy.luxStaleMs);
    CHECK(engine.state().lightState == EnvLightState::Dark);
    CHECK(decision.torchWanted == true);
}

void lux_out_stale_edge()
{
    const MonitorPolicy policy = testPolicy();
    MonitorEngine engine{policy};
    engine.start();

    MonitorDecision decision = engine.onPresenceChanged(presenceAt(true, 1000), 1000);
    CHECK(engine.state().presenceState == PresenceState::PersonPending);
    engine.onConfirmTimerExpired(1000 + policy.presenceConfirmMs);
    decision = engine.onLuxChanged(
        luxAt(41.0, 1000), 1000 + policy.luxStaleMs + 1);
    CHECK(engine.state().lightState == EnvLightState::Unknown);
    CHECK(decision.torchWanted == false);
    decision = engine.onLuxChanged(
        luxAt(19.0, 1001), 1001 + policy.luxStaleMs + 1);
    CHECK(engine.state().lightState == EnvLightState::Unknown);
    CHECK(decision.torchWanted == false);
}

} // namespace

int main()
{
    default_and_start_are_idle();
    pending_is_cancelled_by_absence();
    repeated_presence_does_not_restart_confirm();
    confirm_requires_fresh_present_sample();
    active_absence_enters_cooldown();
    cooldown_return_keeps_camera_and_recording();
    cooldown_timeout_returns_to_no_person();
    unknown_presence_uses_fail_safe_transitions();
    freshness_boundaries_are_explicit();
    light_thresholds_are_hysteretic();
    torch_requires_dark_active_window();
    stop_clears_every_presence_state();
    lux_at_stale_edge();
    lux_out_stale_edge();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All MonitorEngine tests passed\n";
    return 0;
}
