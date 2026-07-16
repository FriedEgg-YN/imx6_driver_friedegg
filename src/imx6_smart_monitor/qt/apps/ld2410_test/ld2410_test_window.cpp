#include "ld2410_test_window.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QScrollArea>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPair>
#include <QSpinBox>
#include <QTextEdit>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const int kPageMargin = 10;
const int kLayoutSpacing = 10;
const int kColumnSpacing = 12;
const int kGridRowSpacing = 8;

void configurePageLayout(QVBoxLayout *layout)
{
    layout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    layout->setSpacing(kLayoutSpacing);
}

void configureFormLayout(QFormLayout *form)
{
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(kColumnSpacing);
    form->setVerticalSpacing(kLayoutSpacing);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

void configureGridLayout(QGridLayout *grid)
{
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kColumnSpacing);
    grid->setVerticalSpacing(kGridRowSpacing);
}

void configureButtonLayout(QHBoxLayout *buttons)
{
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
}

QWidget *scrollableTabPage(QWidget *content, QWidget *parent)
{
    QScrollArea *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

namespace imx6sm {

Ld2410TestWindow::Ld2410TestWindow(QWidget *parent)
    : ModuleTestWindow(QStringLiteral("LD2410C Test"), parent)
    , device()
    , tabs(new QTabWidget(this))
    , refreshTimer(new QTimer(this))
    , miscEdit(nullptr)
    , outEdit(nullptr)
    , uartEdit(nullptr)
    , probeStatus(nullptr)
    , probeMisc(nullptr)
    , probeOut(nullptr)
    , probeUart(nullptr)
    , probeHint(nullptr)
    , stateStatus(nullptr)
    , presenceLabel(nullptr)
    , targetLabel(nullptr)
    , sourceLabel(nullptr)
    , motionDistanceLabel(nullptr)
    , staticDistanceLabel(nullptr)
    , detectDistanceLabel(nullptr)
    , motionEnergyLabel(nullptr)
    , staticEnergyLabel(nullptr)
    , frameCountLabel(nullptr)
    , errorCountLabel(nullptr)
    , sequenceLabel(nullptr)
    , outLevelLabel(nullptr)
    , engineeringMaxLabel(nullptr)
    , engineeringLightLabel(nullptr)
    , configStatus(nullptr)
    , maxGateSpin(nullptr)
    , motionGateSpin(nullptr)
    , staticGateSpin(nullptr)
    , idleTimeSpin(nullptr)
    , resolutionCombo(nullptr)
    , baudCombo(nullptr)
    , auxModeCombo(nullptr)
    , auxThresholdSpin(nullptr)
    , outDefaultHighCheck(nullptr)
    , engineeringModeCheck(nullptr)
    , versionLabel(nullptr)
    , noiseStatusLabel(nullptr)
    , noiseDurationSpin(nullptr)
{
    setStandardBodyVisible(false);
    setLogVisible(false);
    logWidget()->setMinimumHeight(78);
    logWidget()->setMaximumHeight(104);

    QPushButton *logButton = addHeaderButton(QStringLiteral("Log"));
    logButton->setCheckable(true);
    logButton->setChecked(isLogVisible());
    connect(logButton, &QPushButton::clicked, this, [this, logButton]() {
        setLogVisible(!isLogVisible());
        logButton->setChecked(isLogVisible());
        logButton->setText(isLogVisible() ? QStringLiteral("Hide Log") : QStringLiteral("Log"));
    });

    tabs->setUsesScrollButtons(true);
    contentLayout()->insertWidget(1, tabs, 1);

    tabs->addTab(scrollableTabPage(buildProbePage(), tabs), QStringLiteral("Probe"));
    tabs->addTab(scrollableTabPage(buildRealtimePage(), tabs), QStringLiteral("Realtime"));
    tabs->addTab(scrollableTabPage(buildEngineeringPage(), tabs), QStringLiteral("Engineering"));
    tabs->addTab(scrollableTabPage(buildConfigPage(), tabs), QStringLiteral("Config"));
    tabs->addTab(scrollableTabPage(buildMaintenancePage(), tabs), QStringLiteral("Maintenance"));

    connect(refreshTimer, &QTimer::timeout, this, [this]() { refreshState(); });

    refreshProbe();
    refreshState();
    refreshConfig();
    refreshMaintenance();
}

QWidget *Ld2410TestWindow::buildProbePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    QFormLayout *form = new QFormLayout;
    QHBoxLayout *buttons = new QHBoxLayout;
    configurePageLayout(layout);
    configureFormLayout(form);
    configureButtonLayout(buttons);

    miscEdit = new QLineEdit(QStringLiteral("auto"), page);
    outEdit = new QLineEdit(QStringLiteral("auto"), page);
    uartEdit = new QLineEdit(device.defaultUartPath(), page);

    probeStatus = new QLabel(QStringLiteral("Idle"), page);
    probeMisc = new QLabel(QStringLiteral("--"), page);
    probeOut = new QLabel(QStringLiteral("--"), page);
    probeUart = new QLabel(QStringLiteral("--"), page);
    probeHint = new QLabel(QStringLiteral("--"), page);
    probeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    probeMisc->setTextInteractionFlags(Qt::TextSelectableByMouse);
    probeOut->setTextInteractionFlags(Qt::TextSelectableByMouse);
    probeUart->setTextInteractionFlags(Qt::TextSelectableByMouse);
    probeHint->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(QStringLiteral("Misc"), miscEdit);
    form->addRow(QStringLiteral("OUT"), outEdit);
    form->addRow(QStringLiteral("UART"), uartEdit);
    form->addRow(QStringLiteral("Status"), probeStatus);
    form->addRow(QStringLiteral("Misc node"), probeMisc);
    form->addRow(QStringLiteral("OUT node"), probeOut);
    form->addRow(QStringLiteral("UART node"), probeUart);
    form->addRow(QStringLiteral("Input hint"), probeHint);

    QPushButton *probeButton = new QPushButton(QStringLiteral("Probe"), page);
    QPushButton *attachButton = new QPushButton(QStringLiteral("Attach UART"), page);
    buttons->addWidget(probeButton);
    buttons->addWidget(attachButton);
    buttons->addStretch();

    connect(probeButton, &QPushButton::clicked, this, [this]() {
        refreshProbe();
        refreshState();
        refreshConfig();
    });
    connect(attachButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!device.attachUart(uartEdit->text().trimmed(), LD2410C_DEFAULT_BAUD, &error)) {
            probeStatus->setText(QStringLiteral("UART attach failed"));
            showError(QStringLiteral("attach UART"), error);
            return;
        }
        appendLog(QStringLiteral("UART attached with line discipline 29"));
        refreshProbe();
        refreshState();
    });

    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addStretch();
    return page;
}

