#include "storage_test_window.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace imx6sm {

StorageTestWindow::StorageTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("Storage Test"), parent)
    , rootEdit(addPathEdit(QStringLiteral("Root"), QStringLiteral("/smart-monitor")))
    , resultLabel(addRow(QStringLiteral("Result")))
    , sessionLabel(addRow(QStringLiteral("Session")))
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
        sessionLabel->setText(QStringLiteral("--"));
        appendLog(result.error);
        return;
    }

    setStatus(QStringLiteral("Ready"));
    resultLabel->setText(QStringLiteral("ok"));
    sessionLabel->setText(result.sessionPath);
    appendLog(QStringLiteral("storage check wrote test session"));
}

} // namespace imx6sm
