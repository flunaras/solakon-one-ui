#include "registerparser.h"

#include <QtSerialBus/QModbusDataUnit>
#include <QtTest/QtTest>

// ── test_registerparser ─────────────────────────────────────────────────────
//
// Unit tests for RegisterParser — pure decoding logic, no Modbus connection
// required. See AGENTS.md "Register Type Parsing" for the reference
// semantics being verified here.
class TestRegisterParser : public QObject
{
    Q_OBJECT

private slots:
    void parseI16_positive();
    void parseI16_negative();
    void parseU16_basic();
    void parseI32_positive();
    void parseI32_negative();
    void parseU32_basic();
    void parseString_basic();
    void parseString_nullPadded();
    void parseString_partiallyPadded();
};

namespace {
QModbusDataUnit makeUnit(const QList<quint16> &values)
{
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, 0,
                         static_cast<quint16>(values.size()));
    for (int i = 0; i < values.size(); ++i)
        unit.setValue(i, values[i]);
    return unit;
}
}

void TestRegisterParser::parseI16_positive()
{
    const auto unit = makeUnit({1234});
    QCOMPARE(RegisterParser::parseI16(unit, 0), static_cast<int16_t>(1234));
}

void TestRegisterParser::parseI16_negative()
{
    // 0xFFFF -> -1 (two's complement)
    const auto unit = makeUnit({0xFFFF});
    QCOMPARE(RegisterParser::parseI16(unit, 0), static_cast<int16_t>(-1));

    // 0x8000 -> -32768 (most negative i16)
    const auto unit2 = makeUnit({0x8000});
    QCOMPARE(RegisterParser::parseI16(unit2, 0), static_cast<int16_t>(-32768));
}

void TestRegisterParser::parseU16_basic()
{
    const auto unit = makeUnit({0, 65535, 42});
    QCOMPARE(RegisterParser::parseU16(unit, 0), static_cast<uint16_t>(0));
    QCOMPARE(RegisterParser::parseU16(unit, 1), static_cast<uint16_t>(65535));
    QCOMPARE(RegisterParser::parseU16(unit, 2), static_cast<uint16_t>(42));
}

void TestRegisterParser::parseI32_positive()
{
    // FoxESS big-endian word order: high word first, low word second.
    // 0x0001_0000 = 65536
    const auto unit = makeUnit({0x0001, 0x0000});
    QCOMPARE(RegisterParser::parseI32(unit, 0), static_cast<int32_t>(65536));
}

void TestRegisterParser::parseI32_negative()
{
    // 0xFFFF_FFFF -> -1
    const auto unit = makeUnit({0xFFFF, 0xFFFF});
    QCOMPARE(RegisterParser::parseI32(unit, 0), static_cast<int32_t>(-1));
}

void TestRegisterParser::parseU32_basic()
{
    // 0x0001_0002 = 65538
    const auto unit = makeUnit({0x0001, 0x0002});
    QCOMPARE(RegisterParser::parseU32(unit, 0), static_cast<uint32_t>(65538));
}

void TestRegisterParser::parseString_basic()
{
    // "AB" packed as one register (high byte 'A', low byte 'B')
    const auto unit = makeUnit({0x4142});
    QCOMPARE(RegisterParser::parseString(unit, 0, 1), QStringLiteral("AB"));
}

void TestRegisterParser::parseString_nullPadded()
{
    // "AB" followed by fully-null registers (common for MODEL_NAME/SERIAL_NUMBER)
    const auto unit = makeUnit({0x4142, 0x0000, 0x0000});
    QCOMPARE(RegisterParser::parseString(unit, 0, 3), QStringLiteral("AB"));
}

void TestRegisterParser::parseString_partiallyPadded()
{
    // "ABC" -> registers [0x4142, 0x4300] (last register half-padded with a null low byte)
    const auto unit = makeUnit({0x4142, 0x4300});
    QCOMPARE(RegisterParser::parseString(unit, 0, 2), QStringLiteral("ABC"));
}

QTEST_GUILESS_MAIN(TestRegisterParser)
#include "test_registerparser.moc"