QWidget *Ld2410TestWindow::buildRealtimePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    QFormLayout *form = new QFormLayout;
    QHBoxLayout *buttons = new QHBoxLayout;
    configurePageLayout(layout);
    configureFormLayout(form);
    configureButtonLayout(buttons);

    stateStatus = new QLabel(QStringLiteral("Idle"), page);
    presenceLabel = new QLabel(QStringLiteral("--"), page);
    targetLabel = new QLabel(QStringLiteral("--"), page);
    sourceLabel = new QLabel(QStringLiteral("--"), page);
    motionDistanceLabel = new QLabel(QStringLiteral("--"), page);
    staticDistanceLabel = new QLabel(QStringLiteral("--"), page);
    detectDistanceLabel = new QLabel(QStringLiteral("--"), page);
    motionEnergyLabel = new QLabel(QStringLiteral("--"), page);
    staticEnergyLabel = new QLabel(QStringLiteral("--"), page);
    frameCountLabel = new QLabel(QStringLiteral("--"), page);
    errorCountLabel = new QLabel(QStringLiteral("--"), page);
    sequenceLabel = new QLabel(QStringLiteral("--"), page);
    outLevelLabel = new QLabel(QStringLiteral("--"), page);

    for (QLabel *label : {stateStatus, presenceLabel, targetLabel, sourceLabel, motionDistanceLabel,
                          staticDistanceLabel, detectDistanceLabel, motionEnergyLabel, staticEnergyLabel,
                          frameCountLabel, errorCountLabel, sequenceLabel, outLevelLabel}) {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    form->addRow(QStringLiteral("Status"), stateStatus);
    form->addRow(QStringLiteral("Presence"), presenceLabel);
    form->addRow(QStringLiteral("Target"), targetLabel);
    form->addRow(QStringLiteral("Source"), sourceLabel);
    form->addRow(QStringLiteral("Move dist cm"), motionDistanceLabel);
    form->addRow(QStringLiteral("Static dist cm"), staticDistanceLabel);
    form->addRow(QStringLiteral("Detect dist cm"), detectDistanceLabel);
    form->addRow(QStringLiteral("Move energy"), motionEnergyLabel);
    form->addRow(QStringLiteral("Static energy"), staticEnergyLabel);
    form->addRow(QStringLiteral("Frame count"), frameCountLabel);
    form->addRow(QStringLiteral("Error count"), errorCountLabel);
    form->addRow(QStringLiteral("Sequence"), sequenceLabel);
    form->addRow(QStringLiteral("OUT"), outLevelLabel);

    QPushButton *refreshButton = new QPushButton(QStringLiteral("Refresh"), page);
    QPushButton *liveButton = new QPushButton(QStringLiteral("Start Live"), page);
    QPushButton *stopButton = new QPushButton(QStringLiteral("Stop Live"), page);
    buttons->addWidget(refreshButton);
    buttons->addWidget(liveButton);
    buttons->addWidget(stopButton);
    buttons->addStretch();

    connect(refreshButton, &QPushButton::clicked, this, [this]() { refreshState(); });
    connect(liveButton, &QPushButton::clicked, this, [this]() {
        refreshTimer->start(500);
        refreshState();
        stateStatus->setText(QStringLiteral("Live"));
    });
    connect(stopButton, &QPushButton::clicked, this, [this]() {
        refreshTimer->stop();
        stateStatus->setText(QStringLiteral("Paused"));
    });

    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addStretch();
    return page;
}

