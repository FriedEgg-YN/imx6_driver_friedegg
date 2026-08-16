#ifndef AP3216C_CONTROLLER_H
#define AP3216C_CONTROLLER_H

#include "ap3216c_view_state.h"

#include <QObject>

class Ap3216cController : public QObject {
    Q_OBJECT
public:
    explicit Ap3216cController(QObject *parent = nullptr);

public slots:
    void start();
    void stop();
    void refresh();

signals:
    void viewStateChanged(const Ap3216cViewState &viewState);

private:
    void publishViewState();

    Ap3216cViewState m_viewState;
};

#endif // AP3216C_CONTROLLER_H