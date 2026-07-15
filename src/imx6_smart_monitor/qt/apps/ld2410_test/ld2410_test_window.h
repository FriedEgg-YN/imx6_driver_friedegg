#ifndef IMX6SMARTMONITOR_LD2410_TEST_WINDOW_H
#define IMX6SMARTMONITOR_LD2410_TEST_WINDOW_H

#include "qt/common/module_test_window.h"
#include "sensors/ld2410_device.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;
class QWidget;

namespace imx6sm {

class Ld2410TestWindow : public ModuleTestWindow {
public:
    explicit Ld2410TestWindow(QWidget *parent = nullptr);

private:
    QWidget *buildProbePage();
    QWidget *buildRealtimePage();
    QWidget *buildEngineeringPage();
    QWidget *buildConfigPage();
    QWidget *buildMaintenancePage();

    void refreshProbe();
    void refreshState();
    void refreshConfig();
    void refreshMaintenance();
    void applyConfig();
    void updateStateLabels(const Ld2410State &state);
    void updateConfigLabels(const Ld2410Config &config);
    Ld2410Config collectConfigFromUi() const;
    void showError(const QString &action, const QString &error);
    static QString targetText(quint8 targetState);
    static QString boolText(bool value);
    static quint32 baudFromCombo(QComboBox *combo);
    static void setSpinValue(QSpinBox *spin, int value);

    Ld2410Device device;
    QTabWidget *tabs;
    QTimer *refreshTimer;

    QLineEdit *miscEdit;
    QLineEdit *outEdit;
    QLineEdit *uartEdit;
    QLabel *probeStatus;
    QLabel *probeMisc;
    QLabel *probeOut;
    QLabel *probeUart;
    QLabel *probeHint;

    QLabel *stateStatus;
    QLabel *presenceLabel;
    QLabel *targetLabel;
    QLabel *sourceLabel;
    QLabel *motionDistanceLabel;
    QLabel *staticDistanceLabel;
    QLabel *detectDistanceLabel;
    QLabel *motionEnergyLabel;
    QLabel *staticEnergyLabel;
    QLabel *frameCountLabel;
    QLabel *errorCountLabel;
    QLabel *sequenceLabel;
    QLabel *outLevelLabel;

    QLabel *engineeringMaxLabel;
    QLabel *engineeringLightLabel;
    QLabel *engineeringMotionGateLabels[LD2410C_MAX_GATES];
    QLabel *engineeringStaticGateLabels[LD2410C_MAX_GATES];

    QLabel *configStatus;
    QSpinBox *maxGateSpin;
    QSpinBox *motionGateSpin;
    QSpinBox *staticGateSpin;
    QSpinBox *idleTimeSpin;
    QComboBox *resolutionCombo;
    QComboBox *baudCombo;
    QComboBox *auxModeCombo;
    QSpinBox *auxThresholdSpin;
    QCheckBox *outDefaultHighCheck;
    QCheckBox *engineeringModeCheck;
    QSpinBox *motionSensitivitySpins[LD2410C_MAX_GATES];
    QSpinBox *staticSensitivitySpins[LD2410C_MAX_GATES];

    QLabel *versionLabel;
    QLabel *noiseStatusLabel;
    QSpinBox *noiseDurationSpin;
};

} // namespace imx6sm

#endif
