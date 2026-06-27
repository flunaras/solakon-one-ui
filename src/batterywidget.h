#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// BatteryWidget — displays battery voltage, current, power,
// combined power, State of Charge percentage and charging status.
class BatteryWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BatteryWidget(QWidget *parent = nullptr);

public slots:
    void updateData(const BatteryData &data);

private:
    QLabel *m_soc           = nullptr;
    QLabel *m_voltage       = nullptr;
    QLabel *m_current       = nullptr;
    QLabel *m_power         = nullptr;
    QLabel *m_combinedPower = nullptr;
    QLabel *m_statusLabel   = nullptr;
};
