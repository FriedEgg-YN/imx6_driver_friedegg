#ifndef AP3216C_VIEW_STATE_H
#define AP3216C_VIEW_STATE_H

#include <QString>

struct Ap3216cViewState {
    bool running = false;
    QString status = QStringLiteral("Stopped");
};

#endif // AP3216C_VIEW_STATE_H