QWidget *Ld2410TestWindow::buildEngineeringPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    QFormLayout *form = new QFormLayout;
    QGridLayout *grid = new QGridLayout;
    configurePageLayout(layout);
    configureFormLayout(form);
    configureGridLayout(grid);

    engineeringMaxLabel = new QLabel(QStringLiteral("--"), page);
    engineeringLightLabel = new QLabel(QStringLiteral("--"), page);
    engineeringMaxLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    engineeringLightLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(QStringLiteral("Max gates"), engineeringMaxLabel);
    form->addRow(QStringLiteral("Light / engineering"), engineeringLightLabel);

    grid->addWidget(new QLabel(QStringLiteral("Gate"), page), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Move energy"), page), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Static energy"), page), 0, 2);

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        const QString gateText = QString::number(i);
        QLabel *gateLabel = new QLabel(gateText, page);
        engineeringMotionGateLabels[i] = new QLabel(QStringLiteral("--"), page);
        engineeringStaticGateLabels[i] = new QLabel(QStringLiteral("--"), page);
        gateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        engineeringMotionGateLabels[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        engineeringStaticGateLabels[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(gateLabel, i + 1, 0);
        grid->addWidget(engineeringMotionGateLabels[i], i + 1, 1);
        grid->addWidget(engineeringStaticGateLabels[i], i + 1, 2);
    }

    layout->addLayout(form);
    layout->addLayout(grid);
    layout->addStretch();
    return page;
}

