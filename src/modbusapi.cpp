#include "modbusapi.h"
#include "registerparser.h"

#include <QtCore/QDebug>
#include <QtCore/QVariant>

// ── Register addresses (FoxESS Modbus Protocol V1.05) ─────────────────────────
// Read-only — model / serial / manufacturer strings
static constexpr int kAddrModelBlock        = 30000;  // count=48: name+serial+mfg
static constexpr int kAddrVersionBlock      = 36001;  // count=3:  master/slave/mgr
static constexpr int kAddrDeviceBlock       = 39053;  // count=4:  rated + max active power

// Read-only — poll block 1 (status, alarms, PV)
static constexpr int kAddrPollBlock1        = 39063;  // count=57 → last reg 39119
// Offsets within block 1 (relative to 39063)
static constexpr int kOff1Status            = 0;   // 39063 u16 bitfield (Standby/Operation/Fault)
static constexpr int kOff1GridStatus        = 2;   // 39065 u16 bitfield (bit 0 = Off-Grid/EPS)
static constexpr int kOff1Alarm1            = 4;   // 39067
static constexpr int kOff1Alarm2            = 5;   // 39068
static constexpr int kOff1Alarm3            = 6;   // 39069
static constexpr int kOff1Pv1Voltage        = 7;   // 39070 i16 /10 V
static constexpr int kOff1Pv1Current        = 8;   // 39071 i16 /100 A
static constexpr int kOff1Pv2Voltage        = 9;   // 39072 i16 /10 V
static constexpr int kOff1Pv2Current        = 10;  // 39073 i16 /100 A
// PV3/PV4: sequential pattern, addresses inferred from the protocol layout;
// registers are within the already-read block so no extra Modbus requests needed.
static constexpr int kOff1Pv3Voltage        = 11;  // 39074 i16 /10 V  (inferred)
static constexpr int kOff1Pv3Current        = 12;  // 39075 i16 /100 A (inferred)
static constexpr int kOff1Pv4Voltage        = 13;  // 39076 i16 /10 V  (inferred)
static constexpr int kOff1Pv4Current        = 14;  // 39077 i16 /100 A (inferred)
static constexpr int kOff1TotalPvPower      = 55;  // 39118 i32 /1000 kW

// Read-only — poll block 2 (grid, temperature, energy)
static constexpr int kAddrPollBlock2        = 39123; // count=30 → last reg 39152
// Offsets within block 2 (relative to 39123)
static constexpr int kOff2GridRVoltage      = 0;   // 39123 i16 /10 V
static constexpr int kOff2GridSVoltage      = 1;   // 39124 i16 /10 V
static constexpr int kOff2GridTVoltage      = 2;   // 39125 i16 /10 V
static constexpr int kOff2ActivePower       = 11;  // 39134 i32 /1000 kW  (+ = export)
static constexpr int kOff2ReactivePower     = 13;  // 39136 i32 /1000 kVar
static constexpr int kOff2PowerFactor       = 15;  // 39138 i16 /1000
static constexpr int kOff2GridFrequency     = 16;  // 39139 i16 /100 Hz
static constexpr int kOff2InternalTemp      = 18;  // 39141 i16 /10 °C
static constexpr int kOff2CumulativeGen     = 26;  // 39149 u32 /100 kWh
static constexpr int kOff2DailyGen          = 28;  // 39151 u32 /100 kWh

// Read-only — poll block 3 (battery)
static constexpr int kAddrPollBlock3        = 39227; // count=12 → last reg 39238
// Offsets within block 3 (relative to 39227)
static constexpr int kOff3Battery1Voltage   = 0;   // 39227 i16 /10 V
static constexpr int kOff3Battery1Current   = 1;   // 39228 i32 /1000 A
static constexpr int kOff3Battery1Power     = 3;   // 39230 i32 /1 W
static constexpr int kOff3BatteryCombPower  = 10;  // 39237 i32 /1 W

