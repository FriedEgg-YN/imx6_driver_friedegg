#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include <cstdio>

class DemoWidget : public QWidget {
public:
    DemoWidget()
    {
        setWindowTitle(QStringLiteral("i.MX6 Qt5 HMI"));
        resize(480, 272);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRect r = rect();
        painter.fillRect(r, QColor(17, 20, 25));

        const int headerHeight = qMax(64, r.height() / 4);
        painter.fillRect(QRect(0, 0, r.width(), headerHeight), QColor(16, 122, 118));
        painter.fillRect(QRect(0, headerHeight, r.width(), 6), QColor(244, 184, 62));

        QFont title = painter.font();
        title.setPointSize(qMax(18, r.height() / 14));
        title.setBold(true);
        painter.setFont(title);
        painter.setPen(Qt::white);
        painter.drawText(QRect(18, 8, r.width() - 36, headerHeight - 16),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("i.MX6ULL Qt5 linuxfb"));

        QFont body = painter.font();
        body.setPointSize(qMax(10, r.height() / 28));
        body.setBold(false);
        painter.setFont(body);

        const int gap = 12;
        const int cardTop = headerHeight + 24;
        const int cardHeight = qMax(54, (r.height() - cardTop - 20 - gap) / 2);
        const int cardWidth = (r.width() - 54) / 2;

        drawTile(painter, QRect(18, cardTop, cardWidth, cardHeight),
                 QStringLiteral("Sensor"), QStringLiteral("AP3216C ready"), QColor(46, 113, 198));
        drawTile(painter, QRect(36 + cardWidth, cardTop, cardWidth, cardHeight),
                 QStringLiteral("Camera"), QStringLiteral("OV5640 path"), QColor(203, 85, 73));
        drawTile(painter, QRect(18, cardTop + cardHeight + gap, cardWidth, cardHeight),
                 QStringLiteral("RootFS"), QStringLiteral("glibc userland"), QColor(76, 149, 87));
        drawTile(painter, QRect(36 + cardWidth, cardTop + cardHeight + gap, cardWidth, cardHeight),
                 QStringLiteral("Qt"), QString::fromLatin1("Qt %1").arg(qVersion()), QColor(129, 98, 170));

        painter.setPen(QColor(214, 220, 226));
        painter.drawText(r.adjusted(18, 0, -18, -8),
                         Qt::AlignLeft | Qt::AlignBottom,
                         QDateTime::currentDateTime().toString(Qt::ISODate));
    }

private:
    static void drawTile(QPainter &painter, const QRect &rect, const QString &title,
                         const QString &value, const QColor &accent)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(32, 37, 45));
        painter.drawRoundedRect(rect, 6, 6);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRect(rect.left(), rect.top(), 8, rect.height()), 4, 4);

        painter.setPen(Qt::white);
        QFont titleFont = painter.font();
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.drawText(rect.adjusted(18, 8, -10, -rect.height() / 2),
                         Qt::AlignLeft | Qt::AlignVCenter, title);

        QFont valueFont = painter.font();
        valueFont.setBold(false);
        painter.setFont(valueFont);
        painter.setPen(QColor(206, 214, 222));
        painter.drawText(rect.adjusted(18, rect.height() / 2 - 4, -10, -8),
                         Qt::AlignLeft | Qt::AlignVCenter, value);
    }
};

static bool hasArg(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QString::fromLatin1(name))
            return true;
    }

    return false;
}

static int durationMs(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--duration-ms")) {
            bool ok = false;
            const int value = QString::fromLocal8Bit(argv[i + 1]).toInt(&ok);
            return ok ? value : 0;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (hasArg(argc, argv, "--self-test")) {
        QCoreApplication app(argc, argv);
        std::printf("imx6-qt5-demo Qt %s\n", qVersion());
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "linuxfb");

    QApplication app(argc, argv);
    DemoWidget widget;
    widget.showFullScreen();

    const int timeout = durationMs(argc, argv);
    if (timeout > 0)
        QTimer::singleShot(timeout, &app, &QCoreApplication::quit);

    return app.exec();
}
