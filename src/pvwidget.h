#pragma once

#include "inverterdata.h"

#include <QtWidgets/QLabel>
#include <QtWidgets/QWidget>

// PvWidget — grid table for PV1–PV4 string voltage, current and power.
// Total PV power is promoted to the top row in bold for quick reading.
// Inactive strings (V ≈ 0 and I ≈ 0) are dimmed with "--" placeholders.
// Row header colors match the PV chart (green → yellow-green → amber → gold).
class PvWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PvWidget(QWidget *parent = nullptr);

public slots:
    void updateData(const PvData &data);

private:
    struct StringRow {
        QLabel *label   = nullptr;  // colored "PV1" / "PV2" / ...
        QLabel *voltage = nullptr;
        QLabel *current = nullptr;
        QLabel *power   = nullptr;
    };

    StringRow m_rows[4];
    QLabel   *m_totalPvPower = nullptr;

    void setRowActive(int idx, bool active);
};