QWidget *Ld2410TestWindow::buildConfigPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    QFormLayout *form = new QFormLayout;
    QGridLayout *grid = new QGridLayout;
    QHBoxLayout *buttons = new QHBoxLayout;
    configurePageLayout(layout);
    configureFormLayout(form);
    configureGridLayout(grid);
    configureButtonLayout(buttons);

    configStatus = new QLabel(QStringLiteral("Idle"), page);
    maxGateSpin = new QSpinBox(page);
    motionGateSpin = new QSpinBox(page);
    staticGateSpin = new QSpinBox(page);
    idleTimeSpin = new QSpinBox(page);
    resolutionCombo = new QComboBox(page);
    baudCombo = new QComboBox(page);
    auxModeCombo = new QComboBox(page);
    auxThresholdSpin = new QSpinBox(page);
    outDefaultHighCheck = new QCheckBox(QStringLiteral("OUT default high"), page);

    configStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    maxGateSpin->setRange(2, 8);
    motionGateSpin->setRange(1, 8);
    staticGateSpin->setRange(1, 8);
    idleTimeSpin->setRange(0, 65535);
    auxThresholdSpin->setRange(0, 255);

    resolutionCombo->addItem(QStringLiteral("0.75m"), 0);
    resolutionCombo->addItem(QStringLiteral("0.2m"), 1);

    for (const auto &entry : {QPair<QString, quint32>(QStringLiteral("9600"), 9600),
                              QPair<QString, quint32>(QStringLiteral("19200"), 19200),
                              QPair<QString, quint32>(QStringLiteral("38400"), 38400),
                              QPair<QString, quint32>(QStringLiteral("57600"), 57600),
                              QPair<QString, quint32>(QStringLiteral("115200"), 115200),
                              QPair<QString, quint32>(QStringLiteral("230400"), 230400),
                              QPair<QString, quint32>(QStringLiteral("256000"), 256000),
                              QPair<QString, quint32>(QStringLiteral("460800"), 460800)}) {
        baudCombo->addItem(entry.first, entry.second);
    }

    auxModeCombo->addItem(QStringLiteral("Off"), 0);
    auxModeCombo->addItem(QStringLiteral("Light below"), 1);
    auxModeCombo->addItem(QStringLiteral("Light above"), 2);
    baudCombo->setCurrentIndex(6);

    form->addRow(QStringLiteral("Status"), configStatus);
    form->addRow(QStringLiteral("Max gate"), maxGateSpin);
    form->addRow(QStringLiteral("Motion gate"), motionGateSpin);
    form->addRow(QStringLiteral("Static gate"), staticGateSpin);
    form->addRow(QStringLiteral("Idle time s"), idleTimeSpin);
    form->addRow(QStringLiteral("Resolution"), resolutionCombo);
    form->addRow(QStringLiteral("Baud"), baudCombo);
    form->addRow(QStringLiteral("Aux mode"), auxModeCombo);
    form->addRow(QStringLiteral("Aux threshold"), auxThresholdSpin);
    form->addRow(QString(), outDefaultHighCheck);

    grid->addWidget(new QLabel(QStringLiteral("Gate"), page), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Motion sensitivity"), page), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Static sensitivity"), page), 0, 2);

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        QLabel *gateLabel = new QLabel(QString::number(i), page);
        motionSensitivitySpins[i] = new QSpinBox(page);
        staticSensitivitySpins[i] = new QSpinBox(page);
        motionSensitivitySpins[i]->setRange(0, 100);
        staticSensitivitySpins[i]->setRange(0, 100);
        grid->addWidget(gateLabel, i + 1, 0);
        grid->addWidget(motionSensitivitySpins[i], i + 1, 1);
        grid->addWidget(staticSensitivitySpins[i], i + 1, 2);
    }

    QPushButton *readButton = new QPushButton(QStringLiteral("Read Config"), page);
    QPushButton *applyButton = new QPushButton(QStringLiteral("Apply Config"), page);
    QPushButton *engOnButton = new QPushButton(QStringLiteral("Engineering On"), page);
    QPushButton *engOffButton = new QPushButton(QStringLiteral("Engineering Off"), page);
    engineeringModeCheck = new QCheckBox(QStringLiteral("Engineering mode"), page);

    buttons->addWidget(readButton);
    buttons->addWidget(applyButton);
    buttons->addWidget(engOnButton);
    buttons->addWidget(engOffButton);
    buttons->addWidget(engineeringModeCheck);
    buttons->addStretch();

    connect(readButton, &QPushButton::clicked, this, [this]() { refreshConfig(); });
    connect(applyButton, &QPushButton::clicked, this, [this]() { applyConfig(); });
    connect(engOnButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!device.setEngineeringMode(true, &error)) {
            showError(QStringLiteral("enable engineering"), error);
            return;
        }
        appendLog(QStringLiteral("engineering mode enabled"));
        refreshConfig();
        refreshState();
    });
    connect(engOffButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if (!device.setEngineeringMode(false, &error)) {
            showError(QStringLiteral("disable engineering"), error);
            return;
        }
        appendLog(QStringLiteral("engineering mode disabled"));
        refreshConfig();
        refreshState();
    });

    layout->addLayout(form);
    layout->addLayout(grid);
    layout->addLayout(buttons);
    layout->addStretch();
    return page;
}

