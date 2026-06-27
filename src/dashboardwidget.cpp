#include "dashboardwidget.h"
#include "powerformat.h"

#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

// ── Helper: make a titled tile ─────────────────────────────────────────────────

static QGroupBox *makeTile(const QString &title, QLabel *&valueLabel, QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    auto *layout = new QVBoxLayout(box);

    valueLabel = new QLabel(QStringLiteral("--"), box);
    valueLabel->setAlignment(Qt::AlignCenter);
    QFont f = valueLabel->font();
    f.setPointSize(f.pointSize() + 4);
    f.setBold(true);
    valueLabel->setFont(f);

    layout->addWidget(valueLabel);
    return box;
}

// ── Constructor ────────────────────────────────────────────────────────────────

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *grid = new QGridLayout(this);
    grid->setSpacing(12);
    grid->setContentsMargins(16, 16, 16, 16);

    // Row 0: Solar / Grid / Battery / Load / Work Mode
    // Work mode shares the top row so the operator can read the inverter's
    // current control strategy at the same glance as the power-flow tiles.
    // updateWorkMode() recolours the label to highlight any non-Self-Use mode.
    grid->addWidget(makeTile(QStringLiteral("Solar"),     m_pvPower,      this), 0, 0);
    grid->addWidget(makeTile(QStringLiteral("Grid"),      m_gridPower,    this), 0, 1);
    grid->addWidget(makeTile(QStringLiteral("Battery"),   m_batteryPower, this), 0, 2);
    grid->addWidget(makeTile(QStringLiteral("Load"),      m_loadPower,    this), 0, 3);
    grid->addWidget(makeTile(QStringLiteral("Work Mode"), m_workMode,     this), 0, 4);

    // Row 1: Temperature (spans all columns)
    auto *tempBox = new QGroupBox(QStringLiteral("Inverter Temperature"), this);
    auto *tempLayout = new QVBoxLayout(tempBox);
    m_temperature = new QLabel(QStringLiteral("-- °C"), this);
    m_temperature->setAlignment(Qt::AlignCenter);
    tempLayout->addWidget(m_temperature);
    grid->addWidget(tempBox, 1, 0, 1, 5);

    grid->setRowStretch(2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    grid->setColumnStretch(4, 1);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void DashboardWidget::updatePvData(const PvData &data)
{
    m_cachedPvKw = data.totalPvPowerKw;
    m_pvPower->setText(formatPower(data.totalPvPowerKw * 1000.0));
    refreshLoad();
}

void DashboardWidget::updateGridData(const GridData &data)
{
    m_cachedGridKw = data.activePowerKw;
    // Positive = export, negative = import — show with sign and direction label
    const QString direction = data.activePowerKw >= 0
        ? QStringLiteral("Export")
        : QStringLiteral("Import");
    m_gridPower->setText(
        QStringLiteral("%1\n(%2)")
            .arg(formatPower(data.activePowerKw * 1000.0))
            .arg(direction));
    refreshLoad();
}

void DashboardWidget::updateBatteryData(const BatteryData &data)
{
    m_cachedBatteryW = data.combinedPowerW;
    // Positive = charging, negative = discharging (verify sign with inverter display)
    const QString direction = data.isCharging()    ? QStringLiteral("Charging")
                            : data.isDischarging() ? QStringLiteral("Discharging")
                                                   : QStringLiteral("Idle");
    m_batteryPower->setText(
        QStringLiteral("%1\n(%2)")
            .arg(formatPower(data.combinedPowerW))
            .arg(direction));
    refreshLoad();
}

void DashboardWidget::updateTemperature(double celsius)
{
    m_temperature->setText(QStringLiteral("%1 °C").arg(celsius, 0, 'f', 1));
}

void DashboardWidget::updateWorkMode(WorkMode mode)
{
    m_cachedWorkMode = mode;
    m_haveWorkMode   = true;
    refreshWorkModeLabel();
}

void DashboardWidget::updateRemoteControlState(const RemoteControlState &state)
{
    m_cachedRemoteState = state;
    m_haveRemoteState   = true;
    refreshWorkModeLabel();
}

// ── Private helpers ────────────────────────────────────────────────────────────

void DashboardWidget::refreshWorkModeLabel()
{
    if (!m_haveWorkMode) {
        m_workMode->setText(QStringLiteral("--"));
        m_workMode->setStyleSheet(QString());
        return;
    }

    const QString mainLine = workModeLabel(m_cachedWorkMode);

    // When register 46001 bit 0 is set, the inverter is operating under
    // remote control: an external client (this app, the powerregulator tool,
    // or the FoxESS app's strategy periods) has issued an active power
    // setpoint via registers 46002/46003/46005. The inverter typically
    // reports work mode 6 (Force Charge) or 7 (Force Discharge) while the
    // remote command is in effect — *not* because the user configured those
    // modes, but as the runtime response to the remote setpoint.
    //
    // Surface the cause on a second line, matching the two-line "value
    // (state)" pattern used by the Grid and Battery tiles, so the operator
    // can tell at a glance whether the displayed mode reflects a manual
    // configuration or an active remote-control session.
    QString secondLine;
    if (m_haveRemoteState && m_cachedRemoteState.enabled) {
        secondLine = QStringLiteral("Remote control active");
    } else if (m_haveRemoteState) {
        secondLine = QStringLiteral("No remote control");
    } else {
        // Reading register 46001 has not yet completed since connect.
        secondLine = QStringLiteral("--");
    }

    m_workMode->setText(QStringLiteral("%1\n(%2)").arg(mainLine, secondLine));

    // Self Use is the "normal" steady-state mode where the inverter covers
    // house loads from PV/battery and exports only surplus.  Any other mode
    // (Feed-in Priority, Backup, Peak Shaving, Force Charge/Discharge, or an
    // unknown value) is highlighted so the operator notices immediately —
    // this matches the powerregulator's --require-self-use precondition.
    if (m_cachedWorkMode == WorkMode::SelfUse) {
        m_workMode->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    } else if (m_cachedWorkMode == WorkMode::Unknown) {
        m_workMode->setStyleSheet(QStringLiteral("color: red; font-weight: bold;"));
    } else {
        m_workMode->setStyleSheet(QStringLiteral("color: orange; font-weight: bold;"));
    }
}

void DashboardWidget::refreshLoad()
{
    // Estimated load = PV generation
    //                - battery charging power (positive = charging reduces load)
    //                + battery discharging power
    //                - grid export (positive = export)
    //                + grid import (negative activePower = import)
    //
    // All values in kW:
    const double battKw = m_cachedBatteryW / 1000.0;
    // load = pv - battCharging + gridImport
    // battKw > 0 → charging (consume from PV/grid), battKw < 0 → discharging (supply to load)
    // activePowerKw > 0 → export (taken from system), < 0 → import (added to system)
    const double loadKw = m_cachedPvKw - battKw - m_cachedGridKw;
    m_loadPower->setText(formatPower(qMax(0.0, loadKw) * 1000.0));
}
