#include "ap3216c_controller.h"

Ap3216cController::Ap3216cController(QObject *parent)
    : QObject(parent) {
}

void Ap3216cController::start() {
    if (m_viewState.running) {
        return; // Already running
    }
    m_viewState.running = true;
    m_viewState.status = QStringLiteral("Running");
    publishViewState();
}

void Ap3216cController::stop() {
    if (!m_viewState.running) {
        return; // Already stopped
    }
    m_viewState.running = false;
    m_viewState.status = QStringLiteral("Stopped");
    publishViewState();
}

void Ap3216cController::refresh() {
    publishViewState();
}

void Ap3216cController::publishViewState() {
    emit viewStateChanged(m_viewState);
}