QWidget *Ld2410TestWindow::buildMaintenancePage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(page);
    QFormLayout *form = new QFormLayout;
    QHBoxLayout *buttons = new QHBoxLayout;
    configurePageLayout(layout);
    configureFormLayout(form);
    configureButtonLayout(buttons);

    versionLabel = new QLabel(QStringLiteral("--"), page);
    noiseStatusLabel = new QLabel(QStringLiteral("--"), page);
    noiseDurationSpin = new QSpinBox(page);
    noiseDurationSpin->setRange(1, 65535);
    noiseDurationSpin->setValue(60);

    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    noiseStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    form->addRow(QStringLiteral("Version"), versionLabel);
    form->addRow(QStringLiteral("Noise status"), noiseStatusLabel);
    form->addRow(QStringLiteral("Noise duration s"), noiseDurationSpin);

    QPushButton *versionButton = new QPushButton(QStringLiteral("Read Version"), page);
    QPushButton *noiseButton = new QPushButton(QStringLiteral("Start Noise"), page);
    QPushButton *noiseStatusButton = new QPushButton(QStringLiteral("Read Noise"), page);
    QPushButton *resetButton = new QPushButton(QStringLiteral("Factory Reset"), page);
    QPushButton *rebootButton = new QPushButton(QStringLiteral("Reboot"), page);

    buttons->addWidget(versionButton);
    buttons->addWidget(noiseButton);
    buttons->addWidget(noiseStatusButton);
    buttons->addWidget(resetButton);
    buttons->addWidget(rebootButton);
    buttons->addStretch();

    connect(versionButton, &QPushButton::clicked, this, [this]() { refreshMaintenance(); });
    connect(noiseButton, &QPushButton::clicked, this, [this]() {
        const auto reply = QMessageBox::question(this, QStringLiteral("Confirm noise calibration"),
                                                 QStringLiteral("Start noise calibration now?"));
        if (reply != QMessageBox::Yes)
            return;
        QString error;
        quint16 status = 0;
        if (!device.startNoiseCalibration(static_cast<quint16>(noiseDurationSpin->value()), &status, &error)) {
            showError(QStringLiteral("start noise calibration"), error);
            return;
        }
        noiseStatusLabel->setText(QStringLiteral("running (%1)").arg(status));
        appendLog(QStringLiteral("noise calibration started"));
    });
    connect(noiseStatusButton, &QPushButton::clicked, this, [this]() {
        QString error;
        quint16 status = 0;
        if (!device.getNoiseStatus(&status, &error)) {
            showError(QStringLiteral("read noise status"), error);
            return;
        }
        noiseStatusLabel->setText(QStringLiteral("%1").arg(status));
        appendLog(QStringLiteral("noise status read: %1").arg(status));
    });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Confirm factory reset"),
                                  QStringLiteral("Restore factory settings and reboot later?")) != QMessageBox::Yes)
            return;
        QString error;
        if (!device.factoryReset(&error)) {
            showError(QStringLiteral("factory reset"), error);
            return;
        }
        appendLog(QStringLiteral("factory reset sent"));
    });
    connect(rebootButton, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Confirm reboot"),
                                  QStringLiteral("Reboot the radar module now?")) != QMessageBox::Yes)
            return;
        QString error;
        if (!device.reboot(&error)) {
            showError(QStringLiteral("reboot"), error);
            return;
        }
        appendLog(QStringLiteral("reboot sent"));
    });

    layout->addLayout(form);
    layout->addLayout(buttons);
    layout->addStretch();
    return page;
}

