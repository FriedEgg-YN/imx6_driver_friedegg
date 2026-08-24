#include "ap3216c_page.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace smartmonitor {

namespace {

QString samplingStateText(SamplingState state)
{
    switch (state)
    {
    case SamplingState::Idle:
        return QStringLiteral("Stopped");
    case SamplingState::Starting:
        return QStringLiteral("Starting");
    case SamplingState::Running:
        return QStringLiteral("Running");
    case SamplingState::Stopping:
        return QStringLiteral("Stopping");
    }

    return QStringLiteral("Unknown");
}

} // namespace

Ap3216cPage::Ap3216cPage(QWidget *parent)
    : QWidget(parent),
      m_samplingStateLabel(new QLabel(this)),
      m_sampleLabel(new QLabel(this)),
      m_timeLabel(new QLabel(this)),
      m_startButton(new QPushButton(QStringLiteral("Start"), this)),
      m_stopButton(new QPushButton(QStringLiteral("Stop"), this))
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_samplingStateLabel);
    layout->addWidget(m_sampleLabel);
    layout->addWidget(m_timeLabel);
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
    m_samplingStateLabel->setText(
        samplingStateText(viewState.samplingState));

    const bool idle = viewState.samplingState == SamplingState::Idle;
    const bool canStop = viewState.samplingState == SamplingState::Starting
                         || viewState.samplingState == SamplingState::Running;
    m_startButton->setEnabled(idle);
    m_stopButton->setEnabled(canStop);

    if (!viewState.hasSample)
    {
        m_sampleLabel->setText(QStringLiteral("Sample: --"));
    }
    else if (viewState.sample.error.isEmpty())
    {
        const QString luxText = viewState.sample.lux.valid
                                    ? QString::number(viewState.sample.lux.value, 'f', 2)
                                    : viewState.sample.lux.error;
        const QString alsRawText = viewState.sample.alsRaw.valid
                                       ? QString::number(viewState.sample.alsRaw.value)
                                       : viewState.sample.alsRaw.error;
        const QString irRawText = viewState.sample.irRaw.valid
                                      ? QString::number(viewState.sample.irRaw.value)
                                      : viewState.sample.irRaw.error;
        const QString psRawText = viewState.sample.psRaw.valid
                                      ? QString::number(viewState.sample.psRaw.value)
                                      : viewState.sample.psRaw.error;

        m_sampleLabel->setText(
            QStringLiteral("lux: %1\n"
                            "alsRaw: %2\n"
                            "irRaw: %3\n"
                            "psRaw: %4\n"
            ).arg(luxText)
             .arg(alsRawText)
             .arg(irRawText)
             .arg(psRawText));
        m_timeLabel->setText(QStringLiteral("time: %1").arg(viewState.sample.timestamp));
    }
    else
    {
        m_sampleLabel->setText(QStringLiteral("Sample error: %1").arg(viewState.sample.error));
    }
}

} // namespace smartmonitor
