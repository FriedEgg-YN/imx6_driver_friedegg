#ifndef PHONESHELL_H
#define PHONESHELL_H

#include <QColor>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class MonitorPanel;
class QStackedWidget;
class QTimer;
class QToolButton;

class PhoneShell : public QWidget {
    Q_OBJECT

public:
    explicit PhoneShell(QWidget *parent = nullptr);
    ~PhoneShell() override;

private:
    QStackedWidget *stack;
    QWidget *homePage;
    QLabel *clockLabel;
    QLabel *dateLabel;
    QTimer *clockTimer;
    MonitorPanel *monitorPanel = nullptr;

    QWidget *createHomePage();
    QWidget *createInfoPage(const QString &title, const QStringList &lines);
    QToolButton *createAppButton(const QString &title, const QString &tag,
                                 const QColor &color);
    QIcon appIcon(const QString &tag, const QColor &color) const;

    void openMonitorApp();
    void openInfoPage(const QString &title, const QStringList &lines);
    void showHome();
    void updateClock();
};

#endif