void Ld2410TestWindow::refreshProbe()
{
    const Ld2410Probe result = device.probe(miscEdit->text().trimmed(), outEdit->text().trimmed(), uartEdit->text().trimmed());

    probeStatus->setText(result.error.isEmpty() ? QStringLiteral("Ready") : QStringLiteral("Unavailable"));
    probeMisc->setText(QStringLiteral("%1  %2")
                           .arg(result.miscPath.isEmpty() ? QStringLiteral("--") : result.miscPath,
                                boolText(result.miscAvailable)));
    probeOut->setText(QStringLiteral("%1  %2")
                          .arg(result.outPath.isEmpty() ? QStringLiteral("--") : result.outPath,
                               boolText(result.outAvailable)));
    probeUart->setText(QStringLiteral("%1  %2")
                           .arg(result.uartPath.isEmpty() ? QStringLiteral("--") : result.uartPath,
                                boolText(result.uartAvailable)));
    probeHint->setText(result.inputHint.isEmpty() ? QStringLiteral("--") : result.inputHint);

    if (!result.error.isEmpty())
        appendLog(result.error);
}

void Ld2410TestWindow::refreshState()
{
    Ld2410State state;
    QString error;
    if (!device.readState(&state, &error)) {
        stateStatus->setText(QStringLiteral("Unavailable"));
        return;
    }

    updateStateLabels(state);
}

void Ld2410TestWindow::refreshConfig()
{
    Ld2410Config config;
    QString error;
    if (!device.readConfig(&config, &error)) {
        configStatus->setText(QStringLiteral("Config unavailable"));
        appendLog(QStringLiteral("read config failed: %1").arg(error));
        return;
    }

    updateConfigLabels(config);
    configStatus->setText(QStringLiteral("Config ready"));
}

void Ld2410TestWindow::refreshMaintenance()
{
    QString error;
    QString version;
    quint16 noiseStatus = 0;

    if (device.getVersion(&version, &error))
        versionLabel->setText(version.isEmpty() ? QStringLiteral("--") : version);
    else
        versionLabel->setText(QStringLiteral("--"));

    if (device.getNoiseStatus(&noiseStatus, &error))
        noiseStatusLabel->setText(QStringLiteral("%1").arg(noiseStatus));
    else
        noiseStatusLabel->setText(QStringLiteral("--"));
}

void Ld2410TestWindow::applyConfig()
{
    const Ld2410Config config = collectConfigFromUi();
    QString error;

    if (!device.writeConfig(config, &error)) {
        showError(QStringLiteral("apply radar config"), error);
        return;
    }
    if (!device.setResolution(config.resolutionIndex, &error)) {
        showError(QStringLiteral("set resolution"), error);
        return;
    }
    if (!device.setAuxControl(config.auxMode, config.auxThreshold, config.outDefaultHigh, &error)) {
        showError(QStringLiteral("set aux control"), error);
        return;
    }
    if (!device.setBaud(config.baud, &error)) {
        showError(QStringLiteral("set baud"), error);
        return;
    }
    if (!device.setEngineeringMode(engineeringModeCheck->isChecked(), &error)) {
        showError(QStringLiteral("set engineering mode"), error);
        return;
    }

    appendLog(QStringLiteral("config applied"));
    configStatus->setText(QStringLiteral("Config applied"));
}

