#include "core_test_window.h"

#include <QLabel>
#include <QPushButton>

namespace imx6sm {

CoreTestWindow::CoreTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("MonitorCore Test"), parent)
    , monitoringLabel(addRow(QStringLiteral("Monitoring")))
    , presenceLabel(addRow(QStringLiteral("Presence")))
    , lightLabel(addRow(QStringLiteral("Light")))
    , cameraLabel(addRow(QStringLiteral("Camera")))
    , storageLabel(addRow(QStringLiteral("Storage")))
    , torchLabel(addRow(QStringLiteral("Torch")))
    , wantedLabel(addRow(QStringLiteral("Wanted")))
    , recordingLabel(addRow(QStringLiteral("Recording")))
    , errorLabel(addRow(QStringLiteral("Error")))
    , actionLabel(addRow(QStringLiteral("Action")))
{
    QPushButton *start = addButton(QStringLiteral("Start"));
    QPushButton *stop = addButton(QStringLiteral("Stop"));
    QPushButton *person = addButton(QStringLiteral("Person"));
    QPushButton *confirm = addButton(QStringLiteral("Confirm"));
    QPushButton *noPerson = addButton(QStringLiteral("No Person"));
    QPushButton *cooldown = addButton(QStringLiteral("Cooldown"));
    QPushButton *dark = addButton(QStringLiteral("Dark"));
    QPushButton *bright = addButton(QStringLiteral("Bright"));
    QPushButton *reset = addButton(QStringLiteral("Reset"));

    connect(start, &QPushButton::clicked, this, [this]() {
        core.startMonitoring();
        refresh();
    });
    connect(stop, &QPushButton::clicked, this, [this]() {
        core.stopMonitoring();
        refresh();
    });
    connect(person, &QPushButton::clicked, this, [this]() {
        core.handlePresence(true);
        refresh();
    });
    connect(confirm, &QPushButton::clicked, this, [this]() {
        core.confirmPresenceTimeout();
        refresh();
    });
    connect(noPerson, &QPushButton::clicked, this, [this]() {
        core.handlePresence(false);
        refresh();
    });
    connect(cooldown, &QPushButton::clicked, this, [this]() {
        core.cooldownTimeout();
        refresh();
    });
    connect(dark, &QPushButton::clicked, this, [this]() {
        core.handleLux(10.0);
        refresh();
    });
    connect(bright, &QPushButton::clicked, this, [this]() {
        core.handleLux(80.0);
        refresh();
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        core.reset();
        core.startMonitoring();
        refresh();
    });

    core.startMonitoring();
    refresh();
}

void CoreTestWindow::refresh()
{
    const MonitorSnapshot state = core.snapshot();
    monitoringLabel->setText(state.monitoringEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    presenceLabel->setText(toString(state.presence));
    lightLabel->setText(state.strobe_on ? QStringLiteral("on") : QStringLiteral("off"));
    cameraLabel->setText(toString(state.camera));
    storageLabel->setText(toString(state.storage));
    torchLabel->setText(state.strobe_on ? QStringLiteral("on") : QStringLiteral("off"));
    wantedLabel->setText(QStringLiteral("camera:%1 record:%2 torch:%3")
                         .arg(state.cameraWanted ? QStringLiteral("yes") : QStringLiteral("no"),
                              state.recordingWanted ? QStringLiteral("yes") : QStringLiteral("no"),
                               state.strobe_on ? QStringLiteral("yes") : QStringLiteral("no")));
    recordingLabel->setText(state.recordingStatus.isEmpty() ? QStringLiteral("--") : state.recordingStatus);
    errorLabel->setText((state.cameraError + QStringLiteral(" ") + state.storageError).trimmed().isEmpty() ? QStringLiteral("--") : (state.cameraError + QStringLiteral(" ") + state.storageError).trimmed());
    actionLabel->setText(state.lastAction.isEmpty() ? QStringLiteral("--") : state.lastAction);
    setStatus(toString(state.presence));
}

} // namespace imx6sm
