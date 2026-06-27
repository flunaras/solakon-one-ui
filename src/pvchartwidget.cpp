#include "pvchartwidget.h"

#include <QtCharts/QAreaSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCore/QDateTime>
#include <QtCore/QSettings>
#include <QtGui/QPainter>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

#include <cmath>

// ── Window table ───────────────────────────────────────────────────────────────

static const QList<qint64> kWindowMsTable = {
    5LL  * 60 * 1000,       // 0 – 5 min
    15LL * 60 * 1000,       // 1 – 15 min
    30LL * 60 * 1000,       // 2 – 30 min
    60LL * 60 * 1000,       // 3 – 1 h
    2LL  * 60 * 60 * 1000,  // 4 – 2 h  (default)
    4LL  * 60 * 60 * 1000,  // 5 – 4 h
    8LL  * 60 * 60 * 1000,  // 6 – 8 h
    12LL * 60 * 60 * 1000,  // 7 – 12 h
    24LL * 60 * 60 * 1000,  // 8 – 24 h
};

static const QStringList kWindowLabels = {
    QStringLiteral("5 min"),
    QStringLiteral("15 min"),
    QStringLiteral("30 min"),
    QStringLiteral("1 h"),
    QStringLiteral("2 h"),
    QStringLiteral("4 h"),
    QStringLiteral("8 h"),
    QStringLiteral("12 h"),
    QStringLiteral("24 h"),
};

// ── PV string area colours ─────────────────────────────────────────────────────

// Stacked from bottom to top: PV1 (green) → PV2 (yellow-green) → PV3 (amber) → PV4 (gold)
static const QColor kAreaColors[4] = {
    QColor(0x00, 0xAA, 0x00),  // PV1 – green
    QColor(0x66, 0xBB, 0x00),  // PV2 – yellow-green
    QColor(0xCC, 0xCC, 0x00),  // PV3 – amber-yellow
    QColor(0xFF, 0xD7, 0x00),  // PV4 – gold
};

// Exported overlay colour (red, semi-transparent)
static const QColor kExportFillColor(0xFF, 0x44, 0x44, 140);
static const QColor kExportPenColor (0xCC, 0x00, 0x00);

// ── Y-axis tick helpers ────────────────────────────────────────────────────────

// Returns the smallest "nice" step (1·10ⁿ, 2·10ⁿ, or 5·10ⁿ) that divides
// range into approximately targetTicks equal intervals.
static double computeNiceStep(double range, int targetTicks)
{
    if (range <= 0.0 || targetTicks < 1)
        return 1.0;
    const double raw       = range / static_cast<double>(targetTicks);
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm      = raw / magnitude;
    double niceNorm;
    if      (norm < 1.5) niceNorm = 1.0;
    else if (norm < 3.5) niceNorm = 2.0;
    else if (norm < 7.5) niceNorm = 5.0;
    else                 niceNorm = 10.0;
    return niceNorm * magnitude;
}

// ── Constructor / Destructor ────────────────────────────────────────────────────

PvChartWidget::PvChartWidget(QWidget *parent)
    : QWidget(parent)
{
    buildChart();
    buildControls();

    m_noDataLabel = new QLabel(QStringLiteral("Waiting for PV data..."), this);
    m_noDataLabel->setAlignment(Qt::AlignCenter);

    auto *topRow = new QHBoxLayout;
    topRow->addStretch();
    topRow->addWidget(m_lockYScaleCheckBox);
    topRow->addSpacing(8);
    topRow->addWidget(new QLabel(QStringLiteral("Window:"), this));
    topRow->addWidget(m_windowCombo);

    auto *root = new QVBoxLayout(this);
    root->addLayout(topRow);
    root->addWidget(m_chartView, 1);
    root->addWidget(m_noDataLabel, 1);
    root->addWidget(m_scrollBar);

    // Hide chart until the first sample arrives
    m_chartView->hide();

    loadState();
    m_windowCombo->blockSignals(true);
    m_windowCombo->setCurrentIndex(m_windowIndex);
    m_windowCombo->blockSignals(false);
    m_lockYScaleCheckBox->setChecked(m_lockYScale);
}

