#pragma once

#include <gmock/gmock.h>
#include "IMqttClient.hpp"

namespace edge_iot {

/**
 * @brief Google Mock implementation of IMqttClient interface for isolated unit testing.
 */
class MockMqttClient : public IMqttClient {
public:
    MOCK_METHOD(bool, connect, (const AgentConfig& config), (override));
    MOCK_METHOD(bool, disconnect, (int timeoutMs), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(bool, publish, (const std::string& topic, const std::string& payload, int qos, bool retained), (override));
    MOCK_METHOD(bool, subscribe, (const std::string& topicFilter, int qos), (override));
    MOCK_METHOD(bool, unsubscribe, (const std::string& topicFilter), (override));
    MOCK_METHOD(void, setCallback, (IMqttCallback* callback), (override));
};

} // namespace edge_iot
