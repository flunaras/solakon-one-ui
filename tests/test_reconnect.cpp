#include "modbusapi.h"

#include <QtTest/QtTest>

// ── test_reconnect ───────────────────────────────────────────────────────────
//
// Unit tests for the auto-reconnect exponential backoff schedule
// (ModbusApi::nextReconnectDelayMs()).
class TestReconnect : public QObject
{
    Q_OBJECT

private slots:
    void nextDelay_doubles();
    void nextDelay_clampsToMax();
    void nextDelay_clampsToInitial();
    void fullSchedule_reachesMax();
};

void TestReconnect::nextDelay_doubles()
{
    QCOMPARE(ModbusApi::nextReconnectDelayMs(2000), 4000);
    QCOMPARE(ModbusApi::nextReconnectDelayMs(4000), 8000);
    QCOMPARE(ModbusApi::nextReconnectDelayMs(16000), 32000);
}

void TestReconnect::nextDelay_clampsToMax()
{
    QCOMPARE(ModbusApi::nextReconnectDelayMs(32000), ModbusApi::kReconnectMaxDelayMs);
    QCOMPARE(ModbusApi::nextReconnectDelayMs(ModbusApi::kReconnectMaxDelayMs),
             ModbusApi::kReconnectMaxDelayMs);
    QCOMPARE(ModbusApi::nextReconnectDelayMs(std::numeric_limits<int>::max()),
             ModbusApi::kReconnectMaxDelayMs);
}

void TestReconnect::nextDelay_clampsToInitial()
{
    QCOMPARE(ModbusApi::nextReconnectDelayMs(0), ModbusApi::kReconnectInitialDelayMs);
    QCOMPARE(ModbusApi::nextReconnectDelayMs(-5), ModbusApi::kReconnectInitialDelayMs);
}

void TestReconnect::fullSchedule_reachesMax()
{
    QList<int> delays;
    int d = ModbusApi::kReconnectInitialDelayMs;
    for (int i = 0; i < 7; ++i) {
        delays << d;
        d = ModbusApi::nextReconnectDelayMs(d);
    }
    QCOMPARE(delays, (QList<int>{ 2000, 4000, 8000, 16000, 32000, 60000, 60000 }));
}

QTEST_GUILESS_MAIN(TestReconnect)
#include "test_reconnect.moc"