PvChartWidget::~PvChartWidget() = default;

// ── Slots ──────────────────────────────────────────────────────────────────────

void PvChartWidget::onPvDataUpdated(const PvData &data)
{
    appendSample(data);
    if (m_noDataLabel->isVisible()) {
        m_noDataLabel->hide();
        m_chartView->show();
    }
    updateScrollBar();
    rebuildSeries();
}

void PvChartWidget::onGridDataUpdated(const GridData &data)
{
    // Latch export power (W); imports (negative activePowerKw) are treated as 0.
    // Chart is NOT rebuilt here — only onPvDataUpdated() drives redraws, keeping
    // export data tied to the PV poll cadence.
    m_lastExportedW = std::max(0.0, data.activePowerKw * 1000.0);
}

void PvChartWidget::onWindowComboChanged(int index)
{
    m_windowIndex = index;
    saveState();
    updateScrollBar();
    rebuildSeries();
}

void PvChartWidget::onScrollBarChanged(int value)
{
    m_scrollAtEnd = (value >= m_scrollBar->maximum());
    rebuildSeries();
}

void PvChartWidget::onLockYScaleToggled(bool checked)
{
    m_lockYScale = checked;
    saveState();
    if (!checked)
        rebuildSeries();  // rescale Y immediately on unlock
}

// ── Private helpers ────────────────────────────────────────────────────────────

// Constructor
void PvChartWidget::buildChart()
{
    m_chart = new QChart;
    m_chart->setTitle(QString());
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->setMargins(QMargins(4, 4, 4, 4));
    m_chart->setBackgroundRoundness(0);

    // ── Stacked area series ─────────────────────────────────────────────────
    // Series are added bottom-to-top so PV1 is rendered first (below) and
    // PV4 last (on top).  Each QAreaSeries owns its upper and lower
    // QLineSeries via Qt parent-child mechanism (Qt sets itself as parent).
    // Therefore each band carries independent line-series objects; the lower
    // series of band s+1 receives the same point data as the upper series of
    // band s so that band edges tile perfectly.

    const QString pvNames[4] = {
        QStringLiteral("PV1"),
        QStringLiteral("PV2"),
        QStringLiteral("PV3"),
        QStringLiteral("PV4"),
    };

    for (int s = 0; s < 4; ++s) {
        auto *upper = new QLineSeries;
        // Always provide an explicit lower series — even for PV1 (s==0).  If we
        // passed nullptr Qt fills down to the axis minimum, which bleeds into the
        // negative export region once the axis min goes below zero.  The lower
        // series for PV1 is populated with Y=0 points in rebuildSeries().
        auto *lower = new QLineSeries;

        m_areaSeries[s] = new QAreaSeries(upper, lower);
        m_areaSeries[s]->setName(pvNames[s]);

        QColor fillCol = kAreaColors[s];
        fillCol.setAlpha(200);
        m_areaSeries[s]->setBrush(fillCol);

        QPen borderPen(kAreaColors[s].darker(130));
        borderPen.setWidth(1);
        m_areaSeries[s]->setPen(borderPen);

        m_chart->addSeries(m_areaSeries[s]);
    }

    // ── X axis: date/time ───────────────────────────────────────────────────
    m_axisX = new QDateTimeAxis;
    m_axisX->setFormat(QStringLiteral("hh:mm"));
    m_axisX->setTitleText(QStringLiteral("Time"));
    m_axisX->setTickCount(6);
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    // ── Y axis: watts ───────────────────────────────────────────────────────
    m_axisY = new QValueAxis;
    m_axisY->setTitleText(QStringLiteral("Power (W)"));
    // TicksDynamic + anchor=0 guarantees a tick exactly at Y=0 regardless of
    // range; updateYAxis() supplies the step so all ticks land on round values.
    m_axisY->setTickType(QValueAxis::TicksDynamic);
    m_axisY->setTickAnchor(0.0);
    m_axisY->setTickInterval(1000.0);  // placeholder; replaced by first data
    m_axisY->setRange(-1000.0, 5000.0);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // ── Exported power overlay ──────────────────────────────────────────────
    // Blue area below the zero line (upper = Y=0, lower = −exportedW).  Export
    // is rendered as a negative quantity on the same single Y-axis so the chart
    // reads: positive region = PV production (gold stacks) + net line; negative
    // region = grid export (blue area).  No second axis required.
    {
        auto *exportUpper = new QLineSeries;
        auto *exportLower = new QLineSeries;
        m_exportSeries = new QAreaSeries(exportUpper, exportLower);
        m_exportSeries->setName(QStringLiteral("Exported"));
        m_exportSeries->setBrush(kExportFillColor);
        QPen exportPen(kExportPenColor);
        exportPen.setWidth(1);
        m_exportSeries->setPen(exportPen);
        m_chart->addSeries(m_exportSeries);
    }

    // ── Net locally-consumed power line ────────────────────────────────────
    // Black line (width 2) at totalPvW − exportedW, following the fritzhome
    // mixed-group overlay pattern: the net is the signed sum of all
    // producer/consumer members, drawn on the same Y-axis with no second axis.
    {
        m_netSeries = new QLineSeries;
        m_netSeries->setName(QStringLiteral("Net (local)"));
        QPen netPen(QColor(0, 0, 0));
        netPen.setWidth(2);
        m_netSeries->setPen(netPen);
        m_chart->addSeries(m_netSeries);
    }

    // Attach both axes to all series (PV bands + overlay)
    for (int s = 0; s < 4; ++s) {
        m_areaSeries[s]->attachAxis(m_axisX);
        m_areaSeries[s]->attachAxis(m_axisY);
    }
    m_exportSeries->attachAxis(m_axisX);
    m_exportSeries->attachAxis(m_axisY);
    m_netSeries->attachAxis(m_axisX);
    m_netSeries->attachAxis(m_axisY);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(200);
}

