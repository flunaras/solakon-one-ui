#include "gridwidget.h"
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

GridWidget::GridWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("Grid"), this);
    auto *form  = new QFormLayout(group);

    m_voltageR      = makeValueLabel(this);
    m_voltageS      = makeValueLabel(this);
    m_voltageT      = makeValueLabel(this);
    m_activePower   = makeValueLabel(this);
    m_reactivePower = makeValueLabel(this);
    m_powerFactor   = makeValueLabel(this);
    m_frequency     = makeValueLabel(this);

    form->addRow(QStringLiteral("Phase R Voltage:"),  m_voltageR);
    form->addRow(QStringLiteral("Phase S Voltage:"),  m_voltageS);
    form->addRow(QStringLiteral("Phase T Voltage:"),  m_voltageT);
    form->addRow(QStringLiteral("Active Power:"),     m_activePower);
    form->addRow(QStringLiteral("Reactive Power:"),   m_reactivePower);
    form->addRow(QStringLiteral("Power Factor:"),     m_powerFactor);
    form->addRow(QStringLiteral("Frequency:"),        m_frequency);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

void GridWidget::updateData(const GridData &data)
{
    m_voltageR->setText(     QStringLiteral("%1 V").arg(data.voltageR,            0, 'f', 1));
    m_voltageS->setText(     QStringLiteral("%1 V").arg(data.voltageS,            0, 'f', 1));
    m_voltageT->setText(     QStringLiteral("%1 V").arg(data.voltageT,            0, 'f', 1));
    // Positive = export to grid; negative = import from grid (verify sign convention)
    m_activePower->setText(  formatPower(data.activePowerKw * 1000.0));
    m_reactivePower->setText(formatReactivePower(data.reactivePowerKvar * 1000.0));
    m_powerFactor->setText(  QStringLiteral("%1").arg(data.powerFactor,           0, 'f', 3));
    m_frequency->setText(    QStringLiteral("%1 Hz").arg(data.frequencyHz,        0, 'f', 2));
}
