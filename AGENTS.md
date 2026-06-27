# AGENTS.md — solakon-one-ui Agent Guidance

## Project Overview

**solakon-one-ui** is a **Qt desktop application** (C++17, CMake-based) for monitoring and controlling the **FoxESS Solakon One** solar inverter over the local network via **Modbus TCP**. Single-window UI with a power-flow dashboard and dockable panels for PV input, grid, battery, energy statistics, and inverter settings.

Communicates directly with the inverter over the local network using the **FoxESS Modbus Protocol V1.05.02.00** (see `stuff/modbud-doc.pdf` and `stuff/Remote Control Definition V1.5.pdf` in the reference project). No cloud, no account, no internet required.

The TypeScript/Bun reference implementation lives at:
`https://github.com/solakon-de/solakon-one-homeassistant-internal`

Key characteristics:
- **Single-device protocol** — one Modbus TCP endpoint (IP + port 502 + slave ID 1), no device discovery
- **Qt SerialBus module** — uses `QModbusTcpClient` (Qt 6 `Qt::SerialBus`) for all Modbus communication
- **Async polling** — non-blocking QModbus async read/write requests; UI updated via signals
- **Binary register protocol** — register types: U16, I16, U32, I32, String, Bitfield16; scaling by factors of 10/100/1000
- **Read + Write registers** — dashboard is read-only; Settings panel writes work mode, SoC limits, current limits, power limits, remote control

## Build System

### Quick Build Commands

**Always use Docker (recommended — no local Qt or SerialBus packages required):**

```bash
# Build for Tumbleweed x86_64 (Release)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Build for Tumbleweed aarch64 (Release, cross-compiled)
./docker/build.sh --distro opensuse-tumbleweed-aarch64 --build-type Release

# Build for all supported distros
./docker/build.sh --distro all --build-type Release

# Debug build with qDebug output
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Ubuntu 24.04
./docker/build.sh --distro ubuntu-24.04-x86_64 --build-type Release
```

**Manual build (Qt6 + SerialBus installed locally):**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

**Required Qt components:**
- `Qt6::Widgets` — UI
- `Qt6::Network` — TCP connection management
- `Qt6::SerialBus` — `QModbusTcpClient`
- `Qt6::Charts` — live power/energy charts (optional, can be disabled)

### Build Output

```
out/
├── opensuse/
│   └── tumbleweed/
│       ├── x86_64/
│       │   ├── solakon-one-ui
│       │   └── solakon-one-ui-1.0.0-1.x86_64.rpm
│       └── aarch64/
│           ├── solakon-one-ui
│           └── solakon-one-ui-1.0.0-1.aarch64.rpm
└── ubuntu/
    └── 24.04/amd64/
        ├── solakon-one-ui
        └── solakon-one-ui_1.0.0-1_amd64.deb
```

## Architecture & Code Organization

### Planned Source Files

