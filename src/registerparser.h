#pragma once

#include <QtCore/QString>
#include <QtSerialBus/QModbusDataUnit>

#include <cstdint>

// ── RegisterParser ──────────────────────────────────────────────────────────────
//
// Pure, static, network-free helpers for decoding FoxESS Modbus register
// blocks into typed values. Kept independent of ModbusApi/QModbusTcpClient so
// they can be unit-tested (see tests/test_registerparser.cpp) without a live
// Modbus connection.
//
// All offsets are relative to the QModbusDataUnit's own start address (i.e.
// offset 0 is the first register in the block that was read), not the
// absolute Modbus register address.
//
// 32-bit values use FoxESS's big-endian word order: the high word is stored
// at the lower offset, the low word at offset+1.
class RegisterParser
{
public:
    RegisterParser() = delete;

    static int16_t  parseI16(const QModbusDataUnit &unit, int offset);
    static uint16_t parseU16(const QModbusDataUnit &unit, int offset);
    static int32_t  parseI32(const QModbusDataUnit &unit, int offset);
    static uint32_t parseU32(const QModbusDataUnit &unit, int offset);

    // length = number of registers; each register holds 2 chars (high byte
    // first). Null bytes (0x00) are skipped, and the result is trimmed, so
    // both partially- and fully-null-padded strings are handled.
    static QString parseString(const QModbusDataUnit &unit, int offset, int length);
};
