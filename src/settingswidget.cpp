#include "settingswidget.h"

#include "modbusapi.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>

// ── Constructor ────────────────────────────────────────────────────────────────

SettingsWidget::SettingsWidget(ModbusApi *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("Inverter Settings"));
    setMinimumWidth(380);

    auto *tabs = new QTabWidget(this);
    createWorkModeTab(tabs);
    createBatteryTab(tabs);
    createPowerTab(tabs);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &SettingsWidget::hide);

    auto *top = new QVBoxLayout(this);
    top->addWidget(tabs);
    top->addWidget(m_statusLabel);
    top->addWidget(closeBtn, 0, Qt::AlignRight);
}

// ── Tab builders ───────────────────────────────────────────────────────────────

void SettingsWidget::createWorkModeTab(QTabWidget *tabs)
{
    auto *widget = new QWidget(this);
    auto *form   = new QFormLayout(widget);

    m_workModeCombo = new QComboBox(widget);
    // Items aligned with WorkMode enum — value stored in ItemData
    m_workModeCombo->addItem(QStringLiteral("Self Use"),        1);
    m_workModeCombo->addItem(QStringLiteral("Feed-in Priority"),2);
    m_workModeCombo->addItem(QStringLiteral("Backup"),          3);
    m_workModeCombo->addItem(QStringLiteral("Peak Shaving"),    4);
    m_workModeCombo->addItem(QStringLiteral("Force Charge"),    6);
    m_workModeCombo->addItem(QStringLiteral("Force Discharge"), 7);
    // Note: mode 5 is intentionally absent (invalid per protocol)
    form->addRow(QStringLiteral("Work Mode:"), m_workModeCombo);

    auto *applyBtn = new QPushButton(QStringLiteral("Apply"), widget);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsWidget::onApplyWorkMode);
    form->addRow(QString(), applyBtn);

    tabs->addTab(widget, QStringLiteral("Work Mode"));
}

void SettingsWidget::createBatteryTab(QTabWidget *tabs)
{
    auto *widget = new QWidget(this);
    auto *form   = new QFormLayout(widget);

    m_minSocSpin = new QSpinBox(widget);
    m_minSocSpin->setRange(10, 100);
    m_minSocSpin->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("Min SoC:"), m_minSocSpin);

    m_maxSocSpin = new QSpinBox(widget);
    m_maxSocSpin->setRange(10, 100);
    m_maxSocSpin->setValue(100);
    m_maxSocSpin->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("Max SoC:"), m_maxSocSpin);

    m_minSocOnGridSpin = new QSpinBox(widget);
    m_minSocOnGridSpin->setRange(10, 100);
    m_minSocOnGridSpin->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("Min SoC On-Grid:"), m_minSocOnGridSpin);

    m_maxChargeSpin = new QDoubleSpinBox(widget);
    m_maxChargeSpin->setRange(0.0, 50.0);
    m_maxChargeSpin->setDecimals(1);
    m_maxChargeSpin->setSuffix(QStringLiteral(" A"));
    form->addRow(QStringLiteral("Max Charge Current:"), m_maxChargeSpin);

    m_maxDischargeSpin = new QDoubleSpinBox(widget);
    m_maxDischargeSpin->setRange(0.0, 50.0);
    m_maxDischargeSpin->setDecimals(1);
    m_maxDischargeSpin->setSuffix(QStringLiteral(" A"));
    form->addRow(QStringLiteral("Max Discharge Current:"), m_maxDischargeSpin);

    auto *socBtn = new QPushButton(QStringLiteral("Apply SoC Limits"), widget);
    connect(socBtn, &QPushButton::clicked, this, &SettingsWidget::onApplySoCLimits);

    auto *currBtn = new QPushButton(QStringLiteral("Apply Current Limits"), widget);
    connect(currBtn, &QPushButton::clicked, this, &SettingsWidget::onApplyCurrentLimits);

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(socBtn);
    btnRow->addWidget(currBtn);
    form->addRow(btnRow);

    tabs->addTab(widget, QStringLiteral("Battery"));
}

