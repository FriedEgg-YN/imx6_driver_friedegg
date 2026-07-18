#include "launcher_window.h"

#include "smart_monitor_page.h"
#include "qt/apps/ap3216c_test/ap3216c_test_window.h"
#include "qt/apps/camera_test/camera_test_window.h"
#include "qt/apps/core_test/core_test_window.h"
#include "qt/apps/ld2410_test/ld2410_test_window.h"
#include "qt/apps/storage_test/storage_test_window.h"
#include "qt/apps/touch_test/touch_test_window.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPen>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace imx6sm {

static QIcon appIcon(const QString &tag, const QColor &color)
{
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF iconRect(8, 8, 80, 80);
    QPainterPath path;
    path.addRoundedRect(iconRect, 18, 18);
    painter.fillPath(path, color);

    painter.setPen(QPen(color.lighter(135), 3));
    painter.drawArc(QRectF(22, 20, 52, 52), 35 * 16, 135 * 16);

    painter.setPen(Qt::white);
    QFont tagFont = painter.font();
    tagFont.setBold(true);
    tagFont.setPixelSize(22);
    painter.setFont(tagFont);
    painter.drawText(iconRect, Qt::AlignCenter, tag);

    return QIcon(pixmap);
}

SmartMonitorLauncher::SmartMonitorLauncher(QWidget *parent)
    : QWidget(parent)
    , stack(new QStackedWidget(this))
    , monitorPage(new SmartMonitorPage(this))
    , homePage(nullptr)
{
    setMinimumSize(480, 272);
    setWindowTitle(QStringLiteral("i.MX6 Smart Monitor"));
    setStyleSheet(QStringLiteral(
        "QWidget{background:#10171d;color:#eef3f5;font-family:DejaVu Sans;}"
        "QWidget#homePage{background:#10171d;color:#eef3f5;}"
        "QWidget#statusBar{background:#17222b;border-bottom:1px solid #273844;}"
        "QLabel#brandLabel{font-size:15px;font-weight:700;color:#d3dee4;}"
        "QLabel#title{font-size:22px;font-weight:700;color:#f5f8fa;}"
        "QLabel#subtitle{font-size:14px;color:#aab9c2;}"
        "QToolButton#appButton{border:0;background:transparent;color:#f2f6f8;font-size:15px;padding:4px;}"
        "QToolButton#appButton:pressed{background:#22303a;border-radius:8px;}"
        "QToolButton#appButton:disabled{color:#6f7d86;}"
        "QPushButton#quitButton{font-size:14px;padding:7px 12px;border:1px solid #385064;border-radius:4px;background:#17232b;color:#edf3f5;}"
        "QPushButton#quitButton:pressed{background:#253847;}"));

    homePage = createHomePage();
    stack->addWidget(monitorPage);
    stack->addWidget(homePage);

    connect(monitorPage, &SmartMonitorPage::toolsRequested, this, &SmartMonitorLauncher::showHome);
    connect(monitorPage, &SmartMonitorPage::ld2410ConfigRequested, this, &SmartMonitorLauncher::showLd2410Config);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack);
    setLayout(layout);
    showMonitor();
}

