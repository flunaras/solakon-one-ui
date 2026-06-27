#pragma once

#include <QtCore/QString>

// ── Work mode values (register 49203) ─────────────────────────────────────────
enum class WorkMode : int {
    Unknown        = 0,
    SelfUse        = 1,
    FeedinPriority = 2,
    BackUp         = 3,
    PeakShaving    = 4,
    ForceCharge    = 6,
    ForceDischarge = 7,
};

inline QString workModeLabel(WorkMode m)
{
    switch (m) {
    case WorkMode::SelfUse:        return QStringLiteral("Self Use");
    case WorkMode::FeedinPriority: return QStringLiteral("Feed-in Priority");
    case WorkMode::BackUp:         return QStringLiteral("Backup");
    case WorkMode::PeakShaving:    return QStringLiteral("Peak Shaving");
    case WorkMode::ForceCharge:    return QStringLiteral("Force Charge");
    case WorkMode::ForceDischarge: return QStringLiteral("Force Discharge");
    default:                       return QStringLiteral("Unknown (%1)").arg(static_cast<int>(m));
    }
}

inline WorkMode workModeFromInt(int v)
{
    switch (v) {
    case 1: return WorkMode::SelfUse;
    case 2: return WorkMode::FeedinPriority;
    case 3: return WorkMode::BackUp;
    case 4: return WorkMode::PeakShaving;
    case 6: return WorkMode::ForceCharge;
    case 7: return WorkMode::ForceDischarge;
    default: return WorkMode::Unknown;
    }
}

// ── Static device information ─────────────────────────────────────────────────
struct InverterInfo {
    QString modelName;
    QString serialNumber;
    QString mfgId;
    quint16 masterVersion   = 0;
    quint16 slaveVersion    = 0;
    quint16 managerVersion  = 0;
    quint32 protocolVersion = 0;
    double  ratedPowerKw    = 0.0;   // scale 1000
    double  maxActivePowerKw = 0.0;  // scale 1000
};

// ── Inverter operating status (register 39063 bitfield16) ─────────────────────
struct InverterStatus {
    bool standby   = false;  // bit 0
    bool operation = false;  // bit 2
    bool fault     = false;  // bit 6
    quint16 raw    = 0;
};

// ── Alarm state (registers 39067-39069 bitfield16 each) ───────────────────────
struct AlarmState {
    quint16 alarm1 = 0;
    quint16 alarm2 = 0;
    quint16 alarm3 = 0;

    bool hasAlarm() const { return alarm1 || alarm2 || alarm3; }
};

// ── PV input data ─────────────────────────────────────────────────────────────
struct PvData {
    double pv1Voltage   = 0.0;  // V, scale 10  (39070)
    double pv1Current   = 0.0;  // A, scale 100 (39071)
    double pv2Voltage   = 0.0;  // V, scale 10  (39072)
    double pv2Current   = 0.0;  // A, scale 100 (39073)
    // PV3/PV4 follow the same sequential pattern (39074-39077);
    // they read 0 when the string is not connected.
    double pv3Voltage   = 0.0;  // V, scale 10  (39074, inferred)
    double pv3Current   = 0.0;  // A, scale 100 (39075, inferred)
    double pv4Voltage   = 0.0;  // V, scale 10  (39076, inferred)
    double pv4Current   = 0.0;  // A, scale 100 (39077, inferred)
    double totalPvPowerKw = 0.0; // kW, scale 1000 (39118)

    double pv1PowerW() const { return pv1Voltage * pv1Current; }
    double pv2PowerW() const { return pv2Voltage * pv2Current; }
    double pv3PowerW() const { return pv3Voltage * pv3Current; }
    double pv4PowerW() const { return pv4Voltage * pv4Current; }
};

// ── Grid connection data ──────────────────────────────────────────────────────
struct GridData {
    double voltageR    = 0.0;  // V, scale 10
    double voltageS    = 0.0;  // V, scale 10
    double voltageT    = 0.0;  // V, scale 10
    // Positive = export to grid, negative = import from grid (verify sign convention)
    double activePowerKw   = 0.0;  // kW, scale 1000
    double reactivePowerKvar = 0.0; // kVar, scale 1000
    double powerFactor     = 0.0;  // dimensionless, scale 1000
    double frequencyHz     = 0.0;  // Hz, scale 100
};

// ── Battery data ──────────────────────────────────────────────────────────────
struct BatteryData {
    double voltageV          = 0.0;  // V, scale 10
    double currentA          = 0.0;  // A, scale 1000  (i32)
    double powerW            = 0.0;  // W, scale 1      (i32)
    double combinedPowerW    = 0.0;  // W, scale 1      (i32)
    // BMS1 State of Charge — register 37612 (u16, %, scale 1).
    // -1 means the value has not been received yet (first poll pending).
    int    socPercent        = -1;

    // Derived: positive = charging, negative = discharging (verify sign convention)
    bool isCharging() const   { return combinedPowerW > 0.0; }
    bool isDischarging() const { return combinedPowerW < 0.0; }
};

// ── Energy statistics ─────────────────────────────────────────────────────────
struct EnergyData {
    double cumulativeKwh = 0.0;  // kWh, scale 100 (u32)
    double dailyKwh      = 0.0;  // kWh, scale 100 (u32)
};

// ── Writable settings (read back to display current values) ──────────────────
struct InverterSettings {
    WorkMode workMode                  = WorkMode::Unknown;
    int      minSocPercent             = 0;    // register 46609
    int      maxSocPercent             = 100;  // register 46610
    int      minSocOnGridPercent       = 0;    // register 46611
    double   maxChargeCurrentA         = 0.0;  // register 46607, scale 10
    double   maxDischargeCurrentA      = 0.0;  // register 46608, scale 10
    int      importPowerLimitW         = 0;    // register 46501
    int      exportPowerLimitW         = 0;    // register 46504
    int      exportPowerLimit2W        = 0;    // register 46616
    int      thresholdSocPercent       = 0;    // register 46503
};