void PvChartWidget::buildControls()
{
    // Window combo
    m_windowCombo = new QComboBox(this);
    for (const QString &lbl : kWindowLabels)
        m_windowCombo->addItem(lbl);
    connect(m_windowCombo, &QComboBox::currentIndexChanged,
            this, &PvChartWidget::onWindowComboChanged);

    // Lock Y scale checkbox
    m_lockYScaleCheckBox = new QCheckBox(QStringLiteral("Lock Y"), this);
    m_lockYScaleCheckBox->setToolTip(QStringLiteral("Freeze Y-axis range"));
    connect(m_lockYScaleCheckBox, &QCheckBox::toggled,
            this, &PvChartWidget::onLockYScaleToggled);

    // Horizontal scroll bar — value = seconds from dataStart to right edge
    // of the visible window.  Maximum = total span of recorded history in
    // seconds.  Page step = windowMs() / 1000 (one full window width).
    m_scrollBar = new QScrollBar(Qt::Horizontal, this);
    m_scrollBar->setMinimum(0);
    m_scrollBar->setMaximum(0);
    m_scrollBar->setSingleStep(10);
    connect(m_scrollBar, &QScrollBar::valueChanged,
            this, &PvChartWidget::onScrollBarChanged);
}

// Private helpers
void PvChartWidget::appendSample(const PvData &data)
{
    Sample s;
    s.timestampMs = QDateTime::currentMSecsSinceEpoch();
    s.cum[0] = data.pv1PowerW();
    s.cum[1] = s.cum[0] + data.pv2PowerW();
    s.cum[2] = s.cum[1] + data.pv3PowerW();
    s.cum[3] = s.cum[2] + data.pv4PowerW();
    s.exportedW = m_lastExportedW;            // raw grid export (W, ≥ 0)
    s.netW      = s.cum[3] - s.exportedW;    // PV minus export; negative when
                                              // battery supplements the export
    m_history.append(s);
    pruneHistory();
}

