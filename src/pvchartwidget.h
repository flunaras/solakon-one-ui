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
// A translucent full-height shaded band (see updateRemoteControlOverlay())
// marks spans where the FoxESS REMOTE_CONTROL register (46001, bit 0) was
// enabled, so it's visually obvious when the inverter was being driven
// remotely (e.g. by the FoxESS app's "strategy periods" feature, or by this
// app's own Remote Control dialog) rather than following its normal local
// logic. Fed via onRemoteControlStateUpdated(); latched the same way as
// grid/battery power and recorded per-sample alongside them.
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
QT_FORWARD_DECLARE_CLASS(QGraphicsPathItem)
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

    /// Latches whether the FoxESS REMOTE_CONTROL register is currently
    /// enabled (bit 0 of RemoteControlState). Does NOT trigger a redraw by
    /// itself — the next recorded sample (onBatteryDataUpdated()) carries
    /// this flag forward so remote-control-active spans line up with the
    /// same timestamps as the PV/grid/battery data.
    void onRemoteControlStateUpdated(const RemoteControlState &state);

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

    // Repositions the custom gridlines (see m_gridLines) when the widget —
    // and therefore the chart's plotArea() — is resized, since that can
    // happen without any new data arriving to otherwise trigger a redraw.
    void resizeEvent(QResizeEvent *event) override;

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
    // remoteControlActive = whether REMOTE_CONTROL (register 46001, bit 0)
    //              was enabled at this instant, latched from
    //              RemoteControlState. Drives the shaded overlay bands in
    //              rebuildSeries() so it is obvious when the inverter is
    //              being driven remotely (e.g. by the FoxESS app's strategy
    //              periods) vs. operating under its own local logic.
    struct Sample {
        qint64 timestampMs;
        double cum[4];
        double exportedW;
        double importedW;
        double loadW;
        double batteryW;
        bool   remoteControlActive;
    };

    void appendSample(const PvData &data);
    void pruneHistory();

    // ── Chart updates ──────────────────────────────────────────────────────
    void rebuildSeries();
    void updateScrollBar();
    void applyTimeWindow();
    void updateYAxis(double maxW, double minW, const QString &unitLabel);
    // Rebuilds the shaded "remote control active" overlay rectangles spanning
    // the full plot height for each contiguous run of samples with
    // remoteControlActive == true, in the currently visible time window.
    // Must run after applyTimeWindow()/updateYAxis() so plotArea()/
    // mapToPosition() reflect the final axis ranges for this rebuild.
    void updateRemoteControlOverlay();
    // Redraws the custom gridlines (see m_gridLines) at the current X/Y axis
    // tick positions. Must run after applyTimeWindow()/updateYAxis() (so the
    // axis ranges/tick interval are current) and after
    // updateRemoteControlOverlay() (so gridlines end up on top of the
    // bands). Also invoked on chart view resize since plotArea() changes
    // without necessarily receiving new data.
    void updateGridOverlay();
    // Redraws m_batteryLineOverlay — a QGraphicsPathItem duplicate of
    // m_netSeries's own line, drawn at a Z-value above the custom gridlines
    // (updateGridOverlay() draws those at z=10) so the battery power trace
    // stays visible on top of the grid instead of being occluded by it. The
    // native m_netSeries is still kept (and still attached to both axes) so
    // legend/hover/axis-range behavior is unaffected — this overlay is a
    // purely visual duplicate on top. Must run after applyTimeWindow() (so
    // mapToPosition() reflects the final axis ranges) using the same
    // batteryPts computed in rebuildSeries(); also invoked on view resize
    // since plotArea() changes without necessarily receiving new data.
    void updateBatteryLineOverlay();

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

    // Shaded bands marking spans where REMOTE_CONTROL was active, one
    // QGraphicsRectItem per contiguous run within the visible window. Drawn
    // behind the plot data (low Z-value) and parented to m_chart so their
    // coordinates line up with plotArea()/mapToPosition() directly. Rebuilt
    // from scratch (old items deleted) on every updateRemoteControlOverlay()
    // call since the number of runs varies per redraw.
    QVector<QGraphicsRectItem *> m_remoteControlBands;

    // Custom-drawn gridlines. Qt Charts always paints series *above* the
    // axes' built-in gridlines (there is no public API to reorder that), so
    // the native gridlines (m_axisX/m_axisY) are disabled entirely and these
    // QGraphicsLineItems are drawn instead, at a Z-value above every data
    // series and the remote-control bands, so the grid is always visible on
    // top — matching the hover crosshair's approach (m_hoverLine). Rebuilt
    // by updateGridOverlay() on every redraw and on view resize.
    QVector<QGraphicsLineItem *> m_gridLines;
    // Custom X-axis (time) tick labels, one per vertical gridline in
    // m_gridLines, drawn at the same "nice" round timestamps computed by
    // computeNiceTimeStep() (see updateGridOverlay()). Replace the native
    // QDateTimeAxis labels (m_axisX->setLabelsVisible(false) in buildChart())
    // since QDateTimeAxis always spaces its tickCount() labels evenly
    // between min()/max() with no rounding to whole seconds/minutes/hours,
    // which looks inconsistent (e.g. "19:06, 19:08, 19:09, 19:11 ...").
    QVector<QGraphicsSimpleTextItem *> m_gridLabelsX;

    // Visual duplicate of m_netSeries (the "Battery" line), drawn as a plain
    // QGraphicsPathItem parented to m_chart at a Z-value above m_gridLines
    // (z=10) — Qt Charts has no public API to reorder a native QAbstractSeries
    // above our own top-level QGraphicsItems, so instead of trying to fight
    // that, the same points are traced a second time on top. m_netSeries
    // itself is left untouched (still attached to both axes, still driving
    // the legend entry and the hover tooltip's battery reading) — this item
    // is purely cosmetic so the battery trace doesn't visually disappear
    // behind the grid. Rebuilt by updateBatteryLineOverlay() on every redraw
    // and on view resize, from the same batteryPts cached in m_lastBatteryPts.
    // Invisible clip container (no pen/brush) sized to the plot area and
    // parented to m_chart with ItemClipsChildrenToShape set; m_batteryLineOverlay
    // is parented to this rather than directly to m_chart so the trace never
    // bleeds past the plot area edges. Without this, points whose timestamp
    // falls outside the current axis range (e.g. the left-anchor sample used
    // by rebuildSeries() to avoid a gap at the visible window's edge) map to
    // scene X coordinates left of plotArea.left(), and since a plain
    // QGraphicsPathItem has no clipping of its own, the line segment leading
    // to that off-range point was drawn straight across the widget outside
    // the chart's left border.
    QGraphicsRectItem *m_batteryLineClipRect = nullptr;
    QGraphicsPathItem *m_batteryLineOverlay = nullptr;
    // Cached from the most recent rebuildSeries() call so
    // updateBatteryLineOverlay() can re-map points to scene coordinates on a
    // plain resize (plotArea() change) without needing a new sample.
    QVector<QPointF> m_lastBatteryPts;

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

    // ── Latched remote control state ────────────────────────────────────────
    // Updated by onRemoteControlStateUpdated(); read by appendSample() at the
    // next PV tick so each sample records whether REMOTE_CONTROL was active
    // at that instant.
    bool m_lastRemoteControlActive = false;

    // ── Cached PV reading ───────────────────────────────────────────────────
    // Set by onPvDataUpdated(); consumed by onBatteryDataUpdated() (the last
    // block read each poll cycle) to record a sample once PV/grid/battery all
    // reflect the same tick. m_hasPendingPvData is false until the first PV
    // reading arrives.
    PvData m_pendingPvData;
    bool   m_hasPendingPvData = false;
};
