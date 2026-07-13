#ifndef IMX6SMARTMONITOR_TOUCH_TEST_WINDOW_H
#define IMX6SMARTMONITOR_TOUCH_TEST_WINDOW_H

#include "qt/common/module_test_window.h"

#include <QPointF>

class QAbstractButton;
class QEvent;
class QLabel;
class QMouseEvent;

namespace imx6sm {

class TouchTestWindow : public ModuleTestWindow {
public:
    explicit TouchTestWindow(QWidget *parent = nullptr);

protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateTouch(const QString &state, const QPointF &pos);
    QAbstractButton *buttonAtTouchPos(const QPointF &pos) const;

    QLabel *stateLabel;
    QLabel *posLabel;
    QLabel *countLabel;
    QAbstractButton *touchTargetButton = nullptr;
    int eventCount = 0;
};

} // namespace imx6sm

#endif