void PvChartWidget::pruneHistory()
{
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kMaxHistoryMs;
    int removeCount = 0;
    while (removeCount < m_history.size() &&
           m_history[removeCount].timestampMs < cutoff)
        ++removeCount;
    if (removeCount > 0)
        m_history.remove(0, removeCount);
}

void PvChartWidget::rebuildSeries()
{
    if (m_history.isEmpty())
        return;

    const qint64 endMs   = computeWindowEndMs();
    const qint64 startMs = endMs - windowMs();

    // ── Determine display unit ──────────────────────────────────────────────
    // Scan the anchor+visible range for the peak total-PV value.  Switch to
    // kW when that peak reaches 1000 W; stay in W below that threshold so
    // small-output mornings / cloudy days are still readable.
    double maxVisibleW = 0.0;
    {
        int leftIdx  = -1;
        int rightIdx = -1;
        for (int i = 0; i < m_history.size(); ++i) {
            if (m_history[i].timestampMs < startMs)
                leftIdx = i;
            else if (m_history[i].timestampMs > endMs) {
                rightIdx = i;
                break;
            }
        }
        const int scanStart = (leftIdx  >= 0) ? leftIdx  : 0;
        const int scanEnd   = (rightIdx >= 0) ? rightIdx : (m_history.size() - 1);
        for (int i = scanStart; i <= scanEnd; ++i)
            maxVisibleW = std::max(maxVisibleW, m_history[i].cum[3]);
    }
    const double  divisor   = (maxVisibleW >= 1000.0) ? 1000.0 : 1.0;
    const QString unitLabel = (divisor == 1000.0) ? QStringLiteral("kW") : QStringLiteral("W");

    // Collect visible samples — identical X values across all 4 level arrays
    // so that lowerSeries[s] and upperSeries[s-1] share exact X coordinates.
    QVector<QPointF> pts[4];
    QVector<QPointF> netPts;
    QVector<QPointF> exportNegPts;  // exported power as negative values (below Y=0)

    // Helper: append one sample to all point vectors (Y values scaled by divisor).
    auto appendSamplePts = [&](const Sample &sample) {
        const double x = static_cast<double>(sample.timestampMs);
        for (int s = 0; s < 4; ++s)
            pts[s].append(QPointF(x, sample.cum[s] / divisor));
        netPts.append(QPointF(x, sample.netW / divisor));
        exportNegPts.append(QPointF(x, -sample.exportedW / divisor));
    };

    // Left-anchor: the last sample that falls before startMs.  Including it
    // means area fills extend off-screen to the left so Qt Charts clips them
    // cleanly at the axis boundary — no gap at the left edge when scrolling.
    const Sample *leftAnchor = nullptr;
    for (const Sample &sample : m_history) {
        if (sample.timestampMs >= startMs)
            break;
        leftAnchor = &sample;
    }
    if (leftAnchor)
        appendSamplePts(*leftAnchor);

    for (const Sample &sample : m_history) {
        if (sample.timestampMs < startMs)
            continue;
        if (sample.timestampMs > endMs)
            break;
        appendSamplePts(sample);
    }

    // Right-anchor: the first sample after endMs.  Including it means area
    // fills extend off-screen to the right so Qt Charts clips them cleanly at
    // the axis right boundary — no gap at the right edge when scrolling.
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history[i].timestampMs > endMs) {
            appendSamplePts(m_history[i]);
            break;
        }
    }

    // Downsample each cumulative boundary series independently.
    // The same downsampled pts[s-1] is pushed to both upperSeries[s-1] and
    // lowerSeries[s], preserving perfect tiling across band edges.
    for (int s = 0; s < 4; ++s)
        pts[s] = downsampleMinMax(pts[s], kMaxSeriesPoints);
    netPts       = downsampleMinMax(netPts,       kMaxSeriesPoints);
    exportNegPts = downsampleMinMax(exportNegPts, kMaxSeriesPoints);

    // Push data into PV band series
    for (int s = 0; s < 4; ++s) {
        m_areaSeries[s]->upperSeries()->replace(pts[s]);
        if (s > 0) {
            m_areaSeries[s]->lowerSeries()->replace(pts[s - 1]);
        } else {
            // PV1: explicit zero baseline so the fill stops at Y=0 even when
            // the axis min is negative (export region below the zero line).
            QVector<QPointF> zeroBase;
            zeroBase.reserve(pts[0].size());
            for (const QPointF &pt : pts[0])
                zeroBase.append({pt.x(), 0.0});
            m_areaSeries[0]->lowerSeries()->replace(zeroBase);
        }
    }

    // Push exported overlay: upper = Y=0 (zero line), lower = −exportedW.
    // The blue area therefore sits below the axis zero line, showing export as
    // a negative quantity on the same Y-axis (no second axis — fritzhome pattern).
    QVector<QPointF> zeroPts;
    zeroPts.reserve(exportNegPts.size());
    for (const QPointF &pt : exportNegPts)
        zeroPts.append({pt.x(), 0.0});
    m_exportSeries->upperSeries()->replace(zeroPts);
    m_exportSeries->lowerSeries()->replace(exportNegPts);

    // Push net line (locally consumed power, always ≥ 0, above the zero line)
    m_netSeries->replace(netPts);

    // Find the deepest negative value across both the export area and the net
    // line.  net can go below -exportedW when the battery supplements export.
    double minExportW = 0.0;
    for (const QPointF &pt : exportNegPts)
        minExportW = std::min(minExportW, pt.y());
    for (const QPointF &pt : netPts)
        minExportW = std::min(minExportW, pt.y());

    applyTimeWindow();
    updateYAxis(pts[3], minExportW, unitLabel);
}

