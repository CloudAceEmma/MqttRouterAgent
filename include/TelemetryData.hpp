#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <nlohmann/json.hpp>

namespace edge_iot {

/**
 * @brief Represents snapshot telemetry metrics collected from an edge router.
 */
struct TelemetryData {
    double cpu_load_percent{0.0};
    double memory_usage_percent{0.0};
    double memory_used_mb{0.0};
    double memory_total_mb{512.0};
    uint64_t bandwidth_rx_bytes{0};
    uint64_t bandwidth_tx_bytes{0};
    uint64_t uptime_seconds{0};
    int64_t timestamp_epoch_ms{0};

    /**
     * @brief Construct a new TelemetryData with current epoch milliseconds by default.
     */
    TelemetryData() {
        auto now = std::chrono::system_clock::now();
        timestamp_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
    }

    /**
     * @brief Convert telemetry metrics to a structured JSON object.
     * 
     * @param deviceId Identifier of the edge device.
     * @return nlohmann::json JSON representation of the telemetry data.
     */
    nlohmann::json toJson(const std::string& deviceId) const {
        return nlohmann::json{
            {"device_id", deviceId},
            {"timestamp_ms", timestamp_epoch_ms},
            {"metrics", {
                {"cpu_load_percent", cpu_load_percent},
                {"memory_usage_percent", memory_usage_percent},
                {"memory_used_mb", memory_used_mb},
                {"memory_total_mb", memory_total_mb},
                {"bandwidth_rx_bytes", bandwidth_rx_bytes},
                {"bandwidth_tx_bytes", bandwidth_tx_bytes},
                {"uptime_seconds", uptime_seconds}
            }}
        };
    }
};

} // namespace edge_iot
