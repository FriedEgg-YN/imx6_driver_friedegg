#include "touch_test_window.h"

#include <QAbstractButton>

#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QTouchEvent>

namespace imx6sm {

TouchTestWindow::TouchTestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("Touch Test"), parent)
    , stateLabel(addRow(QStringLiteral("State")))
    , posLabel(addRow(QStringLiteral("Position")))
    , countLabel(addRow(QStringLiteral("Events"), QStringLiteral("0")))
{
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setMouseTracking(true);
    setStatus(QStringLiteral("Waiting"));
}

bool TouchTestWindow::event(QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::TouchBegin ||
        type == QEvent::TouchUpdate ||
        type == QEvent::TouchEnd ||
        type == QEvent::TouchCancel) {
        QTouchEvent *touch = static_cast<QTouchEvent *>(event);
        const bool hasPoint = !touch->touchPoints().isEmpty();
        const QPointF pos = hasPoint ? touch->touchPoints().first().pos() : QPointF();
        QAbstractButton *button = hasPoint ? buttonAtTouchPos(pos) : nullptr;

        if ((type == QEvent::TouchBegin && button) || touchTargetButton) {
            if (type == QEvent::TouchBegin) {
                touchTargetButton = button;
                touchTargetButton->setDown(true);
            } else if (type == QEvent::TouchUpdate) {
                touchTargetButton->setDown(button == touchTargetButton);
            } else {
                QAbstractButton *target = touchTargetButton;
                const bool releasedInside = (type == QEvent::TouchEnd && button == target);
                target->setDown(false);
                touchTargetButton = nullptr;
                if (releasedInside)
                    target->click();
            }

            event->accept();
            return true;
        }

        if (hasPoint)
            updateTouch(QStringLiteral("touch"), pos);

        event->accept();
        return true;
    }

    return ModuleTestWindow::event(event);
}

void TouchTestWindow::mousePressEvent(QMouseEvent *event)
{
    updateTouch(QStringLiteral("press"), event->pos());
}

void TouchTestWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() != Qt::NoButton)
        updateTouch(QStringLiteral("move"), event->pos());
}

void TouchTestWindow::mouseReleaseEvent(QMouseEvent *event)
{
    updateTouch(QStringLiteral("release"), event->pos());
}

QAbstractButton *TouchTestWindow::buttonAtTouchPos(const QPointF &pos) const
{
    QWidget *target = childAt(pos.toPoint());
    while (target && target != this) {
        if (QAbstractButton *button = qobject_cast<QAbstractButton *>(target))
            return button->isEnabled() ? button : nullptr;

        target = target->parentWidget();
    }

    return nullptr;
}

void TouchTestWindow::updateTouch(const QString &state, const QPointF &pos)
{
    ++eventCount;
    stateLabel->setText(state);
    posLabel->setText(QStringLiteral("%1, %2").arg(pos.x(), 0, 'f', 1).arg(pos.y(), 0, 'f', 1));
    countLabel->setText(QString::number(eventCount));
    setStatus(state);
}

} // namespace imx6sm
