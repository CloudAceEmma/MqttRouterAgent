#pragma once

#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

#include "AgentConfig.hpp"
#include "TelemetryData.hpp"
#include "IMqttClient.hpp"

namespace edge_iot {

/**
 * @brief Edge Router Management Agent running on TP-Link / Linux IoT routers.
 * Handles bidirectional MQTT communication: telemetry publishing and remote command execution.
 */
class MqttRouterAgent : public IMqttCallback {
public:
    using RebootHook = std::function<void()>;
    using CommandHandler = std::function<bool(const std::string& action, const nlohmann::json& payload)>;

    /**
     * @brief Construct an MqttRouterAgent using Dependency Injection.
     * 
     * @param config Agent configuration containing broker URI, device ID, credentials, and QoS.
     * @param mqttClient Injected MQTT client implementation (nullptr creates default PahoMqttClient).
     */
    explicit MqttRouterAgent(AgentConfig config, std::shared_ptr<IMqttClient> mqttClient = nullptr);

    /**
     * @brief Destructor ensures clean shutdown and graceful disconnection.
     */
    ~MqttRouterAgent() override;

    /**
     * @brief Start the agent: establishes MQTT connection, configures LWT, and subscribes to commands.
     * @return true if start request was initiated successfully, false otherwise.
     */
    bool start();

    /**
     * @brief Stop the agent: publishes offline status, unsubscribes, and disconnects gracefully.
     */
    void stop();

    /**
     * @brief Check whether the agent is currently connected to the MQTT broker.
     */
    bool isConnected() const;

    /**
     * @brief Publish router telemetry metrics asynchronously.
     * Publishes to topic: router/{device_id}/telemetry
     * 
     * @param telemetry Telemetry data containing CPU, memory, bandwidth, and uptime.
     * @return true if publish was initiated successfully, false otherwise.
     */
    bool publishTelemetry(const TelemetryData& telemetry);

    /**
     * @brief Publish a custom telemetry JSON payload asynchronously.
     * 
     * @param jsonPayload Serialized JSON string.
     * @return true if publish was initiated successfully, false otherwise.
     */
    bool publishTelemetryJson(const std::string& jsonPayload);

    /**
     * @brief Publish router online presence status.
     * Publishes to topic: router/{device_id}/status
     */
    bool publishOnlineStatus();

    /**
     * @brief Publish router offline presence status.
     * Publishes to topic: router/{device_id}/status
     */
    bool publishOfflineStatus();

    /**
     * @brief Register a custom hook/callback to be called when executeReboot() is triggered.
     * Useful for hardware abstraction, testing, and mocking system reboot.
     */
    void setRebootHook(RebootHook hook);

    /**
     * @brief Register an additional custom command handler for extensible action dispatch.
     */
    void setCustomCommandHandler(CommandHandler handler);

    /**
     * @brief Check if a reboot has been triggered on this agent.
     */
    bool hasRebootBeenTriggered() const;

    /**
     * @brief Get active configuration.
     */
    const AgentConfig& getConfig() const { return config_; }

    // IMqttCallback interface methods
    void onConnected(const std::string& cause) override;
    void onConnectionLost(const std::string& cause) override;
    void onMessageArrived(const std::string& topic, const std::string& payload) override;
    void onDeliveryComplete(int token) override;

private:
    /**
     * @brief Private mock method triggered upon receiving {"action": "reboot"}.
     * Logs the reboot event, notifies any registered hooks, and simulates / executes router reboot.
     */
    void executeReboot();

    /**
     * @brief Process and route incoming parsed JSON command payloads.
     */
    void processCommand(const std::string& topic, const nlohmann::json& jsonCmd);

    /**
     * @brief Publish execution acknowledgement / response back to the cloud.
     */
    void publishCommandResponse(const std::string& action, const std::string& status, const std::string& details);

    AgentConfig config_;
    std::shared_ptr<IMqttClient> mqttClient_;
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> rebootTriggered_{false};

    mutable std::mutex hookMutex_;
    RebootHook rebootHook_{nullptr};
    CommandHandler customCommandHandler_{nullptr};
};

} // namespace edge_iot