// Read-only — BMS1 SoC (separate address range, Table 3-x in Modbus doc V1.02)
// Register 37612: BMS1 SoC, u16, %, scale=1. Polled immediately before block 3
// so that m_lastSocPercent is fresh when batteryDataUpdated is emitted.
static constexpr int kAddrBms1SocBlock      = 37612; // count=1, u16, %

// Read/write — settings
static constexpr int kAddrImportPowerLimit  = 46501; // count=5: import(i32), threshSoC(u16), export(i32)
static constexpr int kAddrBattSettings      = 46607; // count=11: maxCharge(i16)/maxDisch(i16)/minSoC/maxSoC/minSoCOG + exportLim2(i32 at +9)
static constexpr int kAddrWorkMode          = 49203; // count=1

// Read/write — remote control
static constexpr int kAddrRemoteControl     = 46001; // bitfield16
static constexpr int kAddrRemoteTimeout     = 46002; // u16 seconds
static constexpr int kAddrRemoteActivePower = 46003; // i32 W
static constexpr int kAddrRemoteReactPower  = 46005; // i32 Var

// Write-once — power on/off (do NOT poll back; returned 0 does not mean off)
static constexpr int kAddrExportPowerLimit  = 46504; // i32 W  (within import block)
static constexpr int kAddrExportPowerLimit2 = 46616; // i32 W  (within batt block, offset 9)
static constexpr int kAddrThresholdSoC      = 46503; // u16    (within import block, offset 2)
static constexpr int kAddrMinSoC            = 46609;
static constexpr int kAddrMaxSoC            = 46610;
static constexpr int kAddrMinSoCOnGrid      = 46611;
static constexpr int kAddrMaxChargeCurrent  = 46607;
static constexpr int kAddrMaxDischCurrent   = 46608;

// ── Constructor / Destructor ────────────────────────────────────────────────────

ModbusApi::ModbusApi(QObject *parent)
    : IModbusApi(parent)
    , m_client(new QModbusTcpClient(this))
    , m_pollTimer(new QTimer(this))
{
    m_client->setTimeout(kRequestTimeoutMs);

    connect(m_client, &QModbusDevice::stateChanged,
            this, &ModbusApi::onModbusStateChanged);
    connect(m_client, &QModbusDevice::errorOccurred,
            this, &ModbusApi::onModbusErrorOccurred);

    m_pollTimer->setInterval(kDefaultPollIntervalSec * 1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusApi::onPollTimerFired);
}

ModbusApi::~ModbusApi()
{
    m_pollTimer->stop();
    if (m_client->state() != QModbusDevice::UnconnectedState) {
        m_client->disconnectDevice();
    }
}

// ── Connection control ─────────────────────────────────────────────────────────

void ModbusApi::connectToInverter(const QString &host, int port, int slaveId)
{
    m_host    = host;
    m_port    = port;
    m_slaveId = slaveId;

    if (m_client->state() != QModbusDevice::UnconnectedState) {
        m_client->disconnectDevice();
    }

    m_client->setConnectionParameter(QModbusDevice::NetworkAddressParameter, QVariant(host));
    m_client->setConnectionParameter(QModbusDevice::NetworkPortParameter,    QVariant(port));

    if (!m_client->connectDevice()) {
        m_lastError = m_client->errorString();
        emit errorOccurred(QStringLiteral("Connection to %1:%2 failed: %3")
                               .arg(host).arg(port).arg(m_lastError));
    }
}

void ModbusApi::disconnectFromInverter()
{
    m_pollTimer->stop();
    // Discard queued requests so they don't replay on the next connection.
    m_requestQueue.clear();
    m_requestInFlight = false;
    if (m_client->state() != QModbusDevice::UnconnectedState) {
        m_client->disconnectDevice();
    }
}

// ── Polling ────────────────────────────────────────────────────────────────────

void ModbusApi::startPolling(int intervalSeconds)
{
    m_pollTimer->setInterval(qBound(2, intervalSeconds, 300) * 1000);
    m_pollTimer->start();
}

void ModbusApi::stopPolling()
{
    m_pollTimer->stop();
}

// ── One-shot reads ─────────────────────────────────────────────────────────────

