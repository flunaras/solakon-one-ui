#include "mockmodbusapi.h"

#include <QtTest/QtTest>

// ── test_mockmodbusapi ───────────────────────────────────────────────────────
//
// Verifies MockModbusApi behaves as a faithful IModbusApi test double: write
// calls are recorded and writeSucceeded() is emitted, and simulated read data
// is delivered via the normal signals — exactly as ModbusApi would, but with
// no live Modbus/TCP connection. This is what future widget-level tests
// (SettingsWidget, RemoteControlWidget, MainWindow, ...) should inject
// instead of a real ModbusApi.
class TestMockModbusApi : public QObject
{
    Q_OBJECT

private slots:
    void writeWorkMode_recordsValueAndEmitsSuccess();
    void connectToInverter_recordsParameters();
    void emitSettingsRead_deliversSignal();
    void emitWorkModeUpdated_deliversSignal();
};

void TestMockModbusApi::writeWorkMode_recordsValueAndEmitsSuccess()
{
    MockModbusApi mock;
    QSignalSpy spy(&mock, &IModbusApi::writeSucceeded);

    mock.writeWorkMode(7);

    QCOMPARE(mock.lastWorkModeWritten, 7);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("WORK_MODE"));
}

void TestMockModbusApi::connectToInverter_recordsParameters()
{
    MockModbusApi mock;
    mock.connectToInverter(QStringLiteral("192.168.1.148"), 502, 1);

    QCOMPARE(mock.lastHost, QStringLiteral("192.168.1.148"));
    QCOMPARE(mock.lastPort, 502);
    QCOMPARE(mock.lastSlaveId, 1);
    QCOMPARE(mock.connectCallCount, 1);
}

void TestMockModbusApi::emitSettingsRead_deliversSignal()
{
    MockModbusApi mock;
    QSignalSpy spy(&mock, &IModbusApi::settingsRead);

    InverterSettings s;
    s.workMode = WorkMode::ForceDischarge;
    s.minSocPercent = 15;
    s.maxSocPercent = 90;
    mock.emitSettingsRead(s);

    QCOMPARE(spy.count(), 1);
    const auto received = spy.at(0).at(0).value<InverterSettings>();
    QCOMPARE(received.workMode, WorkMode::ForceDischarge);
    QCOMPARE(received.minSocPercent, 15);
    QCOMPARE(received.maxSocPercent, 90);
}

void TestMockModbusApi::emitWorkModeUpdated_deliversSignal()
{
    MockModbusApi mock;
    QSignalSpy spy(&mock, &IModbusApi::workModeUpdated);

    mock.emitWorkModeUpdated(WorkMode::ForceCharge);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<WorkMode>(), WorkMode::ForceCharge);
}

QTEST_GUILESS_MAIN(TestMockModbusApi)
#include "test_mockmodbusapi.moc"
