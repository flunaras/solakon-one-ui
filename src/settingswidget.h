#pragma once

#include "inverterdata.h"

#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>

class ModbusApi;

// ── SettingsWidget ─────────────────────────────────────────────────────────────
//
// Non-modal dialog for editing and writing writable inverter registers:
// work mode, SoC limits, battery current limits, and power limits.
//
// loadSettings() is called whenever ModbusApi emits settingsRead().
// Write requests are sent directly to the ModbusApi pointer passed to the
// constructor.  Range validation is enforced in the UI before writing.
class SettingsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWidget(ModbusApi *api, QWidget *parent = nullptr);

public slots:
    void loadSettings(const InverterSettings &settings);

private slots:
    void onApplyWorkMode();
    void onApplySoCLimits();
    void onApplyCurrentLimits();
    void onApplyPowerLimits();

private:
    void createWorkModeTab(class QTabWidget *tabs);
    void createBatteryTab(class QTabWidget *tabs);
    void createPowerTab(class QTabWidget *tabs);
    void showStatus(const QString &message, bool ok = true);

    ModbusApi *m_api;

    // Work mode
    QComboBox *m_workModeCombo  = nullptr;

    // SoC limits
    QSpinBox  *m_minSocSpin        = nullptr;
    QSpinBox  *m_maxSocSpin        = nullptr;
    QSpinBox  *m_minSocOnGridSpin  = nullptr;

    // Current limits
    QDoubleSpinBox *m_maxChargeSpin   = nullptr;
    QDoubleSpinBox *m_maxDischargeSpin = nullptr;

    // Power limits
    QSpinBox *m_importLimitSpin   = nullptr;
    QSpinBox *m_exportLimitSpin   = nullptr;
    QSpinBox *m_exportLimit2Spin  = nullptr;
    QSpinBox *m_threshSocSpin     = nullptr;

    QLabel *m_statusLabel = nullptr;
};