QWidget *SmartMonitorLauncher::createHomePage()
{
    QWidget *page = new QWidget(this);
    page->setObjectName(QStringLiteral("homePage"));

    QWidget *statusBar = new QWidget(page);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    statusBar->setFixedHeight(32);

    QLabel *brand = new QLabel(QStringLiteral("i.MX6ULL"), statusBar);
    brand->setObjectName(QStringLiteral("brandLabel"));

    QPushButton *monitor = new QPushButton(QStringLiteral("Monitor"), statusBar);
    monitor->setObjectName(QStringLiteral("quitButton"));
    connect(monitor, &QPushButton::clicked, this, &SmartMonitorLauncher::showMonitor);

    QPushButton *quit = new QPushButton(QStringLiteral("Exit"), statusBar);
    quit->setObjectName(QStringLiteral("quitButton"));
    connect(quit, &QPushButton::clicked, qApp, &QApplication::quit);

    QHBoxLayout *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 0, 14, 0);
    statusLayout->addWidget(brand);
    statusLayout->addStretch();
    statusLayout->addWidget(monitor);
    statusLayout->addWidget(quit);

    QLabel *title = new QLabel(QStringLiteral("Tools"), page);
    title->setObjectName(QStringLiteral("title"));
    title->setAlignment(Qt::AlignHCenter);

    QLabel *subtitle = new QLabel(QStringLiteral("Module Test Apps"), page);
    subtitle->setObjectName(QStringLiteral("subtitle"));
    subtitle->setAlignment(Qt::AlignHCenter);

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    addApp(grid, 0, 0, AppEntry{QStringLiteral("Touch"), QStringLiteral("input events"), QStringLiteral("TCH"), QStringLiteral("#2877b8"), []() { return new TouchTestWindow; }});
    addApp(grid, 0, 1, AppEntry{QStringLiteral("AP3216C"), QStringLiteral("IIO sample"), QStringLiteral("ALS"), QStringLiteral("#1f8f64"), []() { return new Ap3216cTestWindow; }});
    addApp(grid, 0, 2, AppEntry{QStringLiteral("LD2410C"), QStringLiteral("presence nodes"), QStringLiteral("RAD"), QStringLiteral("#c45b31"), []() { return new Ld2410TestWindow; }});
    addApp(grid, 1, 0, AppEntry{QStringLiteral("Camera"), QStringLiteral("V4L2 query"), QStringLiteral("CAM"), QStringLiteral("#7561c8"), []() { return new CameraTestWindow; }});
    addApp(grid, 1, 1, AppEntry{QStringLiteral("Storage"), QStringLiteral("NFS session"), QStringLiteral("NFS"), QStringLiteral("#b58b2a"), []() { return new StorageTestWindow; }});
    addApp(grid, 1, 2, AppEntry{QStringLiteral("Core"), QStringLiteral("state machine"), QStringLiteral("CPU"), QStringLiteral("#b23a48"), []() { return new CoreTestWindow; }});

    for (int column = 0; column < 3; ++column)
        grid->setColumnStretch(column, 1);
    for (int row = 0; row < 2; ++row)
        grid->setRowStretch(row, 1);

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(6);
    layout->addWidget(statusBar);
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(subtitle, 0, Qt::AlignHCenter);
    layout->addLayout(grid, 1);

    return page;
}

QToolButton *SmartMonitorLauncher::createAppButton(const AppEntry &entry)
{
    QToolButton *button = new QToolButton;
    button->setObjectName(QStringLiteral("appButton"));
    button->setText(entry.title);
    button->setToolTip(entry.subtitle);
    button->setIcon(appIcon(entry.tag, QColor(entry.colorName)));
    button->setIconSize(QSize(50, 50));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setMinimumSize(88, 82);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(button, &QToolButton::clicked, this, [this, entry]() { openApp(entry); });
    return button;
}

void SmartMonitorLauncher::addApp(QGridLayout *grid, int row, int column, const AppEntry &entry)
{
    grid->addWidget(createAppButton(entry), row, column);
}

void SmartMonitorLauncher::openApp(const AppEntry &entry)
{
    showHome();

    ModuleTestWindow *page = entry.factory();
    QPushButton *back = page->addHeaderButton(QStringLiteral("Tools"));
    connect(back, &QPushButton::clicked, this, [this]() { showHome(); });

    currentAppPage = page;
    stack->addWidget(currentAppPage);
    stack->setCurrentWidget(currentAppPage);
}

void SmartMonitorLauncher::showMonitor()
{
    if (currentAppPage) {
        QWidget *oldPage = currentAppPage;
        currentAppPage = nullptr;
        stack->removeWidget(oldPage);
        oldPage->deleteLater();
    }
    stack->setCurrentWidget(monitorPage);
}

void SmartMonitorLauncher::showLd2410Config()
{
    openApp(AppEntry{QStringLiteral("LD2410C"), QStringLiteral("presence config"),
                     QStringLiteral("RAD"), QStringLiteral("#c45b31"),
                     []() { return new Ld2410TestWindow; }});
}

void SmartMonitorLauncher::showHome()
{
    stack->setCurrentWidget(homePage);
    if (!currentAppPage)
        return;

    QWidget *oldPage = currentAppPage;
    currentAppPage = nullptr;
    stack->removeWidget(oldPage);
    oldPage->deleteLater();
}

} // namespace imx6sm
