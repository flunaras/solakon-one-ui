#include "remotecontrolwidget.h"

#include "imodbusapi.h"

#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

// ── Constructor ────────────────────────────────────────────────────────────────

RemoteControlWidget::RemoteControlWidget(IModbusApi *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(QStringLiteral("Remote Control"));
    setMinimumWidth(360);

    auto *form = new QFormLayout();

    // ── Current read-back state ────────────────────────────────────────────
    // Shows the live value of register 46001 so the operator can see whether
    // a foreign session (e.g. FoxESS app strategy period) is currently engaged
    // before pressing Apply.
    m_currentStateLabel = new QLabel(QStringLiteral("--"), this);
    m_currentStateLabel->setAlignment(Qt::AlignCenter);
    m_currentStateLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    form->addRow(QStringLiteral("Current state:"), m_currentStateLabel);

    // ── Enable ─────────────────────────────────────────────────────────────
    m_enableCheck = new QCheckBox(QStringLiteral("Enable remote control"), this);
    form->addRow(m_enableCheck);

    // ── Direction ──────────────────────────────────────────────────────────
    auto *dirGroup = new QGroupBox(QStringLiteral("Direction"), this);
    auto *dirLayout = new QHBoxLayout(dirGroup);
    m_generateRadio = new QRadioButton(QStringLiteral("Generate (inject)"), dirGroup);
    m_consumeRadio  = new QRadioButton(QStringLiteral("Consume (absorb)"),  dirGroup);
    m_generateRadio->setChecked(true);
    auto *dirBtnGroup = new QButtonGroup(dirGroup);
    dirBtnGroup->addButton(m_generateRadio, 0);
    dirBtnGroup->addButton(m_consumeRadio,  1);
    dirLayout->addWidget(m_generateRadio);
    dirLayout->addWidget(m_consumeRadio);
    form->addRow(dirGroup);

    // ── Target ─────────────────────────────────────────────────────────────
    auto *tgtGroup = new QGroupBox(QStringLiteral("Target"), this);
    auto *tgtLayout = new QHBoxLayout(tgtGroup);
    m_acRadio      = new QRadioButton(QStringLiteral("AC"),      tgtGroup);
    m_batteryRadio = new QRadioButton(QStringLiteral("Battery"), tgtGroup);
    m_gridRadio    = new QRadioButton(QStringLiteral("Grid"),    tgtGroup);
    m_acRadio->setChecked(true);
    auto *tgtBtnGroup = new QButtonGroup(tgtGroup);
    tgtBtnGroup->addButton(m_acRadio,      0);
    tgtBtnGroup->addButton(m_batteryRadio, 1);
    tgtBtnGroup->addButton(m_gridRadio,    2);
    tgtLayout->addWidget(m_acRadio);
    tgtLayout->addWidget(m_batteryRadio);
    tgtLayout->addWidget(m_gridRadio);
    form->addRow(tgtGroup);

    // ── Timeout ────────────────────────────────────────────────────────────
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(0, 65535);
    m_timeoutSpin->setValue(60);
    m_timeoutSpin->setSuffix(QStringLiteral(" s"));
    form->addRow(QStringLiteral("Timeout:"), m_timeoutSpin);

    // ── Power set points ───────────────────────────────────────────────────
    m_activePowerSpin = new QSpinBox(this);
    m_activePowerSpin->setRange(-1000000, 1000000);
    m_activePowerSpin->setSuffix(QStringLiteral(" W"));
    form->addRow(QStringLiteral("Active Power:"), m_activePowerSpin);

    m_reactivePowerSpin = new QSpinBox(this);
    m_reactivePowerSpin->setRange(-1000000, 1000000);
    m_reactivePowerSpin->setSuffix(QStringLiteral(" Var"));
    form->addRow(QStringLiteral("Reactive Power:"), m_reactivePowerSpin);

    // ── Status / buttons ───────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    auto *applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    connect(applyBtn, &QPushButton::clicked, this, &RemoteControlWidget::onApply);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &RemoteControlWidget::hide);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(closeBtn);

    auto *top = new QVBoxLayout(this);
    top->addLayout(form);
    top->addWidget(m_statusLabel);
    top->addLayout(btnRow);
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void RemoteControlWidget::onApply()
{
    m_api->writeRemoteTimeout(m_timeoutSpin->value());
    m_api->writeRemoteActivePower(m_activePowerSpin->value());
    m_api->writeRemoteReactivePower(m_reactivePowerSpin->value());
    // Write the bitfield last so the inverter sees consistent power values
    m_api->writeRemoteControl(buildBitfield());
    // Re-read the bitfield so the "Current state" line reflects the new value
    // without waiting for the next poll cycle.
    m_api->readRemoteControlState();
    showStatus(QStringLiteral("Sending remote control command..."));
}

void RemoteControlWidget::updateState(const RemoteControlState &state)
{
    if (state.enabled) {
        const QString text =
            QStringLiteral("Active — %1, target: %2 (0x%3)")
                .arg(state.consume ? QStringLiteral("consume") : QStringLiteral("generate"),
                     state.targetLabel(),
                     QStringLiteral("%1").arg(state.raw, 4, 16, QLatin1Char('0')).toUpper());
        m_currentStateLabel->setText(text);
        m_currentStateLabel->setStyleSheet(
            QStringLiteral("color: orange; font-weight: bold;"));
    } else {
        m_currentStateLabel->setText(
            QStringLiteral("Inactive (0x%1)")
                .arg(state.raw, 4, 16, QLatin1Char('0')).toUpper());
        m_currentStateLabel->setStyleSheet(
            QStringLiteral("color: green; font-weight: bold;"));
    }
}

// ── Private helpers ────────────────────────────────────────────────────────────

quint16 RemoteControlWidget::buildBitfield() const
{
    quint16 bits = 0;
    if (m_enableCheck->isChecked())    bits |= 0x01u;  // bit 0: enable
    if (m_consumeRadio->isChecked())   bits |= 0x02u;  // bit 1: direction (0=gen,1=cons)

    // bits 3-2: target (00=AC, 01=Battery, 10=Grid)
    int target = 0;
    if (m_batteryRadio->isChecked()) target = 1;
    else if (m_gridRadio->isChecked()) target = 2;
    bits |= static_cast<quint16>((target & 0x03) << 2);

    return bits;
}

void RemoteControlWidget::showStatus(const QString &message, bool ok)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(
        ok ? QStringLiteral("color: green;") : QStringLiteral("color: red;"));
}