```
src/
├── main.cpp                      Entry point, CLI parsing, QApplication init
├── inverterdata.h                All data structs: InverterStatus, PvData, GridData, BatteryData,
│                                 EnergyData, InverterInfo, AlarmState, InverterSettings
├── imodbusapi.h                  Abstract IModbusApi interface (pure virtual + signals) — depended
│                                 on by UI widgets instead of the concrete ModbusApi
├── registerparser.{h,cpp}        Static, network-free register decoding (I16/U16/I32/U32/string) —
│                                 extracted from ModbusApi so it's unit-testable in isolation
├── modbusapi.{h,cpp}             Real, network-backed IModbusApi implementation — async polling,
│                                 register reads/writes, connection management, request queuing
├── mainwindow.{h,cpp}            Top-level QMainWindow; dock layout, panel switching,
│                                 poll timer, signal routing
├── connectwindow.{h,cpp}         Connection dialog: IP address, port (default 502),
│                                 slave ID (default 1), polling interval
├── dashboardwidget.{h,cpp}       Power-flow dashboard: live PV / grid / battery / load overview
├── pvwidget.{h,cpp}              PV input panel: PV1/PV2 voltage, current, total PV power
├── gridwidget.{h,cpp}            Grid panel: R/S/T voltage, active/reactive power,
│                                 power factor, frequency
├── batterywidget.{h,cpp}         Battery panel: voltage, current, combined power
├── energywidget.{h,cpp}          Energy statistics panel: daily generation, cumulative generation
├── infowidget.{h,cpp}            Device info panel: model name, serial, firmware versions
├── alarmwidget.{h,cpp}           Alarm panel: decoded ALARM_1/2/3 bitfields
├── settingswidget.{h,cpp}        Settings panel: work mode selector, SoC limits,
│                                 charge/discharge current limits, power limits
├── remotecontrolwidget.{h,cpp}   Remote control panel: enable/disable, target, timeout,
│                                 active power, reactive power
├── chartwidget.{h,cpp}           Live rolling charts (power, battery, temperature)
└── i18n_shim.h                   i18n() macro (KLocalizedString or QCoreApplication::translate)
```

### Key Architectural Decisions

**ModbusApi async design:**
- All reads/writes use `QModbusReply` signals — no blocking `waitForReadyRead()`
- Polling timer fires every N seconds; triggers a sequential batch of `readHoldingRegisters()` calls
- Command writes (e.g. set work mode) are sent immediately; next poll invalidates displayed values
- Connection state machine: `Disconnected → Connecting → Connected → Error → Disconnected`
- Reconnect on error: exponential backoff up to 60s; manual reconnect button always available

**Register parsing:**
- `inverterdata.h` defines typed structs for each data group (PvData, GridData, etc.)
- `ModbusApi` parses raw `QModbusDataUnit` register arrays into these structs
- Scaling is applied at parse time: `rawValue / scale → physicalValue`
- Signed 16-bit: mask > 0x7FFF → subtract 0x10000
- Signed 32-bit: mask > 0x7FFFFFFF → subtract 0x100000000
- String registers: two bytes per register, high byte first, null-terminated

**MainWindow dock layout:**
- Three main `QDockWidget` panels: Dashboard (center), Live Charts (right), Status bar
- Settings and Remote Control panels open as non-dockable dialogs (modal or dockable, TBD)
- Dock layout and visibility persisted in `QSettings` across restarts

**UI state persistence (`QSettings`):**
- Connection settings: `connection/host`, `connection/port`, `connection/slaveId`
- Poll interval: `connection/intervalSeconds`
- Window geometry: `window/geometry`, `window/state`
- Dock panel visibility and layout: `window/dockState`
- Last work mode displayed (to detect external changes): `inverter/lastWorkMode`

**Settings write safety:**
- All write operations show a confirmation (either inline status or optional dialog)
- Range validation before writing: `MIN_SOC ∈ [10,100]`, `MAX_SOC ∈ [10,100]`, `MIN_SOC < MAX_SOC`
- Battery current limits: `[0, 26]` for H3 model, `[0, 50]` for H3 Pro
- After write, next poll is triggered within 1s to verify the register was updated

## Modbus Protocol Details

### Connection Parameters

| Parameter    | Default | Notes                                  |
|--------------|---------|----------------------------------------|
| Host/IP      | —       | User-configured; no default            |
| Port         | 502     | Standard Modbus TCP port               |
| Slave ID     | 1       | Typical FoxESS value; configurable     |
| Timeout      | 5000 ms | Per-request timeout                    |
| Poll interval | 10 s   | Configurable 2–300 s in connect dialog |

### Read-Only Register Map

**Model Information (Table 3-1)**

| Name            | Address | Length | Type   | Scale | Unit | Description              |
|-----------------|---------|--------|--------|-------|------|--------------------------|
| MODEL_NAME      | 30000   | 16     | string | —     | —    | Inverter model name      |
| SERIAL_NUMBER   | 30016   | 16     | string | —     | —    | Serial number            |
| MFG_ID          | 30032   | 16     | string | —     | —    | Manufacturer ID          |

