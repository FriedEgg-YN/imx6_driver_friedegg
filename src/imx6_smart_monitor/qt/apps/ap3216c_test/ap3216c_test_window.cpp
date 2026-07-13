#include "ap3216c_test_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>

namespace imx6sm {

Ap3216cTestWindow::Ap3216cTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("AP3216C Test"), parent)
    , pathEdit(addPathEdit(QStringLiteral("IIO"), QStringLiteral("auto")))
    , deviceLabel(addRow(QStringLiteral("Device")))
    , luxLabel(addRow(QStringLiteral("Lux")))
    , alsLabel(addRow(QStringLiteral("ALS raw")))
    , irLabel(addRow(QStringLiteral("IR raw")))
    , psLabel(addRow(QStringLiteral("PS raw")))
    , timer(new QTimer(this))
{
    QPushButton *sample = addButton(QStringLiteral("Sample"));
    QPushButton *start = addButton(QStringLiteral("Start"));
    QPushButton *stop = addButton(QStringLiteral("Stop"));

    connect(sample, &QPushButton::clicked, this, [this]() { readOnce(); });
    connect(start, &QPushButton::clicked, this, [this]() {
        timer->start(1000);
        readOnce();
        setStatus(QStringLiteral("Polling"));
    });
    connect(stop, &QPushButton::clicked, this, [this]() {
        timer->stop();
        setStatus(QStringLiteral("Stopped"));
    });
    connect(timer, &QTimer::timeout, this, [this]() { readOnce(); });

    readOnce();
}

QString Ap3216cTestWindow::preferredPath() const
{
    const QString text = pathEdit->text().trimmed();
    return text == QStringLiteral("auto") ? QString() : text;
}

void Ap3216cTestWindow::readOnce()
{
    const Ap3216cSample sample = device.readSample(preferredPath());
    if (!sample.available) {
        setStatus(QStringLiteral("Unavailable"));
        deviceLabel->setText(sample.error);
        appendLog(sample.error);
        return;
    }

    setStatus(QStringLiteral("Ready"));
    deviceLabel->setText(sample.sysfsPath);
    luxLabel->setText(sample.hasLux ? QString::number(sample.lux, 'f', 2) : QStringLiteral("--"));
    alsLabel->setText(sample.alsRaw >= 0 ? QString::number(sample.alsRaw) : QStringLiteral("--"));
    irLabel->setText(sample.irRaw >= 0 ? QString::number(sample.irRaw) : QStringLiteral("--"));
    psLabel->setText(sample.proximityRaw >= 0 ? QString::number(sample.proximityRaw) : QStringLiteral("--"));
}

} // namespace imx6sm
