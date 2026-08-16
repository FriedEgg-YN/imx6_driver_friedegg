#ifndef AP3216C_PAGE_H
#define AP3216C_PAGE_H

#include "ap3216c_view_state.h"

#include <QWidget>

class QLabel;
class QPushButton;

class Ap3216cPage : public QWidget {
    Q_OBJECT
public:
    explicit Ap3216cPage(QWidget *parent = nullptr);

signals:
    void startRequested();
    void stopRequested();

public slots:
    void render(const Ap3216cViewState &viewState);

private:
    QLabel *m_statusLabel;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
};

#endif // AP3216C_PAGE_H