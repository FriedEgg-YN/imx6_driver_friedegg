#include "camera_test_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace imx6sm {

CameraTestWindow::CameraTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("OV5640 Camera Test"), parent)
    , pathEdit(addPathEdit(QStringLiteral("Video"), QStringLiteral("/dev/video1")))
    , driverLabel(addRow(QStringLiteral("Driver")))
    , cardLabel(addRow(QStringLiteral("Card")))
    , busLabel(addRow(QStringLiteral("Bus")))
    , formatLabel(addRow(QStringLiteral("Formats")))
{
    QPushButton *query = addButton(QStringLiteral("Query"));
    QPushButton *start = addButton(QStringLiteral("Start"));
    QPushButton *stop = addButton(QStringLiteral("Stop"));
    QPushButton *snapshot = addButton(QStringLiteral("Snapshot"));
    QPushButton *torch = addButton(QStringLiteral("Torch"));

    start->setEnabled(false);
    stop->setEnabled(false);
    snapshot->setEnabled(false);
    torch->setEnabled(false);

    connect(query, &QPushButton::clicked, this, [this]() { queryCaps(); });
    queryCaps();
}

void CameraTestWindow::queryCaps()
{
    const CameraCaps caps = camera.queryCaps(pathEdit->text().trimmed());
    if (!caps.available) {
        setStatus(QStringLiteral("Unavailable"));
        driverLabel->setText(caps.error);
        cardLabel->setText(QStringLiteral("--"));
        busLabel->setText(QStringLiteral("--"));
        formatLabel->setText(QStringLiteral("--"));
        appendLog(caps.error);
        return;
    }

    QStringList formats;
    for (const CameraFormat &format : caps.formats)
        formats << QStringLiteral("%1 %2").arg(format.fourcc, format.description);

    setStatus(QStringLiteral("Ready"));
    driverLabel->setText(caps.driver);
    cardLabel->setText(caps.card);
    busLabel->setText(caps.busInfo);
    formatLabel->setText(formats.isEmpty() ? QStringLiteral("--") : formats.join(QStringLiteral(", ")));
    appendLog(QStringLiteral("VIDIOC_QUERYCAP ok, %1 format(s)").arg(caps.formats.size()));
}

} // namespace imx6sm
