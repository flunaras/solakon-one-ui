#pragma once

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpinBox>

class ModbusApi;

// ── RemoteControlWidget ────────────────────────────────────────────────────────
//
// Non-modal dialog for writing the REMOTE_CONTROL bitfield (46001) and
// accompanying power / timeout registers.
//
// All bits of the REMOTE_CONTROL register are derived from the current UI
// state and written as a complete value on each Apply, avoiding the need
// for a read-modify-write cycle (the widget owns all bits).
//
// REMOTE_CONTROL bitfield layout:
//   bit 0:   enable (1 = remote control active)
//   bit 1:   direction (0 = generate/injection, 1 = consume/absorption)
//   bits 3-2: target (00 = AC, 01 = Battery, 10 = Grid)
class RemoteControlWidget : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteControlWidget(ModbusApi *api, QWidget *parent = nullptr);

private slots:
    void onApply();

private:
    quint16 buildBitfield() const;
    void showStatus(const QString &message, bool ok = true);

    ModbusApi *m_api;

    QCheckBox     *m_enableCheck    = nullptr;

    // Direction
    QRadioButton  *m_generateRadio  = nullptr;
    QRadioButton  *m_consumeRadio   = nullptr;

    // Target
    QRadioButton  *m_acRadio        = nullptr;
    QRadioButton  *m_batteryRadio   = nullptr;
    QRadioButton  *m_gridRadio      = nullptr;

    QSpinBox      *m_timeoutSpin    = nullptr;
    QSpinBox      *m_activePowerSpin  = nullptr;
    QSpinBox      *m_reactivePowerSpin = nullptr;

    QLabel *m_statusLabel = nullptr;
};