void ModbusApi::readInfoRegisters()
{
    if (!isConnected()) return;

    // Three blocks arrive asynchronously; shared state collects all three.
    struct InfoState {
        QModbusDataUnit modelBlock;
        QModbusDataUnit versBlock;
        QModbusDataUnit devBlock;
        int pending = 3;
    };
    auto s = std::make_shared<InfoState>();

    enqueueRead(kAddrModelBlock, 48, [this, s](const QModbusDataUnit &u) {
        s->modelBlock = u;
        if (--s->pending == 0) parseAndEmitInfo(s->modelBlock, s->versBlock, s->devBlock);
    });
    enqueueRead(kAddrVersionBlock, 3, [this, s](const QModbusDataUnit &u) {
        s->versBlock = u;
        if (--s->pending == 0) parseAndEmitInfo(s->modelBlock, s->versBlock, s->devBlock);
    });
    enqueueRead(kAddrDeviceBlock, 4, [this, s](const QModbusDataUnit &u) {
        s->devBlock = u;
        if (--s->pending == 0) parseAndEmitInfo(s->modelBlock, s->versBlock, s->devBlock);
    });
}

void ModbusApi::readSettingsRegisters()
{
    if (!isConnected()) return;

    struct SettingsState {
        QModbusDataUnit powLimBlock;
        QModbusDataUnit battBlock;
        QModbusDataUnit modeBlock;
        int pending = 3;
    };
    auto s = std::make_shared<SettingsState>();

    // 46501..46505: import power limit (i32), threshold SoC (u16), export power limit (i32)
    enqueueRead(kAddrImportPowerLimit, 5, [this, s](const QModbusDataUnit &u) {
        s->powLimBlock = u;
        if (--s->pending == 0) parseAndEmitSettings(s->powLimBlock, s->battBlock, s->modeBlock);
    });
    // 46607..46617: charge/discharge currents (i16 each), min/max SoC (u16 each),
    //               minSoCOnGrid (u16), 4 unused regs, exportLimit2 (i32)
    enqueueRead(kAddrBattSettings, 11, [this, s](const QModbusDataUnit &u) {
        s->battBlock = u;
        if (--s->pending == 0) parseAndEmitSettings(s->powLimBlock, s->battBlock, s->modeBlock);
    });
    enqueueRead(kAddrWorkMode, 1, [this, s](const QModbusDataUnit &u) {
        s->modeBlock = u;
        if (--s->pending == 0) parseAndEmitSettings(s->powLimBlock, s->battBlock, s->modeBlock);
    });
}

void ModbusApi::readRemoteControlState()
{
    if (!isConnected()) return;

    enqueueRead(kAddrRemoteControl, 1, [this](const QModbusDataUnit &u) {
        parseAndEmitRemoteControl(u);
    });
}

// ── Slots ──────────────────────────────────────────────────────────────────────

void ModbusApi::onPollTimerFired()
{
    if (!isConnected()) return;

    enqueueRead(kAddrPollBlock1, 57, [this](const QModbusDataUnit &u) {
        parseAndEmitPollBlock1(u);
    });
    enqueueRead(kAddrPollBlock2, 30, [this](const QModbusDataUnit &u) {
        parseAndEmitPollBlock2(u);
    });
    // BMS1 SoC is read before block 3 so m_lastSocPercent is current when
    // batteryDataUpdated is emitted. On error the cached value is preserved
    // and the UI keeps showing the last known percentage.
    enqueueRead(kAddrBms1SocBlock, 1, [this](const QModbusDataUnit &u) {
        m_lastSocPercent = static_cast<int>(parseU16(u, 0));
    });
    enqueueRead(kAddrPollBlock3, 12, [this](const QModbusDataUnit &u) {
        parseAndEmitPollBlock3(u);
    });
    // REMOTE_CONTROL bitfield read-back (46001). Surfaces the current remote-
    // control engagement state so the user can see when an external session
    // (e.g. FoxESS app's strategy periods, or another tool) has taken over.
    enqueueRead(kAddrRemoteControl, 1, [this](const QModbusDataUnit &u) {
        parseAndEmitRemoteControl(u);
    });
    // WORK_MODE (49203) — polled every cycle so external mode changes (e.g.
    // the FoxESS app switching to Force Charge/Discharge) are reflected
    // immediately in the Dashboard. readSettingsRegisters() also reads this
    // value, but only at connect and after writes; the live poll closes the gap.
    enqueueRead(kAddrWorkMode, 1, [this](const QModbusDataUnit &u) {
        const WorkMode m = workModeFromInt(static_cast<int>(parseU16(u, 0)));
        emit workModeUpdated(m);
    });
}

