#pragma once

// ── Power formatting helpers ───────────────────────────────────────────────────
//
// formatPower(watts)          — active power (W → W or kW)
// formatReactivePower(var)    — reactive power (Var → Var or kVar)
//
// Both functions take the value in the base SI unit (W or Var) and return a
// display string with the appropriate prefix and unit suffix:
//   |value| < 1000  →  "NNN W"    / "NNN Var"    (0 decimal places)
//   |value| ≥ 1000  →  "N.NNN kW" / "N.NNN kVar" (3 decimal places → 1 W resolution)
//
// Negative values (import, discharge) are preserved as-is.
//
// Usage:
//   formatPower(data.pv1PowerW())                    →  "435 W"  or  "1.234 kW"
//   formatPower(data.activePowerKw * 1000.0)         →  "-850 W" or  "-2.100 kW"
//   formatReactivePower(data.reactivePowerKvar * 1000.0)  →  "300 Var"
//
// This header is intentionally header-only (no .cpp) so it can be included
// by any widget without adding a translation unit.

#include <QtCore/QString>
#include <cmath>

inline QString formatPower(double watts)
{
    if (std::abs(watts) >= 1000.0)
        return QStringLiteral("%1 kW").arg(watts / 1000.0, 0, 'f', 3);
    return QStringLiteral("%1 W").arg(watts, 0, 'f', 0);
}

inline QString formatReactivePower(double var)
{
    if (std::abs(var) >= 1000.0)
        return QStringLiteral("%1 kVar").arg(var / 1000.0, 0, 'f', 3);
    return QStringLiteral("%1 Var").arg(var, 0, 'f', 0);
}
