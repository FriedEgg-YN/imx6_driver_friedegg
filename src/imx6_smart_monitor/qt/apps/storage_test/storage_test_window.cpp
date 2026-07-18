#include "storage_test_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace imx6sm {

StorageTestWindow::StorageTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("Storage Test"), parent)
    , rootEdit(addPathEdit(QStringLiteral("Root"), QStringLiteral("/smart-monitor")))
    , resultLabel(addRow(QStringLiteral("Result")))
    , framesLabel(addRow(QStringLiteral("Frames")))
    , videosLabel(addRow(QStringLiteral("Videos")))
{
    QPushButton *check = addButton(QStringLiteral("Check"));
    connect(check, &QPushButton::clicked, this, [this]() { checkRoot(); });
}

void StorageTestWindow::checkRoot()
{
    const StorageCheckResult result = storage.checkRoot(rootEdit->text().trimmed());
    if (!result.ok) {
        setStatus(QStringLiteral("Failed"));
        resultLabel->setText(result.error);
        framesLabel->setText(QStringLiteral("--"));
        videosLabel->setText(QStringLiteral("--"));
        appendLog(result.error);
        return;
    }

    setStatus(QStringLiteral("Ready"));
    resultLabel->setText(QStringLiteral("ok"));
    framesLabel->setText(result.framesPath);
    videosLabel->setText(result.videosPath);
    appendLog(QStringLiteral("storage check created frames/videos"));
}

} // namespace imx6sm
