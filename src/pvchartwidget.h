#pragma once

// ── PvChartWidget ─────────────────────────────────────────────────────────────
//
// Rolling stacked-area chart for PV1–PV4 string power over a configurable
// time window (5 min – 24 h), with overlay series for exported grid power,
// estimated house load, and battery power.
//
// PV strings occupy coloured bands stacked bottom-to-top.  A single
// semi-transparent red "Grid" area shows the exported/imported grid flow,
// switching baseline depending on direction: while exporting
// (activePowerKw > 0) it spans from 0 down to −exportedW, below the zero
// line; while importing (activePowerKw < 0) it spans from 0 up to
// importedW, stacked directly *beneath* the PV bands — the whole PV stack
// is shifted up by importedW so the import band renders as the bottom-most
// part of the supply stack (0 → importedW → PV1 → PV2 → PV3 → PV4), rather
// than above the PV bands.  The two directions never overlap for a given
// sample, so a single QAreaSeries (one legend entry, one colour) represents
// both.  A semi-transparent orange area is stacked directly beneath the
// export portion (−exportedW down to −(exportedW+loadW)) showing estimated
// house load as a negative bar on the same Y-axis.  A black line (width 2)
// at batteryW (positive = charging, above the zero line; negative =
// discharging, below the zero line) marks the battery power following the
// fritzhome mixed-group overlay pattern (single Y-axis, no second axis).
//
// Scrolling (fritzhome pattern):
//   A QScrollBar below the chart drives the visible time window.  While the
//   bar is at its maximum the chart tracks "now" (live mode).  Dragging it
//   left pauses live tracking and lets the user inspect historical data.
//   Returning the bar to its maximum resumes live mode.
//
// Feed PV samples via onPvDataUpdated(); feed grid data via
// onGridDataUpdated() and battery data via onBatteryDataUpdated(). Within a
// single poll cycle the inverter is read in blocks — PV first, then grid,
// then battery last (see ModbusApi::onPollTimerFired()) — so PvData for
// "now" always arrives before this cycle's GridData/BatteryData. To avoid
// pairing the current PV reading with the *previous* cycle's battery value
// (which visually looks like the battery overlay lags one interval behind),
// onPvDataUpdated() only caches the PV reading; the actual sample is
// recorded and the chart rebuilt on onBatteryDataUpdated() — the last signal
// in the poll cycle — once all three values (PV/grid/battery) reflect the
// same tick.  The latest GridData power (signed: positive = export, negative
// = import) is latched into m_lastGridPowerW as before.  Up to 24 h of
// samples are retained.
//
// Stacking contract (PV bands, and the export/load bands below the zero
// line):
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
QT_FORWARD_DECLARE_CLASS(QGraphicsLineItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsRectItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsSimpleTextItem)
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
    /// Caches the latest PV reading; does NOT append a sample or redraw by
    /// itself. PV is the first block read each poll cycle, so the sample is
    /// only recorded once the same cycle's battery reading arrives (see
    /// onBatteryDataUpdated()) to keep PV/grid/battery values time-aligned.
    void onPvDataUpdated(const PvData &data);

    /// Latches the latest grid power value used for the next recorded
    /// sample. Does NOT trigger a chart redraw by itself.
    void onGridDataUpdated(const GridData &data);

    /// Latches the latest battery power value, then — since battery is the
    /// last block read each poll cycle — records a new sample from the
    /// cached PV reading plus the latched grid/battery values and redraws
    /// the chart.
    void onBatteryDataUpdated(const BatteryData &data);

private slots:
    void onWindowComboChanged(int index);
    void onScrollBarChanged(int value);
    void onLockYScaleToggled(bool checked);

