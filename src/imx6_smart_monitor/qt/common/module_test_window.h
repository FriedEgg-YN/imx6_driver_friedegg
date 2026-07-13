#ifndef IMX6SMARTMONITOR_MODULE_TEST_WINDOW_H
#define IMX6SMARTMONITOR_MODULE_TEST_WINDOW_H

#include <QWidget>

class QFormLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QVBoxLayout;

namespace imx6sm {

class ModuleTestWindow : public QWidget {
public:
    explicit ModuleTestWindow(const QString &title, QWidget *parent = nullptr);
    QPushButton *addHeaderButton(const QString &text);

protected:
    QLabel *addRow(const QString &name, const QString &initial = QStringLiteral("--"));
    QLineEdit *addPathEdit(const QString &name, const QString &initial);
    QPushButton *addButton(const QString &text);
    void appendLog(const QString &line);
    void setStatus(const QString &status);
    QVBoxLayout *contentLayout() const;

private:
    QLabel *statusLabel;
    QHBoxLayout *headerButtonLayout;
    QFormLayout *formLayout;
    QHBoxLayout *buttonLayout;
    QTextEdit *logEdit;
    QVBoxLayout *mainContentLayout;
};

} // namespace imx6sm

#endif

