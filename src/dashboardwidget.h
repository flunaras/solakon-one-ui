#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// DashboardWidget — central power-flow overview.  Shows solar, grid, battery
// and estimated load power, plus inverter temperature and the current work
// mode (register 49203).  Work mode is shown prominently because it changes
// the entire inverter behaviour (Self Use vs Feed-in vs Force Charge etc.);
// it is highlighted whenever the inverter is in any mode other than Self Use.
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
    void updateWorkMode(WorkMode mode);
    void updateRemoteControlState(const RemoteControlState &state);

private:
    // Recalculates and updates the estimated load label.
    void refreshLoad();
    // Re-renders the Work Mode tile from the cached work mode + remote-control
    // state. Force Charge / Force Discharge are commonly the inverter's
    // response to an active remote-control session (register 46001 bit 0 set),
    // not a user-configured preference; surfacing that cause prevents the
    // operator from assuming the mode change came from the FoxESS app.
    void refreshWorkModeLabel();

    QLabel *m_pvPower       = nullptr;
    QLabel *m_gridPower     = nullptr;
    QLabel *m_batteryPower  = nullptr;
    QLabel *m_loadPower     = nullptr;
    QLabel *m_temperature   = nullptr;
    QLabel *m_workMode      = nullptr;

    // Cached values for load computation
    double m_cachedPvKw      = 0.0;
    double m_cachedGridKw    = 0.0;
    double m_cachedBatteryW  = 0.0;

    // Cached state for the Work Mode tile. -1 means not yet received.
    WorkMode           m_cachedWorkMode    = WorkMode::Unknown;
    RemoteControlState m_cachedRemoteState{};
    bool               m_haveWorkMode      = false;
    bool               m_haveRemoteState   = false;
};
