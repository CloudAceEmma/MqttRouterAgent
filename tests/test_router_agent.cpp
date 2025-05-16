#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

#include "MqttRouterAgent.hpp"
#include "MockMqttClient.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SaveArg;
using ::testing::Eq;

namespace edge_iot {
namespace test {

class MqttRouterAgentTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.deviceId = "test-tplink-router-01";
        config_.brokerUri = "ssl://broker.edge-iot.example.com:8883";
        config_.username = "device_user";
        config_.password = "secret_token";
        config_.qos = 1;
        config_.keepAliveIntervalSeconds = 60;
        config_.automaticReconnect = true;

        mockClient_ = std::make_shared<MockMqttClient>();
        
        // Agent constructor automatically registers itself as the callback
        EXPECT_CALL(*mockClient_, setCallback(_)).Times(1);
        agent_ = std::make_unique<MqttRouterAgent>(config_, mockClient_);
    }

    void TearDown() override {
        agent_.reset();
        mockClient_.reset();
    }

    AgentConfig config_;
    std::shared_ptr<MockMqttClient> mockClient_;
    std::unique_ptr<MqttRouterAgent> agent_;
};

// 1. Verify Connection Management and Last Will & Testament (LWT) Configuration
TEST_F(MqttRouterAgentTest, ConfigureLwtAndConnects) {
    AgentConfig capturedConfig;

    EXPECT_CALL(*mockClient_, connect(_))
        .WillOnce(DoAll(SaveArg<0>(&capturedConfig), Return(true)));

    bool started = agent_->start();
    EXPECT_TRUE(started);

    // Verify LWT parameters embedded in AgentConfig
    EXPECT_EQ(capturedConfig.deviceId, "test-tplink-router-01");
    EXPECT_EQ(capturedConfig.getStatusTopic(), "router/test-tplink-router-01/status");
    
    // Parse LWT payload JSON
    auto lwtJson = nlohmann::json::parse(capturedConfig.getLwtPayload());
    EXPECT_EQ(lwtJson["status"], "offline");
    EXPECT_EQ(capturedConfig.qos, 1);
    EXPECT_TRUE(capturedConfig.automaticReconnect);
}

// 2. Verify Auto-Subscription and Online Status on Connection Established
TEST_F(MqttRouterAgentTest, OnConnectedPublishesOnlineAndSubscribesToCommands) {
    std::string expectedStatusTopic = "router/test-tplink-router-01/status";
    std::string expectedCommandFilter = "router/test-tplink-router-01/command/#";

    // Expect online presence published with QoS 1 and retained = true
    EXPECT_CALL(*mockClient_, publish(expectedStatusTopic, _, 1, true))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["status"], "online");
            return true;
        });

    // Expect auto-subscription to command wildcard topic
    EXPECT_CALL(*mockClient_, subscribe(expectedCommandFilter, 1))
        .WillOnce(Return(true));

    // Trigger onConnected callback (simulating broker handshake completion)
    agent_->onConnected("Initial connection established");
}

// 3. Verify Telemetry JSON Construction and Asynchronous Publishing
TEST_F(MqttRouterAgentTest, ConstructsAndPublishesValidTelemetryJson) {
    TelemetryData data;
    data.cpu_load_percent = 28.75;
    data.memory_usage_percent = 55.40;
    data.memory_used_mb = 283.65;
    data.memory_total_mb = 512.00;
    data.bandwidth_rx_bytes = 104857600; // 100 MB
    data.bandwidth_tx_bytes = 52428800;  // 50 MB
    data.uptime_seconds = 86400;         // 1 day
    data.timestamp_epoch_ms = 1789589694000;

    std::string expectedTelemetryTopic = "router/test-tplink-router-01/telemetry";

    EXPECT_CALL(*mockClient_, publish(expectedTelemetryTopic, _, 1, false))
        .WillOnce([&data](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            
            // Validate JSON fields
            EXPECT_EQ(j["device_id"], "test-tplink-router-01");
            EXPECT_EQ(j["timestamp_ms"], data.timestamp_epoch_ms);

            auto metrics = j["metrics"];
            EXPECT_DOUBLE_EQ(metrics["cpu_load_percent"].get<double>(), 28.75);
            EXPECT_DOUBLE_EQ(metrics["memory_usage_percent"].get<double>(), 55.40);
            EXPECT_DOUBLE_EQ(metrics["memory_used_mb"].get<double>(), 283.65);
            EXPECT_DOUBLE_EQ(metrics["memory_total_mb"].get<double>(), 512.00);
            EXPECT_EQ(metrics["bandwidth_rx_bytes"].get<uint64_t>(), 104857600ULL);
            EXPECT_EQ(metrics["bandwidth_tx_bytes"].get<uint64_t>(), 52428800ULL);
            EXPECT_EQ(metrics["uptime_seconds"].get<uint64_t>(), 86400ULL);

            return true;
        });

    bool published = agent_->publishTelemetry(data);
    EXPECT_TRUE(published);
}