void Ld2410TestWindow::updateStateLabels(const Ld2410State &state)
{
    stateStatus->setText(state.available ? QStringLiteral("Ready") : QStringLiteral("Unavailable"));
    presenceLabel->setText(boolText(state.presence));
    targetLabel->setText(targetText(state.targetState));
    sourceLabel->setText(state.source.isEmpty() ? QStringLiteral("--") : state.source);
    motionDistanceLabel->setText(state.available ? QString::number(state.movingDistanceCm) : QStringLiteral("--"));
    staticDistanceLabel->setText(state.available ? QString::number(state.staticDistanceCm) : QStringLiteral("--"));
    detectDistanceLabel->setText(state.available ? QString::number(state.detectDistanceCm) : QStringLiteral("--"));
    motionEnergyLabel->setText(QString::number(state.movingEnergy));
    staticEnergyLabel->setText(QString::number(state.staticEnergy));
    frameCountLabel->setText(QString::number(state.frameCount));
    errorCountLabel->setText(QString::number(state.errorCount));
    sequenceLabel->setText(QString::number(state.sequence));
    outLevelLabel->setText(state.outValid ? boolText(state.outLevel) : QStringLiteral("--"));
    engineeringMaxLabel->setText(QStringLiteral("motion %1 static %2").arg(state.maxMovingGate).arg(state.maxStaticGate));
    engineeringLightLabel->setText(QStringLiteral("light %1, engineering %2")
                                       .arg(state.light)
                                       .arg(boolText(state.engineering)));
    engineeringModeCheck->setChecked(state.engineering);

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        engineeringMotionGateLabels[i]->setText(QString::number(state.movingGateEnergy[i]));
        engineeringStaticGateLabels[i]->setText(QString::number(state.staticGateEnergy[i]));
    }
}

void Ld2410TestWindow::updateConfigLabels(const Ld2410Config &config)
{
    setSpinValue(maxGateSpin, config.maxGate);
    setSpinValue(motionGateSpin, config.motionGate);
    setSpinValue(staticGateSpin, config.staticGate);
    setSpinValue(idleTimeSpin, config.idleTimeS);

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        setSpinValue(motionSensitivitySpins[i], config.motionSensitivity[i]);
        setSpinValue(staticSensitivitySpins[i], config.staticSensitivity[i]);
    }
}

Ld2410Config Ld2410TestWindow::collectConfigFromUi() const
{
    Ld2410Config config;
    config.maxGate = static_cast<quint8>(maxGateSpin->value());
    config.motionGate = static_cast<quint8>(motionGateSpin->value());
    config.staticGate = static_cast<quint8>(staticGateSpin->value());
    config.idleTimeS = static_cast<quint16>(idleTimeSpin->value());
    config.resolutionIndex = static_cast<quint8>(resolutionCombo->currentData().toUInt());
    config.baud = baudFromCombo(baudCombo);
    config.auxMode = static_cast<quint8>(auxModeCombo->currentData().toUInt());
    config.auxThreshold = static_cast<quint8>(auxThresholdSpin->value());
    config.outDefaultHigh = outDefaultHighCheck->isChecked();

    for (int i = 0; i < LD2410C_MAX_GATES; ++i) {
        config.motionSensitivity[i] = static_cast<quint8>(motionSensitivitySpins[i]->value());
        config.staticSensitivity[i] = static_cast<quint8>(staticSensitivitySpins[i]->value());
    }

    return config;
}

void Ld2410TestWindow::showError(const QString &action, const QString &error)
{
    const QString message = QStringLiteral("%1 failed: %2").arg(action, error);
    appendLog(message);
}

QString Ld2410TestWindow::targetText(quint8 targetState)
{
    switch (targetState) {
    case LD2410C_TARGET_NONE:
        return QStringLiteral("none");
    case LD2410C_TARGET_MOVING:
        return QStringLiteral("moving");
    case LD2410C_TARGET_STATIC:
        return QStringLiteral("static");
    case LD2410C_TARGET_MOVING_STATIC:
        return QStringLiteral("moving + static");
    case LD2410C_TARGET_NOISE_RUNNING:
        return QStringLiteral("noise running");
    case LD2410C_TARGET_NOISE_SUCCESS:
        return QStringLiteral("noise success");
    case LD2410C_TARGET_NOISE_FAILED:
        return QStringLiteral("noise failed");
    default:
        return QStringLiteral("unknown(%1)").arg(targetState);
    }
}

QString Ld2410TestWindow::boolText(bool value)
{
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

quint32 Ld2410TestWindow::baudFromCombo(QComboBox *combo)
{
    return combo->currentData().toUInt();
}

void Ld2410TestWindow::setSpinValue(QSpinBox *spin, int value)
{
    if (spin)
        spin->setValue(value);
}

} // namespace imx6sm