void ModbusApi::onModbusStateChanged(QModbusDevice::State newState)
{
    emit connectionStateChanged(newState);

    if (newState == QModbusDevice::ConnectedState) {
        qDebug() << "[ModbusApi] connected to" << m_host << ":" << m_port;
        readInfoRegisters();
        readSettingsRegisters();
        readRemoteControlState();
    } else if (newState == QModbusDevice::UnconnectedState) {
        qDebug() << "[ModbusApi] disconnected";
        m_requestQueue.clear();
        m_requestInFlight = false;
    }
}

void ModbusApi::onModbusErrorOccurred(QModbusDevice::Error error)
{
    if (error == QModbusDevice::NoError) return;
    m_lastError = m_client->errorString();
    emit errorOccurred(m_lastError);
    qWarning() << "[ModbusApi] device error:" << error << m_lastError;
}

// ── State query ────────────────────────────────────────────────────────────────

QModbusDevice::State ModbusApi::state() const  { return m_client->state(); }
bool ModbusApi::isConnected() const            { return m_client->state() == QModbusDevice::ConnectedState; }
QString ModbusApi::lastError() const           { return m_lastError; }

// ── enqueueRead / enqueueWrite / processNextRequest ───────────────────────────
// All Modbus reads AND writes go through the queue so at most one request is
// in-flight at a time. The FoxESS embedded stack silently drops concurrent
// requests (reads and writes alike), which manifests as "Request timeout".

void ModbusApi::enqueueRead(int startAddress, int count, ReadCallback callback)
{
    m_requestQueue.enqueue(PendingRead{ startAddress, count, std::move(callback) });
    processNextRequest();
}

void ModbusApi::enqueueWrite(const QModbusDataUnit &unit, const QString &name)
{
    m_requestQueue.enqueue(PendingWrite{ unit, name });
    processNextRequest();
}

void ModbusApi::processNextRequest()
{
    if (m_requestInFlight || m_requestQueue.isEmpty() || !isConnected())
        return;

    PendingRequest req = m_requestQueue.dequeue();
    m_requestInFlight = true;

    if (auto *r = std::get_if<PendingRead>(&req)) {
        // ── Dispatch read ──────────────────────────────────────────────────
        PendingRead pending = std::move(*r);
        QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, pending.startAddress,
                             static_cast<quint16>(pending.count));
        auto *reply = m_client->sendReadRequest(unit, m_slaveId);
        if (!reply) {
            m_lastError = m_client->errorString();
            emit errorOccurred(QStringLiteral("Read request failed (addr=%1): %2")
                                   .arg(pending.startAddress).arg(m_lastError));
            m_requestInFlight = false;
            processNextRequest();
            return;
        }
        if (reply->isFinished()) {
            if (reply->error() == QModbusDevice::NoError)
                pending.callback(reply->result());
            else
                emit errorOccurred(reply->errorString());
            reply->deleteLater();
            m_requestInFlight = false;
            processNextRequest();
            return;
        }
        connect(reply, &QModbusReply::finished, this,
                [this, reply, cb = std::move(pending.callback)]() {
                    if (reply->error() == QModbusDevice::NoError) {
                        cb(reply->result());
                    } else {
                        const QString err = reply->errorString();
                        if (!err.isEmpty()) {
                            emit errorOccurred(err);
                            qWarning() << "[ModbusApi] read error:" << err;
                        }
                    }
                    reply->deleteLater();
                    m_requestInFlight = false;
                    processNextRequest();
                });

    } else if (auto *w = std::get_if<PendingWrite>(&req)) {
        // ── Dispatch write ─────────────────────────────────────────────────
        PendingWrite pending = std::move(*w);
        auto *reply = m_client->sendWriteRequest(pending.unit, m_slaveId);
        if (!reply) {
            emit writeFailed(pending.name, m_client->errorString());
            m_requestInFlight = false;
            processNextRequest();
            return;
        }
        if (reply->isFinished()) {
            if (reply->error() == QModbusDevice::NoError) {
                emit writeSucceeded(pending.name);
            } else {
                qWarning() << "[ModbusApi] write error:" << pending.name << reply->errorString();
                emit writeFailed(pending.name, reply->errorString());
            }
            reply->deleteLater();
            m_requestInFlight = false;
            processNextRequest();
            return;
        }
        connect(reply, &QModbusReply::finished, this,
                [this, reply, n = std::move(pending.name)]() {
                    if (reply->error() == QModbusDevice::NoError) {
                        emit writeSucceeded(n);
                    } else {
                        qWarning() << "[ModbusApi] write error:" << n << reply->errorString();
                        emit writeFailed(n, reply->errorString());
                    }
                    reply->deleteLater();
                    m_requestInFlight = false;
                    processNextRequest();
                });
    }
}