void PvChartWidget::applyTimeWindow()
{
    const qint64 endMs   = computeWindowEndMs();
    const qint64 startMs = endMs - windowMs();
    m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(startMs),
                      QDateTime::fromMSecsSinceEpoch(endMs));
}

void PvChartWidget::updateYAxis(const QVector<QPointF> &totalPts, double minW, const QString &unitLabel)
{
    if (m_lockYScale)
        return;

    double maxW = 0.0;
    for (const QPointF &pt : totalPts)
        maxW = std::max(maxW, pt.y());

    // ── 1. Preliminary bounds: magnitude-based nice rounding ────────────────
    double niceMax, niceMin;

    if (maxW < 1.0) {
        niceMax = 100.0;            // no real data yet — minimal placeholder
    } else {
        const double mag = std::pow(10.0, std::floor(std::log10(maxW)));
        niceMax = std::ceil(maxW / mag) * mag;
    }

    // 1 W threshold avoids expanding the axis for floating-point noise.
    if (minW < -1.0) {
        const double absMin = -minW;
        const double minMag = std::pow(10.0, std::floor(std::log10(absMin)));
        niceMin = -std::ceil(absMin / minMag) * minMag;
    } else {
        niceMin = 0.0;
    }

    // ── 2. Nice tick step targeting ~5 ticks across the full range ──────────
    // computeNiceStep returns a member of {1,2,5}×10ⁿ.
    const double step = computeNiceStep(niceMax - niceMin, 5);

    // ── 3. Snap bounds to step multiples so axis limits land on ticks ────────
    niceMax = std::ceil (niceMax / step) * step;
    niceMin = std::floor(niceMin / step) * step;

    // ── 4. Apply — anchor=0 (set once in buildChart) + new interval ─────────
    // Every tick is step×N for integer N; 0 is always included.  All values
    // are members of {1,2,5}×10ⁿ so labels are always round.
    m_axisY->setTitleText(QStringLiteral("Power (%1)").arg(unitLabel));
    m_axisY->setTickInterval(step);
    m_axisY->setRange(niceMin, niceMax);
}

