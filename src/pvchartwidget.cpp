#include "pvchartwidget.h"

#include <QtCharts/QAreaSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCore/QDateTime>
#include <QtCore/QEvent>
#include <QtCore/QHash>
#include <QtCore/QSettings>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGraphicsLineItem>
#include <QtWidgets/QGraphicsRectItem>
#include <QtWidgets/QGraphicsSimpleTextItem>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
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

// Load overlay colour (orange, semi-transparent)
static const QColor kLoadFillColor(0xFF, 0x99, 0x00, 140);
static const QColor kLoadPenColor (0xCC, 0x77, 0x00);

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
    // PV is the first block read each poll cycle — cache it and defer
    // recording the sample until onBatteryDataUpdated() (the last block),
    // so the sample pairs this cycle's PV reading with this cycle's grid
    // and battery readings instead of the previous cycle's battery value.
    m_pendingPvData = data;
    m_hasPendingPvData = true;
}

void PvChartWidget::onGridDataUpdated(const GridData &data)
{
    // Latch signed grid power (W); positive = export, negative = import.
    // Chart is NOT rebuilt here — only onBatteryDataUpdated() drives redraws.
    m_lastGridPowerW = data.activePowerKw * 1000.0;
}

void PvChartWidget::onBatteryDataUpdated(const BatteryData &data)
{
    // Latch combined battery power (W); positive = charging, negative =
    // discharging (matches BatteryData::isCharging()/isDischarging()).
    m_lastBatteryW = data.combinedPowerW;

    // Battery is the last block read each poll cycle: now that PV, grid and
    // battery all reflect the same tick, record the sample and redraw.
    if (!m_hasPendingPvData)
        return;

    appendSample(m_pendingPvData);
    m_hasPendingPvData = false;

    if (m_noDataLabel->isVisible()) {
        m_noDataLabel->hide();
        m_chartView->show();
    }
    updateScrollBar();
    rebuildSeries();
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

bool PvChartWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_chartView->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPointF posInChart =
                m_chart->mapFromScene(m_chartView->mapToScene(mouseEvent->pos()));
            const QRectF plotArea = m_chart->plotArea();

            if (m_history.isEmpty() || !plotArea.contains(posInChart)) {
                hideHoverCrosshair();
                break;
            }

            const double targetXMs = m_chart->mapToValue(posInChart, m_netSeries).x();
            const Sample *sample   = findNearestSample(static_cast<qint64>(targetXMs));
            if (!sample) {
                hideHoverCrosshair();
                break;
            }

            // Snap the crosshair to the sample's exact timestamp (not the
            // raw mouse X) so it lines up with the actual data point.
            const QPointF snappedPos =
                m_chart->mapToPosition(QPointF(static_cast<double>(sample->timestampMs), 0.0),
                                        m_netSeries);
            m_hoverLine->setLine(snappedPos.x(), plotArea.top(),
                                  snappedPos.x(), plotArea.bottom());
            m_hoverLine->show();

            m_hoverInfoText->setText(formatTooltip(*sample));
            positionHoverInfoBox(mouseEvent->pos());
            m_hoverInfoBg->show();
            break;
        }
        case QEvent::Leave:
            hideHoverCrosshair();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

const PvChartWidget::Sample *PvChartWidget::findNearestSample(qint64 targetMs) const
{
    if (m_history.isEmpty())
        return nullptr;

    // m_history is chronologically ordered; binary-search for the first
    // sample whose timestamp is >= targetMs, then compare against its
    // predecessor to find the closest one.
    auto it = std::lower_bound(m_history.begin(), m_history.end(), targetMs,
                                [](const Sample &s, qint64 value) {
                                    return s.timestampMs < value;
                                });

    if (it == m_history.begin())
        return &(*it);
    if (it == m_history.end())
        return &m_history.last();

    const Sample &after  = *it;
    const Sample &before = *(it - 1);
    return (targetMs - before.timestampMs <= after.timestampMs - targetMs) ? &before : &after;
}

