#pragma once

#include "imodbusapi.h"

// ── MockModbusApi ────────────────────────────────────────────────────────────
//
// Test double for IModbusApi. Every write* method just records its
// (compensated-or-not, verbatim) argument in a public field and emits
// writeSucceeded() — no network access, no QModbusTcpClient involved. Tests
// can also call the public emit*() helpers below to simulate the device
// pushing read data, exercising UI/consumer code exactly as ModbusApi would
// via signals, without a live inverter.
class MockModbusApi : public IModbusApi
{
    Q_OBJECT

public:
    explicit MockModbusApi(QObject *parent = nullptr) : IModbusApi(parent) {}

    // ── Connection ─────────────────────────────────────────────────────────
    void connectToInverter(const QString &host, int port, int slaveId) override
    {
        lastHost = host;
        lastPort = port;
        lastSlaveId = slaveId;
        connectCallCount++;
    }
    void disconnectFromInverter() override { disconnectCallCount++; }

    // ── Polling ────────────────────────────────────────────────────────────
    void startPolling(int intervalSeconds) override { lastPollInterval = intervalSeconds; polling = true; }
    void stopPolling() override { polling = false; }

    // ── One-shot reads ─────────────────────────────────────────────────────
    void readInfoRegisters() override { readInfoCallCount++; }
    void readSettingsRegisters() override { readSettingsCallCount++; }
    void readRemoteControlState() override { readRemoteControlCallCount++; }

    // ── Write operations ────────────────────────────────────────────────────
    void writeWorkMode(int modeValue) override
    {
        lastWorkModeWritten = modeValue;
        emit writeSucceeded(QStringLiteral("WORK_MODE"));
    }
    void writeMinSoC(int percent) override
    {
        lastMinSoCWritten = percent;
        emit writeSucceeded(QStringLiteral("MIN_SOC"));
    }
    void writeMaxSoC(int percent) override
    {
        lastMaxSoCWritten = percent;
        emit writeSucceeded(QStringLiteral("MAX_SOC"));
    }
    void writeMinSoCOnGrid(int percent) override
    {
        lastMinSoCOnGridWritten = percent;
        emit writeSucceeded(QStringLiteral("MIN_SOC_ONGRID"));
    }
    void writeBatteryMaxChargeCurrent(double amperes) override
    {
        lastMaxChargeCurrentWritten = amperes;
        emit writeSucceeded(QStringLiteral("BATTERY_MAX_CHARGE_CURRENT"));
    }
    void writeBatteryMaxDischargeCurrent(double amperes) override
    {
        lastMaxDischargeCurrentWritten = amperes;
        emit writeSucceeded(QStringLiteral("BATTERY_MAX_DISCHARGE_CURRENT"));
    }
    void writeImportPowerLimit(int watts) override
    {
        lastImportPowerLimitWritten = watts;
        emit writeSucceeded(QStringLiteral("IMPORT_POWER_LIMIT"));
    }
    void writeExportPowerLimit(int watts) override
    {
        lastExportPowerLimitWritten = watts;
        emit writeSucceeded(QStringLiteral("EXPORT_POWER_LIMIT"));
    }
    void writeExportPowerLimit2(int watts) override
    {
        lastExportPowerLimit2Written = watts;
        emit writeSucceeded(QStringLiteral("EXPORT_POWER_LIMIT_2"));
    }
    void writeThresholdSoC(int percent) override
    {
        lastThresholdSoCWritten = percent;
        emit writeSucceeded(QStringLiteral("THRESHOLD_SOC"));
    }
    void writeRemoteControl(quint16 bitfield) override
    {
        lastRemoteControlBitfieldWritten = bitfield;
        emit writeSucceeded(QStringLiteral("REMOTE_CONTROL"));
    }
    void writeRemoteTimeout(int seconds) override
    {
        lastRemoteTimeoutWritten = seconds;
        emit writeSucceeded(QStringLiteral("REMOTE_TIMEOUT_SET"));
    }
    void writeRemoteActivePower(int watts) override
    {
        lastRemoteActivePowerWritten = watts;
        emit writeSucceeded(QStringLiteral("REMOTE_ACTIVE_POWER"));
    }
    void writeRemoteReactivePower(int var) override
    {
        lastRemoteReactivePowerWritten = var;
        emit writeSucceeded(QStringLiteral("REMOTE_REACTIVE_POWER"));
    }

    // ── State query ────────────────────────────────────────────────────────
    QModbusDevice::State state() const override { return fakeState; }
    bool isConnected() const override { return fakeState == QModbusDevice::ConnectedState; }
    QString lastError() const override { return fakeLastError; }

    // ── Test helpers: simulate the device pushing read data ────────────────
    void emitSettingsRead(const InverterSettings &s) { emit settingsRead(s); }
    void emitWorkModeUpdated(WorkMode mode) { emit workModeUpdated(mode); }
    void emitConnectionStateChanged(QModbusDevice::State s) { fakeState = s; emit connectionStateChanged(s); }
    void emitError(const QString &message) { fakeLastError = message; emit errorOccurred(message); }

    // ── Recorded state (public for direct test assertions) ─────────────────
    QString lastHost;
    int     lastPort               = -1;
    int     lastSlaveId            = -1;
    int     connectCallCount       = 0;
    int     disconnectCallCount    = 0;
    int     lastPollInterval       = -1;
    bool    polling                = false;
    int     readInfoCallCount      = 0;
    int     readSettingsCallCount  = 0;
    int     readRemoteControlCallCount = 0;

    int    lastWorkModeWritten              = -1;
    int    lastMinSoCWritten                = -1;
    int    lastMaxSoCWritten                = -1;
    int    lastMinSoCOnGridWritten           = -1;
    double lastMaxChargeCurrentWritten       = -1.0;
    double lastMaxDischargeCurrentWritten    = -1.0;
    int    lastImportPowerLimitWritten       = -1;
    int    lastExportPowerLimitWritten       = -1;
    int    lastExportPowerLimit2Written      = -1;
    int    lastThresholdSoCWritten           = -1;
    quint16 lastRemoteControlBitfieldWritten = 0;
    int    lastRemoteTimeoutWritten          = -1;
    int    lastRemoteActivePowerWritten      = -1;
    int    lastRemoteReactivePowerWritten    = -1;

    QModbusDevice::State fakeState     = QModbusDevice::UnconnectedState;
    QString              fakeLastError;
};
