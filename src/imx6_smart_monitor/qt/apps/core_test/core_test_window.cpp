#include "core_test_window.h"

#include <QLabel>
#include <QPushButton>

namespace imx6sm {

CoreTestWindow::CoreTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("MonitorCore Test"), parent)
    , presenceLabel(addRow(QStringLiteral("Presence")))
    , lightLabel(addRow(QStringLiteral("Light")))
    , cameraLabel(addRow(QStringLiteral("Camera")))
    , storageLabel(addRow(QStringLiteral("Storage")))
    , torchLabel(addRow(QStringLiteral("Torch")))
    , actionLabel(addRow(QStringLiteral("Action")))
{
    QPushButton *person = addButton(QStringLiteral("Person"));
    QPushButton *confirm = addButton(QStringLiteral("Confirm"));
    QPushButton *noPerson = addButton(QStringLiteral("No Person"));
    QPushButton *cooldown = addButton(QStringLiteral("Cooldown"));
    QPushButton *dark = addButton(QStringLiteral("Dark"));
    QPushButton *bright = addButton(QStringLiteral("Bright"));
    QPushButton *reset = addButton(QStringLiteral("Reset"));

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
        refresh();
    });

    refresh();
}

void CoreTestWindow::refresh()
{
    const MonitorSnapshot state = core.snapshot();
    presenceLabel->setText(toString(state.presence));
    lightLabel->setText(toString(state.light));
    cameraLabel->setText(toString(state.camera));
    storageLabel->setText(toString(state.storage));
    torchLabel->setText(state.torchWanted ? QStringLiteral("on") : QStringLiteral("off"));
    actionLabel->setText(state.lastAction.isEmpty() ? QStringLiteral("--") : state.lastAction);
    setStatus(toString(state.presence));
}

} // namespace imx6sm