QString PvChartWidget::formatTooltip(const Sample &sample)
{
    const double pv1 = sample.cum[0];
    const double pv2 = sample.cum[1] - sample.cum[0];
    const double pv3 = sample.cum[2] - sample.cum[1];
    const double pv4 = sample.cum[3] - sample.cum[2];

    const QString time =
        QDateTime::fromMSecsSinceEpoch(sample.timestampMs).toString(QStringLiteral("hh:mm:ss"));

    QString gridLine;
    if (sample.exportedW > 0.0)
        gridLine = QStringLiteral("Grid: %1 W (exporting)").arg(sample.exportedW, 0, 'f', 0);
    else if (sample.importedW > 0.0)
        gridLine = QStringLiteral("Grid: %1 W (importing)").arg(sample.importedW, 0, 'f', 0);
    else
        gridLine = QStringLiteral("Grid: 0 W");

    QString batteryLine;
    if (sample.batteryW > 0.0)
        batteryLine = QStringLiteral("Battery: +%1 W (charging)").arg(sample.batteryW, 0, 'f', 0);
    else if (sample.batteryW < 0.0)
        batteryLine =
            QStringLiteral("Battery: %1 W (discharging)").arg(sample.batteryW, 0, 'f', 0);
    else
        batteryLine = QStringLiteral("Battery: 0 W");

    return QStringLiteral("%1\n"
                           "Total PV: %2 W\n"
                           "PV1: %3 W   PV2: %4 W   PV3: %5 W   PV4: %6 W\n"
                           "%7\n"
                           "Load: %8 W\n"
                           "%9")
        .arg(time)
        .arg(sample.cum[3], 0, 'f', 0)
        .arg(pv1, 0, 'f', 0)
        .arg(pv2, 0, 'f', 0)
        .arg(pv3, 0, 'f', 0)
        .arg(pv4, 0, 'f', 0)
        .arg(gridLine)
        .arg(sample.loadW, 0, 'f', 0)
        .arg(batteryLine);
}

void PvChartWidget::hideHoverCrosshair()
{
    m_hoverLine->hide();
    m_hoverInfoBg->hide();
}

