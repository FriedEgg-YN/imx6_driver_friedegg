#include "phoneshell.h"
#include "monitorpanel.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
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
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

PhoneShell::PhoneShell(QWidget *parent)
    : QWidget(parent)
    , stack(new QStackedWidget(this))
    , homePage(nullptr)
    , clockLabel(nullptr)
    , dateLabel(nullptr)
    , clockTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("i.MX6 Touch Launcher"));
    setMinimumSize(480, 272);
    setStyleSheet(QStringLiteral(
        "PhoneShell{background:#10171d;color:#eef3f5;font-family:DejaVu Sans;}"
        "QWidget#homePage{background:#10171d;color:#eef3f5;}"
        "QWidget#statusBar{background:#17222b;border-bottom:1px solid #273844;}"
        "QLabel#launcherTitle{font-size:22px;font-weight:700;color:#f5f8fa;}"
        "QLabel#clockLabel{font-size:15px;color:#d3dee4;}"
        "QLabel#hintLabel{font-size:14px;color:#aab9c2;}"
        "QToolButton{border:0;background:transparent;color:#f2f6f8;font-size:15px;padding:4px;}"
        "QToolButton:pressed{background:#22303a;border-radius:8px;}"
        "QToolButton:disabled{color:#6f7d86;}"
        "QWidget#infoPage{background:#10171d;color:#eef3f5;}"
        "QLabel#infoTitle{font-size:24px;font-weight:700;}"
        "QLabel#infoBody{font-size:16px;color:#c8d5dc;}"
        "QPushButton#homeButton{font-size:18px;padding:14px;border-radius:6px;background:#25313b;color:#eef3f5;}"
        "QPushButton#homeButton:pressed{background:#2f4555;}"));

    homePage = createHomePage();
    stack->addWidget(homePage);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack);

    connect(clockTimer, &QTimer::timeout, this, &PhoneShell::updateClock);
    clockTimer->start(1000);
    updateClock();
}

PhoneShell::~PhoneShell() = default;

QWidget *PhoneShell::createHomePage()
{
    QWidget *page = new QWidget;
    page->setObjectName(QStringLiteral("homePage"));

    QWidget *statusBar = new QWidget(page);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    statusBar->setFixedHeight(36);

    QLabel *brand = new QLabel(QStringLiteral("i.MX6ULL"), statusBar);
    brand->setObjectName(QStringLiteral("clockLabel"));
    clockLabel = new QLabel(statusBar);
    clockLabel->setObjectName(QStringLiteral("clockLabel"));
    clockLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    QHBoxLayout *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 0, 14, 0);
    statusLayout->addWidget(brand);
    statusLayout->addStretch();
    statusLayout->addWidget(clockLabel);

    QLabel *title = new QLabel(QStringLiteral("Touch Launcher"), page);
    title->setObjectName(QStringLiteral("launcherTitle"));
    dateLabel = new QLabel(QDateTime::currentDateTime().toString(QStringLiteral("dddd  MMM d")), page);
    dateLabel->setObjectName(QStringLiteral("hintLabel"));

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    QToolButton *monitorApp = createAppButton(QStringLiteral("Monitor"),
                                              QStringLiteral("CAM"),
                                              QColor(QStringLiteral("#1f8f64")));
    QToolButton *sensorApp = createAppButton(QStringLiteral("Sensors"),
                                             QStringLiteral("I2C"),
                                             QColor(QStringLiteral("#2877b8")));
    QToolButton *networkApp = createAppButton(QStringLiteral("Network"),
                                              QStringLiteral("NET"),
                                              QColor(QStringLiteral("#c45b31")));
    QToolButton *settingsApp = createAppButton(QStringLiteral("Settings"),
                                               QStringLiteral("CFG"),
                                               QColor(QStringLiteral("#7561c8")));

    grid->addWidget(monitorApp, 0, 0);
    grid->addWidget(sensorApp, 0, 1);
    grid->addWidget(networkApp, 0, 2);
    grid->addWidget(settingsApp, 0, 3);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);

    connect(monitorApp, &QToolButton::clicked, this, &PhoneShell::openMonitorApp);
    connect(sensorApp, &QToolButton::clicked, this, [this]() {
        openInfoPage(QStringLiteral("Sensors"),
                     QStringList() << QStringLiteral("AP3216C")
                                   << QStringLiteral("IR  --    ALS  --    PS  --"));
    });
    connect(networkApp, &QToolButton::clicked, this, [this]() {
        openInfoPage(QStringLiteral("Network"),
                     QStringList() << QStringLiteral("HTTP API")
                                   << QStringLiteral("Port 8080"));
    });
    connect(settingsApp, &QToolButton::clicked, this, [this]() {
        openInfoPage(QStringLiteral("Settings"),
                     QStringList() << QStringLiteral("Display  linuxfb")
                                   << QStringLiteral("Touch  tslib"));
    });

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(10);
    layout->addWidget(statusBar);
    layout->addWidget(title, 0, Qt::AlignHCenter);
    layout->addWidget(dateLabel, 0, Qt::AlignHCenter);
    layout->addLayout(grid, 1);

    return page;
}

