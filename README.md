# solakon-one-ui

A Qt6 desktop application for monitoring and controlling the **FoxESS Solakon One** solar inverter over the local network via **Modbus TCP**.

No cloud account, no internet connection, and no third-party services required — communicates directly with the inverter on your LAN.

---

## Features

- **Power-flow dashboard** — live overview of PV generation, grid import/export, battery charge/discharge, and load consumption
- **PV panel** — per-string voltage and current (PV1 / PV2) with total PV power
- **Grid panel** — three-phase voltage (R/S/T), active/reactive power, power factor, and grid frequency
- **Battery panel** — voltage, current, and combined battery power
- **Energy statistics** — daily generation and cumulative lifetime generation
- **Device info** — model name, serial number, firmware versions
- **Alarms** — decoded ALARM_1/2/3 bitfields with human-readable descriptions
- **Settings** — work mode selector, SoC limits, charge/discharge current limits, power limits
- **Remote control** — enable/disable remote control, set target, timeout, active power, and reactive power
- **Live charts** — rolling time-series charts for power and battery metrics
- **Dockable panels** — layout and panel visibility persisted across restarts via `QSettings`
- **CLI connection flags** — pass `--host` and other parameters to skip the connection dialog

---

## Requirements

### Runtime

| Dependency | Version |
|---|---|
| Qt6 Widgets | 6.x |
| Qt6 SerialBus | 6.x |
| Qt6 Charts | 6.x |

### Build (Docker — recommended)

- Docker (any recent version)
- `bash`

### Build (manual)

- Qt 6 development packages including `Qt6SerialBus` and `Qt6Charts`
- CMake ≥ 3.20
- Ninja (or another CMake generator)
- A C++17-capable compiler (GCC ≥ 10 or Clang ≥ 10)

---

## Building

### Docker (recommended)

Docker builds produce both a standalone binary and a native package (RPM or DEB) without requiring any Qt packages to be installed locally.

```bash
# openSUSE Tumbleweed x86_64 — Release (produces RPM)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Release

# Ubuntu 24.04 x86_64 — Release (produces DEB)
./docker/build.sh --distro ubuntu-24.04-x86_64 --build-type Release

# All supported distros at once
./docker/build.sh --distro all --build-type Release

# Debug build (enables qDebug output)
./docker/build.sh --distro opensuse-tumbleweed-x86_64 --build-type Debug
```

Build output is placed under `out/`:

```
out/
├── opensuse/tumbleweed/x86_64/
│   ├── solakon-one-ui
│   └── solakon-one-ui-1.0.0-1.x86_64.rpm
└── ubuntu/24.04/amd64/
    ├── solakon-one-ui
    └── solakon-one-ui_1.0.0-1_amd64.deb
```

### Manual

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

---

## Installation

**openSUSE:**

```bash
sudo rpm -i out/opensuse/tumbleweed/x86_64/solakon-one-ui-*.rpm
```

**Ubuntu / Debian:**

```bash
sudo dpkg -i out/ubuntu/24.04/amd64/solakon-one-ui_*.deb
```

---

## Usage

```
solakon-one-ui [options]

Options:
  -H, --host <host>        Inverter IP address or hostname
  -p, --port <port>        Modbus TCP port (default: 502)
  -s, --slave-id <id>      Modbus slave/unit ID (default: 1)
  -i, --interval <seconds> Poll interval in seconds (default: 10, range: 2–300)
```

Without `--host`, the connection dialog is shown on startup. All connection settings are saved in `QSettings` and pre-filled on subsequent launches.

**Examples:**

```bash
# Open connection dialog on startup
solakon-one-ui

# Connect directly, skipping the dialog
solakon-one-ui --host 192.168.1.148 --port 502 --slave-id 1 --interval 10
```

---

## Connection Parameters

| Parameter | Default | Description |
|---|---|---|
| Host / IP | — | Inverter IP address or hostname on your LAN |
| Port | 502 | Standard Modbus TCP port |
| Slave ID | 1 | Modbus unit/slave ID (typical FoxESS value) |
| Poll interval | 10 s | How often registers are read; range 2–300 s |

Settings are saved automatically and restored on next launch.

---

## Supported Inverters

Tested with the **FoxESS Solakon One** (H3 / H3 Pro). The application uses the **FoxESS Modbus Protocol V1.05.02.00** and reads the following register groups:

- Model information and firmware versions
- PV input (strings 1 and 2)
- Grid (three-phase measurements)
- Battery (voltage, current, power)
- Energy statistics (daily and cumulative)
- Status and alarm bitfields
- Writable settings: work mode, SoC limits, current limits, power limits, remote control

---

## License

GPL-3.0-or-later — see [LICENSE](LICENSE) for details.
