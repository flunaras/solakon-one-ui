#include "energywidget.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

static QLabel *makeValueLabel(QWidget *parent)
{
    auto *lbl = new QLabel(QStringLiteral("--"), parent);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setMinimumWidth(110);
    return lbl;
}

EnergyWidget::EnergyWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("Energy Statistics"), this);
    auto *form  = new QFormLayout(group);

    m_dailyKwh      = makeValueLabel(this);
    m_cumulativeKwh = makeValueLabel(this);

    form->addRow(QStringLiteral("Today's Generation:"), m_dailyKwh);
    form->addRow(QStringLiteral("Total Generation:"),   m_cumulativeKwh);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

void EnergyWidget::updateData(const EnergyData &data)
{
    m_dailyKwh->setText(     QStringLiteral("%1 kWh").arg(data.dailyKwh,      0, 'f', 2));
    m_cumulativeKwh->setText(QStringLiteral("%1 kWh").arg(data.cumulativeKwh, 0, 'f', 2));
}
