#include "MqttRouterAgent.hpp"
#include "PahoMqttClient.hpp"
#include <iostream>

namespace edge_iot {

MqttRouterAgent::MqttRouterAgent(AgentConfig config, std::shared_ptr<IMqttClient> mqttClient)
    : config_(std::move(config)),
      mqttClient_(std::move(mqttClient)) {
    if (!mqttClient_) {
        mqttClient_ = std::make_shared<PahoMqttClient>();
    }
    mqttClient_->setCallback(this);
}

MqttRouterAgent::~MqttRouterAgent() {
    stop();
}

bool MqttRouterAgent::start() {
    std::cout << "[MqttRouterAgent] Starting agent for device: " << config_.deviceId << std::endl;
    isRunning_ = true;
    bool success = mqttClient_->connect(config_);
    if (!success) {
        std::cerr << "[MqttRouterAgent] Failed to initiate connection to MQTT broker: " 
                  << config_.brokerUri << std::endl;
        isRunning_ = false;
        return false;
    }
    return true;
}

void MqttRouterAgent::stop() {
    if (isRunning_) {
        std::cout << "[MqttRouterAgent] Stopping agent for device: " << config_.deviceId << std::endl;
        if (mqttClient_ && mqttClient_->isConnected()) {
            publishOfflineStatus();
            mqttClient_->unsubscribe(config_.getCommandTopicFilter());
            mqttClient_->disconnect(2000);
        }
        isRunning_ = false;
    }
}

bool MqttRouterAgent::isConnected() const {
    return mqttClient_ && mqttClient_->isConnected();
}

bool MqttRouterAgent::publishTelemetry(const TelemetryData& telemetry) {
    nlohmann::json jsonPayload = telemetry.toJson(config_.deviceId);
    return publishTelemetryJson(jsonPayload.dump());
}

bool MqttRouterAgent::publishTelemetryJson(const std::string& jsonPayload) {
    if (!mqttClient_) {
        return false;
    }
    std::string topic = config_.getTelemetryTopic();
    std::cout << "[MqttRouterAgent] Publishing telemetry to topic [" << topic << "]: " 
              << jsonPayload << std::endl;
    return mqttClient_->publish(topic, jsonPayload, config_.qos, false);
}

bool MqttRouterAgent::publishOnlineStatus() {
    if (!mqttClient_) {
        return false;
    }
    std::string topic = config_.getStatusTopic();
    std::string payload = config_.getOnlinePayload();
    std::cout << "[MqttRouterAgent] Publishing ONLINE status to [" << topic << "]: " 
              << payload << std::endl;
    return mqttClient_->publish(topic, payload, config_.qos, true);
}

bool MqttRouterAgent::publishOfflineStatus() {
    if (!mqttClient_) {
        return false;
    }
    std::string topic = config_.getStatusTopic();
    std::string payload = config_.getLwtPayload();
    std::cout << "[MqttRouterAgent] Publishing OFFLINE status to [" << topic << "]: " 
              << payload << std::endl;
    return mqttClient_->publish(topic, payload, config_.qos, true);
}

void MqttRouterAgent::setRebootHook(RebootHook hook) {
    std::lock_guard<std::mutex> lock(hookMutex_);
    rebootHook_ = std::move(hook);
}

void MqttRouterAgent::setCustomCommandHandler(CommandHandler handler) {
    std::lock_guard<std::mutex> lock(hookMutex_);
    customCommandHandler_ = std::move(handler);
}

bool MqttRouterAgent::hasRebootBeenTriggered() const {
    return rebootTriggered_.load();
}

void MqttRouterAgent::onConnected(const std::string& cause) {
    std::cout << "[MqttRouterAgent] Successfully connected to broker! (Cause: " 
              << (cause.empty() ? "initial" : cause) << ")" << std::endl;

    // 1. Publish Online Status (retained so cloud dashboard immediately discovers device)
    publishOnlineStatus();

    // 2. Automatically Subscribe to Command Topics: router/{device_id}/command/#
    std::string commandFilter = config_.getCommandTopicFilter();
    std::cout << "[MqttRouterAgent] Subscribing to command topic: " << commandFilter << std::endl;
    mqttClient_->subscribe(commandFilter, config_.qos);
}

void MqttRouterAgent::onConnectionLost(const std::string& cause) {
    std::cerr << "[MqttRouterAgent] [WARNING] Connection to broker lost: " << cause 
              << ". Automatic reconnection in progress..." << std::endl;
}

void MqttRouterAgent::onMessageArrived(const std::string& topic, const std::string& payload) {
    std::cout << "[MqttRouterAgent] Received MQTT message on topic [" << topic << "]: " 
              << payload << std::endl;

    // Parse incoming command as JSON safely
    auto jsonCmd = nlohmann::json::parse(payload, nullptr, false);
    if (jsonCmd.is_discarded()) {
        std::cerr << "[MqttRouterAgent] [ERROR] Received invalid/corrupted JSON payload on topic: " 
                  << topic << std::endl;
        publishCommandResponse("unknown", "error", "Invalid JSON format");
        return;
    }

    processCommand(topic, jsonCmd);
}

void MqttRouterAgent::processCommand(const std::string& topic, const nlohmann::json& jsonCmd) {
    if (!jsonCmd.is_object() || !jsonCmd.contains("action")) {
        std::cerr << "[MqttRouterAgent] [WARNING] Command payload missing 'action' field on topic: " 
                  << topic << std::endl;
        publishCommandResponse("unknown", "rejected", "Missing 'action' field");
        return;
    }

    std::string action = jsonCmd["action"].get<std::string>();
    std::cout << "[MqttRouterAgent] Processing action: '" << action << "'" << std::endl;

    if (action == "reboot") {
        std::cout << "[MqttRouterAgent] [ACTION] Reboot command validated. Triggering executeReboot()..." 
                  << std::endl;
        publishCommandResponse("reboot", "in_progress", "Reboot sequence initiated");
        executeReboot();
    } else {
        // Check for extensible custom command handlers
        bool handled = false;
        {
            std::lock_guard<std::mutex> lock(hookMutex_);
            if (customCommandHandler_) {
                handled = customCommandHandler_(action, jsonCmd);
            }
        }

        if (handled) {
            publishCommandResponse(action, "success", "Custom command handled successfully");
        } else {
            std::cerr << "[MqttRouterAgent] [WARNING] Unsupported action requested: '" 
                      << action << "'" << std::endl;
            publishCommandResponse(action, "unsupported", "Action not supported on this device");
        }
    }
}

void MqttRouterAgent::executeReboot() {
    rebootTriggered_.store(true);

    std::cout << "========================================================\n"
              << "[MqttRouterAgent] [CRITICAL] >>> EXECUTING ROUTER REBOOT <<<\n"
              << "[MqttRouterAgent] Device ID: " << config_.deviceId << "\n"
              << "[MqttRouterAgent] 1. Syncing edge file systems...\n"
              << "[MqttRouterAgent] 2. Gracefully closing network sockets...\n"
              << "[MqttRouterAgent] 3. Simulating hardware reboot (/sbin/reboot)...\n"
              << "========================================================" << std::endl;

    RebootHook hookCopy{nullptr};
    {
        std::lock_guard<std::mutex> lock(hookMutex_);
        hookCopy = rebootHook_;
    }

    if (hookCopy) {
        std::cout << "[MqttRouterAgent] Invoking registered reboot hook..." << std::endl;
        hookCopy();
    }
}

void MqttRouterAgent::publishCommandResponse(const std::string& action, 
                                            const std::string& status, 
                                            const std::string& details) {
    if (!mqttClient_ || !mqttClient_->isConnected()) {
        return;
    }

    nlohmann::json response{
        {"device_id", config_.deviceId},
        {"action", action},
        {"status", status},
        {"details", details},
        {"timestamp_epoch_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()}
    };

    mqttClient_->publish(config_.getResponseTopic(), response.dump(), config_.qos, false);
}

void MqttRouterAgent::onDeliveryComplete(int token) {
    std::cout << "[MqttRouterAgent] MQTT Message delivery confirmed (Token: " << token << ")" << std::endl;
}

} // namespace edge_iot
