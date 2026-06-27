#pragma once

#include "inverterdata.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtSerialBus/QModbusDevice>

// ── IModbusApi ───────────────────────────────────────────────────────────────
//
// Abstract interface for Modbus communication with the inverter. Extracted so
// UI widgets (SettingsWidget, RemoteControlWidget, MainWindow, ...) depend on
// this interface rather than the concrete ModbusApi implementation, allowing
// tests to inject a MockModbusApi that emits canned signals with fake
// register data — no live Modbus/TCP connection required.
//
// ModbusApi is the real, network-backed implementation (see modbusapi.h).
// MockModbusApi (tests/mockmodbusapi.h) is a test double.
class IModbusApi : public QObject
{
    Q_OBJECT

public:
    explicit IModbusApi(QObject *parent = nullptr) : QObject(parent) {}
    ~IModbusApi() override = default;

    // ── Connection ─────────────────────────────────────────────────────────
    virtual void connectToInverter(const QString &host, int port, int slaveId) = 0;
    virtual void disconnectFromInverter() = 0;

    // ── Polling ────────────────────────────────────────────────────────────
    virtual void startPolling(int intervalSeconds) = 0;
    virtual void stopPolling() = 0;

    // ── One-shot reads ─────────────────────────────────────────────────────
    virtual void readInfoRegisters() = 0;
    virtual void readSettingsRegisters() = 0;
    virtual void readRemoteControlState() = 0;

    // ── Write operations (range validation is the caller's responsibility) ─
    virtual void writeWorkMode(int modeValue) = 0;
    virtual void writeMinSoC(int percent) = 0;
    virtual void writeMaxSoC(int percent) = 0;
    virtual void writeMinSoCOnGrid(int percent) = 0;
    virtual void writeBatteryMaxChargeCurrent(double amperes) = 0;
    virtual void writeBatteryMaxDischargeCurrent(double amperes) = 0;
    virtual void writeImportPowerLimit(int watts) = 0;
    virtual void writeExportPowerLimit(int watts) = 0;
    virtual void writeExportPowerLimit2(int watts) = 0;
    virtual void writeThresholdSoC(int percent) = 0;
    virtual void writeRemoteControl(quint16 bitfield) = 0;
    virtual void writeRemoteTimeout(int seconds) = 0;
    virtual void writeRemoteActivePower(int watts) = 0;
    virtual void writeRemoteReactivePower(int var) = 0;

    // ── State query ────────────────────────────────────────────────────────
    virtual QModbusDevice::State state() const = 0;
    virtual bool isConnected() const = 0;
    virtual QString lastError() const = 0;

signals:
    // ── Connection ─────────────────────────────────────────────────────────
    void connectionStateChanged(QModbusDevice::State newState);
    void errorOccurred(const QString &message);

    // ── Read data ────────────────────────────────────────────────────────
    void infoUpdated(const InverterInfo &info);
    void statusUpdated(const InverterStatus &status);
    void gridStatusUpdated(const GridStatus &status);
    void alarmStateUpdated(const AlarmState &alarms);
    void pvDataUpdated(const PvData &data);
    void gridDataUpdated(const GridData &data);
    void batteryDataUpdated(const BatteryData &data);
    void energyDataUpdated(const EnergyData &data);
    void temperatureUpdated(double celsius);
    void settingsRead(const InverterSettings &settings);
    void remoteControlStateUpdated(const RemoteControlState &state);
    void workModeUpdated(WorkMode mode);

    // ── Write results ──────────────────────────────────────────────────────
    void writeSucceeded(const QString &registerName);
    void writeFailed(const QString &registerName, const QString &error);
};
