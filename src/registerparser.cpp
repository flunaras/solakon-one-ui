#include "registerparser.h"

int16_t RegisterParser::parseI16(const QModbusDataUnit &unit, int offset)
{
    return static_cast<int16_t>(unit.value(offset));
}

uint16_t RegisterParser::parseU16(const QModbusDataUnit &unit, int offset)
{
    return unit.value(offset);
}

// High word first (FoxESS big-endian word order)
int32_t RegisterParser::parseI32(const QModbusDataUnit &unit, int offset)
{
    auto hi = static_cast<uint32_t>(unit.value(offset));
    auto lo = static_cast<uint32_t>(unit.value(offset + 1));
    return static_cast<int32_t>((hi << 16) | lo);
}

uint32_t RegisterParser::parseU32(const QModbusDataUnit &unit, int offset)
{
    auto hi = static_cast<uint32_t>(unit.value(offset));
    auto lo = static_cast<uint32_t>(unit.value(offset + 1));
    return (hi << 16) | lo;
}

// Each register holds 2 chars: high byte first. Null bytes are skipped.
QString RegisterParser::parseString(const QModbusDataUnit &unit, int offset, int length)
{
    QByteArray bytes;
    bytes.reserve(length * 2);
    for (int i = 0; i < length; ++i) {
        const uint16_t reg = unit.value(offset + i);
        const char hi = static_cast<char>((reg >> 8) & 0xFF);
        const char lo = static_cast<char>(reg & 0xFF);
        if (hi) bytes.append(hi);
        if (lo) bytes.append(lo);
    }
    return QString::fromLatin1(bytes).trimmed();
}