**Version Information (Table 3-2)**

| Name             | Address | Length | Type | Scale | Unit | Description          |
|------------------|---------|--------|------|-------|------|----------------------|
| MASTER_VERSION   | 36001   | 1      | u16  | —     | —    | Master firmware ver. |
| SLAVE_VERSION    | 36002   | 1      | u16  | —     | —    | Slave firmware ver.  |
| MANAGER_VERSION  | 36003   | 1      | u16  | —     | —    | Manager firmware ver.|

**Device Info (Table 3-5)**

| Name              | Address | Length | Type | Scale | Unit | Description      |
|-------------------|---------|--------|------|-------|------|------------------|
| PROTOCOL_VERSION  | 39000   | 2      | u32  | —     | —    | Modbus protocol  |
| RATED_POWER       | 39053   | 2      | i32  | 1000  | kW   | Rated power      |
| MAX_ACTIVE_POWER  | 39055   | 2      | i32  | 1000  | kW   | Max active power |

**Status / Alarms**

| Name     | Address | Length | Type       | Notes                              |
|----------|---------|--------|------------|------------------------------------|
| STATUS_1 | 39063   | 1      | bitfield16 | bit0=standby, bit2=operation, bit6=fault |
| ALARM_1  | 39067   | 1      | bitfield16 | See documentation Table 4-x        |
| ALARM_2  | 39068   | 1      | bitfield16 |                                    |
| ALARM_3  | 39069   | 1      | bitfield16 |                                    |

**PV Input**

| Name          | Address | Length | Type | Scale | Unit | Description         |
|---------------|---------|--------|------|-------|------|---------------------|
| PV1_VOLTAGE   | 39070   | 1      | i16  | 10    | V    | PV string 1 voltage |
| PV1_CURRENT   | 39071   | 1      | i16  | 100   | A    | PV string 1 current |
| PV2_VOLTAGE   | 39072   | 1      | i16  | 10    | V    | PV string 2 voltage |
| PV2_CURRENT   | 39073   | 1      | i16  | 100   | A    | PV string 2 current |
| TOTAL_PV_POWER| 39118   | 2      | i32  | 1000  | kW   | Total PV power      |

**Grid**

| Name           | Address | Length | Type | Scale | Unit  | Description        |
|----------------|---------|--------|------|-------|-------|--------------------|
| GRID_R_VOLTAGE | 39123   | 1      | i16  | 10    | V     | Phase R voltage    |
| GRID_S_VOLTAGE | 39124   | 1      | i16  | 10    | V     | Phase S voltage    |
| GRID_T_VOLTAGE | 39125   | 1      | i16  | 10    | V     | Phase T voltage    |
| ACTIVE_POWER   | 39134   | 2      | i32  | 1000  | kW    | Active power (+ = export) |
| REACTIVE_POWER | 39136   | 2      | i32  | 1000  | kVar  | Reactive power     |
| POWER_FACTOR   | 39138   | 1      | i16  | 1000  | —     | Power factor       |
| GRID_FREQUENCY | 39139   | 1      | i16  | 100   | Hz    | Grid frequency     |

**Temperature**

| Name          | Address | Length | Type | Scale | Unit | Description          |
|---------------|---------|--------|------|-------|------|----------------------|
| INTERNAL_TEMP | 39141   | 1      | i16  | 10    | °C   | Inverter temperature |

**Energy Statistics**

| Name                  | Address | Length | Type | Scale | Unit | Description              |
|-----------------------|---------|--------|------|-------|------|--------------------------|
| CUMULATIVE_GENERATION | 39149   | 2      | u32  | 100   | kWh  | Total lifetime generation|
| DAILY_GENERATION      | 39151   | 2      | u32  | 100   | kWh  | Today's generation       |

**Battery**

