#include "pvwidget.h"
#include "powerformat.h"

#include <QtGui/QColor>
#include <QtGui/QPalette>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QVBoxLayout>

// ── String colours — must match kAreaColors in pvchartwidget.cpp ──────────────

static const QColor kStringColors[4] = {
    QColor(0x00, 0xAA, 0x00),  // PV1 – green
    QColor(0x66, 0xBB, 0x00),  // PV2 – yellow-green
    QColor(0xCC, 0xCC, 0x00),  // PV3 – amber-yellow
    QColor(0xFF, 0xD7, 0x00),  // PV4 – gold
};

static const char * const kStringNames[4] = { "PV1", "PV2", "PV3", "PV4" };

// Thresholds below which a string is considered inactive (not connected).
static constexpr double kActiveVoltageThreshold = 1.0;   // V
static constexpr double kActiveCurrentThreshold = 0.01;  // A

// ── Helpers ───────────────────────────────────────────────────────────────────

static QLabel *makeHeaderLabel(const QString &text, QWidget *parent)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setAlignment(Qt::AlignCenter);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    return lbl;
}

static QLabel *makeValueLabel(QWidget *parent)
{
    auto *lbl = new QLabel(QStringLiteral("--"), parent);
    lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl->setMinimumWidth(80);
    return lbl;
}

// ── Constructor ───────────────────────────────────────────────────────────────

PvWidget::PvWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *group = new QGroupBox(QStringLiteral("PV Input"), this);
    auto *grid  = new QGridLayout(group);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);

    // ── Row 0: Total PV power ─────────────────────────────────────────────────
    {
        auto *totalLbl = makeHeaderLabel(QStringLiteral("Total"), this);
        m_totalPvPower = new QLabel(QStringLiteral("--"), this);

        QFont f = m_totalPvPower->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        m_totalPvPower->setFont(f);
        m_totalPvPower->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        grid->addWidget(totalLbl,       0, 0);
        grid->addWidget(m_totalPvPower, 0, 1, 1, 3);
    }

    // ── Row 1: Horizontal separator ───────────────────────────────────────────
    {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        grid->addWidget(sep, 1, 0, 1, 4);
    }

    // ── Row 2: Column headers ─────────────────────────────────────────────────
    grid->addWidget(makeHeaderLabel(QStringLiteral("Voltage"), this), 2, 1);
    grid->addWidget(makeHeaderLabel(QStringLiteral("Current"), this), 2, 2);
    grid->addWidget(makeHeaderLabel(QStringLiteral("Power"),   this), 2, 3);

    // ── Rows 3–6: Per-string data ─────────────────────────────────────────────
    for (int i = 0; i < 4; ++i) {
        auto &row = m_rows[i];

        row.label = new QLabel(QString::fromLatin1(kStringNames[i]), this);
        {
            QPalette pal = row.label->palette();
            pal.setColor(QPalette::WindowText, kStringColors[i]);
            row.label->setPalette(pal);
            QFont f = row.label->font();
            f.setBold(true);
            row.label->setFont(f);
        }

        row.voltage = makeValueLabel(this);
        row.current = makeValueLabel(this);
        row.power   = makeValueLabel(this);

        const int gridRow = i + 3;
        grid->addWidget(row.label,   gridRow, 0);
        grid->addWidget(row.voltage, gridRow, 1);
        grid->addWidget(row.current, gridRow, 2);
        grid->addWidget(row.power,   gridRow, 3);
    }

    grid->setColumnStretch(3, 1);

    auto *top = new QVBoxLayout(this);
    top->addWidget(group);
    top->addStretch();
}

// ── Private helpers ───────────────────────────────────────────────────────────

void PvWidget::setRowActive(int idx, bool active)
{
    auto &row = m_rows[idx];

    // String name label: full color when active; blended 70% toward #888 when dimmed
    {
        QPalette pal = row.label->palette();
        QColor col = kStringColors[idx];
        if (!active) {
            constexpr int kGray = 0x88;
            col = QColor(
                (col.red()   * 3 + kGray * 7) / 10,
                (col.green() * 3 + kGray * 7) / 10,
                (col.blue()  * 3 + kGray * 7) / 10
            );
        }
        pal.setColor(QPalette::WindowText, col);
        row.label->setPalette(pal);
    }

    // Value labels: reset palette when active, dim to #888 when inactive
    for (QLabel *lbl : { row.voltage, row.current, row.power }) {
        if (active) {
            lbl->setPalette(QPalette());
        } else {
            QPalette pal;
            pal.setColor(QPalette::WindowText, QColor(0x88, 0x88, 0x88));
            lbl->setPalette(pal);
        }
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void PvWidget::updateData(const PvData &data)
{
    m_totalPvPower->setText(formatPower(data.totalPvPowerKw * 1000.0));

    const double voltages[4] = {
        data.pv1Voltage, data.pv2Voltage, data.pv3Voltage, data.pv4Voltage
    };
    const double currents[4] = {
        data.pv1Current, data.pv2Current, data.pv3Current, data.pv4Current
    };
    const double powers[4] = {
        data.pv1PowerW(), data.pv2PowerW(), data.pv3PowerW(), data.pv4PowerW()
    };

    for (int i = 0; i < 4; ++i) {
        const bool active = voltages[i] > kActiveVoltageThreshold
                         || currents[i] > kActiveCurrentThreshold;
        setRowActive(i, active);
        if (active) {
            m_rows[i].voltage->setText(QStringLiteral("%1 V").arg(voltages[i], 0, 'f', 1));
            m_rows[i].current->setText(QStringLiteral("%1 A").arg(currents[i], 0, 'f', 2));
            m_rows[i].power->setText(formatPower(powers[i]));
        } else {
            m_rows[i].voltage->setText(QStringLiteral("--"));
            m_rows[i].current->setText(QStringLiteral("--"));
            m_rows[i].power->setText(QStringLiteral("--"));
        }
    }
}
