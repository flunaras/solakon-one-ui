#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// AlarmWidget — displays the decoded STATUS_1 bitfield (standby / operation /
// fault), the GRID_STATUS bitfield (Off-Grid / EPS island mode) and the raw
// ALARM_1/2/3 bitfields as hex values.
class AlarmWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AlarmWidget(QWidget *parent = nullptr);

public slots:
    void updateStatus(const InverterStatus &status);
    void updateGridStatus(const GridStatus &status);
    void updateAlarms(const AlarmState &alarms);

private:
    QLabel *m_statusLine     = nullptr;
    QLabel *m_gridStatusLine = nullptr;
    QLabel *m_alarm1Label    = nullptr;
    QLabel *m_alarm2Label    = nullptr;
    QLabel *m_alarm3Label    = nullptr;
};