// 4. Verify Command Listener: Parses "reboot" action and triggers executeReboot()
TEST_F(MqttRouterAgentTest, ParsesRebootCommandAndTriggersRebootExecution) {
    bool hookCalled = false;
    agent_->setRebootHook([&hookCalled]() {
        hookCalled = true;
    });

    EXPECT_FALSE(agent_->hasRebootBeenTriggered());

    // Mock client connected state for response publishing
    EXPECT_CALL(*mockClient_, isConnected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mockClient_, publish("router/test-tplink-router-01/response", _, 1, false))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["action"], "reboot");
            EXPECT_EQ(j["status"], "in_progress");
            return true;
        });

    std::string commandTopic = "router/test-tplink-router-01/command/power";
    std::string rebootPayload = R"({
        "action": "reboot",
        "request_id": "cloud-cmd-7788",
        "reason": "Scheduled OTA firmware upgrade"
    })";

    // Simulate incoming command message arrival
    agent_->onMessageArrived(commandTopic, rebootPayload);

    EXPECT_TRUE(agent_->hasRebootBeenTriggered());
    EXPECT_TRUE(hookCalled);
}

// 5. Verify Command Listener: Malformed JSON handled gracefully without crashing
TEST_F(MqttRouterAgentTest, HandlesMalformedJsonGracefully) {
    bool hookCalled = false;
    agent_->setRebootHook([&hookCalled]() {
        hookCalled = true;
    });

    EXPECT_CALL(*mockClient_, isConnected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mockClient_, publish("router/test-tplink-router-01/response", _, 1, false))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["status"], "error");
            return true;
        });

    std::string commandTopic = "router/test-tplink-router-01/command/system";
    std::string corruptedPayload = "{ action: 'reboot', unclosed_bracket: ";

    // Should not throw or crash
    EXPECT_NO_THROW({
        agent_->onMessageArrived(commandTopic, corruptedPayload);
    });

    EXPECT_FALSE(agent_->hasRebootBeenTriggered());
    EXPECT_FALSE(hookCalled);
}

// 6. Verify Command Listener: Missing 'action' field handled safely
TEST_F(MqttRouterAgentTest, HandlesMissingActionFieldGracefully) {
    bool hookCalled = false;
    agent_->setRebootHook([&hookCalled]() {
        hookCalled = true;
    });

    EXPECT_CALL(*mockClient_, isConnected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mockClient_, publish("router/test-tplink-router-01/response", _, 1, false))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["status"], "rejected");
            return true;
        });

    std::string commandTopic = "router/test-tplink-router-01/command/system";
    std::string noActionPayload = R"({"parameters": {"delay": 5}})";

    agent_->onMessageArrived(commandTopic, noActionPayload);

    EXPECT_FALSE(agent_->hasRebootBeenTriggered());
    EXPECT_FALSE(hookCalled);
}

// 7. Verify Custom Command Handler Extensibility
TEST_F(MqttRouterAgentTest, SupportsCustomCommandDispatch) {
    bool customHandlerCalled = false;
    std::string receivedColor;

    agent_->setCustomCommandHandler([&](const std::string& action, const nlohmann::json& payload) {
        if (action == "set_led") {
            customHandlerCalled = true;
            receivedColor = payload.value("color", "");
            return true;
        }
        return false;
    });

    EXPECT_CALL(*mockClient_, isConnected()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mockClient_, publish("router/test-tplink-router-01/response", _, 1, false))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["action"], "set_led");
            EXPECT_EQ(j["status"], "success");
            return true;
        });

    std::string commandTopic = "router/test-tplink-router-01/command/led";
    std::string ledPayload = R"({"action": "set_led", "color": "green"})";

    agent_->onMessageArrived(commandTopic, ledPayload);

    EXPECT_TRUE(customHandlerCalled);
    EXPECT_EQ(receivedColor, "green");
    EXPECT_FALSE(agent_->hasRebootBeenTriggered());
}

// 8. Verify Graceful Agent Stop (Publishes Offline Status, Unsubscribes, Disconnects)
TEST_F(MqttRouterAgentTest, GracefulStopLifecycle) {
    // Start agent
    EXPECT_CALL(*mockClient_, connect(_)).WillOnce(Return(true));
    agent_->start();

    EXPECT_CALL(*mockClient_, isConnected()).WillRepeatedly(Return(true));

    // Stop expectations
    EXPECT_CALL(*mockClient_, publish("router/test-tplink-router-01/status", _, 1, true))
        .WillOnce([](const std::string& topic, const std::string& payload, int qos, bool retained) {
            auto j = nlohmann::json::parse(payload);
            EXPECT_EQ(j["status"], "offline");
            return true;
        });

    EXPECT_CALL(*mockClient_, unsubscribe("router/test-tplink-router-01/command/#"))
        .WillOnce(Return(true));

    EXPECT_CALL(*mockClient_, disconnect(2000))
        .WillOnce(Return(true));

    agent_->stop();
}

} // namespace test
} // namespace edge_iot
