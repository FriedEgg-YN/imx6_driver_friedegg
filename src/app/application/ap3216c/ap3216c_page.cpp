#include "ap3216c_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace smartmonitor {

Ap3216cPage::Ap3216cPage(QWidget *parent)
    : QWidget(parent),
      m_statusLabel(new QLabel(this)),
      m_sampleLabel(new QLabel(this)),
      m_startButton(new QPushButton(QStringLiteral("Start"), this)),
      m_stopButton(new QPushButton(QStringLiteral("Stop"), this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_sampleLabel);
    layout->addStretch();
    layout->addWidget(m_startButton);
    layout->addWidget(m_stopButton);

    connect(m_startButton, &QPushButton::clicked,
            this, &Ap3216cPage::startRequested);
    connect(m_stopButton, &QPushButton::clicked,
            this, &Ap3216cPage::stopRequested);
}

void Ap3216cPage::renderViewState(const Ap3216cViewState &viewState)
{
    m_statusLabel->setText(viewState.status);

    const bool idle = viewState.samplingState == SamplingState::Idle;
    const bool canStop = viewState.samplingState == SamplingState::Starting
                         || viewState.samplingState == SamplingState::Running;
    m_startButton->setEnabled(idle);
    m_stopButton->setEnabled(canStop);

    if (!viewState.hasSample)
    {
        m_sampleLabel->setText(QStringLiteral("Sample: --"));
    }
    else if (viewState.sample.valid)
    {
        m_sampleLabel->setText(
            QStringLiteral("Sample: %1").arg(viewState.sample.value));
    }
    else
    {
        m_sampleLabel->setText(QStringLiteral("Sample: --"));
    }
}

} // namespace smartmonitor
