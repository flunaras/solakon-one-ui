#include "batterywidget.h"
#include "powerformat.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

static QLabel *makeValueLabel(QWidget *parent)
{
    auto *lbl = new QLabel(QStringLiteral("--"), parent);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setMinimumWidth(100);
    return lbl;
}

BatteryWidget::BatteryWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("Battery"), this);
    auto *form  = new QFormLayout(group);

    m_soc           = makeValueLabel(this);
    m_voltage       = makeValueLabel(this);
    m_current       = makeValueLabel(this);
    m_power         = makeValueLabel(this);
    m_combinedPower = makeValueLabel(this);

    m_statusLabel = new QLabel(QStringLiteral("--"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));

    form->addRow(QStringLiteral("State of Charge:"), m_soc);
    form->addRow(QStringLiteral("Voltage:"),        m_voltage);
    form->addRow(QStringLiteral("Current:"),        m_current);
    form->addRow(QStringLiteral("Power:"),          m_power);
    form->addRow(QStringLiteral("Combined Power:"), m_combinedPower);
    form->addRow(QStringLiteral("Status:"),         m_statusLabel);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

void BatteryWidget::updateData(const BatteryData &data)
{
    if (data.socPercent >= 0)
        m_soc->setText(QStringLiteral("%1 %").arg(data.socPercent));
    else
        m_soc->setText(QStringLiteral("--"));

    m_voltage->setText(      QStringLiteral("%1 V").arg(data.voltageV,       0, 'f', 1));
    m_current->setText(      QStringLiteral("%1 A").arg(data.currentA,       0, 'f', 3));
    m_power->setText(        formatPower(data.powerW));
    m_combinedPower->setText(formatPower(data.combinedPowerW));

    if (data.isCharging()) {
        m_statusLabel->setText(QStringLiteral("Charging"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
    } else if (data.isDischarging()) {
        m_statusLabel->setText(QStringLiteral("Discharging"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: orange; font-weight: bold;"));
    } else {
        m_statusLabel->setText(QStringLiteral("Idle"));
        m_statusLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    }
}