QWidget *PhoneShell::createInfoPage(const QString &title, const QStringList &lines)
{
    QWidget *page = new QWidget;
    page->setObjectName(QStringLiteral("infoPage"));

    QLabel *titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("infoTitle"));
    titleLabel->setAlignment(Qt::AlignCenter);

    QLabel *bodyLabel = new QLabel(lines.join(QLatin1Char('\n')), page);
    bodyLabel->setObjectName(QStringLiteral("infoBody"));
    bodyLabel->setAlignment(Qt::AlignCenter);
    bodyLabel->setWordWrap(true);

    QPushButton *homeButton = new QPushButton(QStringLiteral("Home"), page);
    homeButton->setObjectName(QStringLiteral("homeButton"));
    homeButton->setMinimumHeight(52);
    connect(homeButton, &QPushButton::clicked, this, [this, page]() {
        showHome();
        stack->removeWidget(page);
        page->deleteLater();
    });

    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(16);
    layout->addWidget(titleLabel);
    layout->addWidget(bodyLabel, 1);
    layout->addWidget(homeButton);

    return page;
}

QToolButton *PhoneShell::createAppButton(const QString &title, const QString &tag,
                                         const QColor &color)
{
    QToolButton *button = new QToolButton;
    button->setText(title);
    button->setIcon(appIcon(tag, color));
    button->setIconSize(QSize(62, 62));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setMinimumSize(96, 92);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

QIcon PhoneShell::appIcon(const QString &tag, const QColor &color) const
{
    QPixmap pixmap(96, 96);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF iconRect(8, 8, 80, 80);
    QPainterPath path;
    path.addRoundedRect(iconRect, 18, 18);
    painter.fillPath(path, color);

    QColor highlight = color.lighter(135);
    painter.setPen(QPen(highlight, 3));
    painter.drawArc(QRectF(22, 20, 52, 52), 35 * 16, 135 * 16);

    painter.setPen(Qt::white);
    QFont tagFont = painter.font();
    tagFont.setBold(true);
    tagFont.setPixelSize(22);
    painter.setFont(tagFont);
    painter.drawText(iconRect, Qt::AlignCenter, tag);

    return QIcon(pixmap);
}

void PhoneShell::openMonitorApp()
{
    if (!monitorPanel) {
        monitorPanel = new MonitorPanel;
        stack->addWidget(monitorPanel);
        connect(monitorPanel, &MonitorPanel::homeRequested, this, &PhoneShell::showHome);
    }

    stack->setCurrentWidget(monitorPanel);
}

void PhoneShell::openInfoPage(const QString &title, const QStringList &lines)
{
    QWidget *page = createInfoPage(title, lines);
    stack->addWidget(page);
    stack->setCurrentWidget(page);
}

void PhoneShell::showHome()
{
    if (monitorPanel) {
        stack->removeWidget(monitorPanel);
        monitorPanel->deleteLater();
        monitorPanel = nullptr;
    }

    stack->setCurrentWidget(homePage);
}

void PhoneShell::updateClock()
{
    if (!clockLabel)
        return;

    const QDateTime now = QDateTime::currentDateTime();
    clockLabel->setText(now.toString(QStringLiteral("hh:mm")));
    if (dateLabel)
        dateLabel->setText(now.toString(QStringLiteral("dddd  MMM d")));
}