// ── sendWriteSingleRegister ────────────────────────────────────────────────────

void ModbusApi::sendWriteSingleRegister(int address, quint16 value, const QString &name)
{
    if (!isConnected()) {
        emit writeFailed(name, QStringLiteral("Not connected"));
        return;
    }
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, 1);
    unit.setValue(0, value);
    enqueueWrite(unit, name);
}

// ── sendWriteDoubleRegister ────────────────────────────────────────────────────
// Writes an i32/u32 as [high word, low word] (big-endian word order, FoxESS convention).

void ModbusApi::sendWriteDoubleRegister(int address, qint32 value, const QString &name)
{
    if (!isConnected()) {
        emit writeFailed(name, QStringLiteral("Not connected"));
        return;
    }
    QModbusDataUnit unit(QModbusDataUnit::HoldingRegisters, address, 2);
    unit.setValue(0, static_cast<quint16>((static_cast<quint32>(value) >> 16) & 0xFFFF));
    unit.setValue(1, static_cast<quint16>(static_cast<quint32>(value) & 0xFFFF));
    enqueueWrite(unit, name);
}

// ── Write operations ───────────────────────────────────────────────────────────

// ── Firmware off-by-one workaround (WORK_MODE write) ───────────────────────────
// This inverter's firmware persists (writtenValue - 1) into WORK_MODE (49203)
// whenever it is written — confirmed via wire-level logging: writing 7 (Force
// Discharge) settles to a stable readback of 6, and writing 6 (Force Charge)
// settles to a stable readback of 5 ("Unknown"). Reads of this register are
// accurate; only the write path is affected. Compensate by writing one more
// than the intended mode so the firmware's decrement lands on the value the
// user actually selected. Extracted as a pure static function so the mapping
// itself is unit-testable (see tests/test_workmode.cpp) without a live device.
int ModbusApi::compensateWorkModeWrite(int modeValue)
{
    return modeValue + 1;
}