| Name                    | Address | Length | Type | Scale | Unit | Description              |
|-------------------------|---------|--------|------|-------|------|--------------------------|
| BATTERY1_VOLTAGE        | 39227   | 1      | i16  | 10    | V    | Battery voltage          |
| BATTERY1_CURRENT        | 39228   | 2      | i32  | 1000  | A    | Battery current          |
| BATTERY1_POWER          | 39230   | 2      | i32  | 1     | W    | Battery power            |
| BATTERY_COMBINED_POWER  | 39237   | 2      | i32  | 1     | W    | Combined battery power   |

### Writable Register Map

**Remote Control (Table 3-8)**

| Name                  | Address | Length | Type       | Scale | Unit | Range    | Description                         |
|-----------------------|---------|--------|------------|-------|------|----------|-------------------------------------|
| REMOTE_CONTROL        | 46001   | 1      | bitfield16 | —     | —    | —        | bit0=enable, bit1=direction(0=gen/1=cons), bits3-2=target(00=AC/01=Battery/10=Grid) |
| REMOTE_TIMEOUT_SET    | 46002   | 1      | u16        | —     | s    | —        | Timeout in seconds                  |
| REMOTE_ACTIVE_POWER   | 46003   | 2      | i32        | —     | W    | —        | Active power command                |
| REMOTE_REACTIVE_POWER | 46005   | 2      | i32        | —     | Var  | —        | Reactive power command              |

**Power Limits (Table 3-9)**

| Name                | Address | Length | Type | Scale | Unit | Range    | Description          |
|---------------------|---------|--------|------|-------|------|----------|----------------------|
| IMPORT_POWER_LIMIT  | 46501   | 2      | i32  | —     | W    | —        | Import power limit   |
| THRESHOLD_SOC       | 46503   | 1      | u16  | —     | %    | [0,100]  | Threshold SoC        |
| EXPORT_POWER_LIMIT  | 46504   | 2      | i32  | —     | W    | —        | Export power limit   |
| EXPORT_POWER_LIMIT_2| 46616   | 2      | i32  | —     | W    | —        | Export power limit 2 |

**Battery Settings (Table 3-10)**

| Name                          | Address | Length | Type | Scale | Unit | Range         | Description                    |
|-------------------------------|---------|--------|------|-------|------|---------------|--------------------------------|
| BATTERY_MAX_CHARGE_CURRENT    | 46607   | 1      | i16  | 10    | A    | H3:[0,26] H3Pro:[0,50] | Max charge current    |
| BATTERY_MAX_DISCHARGE_CURRENT | 46608   | 1      | i16  | 10    | A    | H3:[0,26] H3Pro:[0,50] | Max discharge current |
| MIN_SOC                       | 46609   | 1      | u16  | —     | %    | [10,100]      | Minimum SoC                    |
| MAX_SOC                       | 46610   | 1      | u16  | —     | %    | [10,100]      | Maximum SoC                    |
| MIN_SOC_ONGRID                | 46611   | 1      | u16  | —     | %    | [10,100]      | Minimum SoC on-grid            |

**System Settings (Table 3-11)**

| Name               | Address | Length | Type | Scale | Unit | Range  | Description                                                       |
|--------------------|---------|--------|------|-------|------|--------|-------------------------------------------------------------------|
| WORK_MODE          | 49203   | 1      | u16  | —     | —    | —      | 1=Self Use, 2=Feedin Priority, 3=BackUp, 4=Peak Shaving, 6=Force Charge, 7=Force Discharge |
| POWER_ON           | 49077   | 1      | u16  | —     | —    | [0,1]  | 0=invalid, 1=power on                                             |
| POWER_OFF          | 49078   | 1      | u16  | —     | —    | [0,1]  | 0=invalid, 1=power off / shutdown                                 |
| GRID_STANDARD_CODE | 49079   | 1      | u16  | —     | —    | —      | Grid standard code (Table 4-2 in Modbus documentation)            |

### Register Type Parsing

