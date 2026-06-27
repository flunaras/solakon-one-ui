#pragma once

#include "inverterdata.h"

#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtSerialBus/QModbusDataUnit>
#include <QtSerialBus/QModbusDevice>
#include <QtSerialBus/QModbusReply>
#include <QtSerialBus/QModbusTcpClient>

#include <functional>
#include <variant>

// ── ModbusApi ──────────────────────────────────────────────────────────────────
//
// Owns the QModbusTcpClient and all Modbus communication for the application.
// All reads and writes are non-blocking (async via QModbusReply::finished()).
// UI widgets must never access QModbusTcpClient directly; they receive data
// exclusively through the signals emitted by this class.
//
// Lifecycle:
//   connectToInverter()      → connection attempt begins
//   startPolling()           → periodic register reads start
//   stopPolling()            → polling suspended (connection kept alive)
//   disconnectFromInverter() → TCP connection closed
//
// Error recovery:
//   On QModbusDevice::errorOccurred(), errorOccurred() is emitted and the
//   Modbus device transitions to a disconnected/error state. MainWindow
//   should then show the reconnect button or auto-reconnect after a delay.
//
// Register reads:
//   - Info registers (model, serial, versions) are read once after connect.
//   - Settings registers are read once after connect and after each write.
//   - Poll registers are read every intervalSeconds via a QTimer.
class ModbusApi : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultPort              = 502;
    static constexpr int kDefaultSlaveId           = 1;
    static constexpr int kDefaultPollIntervalSec   = 10;
    static constexpr int kRequestTimeoutMs         = 5000;

    explicit ModbusApi(QObject *parent = nullptr);
    ~ModbusApi() override;

    // ── Connection ─────────────────────────────────────────────────────────
    void connectToInverter(const QString &host, int port, int slaveId);
    void disconnectFromInverter();

    // ── Polling ────────────────────────────────────────────────────────────
    void startPolling(int intervalSeconds = kDefaultPollIntervalSec);
    void stopPolling();

    // ── One-shot reads ─────────────────────────────────────────────────────
    void readInfoRegisters();
    void readSettingsRegisters();

    // ── Write operations (range validation is the caller's responsibility) ─
    void writeWorkMode(int modeValue);
    void writeMinSoC(int percent);
    void writeMaxSoC(int percent);
    void writeMinSoCOnGrid(int percent);
    // Amperes → raw = qRound(A * 10), i16 in register
    void writeBatteryMaxChargeCurrent(double amperes);
    void writeBatteryMaxDischargeCurrent(double amperes);
    void writeImportPowerLimit(int watts);
    void writeExportPowerLimit(int watts);
    void writeExportPowerLimit2(int watts);
    void writeThresholdSoC(int percent);
    // Remote control
    void writeRemoteControl(quint16 bitfield);
    void writeRemoteTimeout(int seconds);
    void writeRemoteActivePower(int watts);
    void writeRemoteReactivePower(int var);

    // ── State query ────────────────────────────────────────────────────────
    QModbusDevice::State state() const;
    bool isConnected() const;
    QString lastError() const;

signals:
    // ── Connection ─────────────────────────────────────────────────────────
    void connectionStateChanged(QModbusDevice::State newState);
    void errorOccurred(const QString &message);

    // ── Read data (emitted on each successful poll or one-shot read) ───────
    void infoUpdated(const InverterInfo &info);
    void statusUpdated(const InverterStatus &status);
    void alarmStateUpdated(const AlarmState &alarms);
    void pvDataUpdated(const PvData &data);
    void gridDataUpdated(const GridData &data);
    void batteryDataUpdated(const BatteryData &data);
    void energyDataUpdated(const EnergyData &data);
    void temperatureUpdated(double celsius);
    void settingsRead(const InverterSettings &settings);

    // ── Write results ──────────────────────────────────────────────────────
    void writeSucceeded(const QString &registerName);
    void writeFailed(const QString &registerName, const QString &error);

private slots:
    void onPollTimerFired();
    void onModbusStateChanged(QModbusDevice::State newState);
    void onModbusErrorOccurred(QModbusDevice::Error error);

private:
    // ── Unified request queue ──────────────────────────────────────────────
    // All reads AND writes go through this queue so at most one Modbus request
    // is in-flight at a time. The FoxESS embedded stack silently drops any
    // concurrent request (reads and writes alike), causing "Request timeout".
    using ReadCallback = std::function<void(const QModbusDataUnit &)>;

    struct PendingRead {
        int          startAddress;
        int          count;
        ReadCallback callback;
    };
    struct PendingWrite {
        QModbusDataUnit unit;
        QString         name;
    };
    using PendingRequest = std::variant<PendingRead, PendingWrite>;

    void enqueueRead(int startAddress, int count, ReadCallback callback);
    void enqueueWrite(const QModbusDataUnit &unit, const QString &name);
    // Dispatches the next queued request if none is currently in-flight.
    void processNextRequest();

    // ── Write helpers ──────────────────────────────────────────────────────
    void sendWriteSingleRegister(int address, quint16 value, const QString &name);
    void sendWriteDoubleRegister(int address, qint32 value, const QString &name);

    // ── Register-type parsers (static, index is relative to unit.startAddress) ─
    static int16_t  parseI16(const QModbusDataUnit &unit, int offset);
    static uint16_t parseU16(const QModbusDataUnit &unit, int offset);
    static int32_t  parseI32(const QModbusDataUnit &unit, int offset);
    static uint32_t parseU32(const QModbusDataUnit &unit, int offset);
    // length = number of registers; each register holds 2 chars (high byte first)
    static QString  parseString(const QModbusDataUnit &unit, int offset, int length);

    // ── Poll block parsers ─────────────────────────────────────────────────
    void parseAndEmitPollBlock1(const QModbusDataUnit &unit);   // 39063, count=57
    void parseAndEmitPollBlock2(const QModbusDataUnit &unit);   // 39123, count=30
    void parseAndEmitPollBlock3(const QModbusDataUnit &unit);   // 39227, count=12

    // ── Multi-block read completions ───────────────────────────────────────
    void parseAndEmitInfo(const QModbusDataUnit &modelBlock,
                          const QModbusDataUnit &versBlock,
                          const QModbusDataUnit &devBlock);
    void parseAndEmitSettings(const QModbusDataUnit &powLimBlock,
                              const QModbusDataUnit &battBlock,
                              const QModbusDataUnit &modeBlock);

    // ── Members ────────────────────────────────────────────────────────────
    QModbusTcpClient    *m_client           = nullptr;
    QTimer              *m_pollTimer        = nullptr;
    QString              m_host;
    int                  m_port             = kDefaultPort;
    int                  m_slaveId          = kDefaultSlaveId;
    QString              m_lastError;
    // Cached BMS1 SoC from the dedicated poll read; merged into BatteryData
    // when poll block 3 (battery electrical data) is emitted.
    // -1 means not yet received.
    int                  m_lastSocPercent   = -1;

    // Serialized request queue — only one Modbus request (read or write) is
    // in-flight at a time.
    QQueue<PendingRequest> m_requestQueue;
    bool                   m_requestInFlight = false;
};
