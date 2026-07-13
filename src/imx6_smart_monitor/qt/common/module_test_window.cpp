#include "qt/common/module_test_window.h"

#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace imx6sm {

ModuleTestWindow::ModuleTestWindow(const QString &title, QWidget *parent)
    : QWidget(parent)
    , statusLabel(new QLabel(QStringLiteral("Idle"), this))
    , headerButtonLayout(new QHBoxLayout)
    , formLayout(new QFormLayout)
    , buttonLayout(new QHBoxLayout)
    , logEdit(new QTextEdit(this))
    , mainContentLayout(new QVBoxLayout)
{
    setMinimumSize(480, 272);
    setWindowTitle(title);
    setStyleSheet(QStringLiteral(
        "QWidget{background:#111820;color:#edf3f5;font-family:DejaVu Sans;}"
        "QLabel#title{font-size:22px;font-weight:700;color:#ffffff;}"
        "QLabel#status{font-size:14px;color:#b8c7cf;}"
        "QLabel#field{font-size:15px;color:#edf3f5;}"
        "QLineEdit{font-size:15px;padding:8px;border:1px solid #344550;border-radius:4px;background:#17232b;color:#edf3f5;}"
        "QPushButton{font-size:15px;padding:10px 12px;border:0;border-radius:4px;background:#1f6f8b;color:white;}"
        "QPushButton:pressed{background:#2d88aa;}"
        "QPushButton:disabled{background:#31414a;color:#7c8990;}"
        "QPushButton#headerButton{font-size:14px;padding:7px 10px;border:1px solid #385064;border-radius:4px;background:#17232b;color:#edf3f5;}"
        "QPushButton#headerButton:pressed{background:#253847;}"
        "QTextEdit{font-size:13px;border:1px solid #344550;background:#0b1117;color:#d9e3e8;}"));

    QLabel *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("title"));

    statusLabel->setObjectName(QStringLiteral("status"));
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    headerButtonLayout->setSpacing(6);

    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addLayout(headerButtonLayout);
    headerLayout->addWidget(statusLabel);

    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignTop);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(8);

    buttonLayout->setSpacing(8);
    buttonLayout->addStretch();

    logEdit->setReadOnly(true);
    logEdit->setMinimumHeight(78);

    mainContentLayout->setContentsMargins(14, 12, 14, 12);
    mainContentLayout->setSpacing(10);
    mainContentLayout->addLayout(headerLayout);
    mainContentLayout->addLayout(formLayout);
    mainContentLayout->addLayout(buttonLayout);
    mainContentLayout->addWidget(logEdit, 1);

    setLayout(mainContentLayout);
}

QPushButton *ModuleTestWindow::addHeaderButton(const QString &text)
{
    QPushButton *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("headerButton"));
    headerButtonLayout->addWidget(button);
    return button;
}

QLabel *ModuleTestWindow::addRow(const QString &name, const QString &initial)
{
    QLabel *value = new QLabel(initial, this);
    value->setObjectName(QStringLiteral("field"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formLayout->addRow(name, value);
    return value;
}

QLineEdit *ModuleTestWindow::addPathEdit(const QString &name, const QString &initial)
{
    QLineEdit *edit = new QLineEdit(initial, this);
    formLayout->addRow(name, edit);
    return edit;
}

QPushButton *ModuleTestWindow::addButton(const QString &text)
{
    QPushButton *button = new QPushButton(text, this);
    buttonLayout->insertWidget(qMax(0, buttonLayout->count() - 1), button);
    return button;
}

void ModuleTestWindow::appendLog(const QString &line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"));
    logEdit->append(QStringLiteral("[%1] %2").arg(stamp, line));
}

void ModuleTestWindow::setStatus(const QString &status)
{
    statusLabel->setText(status);
}

QVBoxLayout *ModuleTestWindow::contentLayout() const
{
    return mainContentLayout;
}

} // namespace imx6sm

