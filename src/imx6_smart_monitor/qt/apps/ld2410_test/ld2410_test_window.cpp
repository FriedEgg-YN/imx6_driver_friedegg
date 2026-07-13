#include "ld2410_test_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace imx6sm {

Ld2410TestWindow::Ld2410TestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("LD2410C Test"), parent)
    , outEdit(addPathEdit(QStringLiteral("OUT"), QStringLiteral("auto")))
    , uartEdit(addPathEdit(QStringLiteral("UART"), QStringLiteral("/dev/ttymxcX")))
    , hintLabel(addRow(QStringLiteral("Input hint")))
    , outLabel(addRow(QStringLiteral("OUT node")))
    , uartLabel(addRow(QStringLiteral("UART node")))
{
    QPushButton *probeButton = addButton(QStringLiteral("Probe"));
    QPushButton *frameButton = addButton(QStringLiteral("Frame"));
    frameButton->setEnabled(false);

    connect(probeButton, &QPushButton::clicked, this, [this]() { probe(); });
    probe();
}

void Ld2410TestWindow::probe()
{
    const Ld2410Probe result = device.probe(outEdit->text().trimmed(), uartEdit->text().trimmed());
    hintLabel->setText(result.inputHint.isEmpty() ? QStringLiteral("--") : result.inputHint);
    outLabel->setText(QStringLiteral("%1  %2")
                          .arg(result.outPath.isEmpty() ? QStringLiteral("--") : result.outPath,
                               result.outAvailable ? QStringLiteral("ok") : QStringLiteral("missing")));
    uartLabel->setText(QStringLiteral("%1  %2")
                           .arg(result.uartPath.isEmpty() ? QStringLiteral("--") : result.uartPath,
                                result.uartAvailable ? QStringLiteral("ok") : QStringLiteral("missing")));

    if (result.outAvailable || result.uartAvailable) {
        setStatus(QStringLiteral("Ready"));
        appendLog(QStringLiteral("LD2410C node probe finished"));
    } else {
        setStatus(QStringLiteral("Unavailable"));
        appendLog(result.error);
    }
}

} // namespace imx6sm