void SettingsWidget::createPowerTab(QTabWidget *tabs)
{
    auto *widget = new QWidget(this);
    auto *form   = new QFormLayout(widget);

    m_importLimitSpin = new QSpinBox(widget);
    m_importLimitSpin->setRange(0, 100000);
    m_importLimitSpin->setSuffix(QStringLiteral(" W"));
    form->addRow(QStringLiteral("Import Power Limit:"), m_importLimitSpin);

    m_exportLimitSpin = new QSpinBox(widget);
    m_exportLimitSpin->setRange(0, 100000);
    m_exportLimitSpin->setSuffix(QStringLiteral(" W"));
    form->addRow(QStringLiteral("Export Power Limit:"), m_exportLimitSpin);

    m_exportLimit2Spin = new QSpinBox(widget);
    m_exportLimit2Spin->setRange(0, 100000);
    m_exportLimit2Spin->setSuffix(QStringLiteral(" W"));
    form->addRow(QStringLiteral("Export Power Limit 2:"), m_exportLimit2Spin);

    m_threshSocSpin = new QSpinBox(widget);
    m_threshSocSpin->setRange(0, 100);
    m_threshSocSpin->setSuffix(QStringLiteral(" %"));
    form->addRow(QStringLiteral("Threshold SoC:"), m_threshSocSpin);

    auto *applyBtn = new QPushButton(QStringLiteral("Apply"), widget);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsWidget::onApplyPowerLimits);
    form->addRow(QString(), applyBtn);

    tabs->addTab(widget, QStringLiteral("Power Limits"));
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void SettingsWidget::loadSettings(const InverterSettings &s)
{
    // Work mode — find matching combo entry by stored integer data
    for (int i = 0; i < m_workModeCombo->count(); ++i) {
        if (m_workModeCombo->itemData(i).toInt() == static_cast<int>(s.workMode)) {
            m_workModeCombo->setCurrentIndex(i);
            break;
        }
    }

    m_minSocSpin->setValue(s.minSocPercent);
    m_maxSocSpin->setValue(s.maxSocPercent);
    m_minSocOnGridSpin->setValue(s.minSocOnGridPercent);
    m_maxChargeSpin->setValue(s.maxChargeCurrentA);
    m_maxDischargeSpin->setValue(s.maxDischargeCurrentA);
    m_importLimitSpin->setValue(s.importPowerLimitW);
    m_exportLimitSpin->setValue(s.exportPowerLimitW);
    m_exportLimit2Spin->setValue(s.exportPowerLimit2W);
    m_threshSocSpin->setValue(s.thresholdSocPercent);
}

void SettingsWidget::onApplyWorkMode()
{
    const int mode = m_workModeCombo->currentData().toInt();
    m_api->writeWorkMode(mode);
    showStatus(QStringLiteral("Sending work mode %1...").arg(mode));
}

void SettingsWidget::onApplySoCLimits()
{
    const int minSoC = m_minSocSpin->value();
    const int maxSoC = m_maxSocSpin->value();

    if (minSoC >= maxSoC) {
        showStatus(QStringLiteral("Error: Min SoC must be less than Max SoC."), false);
        return;
    }

    m_api->writeMinSoC(minSoC);
    m_api->writeMaxSoC(maxSoC);
    m_api->writeMinSoCOnGrid(m_minSocOnGridSpin->value());
    showStatus(QStringLiteral("Sending SoC limits..."));
}

void SettingsWidget::onApplyCurrentLimits()
{
    m_api->writeBatteryMaxChargeCurrent(m_maxChargeSpin->value());
    m_api->writeBatteryMaxDischargeCurrent(m_maxDischargeSpin->value());
    showStatus(QStringLiteral("Sending current limits..."));
}

void SettingsWidget::onApplyPowerLimits()
{
    m_api->writeImportPowerLimit(m_importLimitSpin->value());
    m_api->writeExportPowerLimit(m_exportLimitSpin->value());
    m_api->writeExportPowerLimit2(m_exportLimit2Spin->value());
    m_api->writeThresholdSoC(m_threshSocSpin->value());
    showStatus(QStringLiteral("Sending power limits..."));
}

// ── Private helpers ────────────────────────────────────────────────────────────

void SettingsWidget::showStatus(const QString &message, bool ok)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(
        ok ? QStringLiteral("color: green;") : QStringLiteral("color: red;"));
}