void PvChartWidget::updateScrollBar()
{
    if (m_history.isEmpty()) {
        m_scrollBar->blockSignals(true);
        m_scrollBar->setRange(0, 0);
        m_scrollBar->setValue(0);
        m_scrollBar->blockSignals(false);
        return;
    }

    const qint64 dataStartMs = m_history.first().timestampMs;
    const qint64 nowMs       = QDateTime::currentMSecsSinceEpoch();
    const int    spanSec     = static_cast<int>((nowMs - dataStartMs) / 1000);
    const int    pageSec     = static_cast<int>(windowMs() / 1000);
    const int    maxVal      = std::max(spanSec, pageSec);

    m_scrollBar->blockSignals(true);
    m_scrollBar->setRange(pageSec, maxVal);
    m_scrollBar->setPageStep(pageSec);
    m_scrollBar->setSingleStep(std::max(1, pageSec / 20));
    if (m_scrollAtEnd)
        m_scrollBar->setValue(maxVal);
    m_scrollBar->blockSignals(false);
}

qint64 PvChartWidget::computeWindowEndMs() const
{
    if (m_scrollAtEnd || m_scrollBar->maximum() == 0 || m_history.isEmpty())
        return QDateTime::currentMSecsSinceEpoch();

    const qint64 dataStartMs = m_history.first().timestampMs;
    return dataStartMs + static_cast<qint64>(m_scrollBar->value()) * 1000LL;
}

// Slots
QVector<QPointF> PvChartWidget::downsampleMinMax(const QVector<QPointF> &pts, int maxPoints)
{
    if (pts.size() <= maxPoints)
        return pts;

    const int    bucketCount = maxPoints / 2;
    const double bucketSize  = static_cast<double>(pts.size()) / bucketCount;

    QVector<QPointF> result;
    result.reserve(maxPoints);

    for (int b = 0; b < bucketCount; ++b) {
        const int start = static_cast<int>(b * bucketSize);
        const int end   = std::min(static_cast<int>((b + 1) * bucketSize),
                                   static_cast<int>(pts.size()));
        if (start >= end)
            break;

        int minIdx = start, maxIdx = start;
        for (int i = start + 1; i < end; ++i) {
            if (pts[i].y() < pts[minIdx].y()) minIdx = i;
            if (pts[i].y() > pts[maxIdx].y()) maxIdx = i;
        }

        // Emit in chronological (ascending X) order
        if (minIdx <= maxIdx) {
            result.append(pts[minIdx]);
            if (maxIdx != minIdx)
                result.append(pts[maxIdx]);
        } else {
            result.append(pts[maxIdx]);
            if (minIdx != maxIdx)
                result.append(pts[minIdx]);
        }
    }

    return result;
}

void PvChartWidget::saveState() const
{
    QSettings s;
    s.setValue(QStringLiteral("pvChart/windowIndex"), m_windowIndex);
    s.setValue(QStringLiteral("pvChart/lockYScale"),  m_lockYScale);
}

void PvChartWidget::loadState()
{
    QSettings s;
    m_windowIndex = s.value(QStringLiteral("pvChart/windowIndex"), 4).toInt();
    if (m_windowIndex < 0 || m_windowIndex >= kWindowMsTable.size())
        m_windowIndex = 4;
    m_lockYScale = s.value(QStringLiteral("pvChart/lockYScale"), false).toBool();
}

qint64 PvChartWidget::windowMs() const
{
    if (m_windowIndex >= 0 && m_windowIndex < kWindowMsTable.size())
        return kWindowMsTable[m_windowIndex];
    return kWindowMsTable[4];  // 2 h fallback
}
