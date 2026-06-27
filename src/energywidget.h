#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// EnergyWidget — displays today's and lifetime cumulative generation.
class EnergyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EnergyWidget(QWidget *parent = nullptr);

public slots:
    void updateData(const EnergyData &data);

private:
    QLabel *m_dailyKwh      = nullptr;
    QLabel *m_cumulativeKwh = nullptr;
};