void ModbusApi::writeWorkMode(int modeValue) {
    const int compensatedValue = compensateWorkModeWrite(modeValue);
    qDebug() << "[ModbusApi] writeWorkMode:" << modeValue
             << "(compensated write value =" << compensatedValue << ")";
    sendWriteSingleRegister(kAddrWorkMode, static_cast<quint16>(compensatedValue),
                            QStringLiteral("WORK_MODE"));
}
void ModbusApi::writeMinSoC(int percent) {
    sendWriteSingleRegister(kAddrMinSoC, static_cast<quint16>(percent),
                            QStringLiteral("MIN_SOC"));
}
void ModbusApi::writeMaxSoC(int percent) {
    sendWriteSingleRegister(kAddrMaxSoC, static_cast<quint16>(percent),
                            QStringLiteral("MAX_SOC"));
}
void ModbusApi::writeMinSoCOnGrid(int percent) {
    sendWriteSingleRegister(kAddrMinSoCOnGrid, static_cast<quint16>(percent),
                            QStringLiteral("MIN_SOC_ONGRID"));
}
void ModbusApi::writeBatteryMaxChargeCurrent(double amperes) {
    // Scale=10: raw = qRound(A * 10), stored as i16
    auto raw = static_cast<qint16>(qRound(amperes * 10.0));
    sendWriteSingleRegister(kAddrMaxChargeCurrent, static_cast<quint16>(raw),
                            QStringLiteral("BATTERY_MAX_CHARGE_CURRENT"));
}
void ModbusApi::writeBatteryMaxDischargeCurrent(double amperes) {
    auto raw = static_cast<qint16>(qRound(amperes * 10.0));
    sendWriteSingleRegister(kAddrMaxDischCurrent, static_cast<quint16>(raw),
                            QStringLiteral("BATTERY_MAX_DISCHARGE_CURRENT"));
}
void ModbusApi::writeImportPowerLimit(int watts) {
    sendWriteDoubleRegister(kAddrImportPowerLimit, static_cast<qint32>(watts),
                            QStringLiteral("IMPORT_POWER_LIMIT"));
}
void ModbusApi::writeExportPowerLimit(int watts) {
    sendWriteDoubleRegister(kAddrExportPowerLimit, static_cast<qint32>(watts),
                            QStringLiteral("EXPORT_POWER_LIMIT"));
}
void ModbusApi::writeExportPowerLimit2(int watts) {
    sendWriteDoubleRegister(kAddrExportPowerLimit2, static_cast<qint32>(watts),
                            QStringLiteral("EXPORT_POWER_LIMIT_2"));
}
void ModbusApi::writeThresholdSoC(int percent) {
    sendWriteSingleRegister(kAddrThresholdSoC, static_cast<quint16>(percent),
                            QStringLiteral("THRESHOLD_SOC"));
}
void ModbusApi::writeRemoteControl(quint16 bitfield) {
    sendWriteSingleRegister(kAddrRemoteControl, bitfield,
                            QStringLiteral("REMOTE_CONTROL"));
}
void ModbusApi::writeRemoteTimeout(int seconds) {
    sendWriteSingleRegister(kAddrRemoteTimeout, static_cast<quint16>(seconds),
                            QStringLiteral("REMOTE_TIMEOUT_SET"));
}
void ModbusApi::writeRemoteActivePower(int watts) {
    sendWriteDoubleRegister(kAddrRemoteActivePower, static_cast<qint32>(watts),
                            QStringLiteral("REMOTE_ACTIVE_POWER"));
}
void ModbusApi::writeRemoteReactivePower(int var) {
    sendWriteDoubleRegister(kAddrRemoteReactPower, static_cast<qint32>(var),
                            QStringLiteral("REMOTE_REACTIVE_POWER"));
}

// ── Static register parsers ────────────────────────────────────────────────────
// Thin delegations to RegisterParser (extracted to a standalone, network-free
// class so the parsing/sign-conversion logic is unit-testable — see
// tests/test_registerparser.cpp — without needing a live Modbus connection).

int16_t ModbusApi::parseI16(const QModbusDataUnit &unit, int offset)
{
    return RegisterParser::parseI16(unit, offset);
}

uint16_t ModbusApi::parseU16(const QModbusDataUnit &unit, int offset)
{
    return RegisterParser::parseU16(unit, offset);
}

int32_t ModbusApi::parseI32(const QModbusDataUnit &unit, int offset)
{
    return RegisterParser::parseI32(unit, offset);
}

uint32_t ModbusApi::parseU32(const QModbusDataUnit &unit, int offset)
{
    return RegisterParser::parseU32(unit, offset);
}

QString ModbusApi::parseString(const QModbusDataUnit &unit, int offset, int length)
{
    return RegisterParser::parseString(unit, offset, length);
}

// ── Poll block 1 parser (39063, count=57) ─────────────────────────────────────