```cpp
// U16: raw register value, unsigned
uint16_t rawU16 = data.value(offset);

// I16: two's complement signed
int16_t rawI16 = static_cast<int16_t>(data.value(offset));

// U32: high word first, low word second
uint32_t rawU32 = (static_cast<uint32_t>(data.value(offset)) << 16)
                | static_cast<uint32_t>(data.value(offset + 1));

// I32: high word first, low word second, signed
int32_t rawI32 = static_cast<int32_t>(
    (static_cast<uint32_t>(data.value(offset)) << 16)
  | static_cast<uint32_t>(data.value(offset + 1)));

// String: two chars per register, high byte first
QString parseString(const QModbusDataUnit &unit, int offset, int length) {
    QString result;
    for (int i = 0; i < length; ++i) {
        uint16_t reg = unit.value(offset + i);
        char hi = static_cast<char>((reg >> 8) & 0xFF);
        char lo = static_cast<char>(reg & 0xFF);
        if (hi) result += hi;
        if (lo) result += lo;
    }
    return result.trimmed();
}

// Scaling: divide raw integer by scale factor
double physicalValue = rawI32 / static_cast<double>(scale); // e.g. scale=1000 → kW
```

### Write Protocol

- **Single register (U16/I16/Bitfield16, length=1):** use `QModbusDataUnit::HoldingRegisters` with 1 value; `QModbusTcpClient::sendWriteRequest()`
- **Double register (U32/I32, length=2):** split into `[highWord, lowWord]` array, write 2 registers
- **Scale on write:** multiply physical value by scale before writing: `rawValue = qRound(physicalValue * scale)`
- **Range validation** must happen before scaling in the UI layer, not inside `ModbusApi`

## Code Quality Standards

### C++ Standard and Style

- **C++17 standard** — required; use modern features (auto, structured bindings, lambdas, `if constexpr`)
- **Naming conventions:**
  - **Member variables:** `m_camelCase` prefix (e.g., `m_modbusClient`, `m_pollTimer`, `m_inverterHost`)
  - **Constants/enums:** `kCamelCase` prefix (e.g., `kDefaultPort`, `kPollIntervalMs`, `kMinSoC`)
  - **Local variables:** `camelCase` (no prefix)
  - **Slots:** `onCamelCase` (e.g., `onPollTimerFired`, `onModbusReplyFinished`)
  - **Helpers:** `computeCamelCase` or `parseCamelCase`
- **Header organization:** Qt includes first, then system, then local headers
- **Scope markers:**
  - `// ── Description ────────────────────────────────────────`
  - `// Constructor`, `// Slots`, `// Private helpers` section comments
- **Qt-specific:**
  - Use `Q_OBJECT` for all QObject subclasses
  - Prefer `QString` over `std::string` for all user-visible text
  - Use `QPointer<T>` for pointers to QObjects to avoid dangling pointer access
  - Connect signals in constructors or `wireSignals()` methods, never in poll/loop bodies
  - Avoid raw `new`/`delete`; rely on parent-child QObject ownership

### Object-Oriented Design

- **Avoid god functions** with nested if/else chains — extract into separate methods or classes
- **Register group structs** (`PvData`, `GridData`, `BatteryData`, etc.) are plain value types (no QObject); they travel by value or `const &`
- **ModbusApi** owns all network state; UI widgets must not access `QModbusTcpClient` directly
- **Polymorphism for panels**: all data panels inherit a common abstract base that receives a data struct via a virtual `update()` method
- **Factory for panels**: `MainWindow::createPanel(PanelType)` instantiates the correct widget, hiding all `if` chains

**Anti-pattern:**
```cpp
// BAD: god function in MainWindow
void MainWindow::onDataReceived() {
    if (m_currentPanel == PANEL_PV) {
        pvVoltageLabel->setText(...);
        pvCurrentLabel->setText(...);
    } else if (m_currentPanel == PANEL_BATTERY) {
        battVoltLabel->setText(...);
        // ... 20 lines
    }
    // ... etc
}
```

