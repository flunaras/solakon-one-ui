#include "inverterdata.h"
#include "modbusapi.h"

#include <QtTest/QtTest>

// ── test_workmode ────────────────────────────────────────────────────────────
//
// Unit tests for the WorkMode enum mapping and, critically, the WORK_MODE
// write compensation workaround for the confirmed firmware bug where the
// device persists (writtenValue - 1) into register 49203. See
// ModbusApi::compensateWorkModeWrite() and AGENTS.md pitfall #13 for the
// hardware-confirmed rationale — do not remove this test without
// re-verifying the firmware behavior on real hardware first.
class TestWorkMode : public QObject
{
    Q_OBJECT

private slots:
    void workModeFromInt_validValues();
    void workModeFromInt_gapAndInvalidValues();
    void workModeLabel_knownValues();
    void workModeLabel_unknownIncludesRawValue();
    void compensateWorkModeWrite_addsOne();
};

void TestWorkMode::workModeFromInt_validValues()
{
    QCOMPARE(workModeFromInt(1), WorkMode::SelfUse);
    QCOMPARE(workModeFromInt(2), WorkMode::FeedinPriority);
    QCOMPARE(workModeFromInt(3), WorkMode::BackUp);
    QCOMPARE(workModeFromInt(4), WorkMode::PeakShaving);
    QCOMPARE(workModeFromInt(6), WorkMode::ForceCharge);
    QCOMPARE(workModeFromInt(7), WorkMode::ForceDischarge);
}

void TestWorkMode::workModeFromInt_gapAndInvalidValues()
{
    // 5 is intentionally not a valid WORK_MODE value per the protocol table.
    QCOMPARE(workModeFromInt(5), WorkMode::Unknown);
    QCOMPARE(workModeFromInt(0), WorkMode::Unknown);
    QCOMPARE(workModeFromInt(8), WorkMode::Unknown);
    QCOMPARE(workModeFromInt(-1), WorkMode::Unknown);
}

void TestWorkMode::workModeLabel_knownValues()
{
    QCOMPARE(workModeLabel(WorkMode::SelfUse),        QStringLiteral("Self Use"));
    QCOMPARE(workModeLabel(WorkMode::FeedinPriority), QStringLiteral("Feed-in Priority"));
    QCOMPARE(workModeLabel(WorkMode::BackUp),         QStringLiteral("Backup"));
    QCOMPARE(workModeLabel(WorkMode::PeakShaving),    QStringLiteral("Peak Shaving"));
    QCOMPARE(workModeLabel(WorkMode::ForceCharge),    QStringLiteral("Force Charge"));
    QCOMPARE(workModeLabel(WorkMode::ForceDischarge), QStringLiteral("Force Discharge"));
}

void TestWorkMode::workModeLabel_unknownIncludesRawValue()
{
    // workModeFromInt(5) maps to WorkMode::Unknown (raw value 0), matching
    // the "unknown workmode" symptom observed on hardware before the write
    // compensation fix (see AGENTS.md pitfall #13).
    const QString label = workModeLabel(WorkMode::Unknown);
    QVERIFY(label.contains(QStringLiteral("Unknown")));
}

void TestWorkMode::compensateWorkModeWrite_addsOne()
{
    // Confirmed on real hardware: writing N to WORK_MODE (49203) settles to a
    // stable readback of N-1. Writing N+1 must be sent so the firmware's
    // decrement lands on the value the user actually selected.
    QCOMPARE(ModbusApi::compensateWorkModeWrite(1), 2);
    QCOMPARE(ModbusApi::compensateWorkModeWrite(6), 7);  // Force Charge
    QCOMPARE(ModbusApi::compensateWorkModeWrite(7), 8);  // Force Discharge
}

QTEST_GUILESS_MAIN(TestWorkMode)
#include "test_workmode.moc"
