#include "infowidget.h"
#include "powerformat.h"

#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

static QLabel *makeValueLabel(QWidget *parent)
{
    auto *lbl = new QLabel(QStringLiteral("--"), parent);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setMinimumWidth(120);
    return lbl;
}

InfoWidget::InfoWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("Device Information"), this);
    auto *form  = new QFormLayout(group);

    m_modelName      = makeValueLabel(this);
    m_serialNumber   = makeValueLabel(this);
    m_mfgId          = makeValueLabel(this);
    m_masterVersion  = makeValueLabel(this);
    m_slaveVersion   = makeValueLabel(this);
    m_managerVersion = makeValueLabel(this);
    m_ratedPower     = makeValueLabel(this);
    m_maxActivePower = makeValueLabel(this);

    form->addRow(QStringLiteral("Model:"),            m_modelName);
    form->addRow(QStringLiteral("Serial Number:"),    m_serialNumber);
    form->addRow(QStringLiteral("Manufacturer ID:"),  m_mfgId);
    form->addRow(QStringLiteral("Master FW:"),        m_masterVersion);
    form->addRow(QStringLiteral("Slave FW:"),         m_slaveVersion);
    form->addRow(QStringLiteral("Manager FW:"),       m_managerVersion);
    form->addRow(QStringLiteral("Rated Power:"),      m_ratedPower);
    form->addRow(QStringLiteral("Max Active Power:"), m_maxActivePower);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

void InfoWidget::updateData(const InverterInfo &info)
{
    m_modelName->setText(     info.modelName.isEmpty()    ? QStringLiteral("--") : info.modelName);
    m_serialNumber->setText(  info.serialNumber.isEmpty() ? QStringLiteral("--") : info.serialNumber);
    m_mfgId->setText(         info.mfgId.isEmpty()        ? QStringLiteral("--") : info.mfgId);
    m_masterVersion->setText( QStringLiteral("%1").arg(info.masterVersion));
    m_slaveVersion->setText(  QStringLiteral("%1").arg(info.slaveVersion));
    m_managerVersion->setText(QStringLiteral("%1").arg(info.managerVersion));
    m_ratedPower->setText(    formatPower(info.ratedPowerKw     * 1000.0));
    m_maxActivePower->setText(formatPower(info.maxActivePowerKw * 1000.0));
}