void PvChartWidget::positionHoverInfoBox(const QPoint &viewportPos)
{
    const QRectF textRect = m_hoverInfoText->boundingRect();
    const QRectF bgRect(0, 0, textRect.width() + 12, textRect.height() + 8);
    m_hoverInfoBg->setRect(bgRect);

    // Anchor near the cursor, offset down-right by default, flipping to the
    // opposite side when it would overflow the viewport so the box always
    // stays fully visible.
    const QSize viewSize = m_chartView->viewport()->size();
    qreal x = viewportPos.x() + 16;
    qreal y = viewportPos.y() + 16;
    if (x + bgRect.width() > viewSize.width())
        x = viewportPos.x() - bgRect.width() - 16;
    if (y + bgRect.height() > viewSize.height())
        y = viewportPos.y() - bgRect.height() - 16;

    const QPointF scenePos = m_chartView->mapToScene(QPoint(qRound(x), qRound(y)));
    m_hoverInfoBg->setPos(scenePos);
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

    // ── Grid (export/import) power overlay ──────────────────────────────────
    // A single red QAreaSeries represents both directions of grid flow, since
    // export and import never coexist for the same sample. While exporting,
    // it spans from the zero line down to −exportedW (below zero). While
    // importing, it spans from 0 up to importedW, stacked directly beneath
    // the PV bands (rebuildSeries() shifts every PV level up by importedW so
    // the PV stack makes room for this band underneath it, rather than
    // stacking the import on top of the PV bands). The chart reads: positive
    // region = import (red, bottom-most) + PV production (gold stacks) +
    // battery line; negative region = grid export (red area, below zero) +
    // load (orange area).  No second axis required.
    {
        auto *gridUpper = new QLineSeries;
        auto *gridLower = new QLineSeries;
        m_gridSeries = new QAreaSeries(gridUpper, gridLower);
        m_gridSeries->setName(QStringLiteral("Grid"));
        m_gridSeries->setBrush(kExportFillColor);
        QPen gridPen(kExportPenColor);
        gridPen.setWidth(1);
        m_gridSeries->setPen(gridPen);
        m_chart->addSeries(m_gridSeries);
    }

    // ── Estimated load overlay ──────────────────────────────────────────────
    // Orange area stacked directly beneath the export portion of the grid
    // band (upper = −exportedW,
    // lower = −(exportedW + loadW)), so export and load are shown as stacked
    // negative bars below the zero line, mirroring the PV band stacking above.
    {
        auto *loadUpper = new QLineSeries;
        auto *loadLower = new QLineSeries;
        m_loadSeries = new QAreaSeries(loadUpper, loadLower);
        m_loadSeries->setName(QStringLiteral("Load"));
        m_loadSeries->setBrush(kLoadFillColor);
        QPen loadPen(kLoadPenColor);
        loadPen.setWidth(1);
        m_loadSeries->setPen(loadPen);
        m_chart->addSeries(m_loadSeries);
    }

    // ── Battery power line ──────────────────────────────────────────────────
    // Black line (width 2) at the latched battery power, following the
    // fritzhome mixed-group overlay pattern: positive = charging (above the
    // zero line), negative = discharging (below the zero line), drawn on the
    // same Y-axis with no second axis.
    {
        m_netSeries = new QLineSeries;
        m_netSeries->setName(QStringLiteral("Battery"));
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
    m_gridSeries->attachAxis(m_axisX);
    m_gridSeries->attachAxis(m_axisY);
    m_loadSeries->attachAxis(m_axisX);
    m_loadSeries->attachAxis(m_axisY);
    m_netSeries->attachAxis(m_axisX);
    m_netSeries->attachAxis(m_axisY);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(200);
    m_chartView->setMouseTracking(true);
    m_chartView->viewport()->setMouseTracking(true);
    m_chartView->viewport()->installEventFilter(this);

    // ── Hover crosshair ──────────────────────────────────────────────────────
    // Parented to m_chart so its coordinate system matches plotArea() /
    // mapToValue() / mapToPosition() directly (no manual offset needed).
    m_hoverLine = new QGraphicsLineItem(m_chart);
    QPen hoverPen(QColor(80, 80, 80));
    hoverPen.setStyle(Qt::DotLine);
    hoverPen.setWidth(1);
    m_hoverLine->setPen(hoverPen);
    m_hoverLine->setZValue(100);
    m_hoverLine->hide();

    // ── Hover info box ───────────────────────────────────────────────────────
    // Added directly to the scene (view coordinates) so it can be freely
    // positioned near the cursor. Persistent: only hidden explicitly on
    // Leave, unlike QToolTip which auto-hides on a timer.
    m_hoverInfoBg = new QGraphicsRectItem;
    m_hoverInfoBg->setBrush(QColor(255, 255, 225, 235));
    m_hoverInfoBg->setPen(QPen(QColor(120, 120, 120)));
    m_hoverInfoBg->setZValue(101);
    m_hoverInfoBg->hide();
    m_chartView->scene()->addItem(m_hoverInfoBg);

    m_hoverInfoText = new QGraphicsSimpleTextItem(m_hoverInfoBg);
    m_hoverInfoText->setPos(6, 4);
    m_hoverInfoText->setBrush(QColor(20, 20, 20));
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
    s.exportedW = std::max(0.0, m_lastGridPowerW);   // W, ≥ 0; 0 when importing
    s.importedW = std::max(0.0, -m_lastGridPowerW);  // W, ≥ 0; 0 when exporting
    // Estimated house load — same formula as DashboardWidget::refreshLoad():
    // load = pv - battCharging - gridPower (signed), clamped to ≥ 0.  Using
    // the signed grid power (not exportedW) so importing correctly adds to
    // the estimated load instead of being dropped.
    s.loadW     = std::max(0.0, s.cum[3] - m_lastBatteryW - m_lastGridPowerW);
    s.batteryW  = m_lastBatteryW;             // + = charging, − = discharging
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
    // Always display in watts. Switching to kW previously caused the
    // negative (export/load) portion of the axis to be clipped, so the
    // Y axis is now permanently kept in W regardless of the PV peak.
    const double  divisor   = 1.0;
    const QString unitLabel = QStringLiteral("W");

    // Collect visible samples — identical X values across all 4 level arrays
    // so that lowerSeries[s] and upperSeries[s-1] share exact X coordinates.
    // The PV levels are offset by importedW so that, while importing, the
    // whole PV stack is shifted up and the import band renders underneath it
    // (between 0 and importedW) instead of on top — see the comment above
    // m_gridSeries in the header for the overall stacking order.
    QVector<QPointF> pts[4];
    QVector<QPointF> batteryPts;
    // Per-timestamp lookups used to derive the grid/load/PV1-baseline bands
    // after pts[0] is downsampled below, so every shared boundary reuses
    // pts[0]'s exact (downsampled) x-coordinates and tiles perfectly.
    QHash<qint64, double> exportedByX;
    QHash<qint64, double> importedByX;
    QHash<qint64, double> loadByX;

    // Helper: append one sample to all point vectors (Y values scaled by divisor).
    auto appendSamplePts = [&](const Sample &sample) {
        const double x = static_cast<double>(sample.timestampMs);
        for (int s = 0; s < 4; ++s)
            pts[s].append(QPointF(x, (sample.cum[s] + sample.importedW) / divisor));
        batteryPts.append(QPointF(x, sample.batteryW / divisor));
        exportedByX.insert(sample.timestampMs, sample.exportedW / divisor);
        importedByX.insert(sample.timestampMs, sample.importedW / divisor);
        loadByX.insert(sample.timestampMs, sample.loadW / divisor);
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
    batteryPts = downsampleMinMax(batteryPts, kMaxSeriesPoints);

    // Derive the PV1 baseline / grid band / load band edges from pts[0]'s
    // exact (downsampled) x-coordinates, looking up each timestamp's
    // exported/imported/load power.  Because every edge below is built from
    // the same x-set, every shared boundary tiles perfectly:
    //   PV1 lower      == grid upper (both == importedW; 0 while exporting)
    //   grid lower      == load upper (both == −exportedW; 0 while importing)
    //   load lower      == −(exportedW + loadW)
    QVector<QPointF> pv1BasePts, gridUpperPts, gridLowerPts, loadLowerPts;
    pv1BasePts.reserve(pts[0].size());
    gridUpperPts.reserve(pts[0].size());
    gridLowerPts.reserve(pts[0].size());
    loadLowerPts.reserve(pts[0].size());
    for (const QPointF &pt : pts[0]) {
        const qint64 ts       = static_cast<qint64>(pt.x());
        const double imported = importedByX.value(ts, 0.0);  // ≥ 0, 0 while exporting
        const double exported = exportedByX.value(ts, 0.0);  // ≥ 0, 0 while importing
        const double load      = loadByX.value(ts, 0.0);
        pv1BasePts.append({pt.x(), imported});
        gridUpperPts.append({pt.x(), imported});
        gridLowerPts.append({pt.x(), -exported});
        loadLowerPts.append({pt.x(), -(exported + load)});
    }

    // Push data into PV band series
    for (int s = 0; s < 4; ++s) {
        m_areaSeries[s]->upperSeries()->replace(pts[s]);
        if (s > 0) {
            m_areaSeries[s]->lowerSeries()->replace(pts[s - 1]);
        } else {
            // PV1: baseline is importedW while importing (so the whole PV
            // stack shifts up and the import band renders underneath it),
            // or Y=0 otherwise (e.g. while exporting, so the fill still
            // stops at the zero line).
            m_areaSeries[0]->lowerSeries()->replace(pv1BasePts);
        }
    }

    // Push the single grid (export/import) band: upper == importedW (0 while
    // exporting), lower == −exportedW (0 while importing).  Since the two
    // never coexist for a given sample, one QAreaSeries covers both flow
    // directions: below the zero line while exporting, stacked directly
    // beneath the PV bands while importing.
    m_gridSeries->upperSeries()->replace(gridUpperPts);
    m_gridSeries->lowerSeries()->replace(gridLowerPts);

    // Push load overlay stacked directly beneath the export portion of the
    // grid band: upper = −exportedW (0 while importing), lower =
    // −(exportedW + loadW).  Rendered in orange.
    m_loadSeries->upperSeries()->replace(gridLowerPts);
    m_loadSeries->lowerSeries()->replace(loadLowerPts);

    // Push battery power line (positive = charging above the zero line,
    // negative = discharging below the zero line)
    m_netSeries->replace(batteryPts);

    // Find the deepest negative value across the export+load stack and the
    // battery line.  Battery can go negative when discharging.
    double minExportW = 0.0;
    for (const QPointF &pt : loadLowerPts)
        minExportW = std::min(minExportW, pt.y());
    for (const QPointF &pt : batteryPts)
        minExportW = std::min(minExportW, pt.y());

    // Find the highest positive value across the PV top.  pts[3] already
    // includes the importedW offset baked into every PV level, so the PV+
    // import stack height is captured without scanning the grid band
    // separately.
    double maxTopW = 0.0;
    for (const QPointF &pt : pts[3])
        maxTopW = std::max(maxTopW, pt.y());

    applyTimeWindow();
    updateYAxis(maxTopW, minExportW, unitLabel);
}

void PvChartWidget::applyTimeWindow()
{
    const qint64 endMs   = computeWindowEndMs();
    const qint64 startMs = endMs - windowMs();
    m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(startMs),
                      QDateTime::fromMSecsSinceEpoch(endMs));
}

void PvChartWidget::updateYAxis(double maxW, double minW, const QString &unitLabel)
{
    if (m_lockYScale)
        return;

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
