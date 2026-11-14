/**
 * @file status_state.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-04
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hal {
enum class ConnectionState { Off, Idle, Connecting, Connected, Fault, Unknown };
struct WirelessStatus {
    ConnectionState wifi{ConnectionState::Off}, bluetooth{ConnectionState::Off};
};
enum class BatteryPresence { Unknown, Absent, Present };
struct PowerStatus {
    bool valid{}, charging{};
    BatteryPresence battery{BatteryPresence::Unknown};
    std::uint8_t level{}; // Coarse voltage bars, 0..4; not a fuel-gauge percentage.
    float voltage{}, current{}; // INA226 shunt convention: negative = charging.
};
// The TAB5 measures pack voltage. This is a 2S voltage-based estimate.
inline unsigned battery_percent(float voltage)
{
    return static_cast<unsigned>(std::round(std::clamp((voltage - 6.F) / (8.23F - 6.F), 0.F, 1.F) * 100.F));
}

struct AutoChargePolicy {
    unsigned idle_samples{}, settle_samples{};
    bool enable(const PowerStatus& power, bool enabled, float current)
    {
        if (!power.valid || power.battery != BatteryPresence::Present) {
            idle_samples = 0;
            settle_samples = enabled ? 3 : 0;
            return false;
        }
        if (!enabled) {
            idle_samples = 0;
            if (settle_samples) {
                --settle_samples;
                return false;
            }
            return true;
        }
        if (std::abs(current) > .03F) idle_samples = 0;
        if (++idle_samples < 30) return true;
        // With no shunt current, recheck with CHG_EN off so the charger
        // cannot make a disconnected BAT terminal look like a full pack.
        settle_samples = 3;
        return false;
    }
};

struct PowerStatusFilter {
    PowerStatus state, candidate;
    unsigned consistent{};
    void update(bool valid, float voltage, float current, bool charge_enabled)
    {
        if (!valid || !std::isfinite(voltage) || !std::isfinite(current) || voltage < 0 || voltage > 9) {
            state = {}; candidate = {}; consistent = 0;
            return;
        }
        PowerStatus next;
        next.valid = true;
        next.voltage = voltage; next.current = current;
        if (voltage < 4.5F) next.battery = BatteryPresence::Absent;
        else if (voltage >= 5.5F && (!charge_enabled || std::abs(current) > .03F ||
                                      state.battery == BatteryPresence::Present))
            next.battery = BatteryPresence::Present;
        // An enabled charger can raise an empty BAT connector to 8.4 V.
        // Retain a confirmed pack until the periodic charger-off check.
        next.charging = next.battery == BatteryPresence::Present && charge_enabled && current < -.03F;
        next.level = static_cast<std::uint8_t>(std::clamp((voltage - 6.F) / 2.4F * 4.F, 0.F, 4.F));
        if (next.battery != candidate.battery || next.charging != candidate.charging || next.level != candidate.level)
            consistent = 0;
        candidate = next;
        if (++consistent >= 3) { state = next; consistent = 3; }
    }
};
}  // namespace hal
