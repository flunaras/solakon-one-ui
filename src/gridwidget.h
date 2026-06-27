#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// GridWidget — displays grid phase voltages, active/reactive power,
// power factor and frequency.
class GridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GridWidget(QWidget *parent = nullptr);

public slots:
    void updateData(const GridData &data);

private:
    QLabel *m_voltageR       = nullptr;
    QLabel *m_voltageS       = nullptr;
    QLabel *m_voltageT       = nullptr;
    QLabel *m_activePower    = nullptr;
    QLabel *m_reactivePower  = nullptr;
    QLabel *m_powerFactor    = nullptr;
    QLabel *m_frequency      = nullptr;
};