void ModbusApi::parseAndEmitPollBlock1(const QModbusDataUnit &unit)
{
    // STATUS_1 (39063)
    InverterStatus status;
    status.raw       = parseU16(unit, kOff1Status);
    status.standby   = (status.raw & (1u << 0)) != 0;
    status.operation = (status.raw & (1u << 2)) != 0;
    status.fault     = (status.raw & (1u << 6)) != 0;
    emit statusUpdated(status);

    // GRID_STATUS / STATUS_3 (39065) — bit 0 = Off-Grid / EPS island mode.
    // Useful to surface in the UI so the operator can distinguish a normal
    // grid-tied configuration from an active grid outage where the inverter
    // is running in backup mode.
    GridStatus gridStatus;
    gridStatus.raw     = parseU16(unit, kOff1GridStatus);
    gridStatus.offGrid = (gridStatus.raw & (1u << 0)) != 0;
    emit gridStatusUpdated(gridStatus);

    // ALARM_1/2/3 (39067-39069)
    AlarmState alarms;
    alarms.alarm1 = parseU16(unit, kOff1Alarm1);
    alarms.alarm2 = parseU16(unit, kOff1Alarm2);
    alarms.alarm3 = parseU16(unit, kOff1Alarm3);
    emit alarmStateUpdated(alarms);

    // PV (39070-39073 + 39074-39077 + 39118-39119)
    PvData pv;
    pv.pv1Voltage     = parseI16(unit, kOff1Pv1Voltage) / 10.0;
    pv.pv1Current     = parseI16(unit, kOff1Pv1Current) / 100.0;
    pv.pv2Voltage     = parseI16(unit, kOff1Pv2Voltage) / 10.0;
    pv.pv2Current     = parseI16(unit, kOff1Pv2Current) / 100.0;
    pv.pv3Voltage     = parseI16(unit, kOff1Pv3Voltage) / 10.0;
    pv.pv3Current     = parseI16(unit, kOff1Pv3Current) / 100.0;
    pv.pv4Voltage     = parseI16(unit, kOff1Pv4Voltage) / 10.0;
    pv.pv4Current     = parseI16(unit, kOff1Pv4Current) / 100.0;
    pv.totalPvPowerKw = parseI32(unit, kOff1TotalPvPower) / 1000.0;
    emit pvDataUpdated(pv);
}

// ── Poll block 2 parser (39123, count=30) ─────────────────────────────────────

void ModbusApi::parseAndEmitPollBlock2(const QModbusDataUnit &unit)
{
    GridData grid;
    grid.voltageR           = parseI16(unit, kOff2GridRVoltage) / 10.0;
    grid.voltageS           = parseI16(unit, kOff2GridSVoltage) / 10.0;
    grid.voltageT           = parseI16(unit, kOff2GridTVoltage) / 10.0;
    grid.activePowerKw      = parseI32(unit, kOff2ActivePower)   / 1000.0;
    grid.reactivePowerKvar  = parseI32(unit, kOff2ReactivePower) / 1000.0;
    grid.powerFactor        = parseI16(unit, kOff2PowerFactor)   / 1000.0;
    grid.frequencyHz        = parseI16(unit, kOff2GridFrequency) / 100.0;
    emit gridDataUpdated(grid);

    const double tempC = parseI16(unit, kOff2InternalTemp) / 10.0;
    emit temperatureUpdated(tempC);

    EnergyData energy;
    energy.cumulativeKwh = parseU32(unit, kOff2CumulativeGen) / 100.0;
    energy.dailyKwh      = parseU32(unit, kOff2DailyGen)      / 100.0;
    emit energyDataUpdated(energy);
}

// ── Poll block 3 parser (39227, count=12) ─────────────────────────────────────

void ModbusApi::parseAndEmitPollBlock3(const QModbusDataUnit &unit)
{
    BatteryData battery;
    battery.voltageV       = parseI16(unit, kOff3Battery1Voltage) / 10.0;
    battery.currentA       = parseI32(unit, kOff3Battery1Current) / 1000.0;
    battery.powerW         = static_cast<double>(parseI32(unit, kOff3Battery1Power));
    battery.combinedPowerW = static_cast<double>(parseI32(unit, kOff3BatteryCombPower));
    battery.socPercent     = m_lastSocPercent;  // set by BMS1 SoC read in same poll cycle
    emit batteryDataUpdated(battery);
}

