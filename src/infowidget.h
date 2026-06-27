#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// InfoWidget — displays static device information read once after connect:
// model name, serial number, firmware versions and rated power.
class InfoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit InfoWidget(QWidget *parent = nullptr);

public slots:
    void updateData(const InverterInfo &info);

private:
    QLabel *m_modelName      = nullptr;
    QLabel *m_serialNumber   = nullptr;
    QLabel *m_mfgId          = nullptr;
    QLabel *m_masterVersion  = nullptr;
    QLabel *m_slaveVersion   = nullptr;
    QLabel *m_managerVersion = nullptr;
    QLabel *m_ratedPower     = nullptr;
    QLabel *m_maxActivePower = nullptr;
};
