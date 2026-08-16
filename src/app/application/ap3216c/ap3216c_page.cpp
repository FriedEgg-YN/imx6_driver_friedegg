#include "ap3216c_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

Ap3216cPage::Ap3216cPage(QWidget *parent)
    : QWidget(parent),
      m_statusLabel(new QLabel(this)),
      m_startButton(new QPushButton(QStringLiteral("Start"), this)),
      m_stopButton(new QPushButton(QStringLiteral("Stop"), this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);
    layout->addStretch();
    layout->addWidget(m_startButton);
    layout->addWidget(m_stopButton);

    connect(m_startButton, &QPushButton::clicked, this, &Ap3216cPage::startRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &Ap3216cPage::stopRequested);
}

void Ap3216cPage::render(const Ap3216cViewState &viewState)
{
    m_statusLabel->setText(viewState.status);
    m_startButton->setEnabled(!viewState.running);
    m_stopButton->setEnabled(viewState.running);
}