**Correct pattern:**
```cpp
// GOOD: each panel updates itself
void PvWidget::updateData(const PvData &data) {
    m_pv1VoltageLabel->setText(QStringLiteral("%1 V").arg(data.pv1Voltage, 0, 'f', 1));
    m_pv1CurrentLabel->setText(QStringLiteral("%1 A").arg(data.pv1Current, 0, 'f', 2));
    // ... only PV logic here
}
```

### Async & Signal Safety

- **No blocking I/O** — never call `waitForReadyRead()` or similar; all Modbus replies are handled via `QModbusReply::finished()` signal
- **Reply lifecycle:** always connect `reply->finished()` before checking `reply->isFinished()`; delete reply in the slot via `reply->deleteLater()`
- **Reconnect handling:** on `QModbusDevice::errorOccurred()`, emit `connectionLost()` signal; MainWindow resets the UI and shows the connect dialog or auto-reconnects
- **Queue writes during poll:** if a write command arrives while a poll read is in-flight, queue it and send after the read completes

### Memory Management

- All `QWidget` subclasses must have a parent to avoid leaks
- Exception: top-level windows (`MainWindow`, `ConnectWindow`) have no parent
- `QModbusReply *` objects are always deleted via `deleteLater()` in the `finished()` slot
- `QTimer` instances must be stopped before their parent is deleted

### Documentation

- Public class headers: brief Doxygen comment explaining purpose, single responsibility, and async contract
- Complex parsing methods: inline comment explaining register layout and scaling
- Write-safety critical code (e.g., range validation before register writes) must be annotated with rationale
- Update `AGENTS.md` when new architectural patterns are introduced
- Update `README.md` when user-facing features, connection parameters, or CLI flags change

## Testing & Verification

### Build Verification

```bash
# Standard Release build — must complete without warnings
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Debug build for runtime testing
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Run the binary
./out/opensuse/tumbleweed/x86_64/solakon-one-ui --host 192.168.1.148 --port 502 --slave-id 1
```

### Manual Test Checklist (against real inverter)

1. Connection dialog appears on startup; fill in IP, port, slave ID
2. Connection succeeds; dashboard shows live data
3. PV voltage/current match physical display on inverter
4. Active power sign matches inverter: positive = exporting to grid (verify sign convention)
5. Battery power: positive = charging, negative = discharging (verify against inverter)
6. Work mode change: select "Self Use", verify register 49203 reads back 1 after next poll
7. SoC limits: set Min=15%, Max=85%, verify both registers 46609/46610 update
8. Disconnect inverter from LAN: error state appears in UI within one poll interval
9. Reconnect inverter: UI recovers automatically
10. Quit and relaunch: connection settings are pre-filled; dock layout is restored

### Running Unit Tests