protected:
    // Handles mouse hover/leave over the chart view: draws the vertical
    // dotted crosshair line and shows a persistent info box with the sample
    // data at the hovered timestamp (does NOT auto-hide on a timer, unlike
    // QToolTip — it only disappears when the mouse leaves the plot area).
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // ── Setup ──────────────────────────────────────────────────────────────
    void buildChart();
    void buildControls();   // window combo + lock checkbox + scroll bar

    // ── History ────────────────────────────────────────────────────────────
    // One sample: timestamp + 4 cumulative PV power levels + grid/load/battery (W).
    //
    // cum[0] = pv1Power
    // cum[1] = pv1Power + pv2Power
    // cum[2] = pv1Power + pv2Power + pv3Power
    // cum[3] = total PV power  (all four strings)
    // exportedW  = grid export power at this instant (W, ≥ 0; 0 when importing).
    //              Contributes to the "Grid" band from 0 down to −exportedW.
    // importedW  = grid import power at this instant (W, ≥ 0; 0 when exporting).
    //              Contributes to the "Grid" band from 0 up to importedW,
    //              stacked directly beneath the PV bands (the PV levels in
    //              cum[] are themselves offset by importedW when rendered —
    //              see rebuildSeries() — so the whole PV stack shifts up to
    //              make room for this band underneath it).  Only one of
    //              exportedW / importedW is non-zero per sample.
    // loadW      = estimated house load at this instant (W, ≥ 0), computed as
    //              totalPvW − batteryChargingW − gridPowerW (signed grid power;
    //              same formula as DashboardWidget::refreshLoad(), clamped to
    //              ≥ 0).  Rendered stacked directly beneath the export band,
    //              from −exportedW down to −(exportedW + loadW).
    // batteryW   = battery power at this instant (W; positive = charging,
    //              negative = discharging), latched from BatteryData
    struct Sample {
        qint64 timestampMs;
        double cum[4];
        double exportedW;
        double importedW;
        double loadW;
        double batteryW;
    };

    void appendSample(const PvData &data);
    void pruneHistory();

    // ── Chart updates ──────────────────────────────────────────────────────
    void rebuildSeries();
    void updateScrollBar();
    void applyTimeWindow();
    void updateYAxis(double maxW, double minW, const QString &unitLabel);

    // ── Helpers ────────────────────────────────────────────────────────────
    // Returns the right-edge timestamp of the visible window (ms since epoch).
    // Returns currentMSecsSinceEpoch() in live mode (scroll bar at maximum).
    qint64 computeWindowEndMs() const;

    // Reduces pts to at most maxPoints preserving local min and max per bucket.
    static QVector<QPointF> downsampleMinMax(const QVector<QPointF> &pts, int maxPoints);
    qint64 windowMs() const;

    // ── Hover crosshair / tooltip ────────────────────────────────────────────
    // Returns the sample whose timestamp is closest to targetMs, or nullptr
    // if there is no recorded history.
    const Sample *findNearestSample(qint64 targetMs) const;
    // Builds the multi-line tooltip text describing the given sample.
    static QString formatTooltip(const Sample &sample);
    void hideHoverCrosshair();
    // Positions/resizes the info box background rect to fit the text item,
    // anchored near the given viewport-local mouse position (view coords).
    void positionHoverInfoBox(const QPoint &viewportPos);

    // ── State persistence ──────────────────────────────────────────────────
    void saveState() const;
    void loadState();

    // ── Chart objects ──────────────────────────────────────────────────────
    QChart        *m_chart           = nullptr;
    QChartView    *m_chartView       = nullptr;
    QAreaSeries   *m_areaSeries[4]   = {};
    // Exported/imported overlay — a single "Grid" QAreaSeries whose baseline
    // switches per-point depending on flow direction: exporting renders below
    // the zero line (0 down to −exportedW), importing renders stacked
    // directly beneath the PV bands (0 up to importedW; the PV bands
    // themselves are shifted up by importedW in rebuildSeries() to make room
    // for this band).  Computed each rebuild so a single upper/lower pair
    // correctly covers both cases (see rebuildSeries).
    QAreaSeries   *m_gridSeries      = nullptr;
    // Load overlay: orange area stacked directly beneath the export portion
    // of the grid band, from −exportedW down to −(exportedW + loadW),
    // mirroring the PV band stacking pattern above the zero line.
    QAreaSeries   *m_loadSeries      = nullptr;
    // Net line: black line at battery power (positive = charging, negative =
    // discharging), following the fritzhome mixed-group overlay pattern.
    QLineSeries   *m_netSeries       = nullptr;
    QDateTimeAxis *m_axisX           = nullptr;
    QValueAxis    *m_axisY           = nullptr;
    // Vertical dotted crosshair line shown while hovering over the plot
    // area; parented to m_chart so its coordinates line up with
    // m_chart->plotArea() / mapToValue() directly. Hidden when the mouse
    // leaves the chart view or the plot area.
    QGraphicsLineItem *m_hoverLine   = nullptr;
    // Persistent info box (background + text) shown alongside the
    // crosshair; added directly to the chart view's QGraphicsScene (view
    // coordinates), not parented to m_chart, so it can be freely positioned
    // near the cursor regardless of the plot area's data coordinates. Unlike
    // QToolTip, it has no auto-hide timer — it is only hidden explicitly in
    // hideHoverCrosshair().
    QGraphicsRectItem       *m_hoverInfoBg   = nullptr;
    QGraphicsSimpleTextItem *m_hoverInfoText = nullptr;

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

    // ── Latched grid power ──────────────────────────────────────────────────
    // Updated by onGridDataUpdated(); read by appendSample() at the next PV
    // tick. Signed: positive = export, negative = import.
    double m_lastGridPowerW = 0.0;
    // ── Latched battery power ──────────────────────────────────────────────
    // Updated by onBatteryDataUpdated(); read by appendSample() at the next PV
    // tick. Positive = charging, negative = discharging (matches BatteryData).
    double m_lastBatteryW = 0.0;

    // ── Cached PV reading ───────────────────────────────────────────────────
    // Set by onPvDataUpdated(); consumed by onBatteryDataUpdated() (the last
    // block read each poll cycle) to record a sample once PV/grid/battery all
    // reflect the same tick. m_hasPendingPvData is false until the first PV
    // reading arrives.
    PvData m_pendingPvData;
    bool   m_hasPendingPvData = false;
};
