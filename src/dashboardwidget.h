#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// DashboardWidget — central power-flow overview.  Shows solar, grid, battery
// and estimated load power, plus inverter temperature.
class DashboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget *parent = nullptr);

public slots:
    void updatePvData(const PvData &data);
    void updateGridData(const GridData &data);
    void updateBatteryData(const BatteryData &data);
    void updateTemperature(double celsius);

private:
    // Recalculates and updates the estimated load label.
    void refreshLoad();

    QLabel *m_pvPower       = nullptr;
    QLabel *m_gridPower     = nullptr;
    QLabel *m_batteryPower  = nullptr;
    QLabel *m_loadPower     = nullptr;
    QLabel *m_temperature   = nullptr;

    // Cached values for load computation
    double m_cachedPvKw      = 0.0;
    double m_cachedGridKw    = 0.0;
    double m_cachedBatteryW  = 0.0;
};
