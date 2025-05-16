#pragma once

#include <memory>
#include <mutex>
#include <atomic>
#include <mqtt/async_client.h>
#include <mqtt/callback.h>
#include <mqtt/iaction_listener.h>

#include "IMqttClient.hpp"
#include "AgentConfig.hpp"

namespace edge_iot {

/**
 * @brief Concrete implementation of IMqttClient using Eclipse Paho MQTT C++ async_client.
 * Handles TLS, LWT, automatic reconnection, and non-blocking asynchronous I/O.
 */
class PahoMqttClient : public IMqttClient,
                       public virtual mqtt::callback,
                       public virtual mqtt::iaction_listener {
public:
    PahoMqttClient();
    ~PahoMqttClient() override;

    // IMqttClient interface
    bool connect(const AgentConfig& config) override;
    bool disconnect(int timeoutMs = 5000) override;
    bool isConnected() const override;
    bool publish(const std::string& topic, const std::string& payload, int qos = 1, bool retained = false) override;
    virtual bool subscribe(const std::string& topicFilter, int qos = 1) override;
    virtual bool unsubscribe(const std::string& topicFilter) override;
    void setCallback(IMqttCallback* callback) override;

    // mqtt::callback interface
    void connected(const mqtt::string& cause) override;
    void connection_lost(const mqtt::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;
    void delivery_complete(mqtt::delivery_token_ptr tok) override;

    // mqtt::iaction_listener interface
    void on_failure(const mqtt::token& tok) override;
    void on_success(const mqtt::token& tok) override;

private:
    std::unique_ptr<mqtt::async_client> client_;
    IMqttCallback* callback_{nullptr};
    mutable std::mutex callbackMutex_;
    std::atomic<bool> isConnected_{false};
    AgentConfig currentConfig_;
};

} // namespace edge_iot
