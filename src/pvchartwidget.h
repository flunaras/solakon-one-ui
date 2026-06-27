#pragma once

// ── PvChartWidget ─────────────────────────────────────────────────────────────
//
// Rolling stacked-area chart for PV1–PV4 string power over a configurable
// time window (5 min – 24 h), with overlay series for exported grid power and
// net locally-consumed power.
//
// PV strings occupy coloured bands stacked bottom-to-top; the top edge of the
// topmost band equals total PV power.  A semi-transparent red area below the
// zero line (spanning from 0 down to −exportedW) shows the exported power as a
// negative quantity on the same Y-axis.  A black line (width 2) at
// netW = totalPvW − exportedW marks the net power following the fritzhome
// mixed-group overlay pattern (single Y-axis, no second axis).
//
// Scrolling (fritzhome pattern):
//   A QScrollBar below the chart drives the visible time window.  While the
//   bar is at its maximum the chart tracks "now" (live mode).  Dragging it
//   left pauses live tracking and lets the user inspect historical data.
//   Returning the bar to its maximum resumes live mode.
//
// Feed PV samples via onPvDataUpdated(); feed grid data via
// onGridDataUpdated().  Only PvData drives sample recording and chart redraws;
// the latest GridData export value is latched into m_lastExportedW and used at
// the next PV sample.  Up to 24 h of samples are retained.
//
// Stacking contract (PV bands):
//   lowerSeries[s] receives the same point list as upperSeries[s-1], so band
//   edges align perfectly.  Each QAreaSeries owns its upper and lower
//   QLineSeries (Qt takes ownership), so data is duplicated as needed.

#include "inverterdata.h"

#include <QtCore/QPointF>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

QT_FORWARD_DECLARE_CLASS(QAreaSeries)
QT_FORWARD_DECLARE_CLASS(QChart)
QT_FORWARD_DECLARE_CLASS(QChartView)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QDateTimeAxis)
QT_FORWARD_DECLARE_CLASS(QLineSeries)
QT_FORWARD_DECLARE_CLASS(QScrollBar)
QT_FORWARD_DECLARE_CLASS(QValueAxis)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QLabel)

class PvChartWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PvChartWidget(QWidget *parent = nullptr);
    ~PvChartWidget() override;

public slots:
    /// Records a new PV sample and redraws the chart.
    void onPvDataUpdated(const PvData &data);

    /// Latches the latest grid export value used for the next PV sample.
    /// Does NOT trigger a chart redraw by itself; only PvData drives redraws.
    void onGridDataUpdated(const GridData &data);

private slots:
    void onWindowComboChanged(int index);
    void onScrollBarChanged(int value);
    void onLockYScaleToggled(bool checked);

private:
    // ── Setup ──────────────────────────────────────────────────────────────
    void buildChart();
    void buildControls();   // window combo + lock checkbox + scroll bar

    // ── History ────────────────────────────────────────────────────────────
    // One sample: timestamp + 4 cumulative PV power levels + export/net (W).
    //
    // cum[0] = pv1Power
    // cum[1] = pv1Power + pv2Power
    // cum[2] = pv1Power + pv2Power + pv3Power
    // cum[3] = total PV power  (all four strings)
    // exportedW  = grid export power at this instant (W, ≥ 0; 0 when importing)
    // netW       = cum[3] − exportedW  (positive: PV surplus; negative: battery
    //              is discharging to supplement the export beyond PV production)
    struct Sample {
        qint64 timestampMs;
        double cum[4];
        double exportedW;
        double netW;
    };

    void appendSample(const PvData &data);
    void pruneHistory();

    // ── Chart updates ──────────────────────────────────────────────────────
    void rebuildSeries();
    void updateScrollBar();
    void applyTimeWindow();
    void updateYAxis(const QVector<QPointF> &totalPts, double minW, const QString &unitLabel);

    // ── Helpers ────────────────────────────────────────────────────────────
    // Returns the right-edge timestamp of the visible window (ms since epoch).
    // Returns currentMSecsSinceEpoch() in live mode (scroll bar at maximum).
    qint64 computeWindowEndMs() const;

    // Reduces pts to at most maxPoints preserving local min and max per bucket.
    static QVector<QPointF> downsampleMinMax(const QVector<QPointF> &pts, int maxPoints);
    qint64 windowMs() const;

    // ── State persistence ──────────────────────────────────────────────────
    void saveState() const;
    void loadState();

    // ── Chart objects ──────────────────────────────────────────────────────
    QChart        *m_chart           = nullptr;
    QChartView    *m_chartView       = nullptr;
    QAreaSeries   *m_areaSeries[4]   = {};
    // Exported overlay: red area from 0 down to −exportedW (below zero line).
    QAreaSeries   *m_exportSeries    = nullptr;
    // Net line: black line at netW = totalPvW − exportedW (fritzhome pattern).
    QLineSeries   *m_netSeries       = nullptr;
    QDateTimeAxis *m_axisX           = nullptr;
    QValueAxis    *m_axisY           = nullptr;

    // ── UI ─────────────────────────────────────────────────────────────────
    QScrollBar *m_scrollBar          = nullptr;
    QCheckBox  *m_lockYScaleCheckBox = nullptr;
    QComboBox  *m_windowCombo        = nullptr;
    QLabel     *m_noDataLabel        = nullptr;
    int         m_windowIndex        = 4;    // default: 2 h

    // ── Scroll / Y-lock state ───────────────────────────────────────────────
    bool m_scrollAtEnd = true;   // true = live (bar pinned to maximum)
    bool m_lockYScale  = false;  // true = Y axis frozen; updateYAxis() is a no-op

    // ── History storage ────────────────────────────────────────────────────
    static constexpr qint64 kMaxHistoryMs    = 24LL * 60 * 60 * 1000;
    static constexpr int    kMaxSeriesPoints = 800;
    QVector<Sample> m_history;

    // ── Latched grid export ────────────────────────────────────────────────
    // Updated by onGridDataUpdated(); read by appendSample() at the next PV tick.
    double m_lastExportedW = 0.0;
};