// ── Remote control parser (46001, count=1) ────────────────────────────────────
//
// Decodes the REMOTE_CONTROL bitfield read-back. The same bits are written by
// RemoteControlWidget::buildBitfield(); keeping them in sync ensures the UI's
// display matches what would be written on Apply.
void ModbusApi::parseAndEmitRemoteControl(const QModbusDataUnit &unit)
{
    RemoteControlState s;
    s.raw     = parseU16(unit, 0);
    s.enabled = (s.raw & (1u << 0)) != 0;
    s.consume = (s.raw & (1u << 1)) != 0;
    s.target  = static_cast<int>((s.raw >> 2) & 0x03);
    emit remoteControlStateUpdated(s);
}

// ── Info parser ────────────────────────────────────────────────────────────────

void ModbusApi::parseAndEmitInfo(const QModbusDataUnit &modelBlock,
                                  const QModbusDataUnit &versBlock,
                                  const QModbusDataUnit &devBlock)
{
    InverterInfo info;
    // modelBlock: 30000..30047 (48 regs = MODEL_NAME[16] + SERIAL[16] + MFG[16])
    info.modelName      = parseString(modelBlock,  0, 16);
    info.serialNumber   = parseString(modelBlock, 16, 16);
    info.mfgId          = parseString(modelBlock, 32, 16);
    // versBlock: 36001..36003
    info.masterVersion  = parseU16(versBlock, 0);
    info.slaveVersion   = parseU16(versBlock, 1);
    info.managerVersion = parseU16(versBlock, 2);
    // devBlock: 39053..39056
    info.ratedPowerKw     = parseI32(devBlock, 0) / 1000.0;
    info.maxActivePowerKw = parseI32(devBlock, 2) / 1000.0;
    emit infoUpdated(info);
}

// ── Settings parser ────────────────────────────────────────────────────────────

void ModbusApi::parseAndEmitSettings(const QModbusDataUnit &powLimBlock,
                                      const QModbusDataUnit &battBlock,
                                      const QModbusDataUnit &modeBlock)
{
    InverterSettings s;

    // powLimBlock: 46501..46505
    //   [0-1] IMPORT_POWER_LIMIT  i32
    //   [2]   THRESHOLD_SOC       u16
    //   [3-4] EXPORT_POWER_LIMIT  i32
    s.importPowerLimitW   = static_cast<int>(parseI32(powLimBlock, 0));
    s.thresholdSocPercent = static_cast<int>(parseU16(powLimBlock, 2));
    s.exportPowerLimitW   = static_cast<int>(parseI32(powLimBlock, 3));

    // battBlock: 46607..46617
    //   [0]   BATTERY_MAX_CHARGE_CURRENT    i16 scale=10
    //   [1]   BATTERY_MAX_DISCHARGE_CURRENT i16 scale=10
    //   [2]   MIN_SOC                       u16
    //   [3]   MAX_SOC                       u16
    //   [4]   MIN_SOC_ONGRID               u16
    //   [5-8] unused
    //   [9-10] EXPORT_POWER_LIMIT_2         i32
    s.maxChargeCurrentA    = parseI16(battBlock, 0) / 10.0;
    s.maxDischargeCurrentA = parseI16(battBlock, 1) / 10.0;
    s.minSocPercent        = static_cast<int>(parseU16(battBlock, 2));
    s.maxSocPercent        = static_cast<int>(parseU16(battBlock, 3));
    s.minSocOnGridPercent  = static_cast<int>(parseU16(battBlock, 4));
    s.exportPowerLimit2W   = static_cast<int>(parseI32(battBlock, 9));

    // modeBlock: 49203 (1 reg)
    s.workMode = workModeFromInt(static_cast<int>(parseU16(modeBlock, 0)));

    emit settingsRead(s);
}
