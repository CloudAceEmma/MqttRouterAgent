#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace edge_iot {

/**
 * @brief Configuration settings for MqttRouterAgent.
 */
struct AgentConfig {
    std::string brokerUri{"tcp://localhost:1883"};
    std::string deviceId{"tplink-router-001"};
    std::string clientId{""}; // Empty means will use deviceId + "_client"
    std::string username{""};
    std::string password{""};

    int keepAliveIntervalSeconds{60};
    bool cleanSession{true};
    bool automaticReconnect{true};
    int minRetryIntervalMs{1000};
    int maxRetryIntervalMs{16000};
    int qos{1};

    // TLS / Security Options
    bool enableTls{false};
    std::string caPath{""};
    std::string clientCertPath{""};
    std::string clientKeyPath{""};
    bool verifyServerCert{true};

    /**
     * @brief Effective client ID used for MQTT session.
     */
    std::string getEffectiveClientId() const {
        return clientId.empty() ? (deviceId + "_agent") : clientId;
    }

    /**
     * @brief Topic used for reporting router presence and LWT.
     * Topic: router/{device_id}/status
     */
    std::string getStatusTopic() const {
        return "router/" + deviceId + "/status";
    }

    /**
     * @brief Topic used for publishing telemetry data.
     * Topic: router/{device_id}/telemetry
     */
    std::string getTelemetryTopic() const {
        return "router/" + deviceId + "/telemetry";
    }

    /**
     * @brief Topic filter used for receiving remote commands.
     * Filter: router/{device_id}/command/#
     */
    std::string getCommandTopicFilter() const {
        return "router/" + deviceId + "/command/#";
    }

    /**
     * @brief Topic used for publishing command execution responses.
     * Topic: router/{device_id}/response
     */
    std::string getResponseTopic() const {
        return "router/" + deviceId + "/response";
    }

    /**
     * @brief Standard LWT payload published when router ungracefully disconnects.
     * Payload: {"status": "offline"}
     */
    std::string getLwtPayload() const {
        return nlohmann::json{{"status", "offline"}}.dump();
    }

    /**
     * @brief Online status payload published upon successful connection.
     * Payload: {"status": "online"}
     */
    std::string getOnlinePayload() const {
        return nlohmann::json{{"status", "online"}}.dump();
    }

    /**
     * @brief Determine if TLS is required based on URI scheme or explicit flag.
     */
    bool isTlsRequired() const {
        if (enableTls) {
            return true;
        }
        return (brokerUri.rfind("ssl://", 0) == 0 ||
                brokerUri.rfind("tls://", 0) == 0 ||
                brokerUri.rfind("wss://", 0) == 0);
    }
};

} // namespace edge_iot