Unit tests cover register parsing, `WorkMode` mapping, and the `IModbusApi`/`MockModbusApi` contract (see `tests/`). They require no live Modbus connection and no display server (`QTEST_GUILESS_MAIN`). Qt6::Test is optional at configure time — if not found, the `tests/` target is skipped with a `message(STATUS ...)` rather than failing the build (openSUSE needs the extra `qt6-test-devel` package; Ubuntu's `qt6-base-dev` already bundles it).

```bash
# Inside the Docker build container (or locally with Qt6::Test installed):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
ctest --test-dir build --output-on-failure
```

`docker/build.sh` builds the `tests/` target as part of the normal `ninja` invocation but does not currently run `ctest` automatically — run it manually as shown above (e.g., via `docker run` against the builder image) after making changes to `ModbusApi`, `RegisterParser`, or the `WorkMode` mapping/compensation logic.

### Testability Guidelines

- **ModbusApi** is mockable: `IModbusApi` (`src/imodbusapi.h`) declares the interface (pure virtual methods + all signals); `ModbusApi` is the real, network-backed implementation, and `tests/mockmodbusapi.h` provides `MockModbusApi`, a test double that records write calls and lets tests emit canned signals (`emitSettingsRead()`, `emitWorkModeUpdated()`, etc.) with fake register data — no live connection required. `SettingsWidget`, `RemoteControlWidget`, and `MainWindow` all depend on `IModbusApi *`, not the concrete `ModbusApi`, so a `MockModbusApi` can be substituted in widget-level tests.
- **Data structs** (`PvData`, `GridData`, etc.) are plain value types — no QObject dependency — making them trivially testable
- **Register parsing helpers** (scale, sign conversion, string parsing) are `static` pure functions in `RegisterParser` (`src/registerparser.h/.cpp`) — unit-tested in `tests/test_registerparser.cpp` without any network dependency. `ModbusApi`'s own `parseI16/parseU16/parseI32/parseU32/parseString` are thin delegations kept for internal call-site compatibility.
- **Write range validation** logic must live in a standalone `RegisterValidator` or in the widget layer, not buried inside `ModbusApi`
- **Firmware workaround logic** (e.g., the WORK_MODE off-by-one compensation, pitfall #13) is extracted into small pure static functions (`ModbusApi::compensateWorkModeWrite()`) specifically so hardware-confirmed quirks can be regression-tested (`tests/test_workmode.cpp`) without needing the real inverter on hand for every change.


## Common Agent Pitfalls

1. **Register address vs. start address** — Qt's `QModbusDataUnit` uses the Modbus PDU address (0-based within function code), but FoxESS documentation uses the absolute Modbus address (e.g., 39070). Verify whether your `QModbusDataUnit` start address matches the documentation convention; some libraries offset by 1 or subtract the function-code base address.

2. **Sign convention for power** — `ACTIVE_POWER` (39134) sign convention: positive may mean import or export depending on firmware version. Validate against inverter display before labelling in UI. Same for `BATTERY_COMBINED_POWER` — confirm positive = charging or discharging.

3. **I32 high/low word order** — FoxESS uses **high word first** for 32-bit registers (big-endian word order). `Qt::BigEndian` byte order or manual `(high << 16) | low` — do not assume word order matches system endianness.

4. **Scaling on write** — when writing `BATTERY_MAX_CHARGE_CURRENT` (scale=10), the raw value written is `amps * 10`. Writing 20A → write value 200. Forgetting to scale is a silent error that sets wrong register values.

5. **REMOTE_CONTROL bitfield** — bits 3-2 encode a 2-bit target field (00=AC, 01=Battery, 10=Grid). When setting individual bits, preserve the other bits using read-modify-write (or track the current bitfield value in `InverterSettings`). Blindly writing only the changed bit will corrupt the rest.

6. **POWER_ON / POWER_OFF write-once registers** — writing 1 to `POWER_ON` (49077) powers on the inverter; it does not stay 1 after the command is processed. Do not poll these back and interpret the returned 0 as "off". Display-only: after write, show a success/failure status; do not try to read back.

7. **QModbusTcpClient reconnect** — `QModbusDevice::ConnectedState` does not guarantee the remote is responsive. A stale TCP connection (inverter powered off without FIN) may appear connected. Treat timeout errors on `QModbusReply` as a signal to force-reconnect (close and re-open the client).

8. **Poll interval vs. write timing** — if a write request and a poll timer fire simultaneously, `QModbusTcpClient` serializes them internally. However, in debug builds, add assertions to verify no concurrent Modbus requests are in-flight when issuing a write. Log all request/reply sequences in Debug mode.

9. **Battery current register BATTERY1_CURRENT (39228, i32, scale=1000)** — the scale is 1000 (not 10 or 100), so raw values ÷ 1000 → Amperes. This is unusual; double-check against the Modbus documentation before displaying.

10. **WORK_MODE values are not contiguous** — valid values are 1, 2, 3, 4, 6, 7 (no 5). Treat mode 5 as invalid. When reading back the work mode, map unknown values to "Unknown" rather than crashing or asserting.

11. **String register null termination** — model name and serial number registers may be padded with null bytes (0x0000). Strip nulls and trailing whitespace when displaying. The `parseString()` helper must handle both partial and fully-padded cases.

12. **Qt SerialBus availability** — `Qt::SerialBus` is an optional Qt module. On some minimal Qt installations it may be absent. CMakeLists.txt must check `find_package(Qt6 COMPONENTS SerialBus)` and `REQUIRED`; provide a clear error message if missing.

13. **WORK_MODE write off-by-one (firmware bug, confirmed on hardware)** — writing register 49203 (WORK_MODE) persists `writtenValue - 1` instead of the value sent, confirmed via wire-level Modbus logging (writing 7/Force Discharge settles to a stable readback of 6; writing 6/Force Charge settles to a stable readback of 5/"Unknown"). This only affects the *write* path — reads of this register otherwise reflect the true device state accurately, and no other register showed this behavior. `ModbusApi::writeWorkMode()` compensates by writing `modeValue + 1`; do not "simplify" this back to a bare write without re-verifying against real hardware first. If a firmware update ever fixes this on the device side, this workaround will need to be removed (watch for WORK_MODE settling to `requested + 1` after the fix ships).

## Useful Commands

```bash
# Build Release (Tumbleweed)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Build Release (Tumbleweed, aarch64, cross-compiled)
./docker/build.sh --distro opensuse-tumbleweed-aarch64 --build-type Release

# Build Debug (Tumbleweed, enables qDebug output)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug

# Build Ubuntu 24.04
./docker/build.sh --distro ubuntu-24.04-x86_64 --build-type Release

# Run with explicit connection parameters
./out/opensuse/tumbleweed/x86_64/solakon-one-ui \
    --host 192.168.1.148 --port 502 --slave-id 1 --interval 10

# Inspect RPM package
rpm -qpl out/opensuse/tumbleweed/x86_64/solakon-one-ui-*.rpm

# Inspect DEB package
dpkg -c out/ubuntu/24.04/amd64/solakon-one-ui_*.deb
```

## CLI Flags

```
solakon-one-ui [options]

Options:
  -H, --host <host>        Inverter IP address or hostname
  -p, --port <port>        Modbus TCP port (default: 502)
  -s, --slave-id <id>      Modbus slave/unit ID (default: 1)
  -i, --interval <seconds> Poll interval in seconds (default: 10, range 2–300)
```

Without `--host`, the connection dialog is shown on startup. Connection settings are persisted in `QSettings` and pre-filled on subsequent launches.

## Deployment & Release

### Version Format

`MAJOR.MINOR.PATCH` — update in:
1. `CMakeLists.txt` → `project(solakon-one-ui VERSION x.y.z)`
2. Packaging metadata (`.spec`, `debian/changelog`)
3. Git tag: `git tag -a vx.y.z -m "Release x.y.z"`

### Release Checklist

- [ ] All Docker distro builds pass: `./docker/build.sh --distro all --build-type Release`
- [ ] Binaries exist in `out/` for each distro
- [ ] Manual test against real inverter: dashboard loads, work mode write succeeds
- [ ] No new compiler warnings in build output
- [ ] `file out/opensuse/tumbleweed/x86_64/solakon-one-ui` shows ELF 64-bit executable
- [ ] Git tag created and pushed

## Reference Material

- **Modbus documentation:** `stuff/modbud-doc.pdf` in the reference repo
  (`https://github.com/solakon-de/solakon-one-homeassistant-internal`)
- **Remote Control definition:** `stuff/Remote Control Definition V1.5.pdf` in the reference repo
- **Reference TypeScript implementation:** `bun/index.ts` in the reference repo — complete register map and working parse/write logic
- **Template Qt project:** `https://github.com/flunaras/fritzhome` — follow its architecture, build system, AGENTS.md style, and Docker build structure closely
