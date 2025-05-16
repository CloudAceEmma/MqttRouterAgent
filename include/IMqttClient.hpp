#pragma once

#include <string>
#include <memory>
#include "AgentConfig.hpp"

namespace edge_iot {

/**
 * @brief Callback interface for handling asynchronous MQTT transport events.
 */
class IMqttCallback {
public:
    virtual ~IMqttCallback() = default;

    /**
     * @brief Called when MQTT client successfully connects or reconnects to the broker.
     * @param cause Reconnect cause or empty string for initial connection.
     */
    virtual void onConnected(const std::string& cause) = 0;

    /**
     * @brief Called when MQTT connection to the broker is lost.
     * @param cause Reason/error string indicating why connection was lost.
     */
    virtual void onConnectionLost(const std::string& cause) = 0;

    /**
     * @brief Called when a message arrives on a subscribed topic.
     * @param topic Topic the message was published to.
     * @param payload Raw payload string content.
     */
    virtual void onMessageArrived(const std::string& topic, const std::string& payload) = 0;

    /**
     * @brief Called when delivery for a QoS 1 or QoS 2 message is confirmed.
     * @param token Message token identifier.
     */
    virtual void onDeliveryComplete(int token) = 0;
};

/**
 * @brief Pure virtual interface for MQTT client operations.
 * Allows seamless Dependency Injection and Mocking with Google Mock for unit testing.
 */
class IMqttClient {
public:
    virtual ~IMqttClient() = default;

    /**
     * @brief Connect asynchronously/synchronously to the MQTT broker using provided configuration.
     * @param config Configuration parameters including LWT and TLS options.
     * @return true if connect request was successfully initiated, false otherwise.
     */
    virtual bool connect(const AgentConfig& config) = 0;

    /**
     * @brief Disconnect from the MQTT broker.
     * @param timeoutMs Timeout in milliseconds to wait for clean disconnect.
     * @return true if disconnect succeeded, false otherwise.
     */
    virtual bool disconnect(int timeoutMs = 5000) = 0;

    /**
     * @brief Check whether client is currently connected to the broker.
     * @return true if connected, false otherwise.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Publish a message asynchronously to a specified topic.
     * @param topic Target MQTT topic.
     * @param payload Message payload string.
     * @param qos Quality of Service level (0, 1, or 2).
     * @param retained Retain flag for the broker.
     * @return true if publish request was queued/sent successfully, false otherwise.
     */
    virtual bool publish(const std::string& topic, const std::string& payload, int qos = 1, bool retained = false) = 0;

    /**
     * @brief Subscribe to a topic or topic filter with wildcards.
     * @param topicFilter MQTT topic filter (e.g. "router/123/command/#").
     * @param qos Quality of Service level.
     * @return true if subscription request was initiated successfully, false otherwise.
     */
    virtual bool subscribe(const std::string& topicFilter, int qos = 1) = 0;

    /**
     * @brief Unsubscribe from a topic filter.
     * @param topicFilter MQTT topic filter.
     * @return true if unsubscribe request was initiated successfully, false otherwise.
     */
    virtual bool unsubscribe(const std::string& topicFilter) = 0;

    /**
     * @brief Set the callback listener for connection and message arrival events.
     * @param callback Pointer to the IMqttCallback implementation.
     */
    virtual void setCallback(IMqttCallback* callback) = 0;
};

} // namespace edge_iot
