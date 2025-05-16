#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <cstdlib>

#include "MqttRouterAgent.hpp"

// Atomic flag for graceful termination on SIGINT/SIGTERM
static std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\n[Main] Shutdown signal received (" << signal << "). Terminating edge agent..." << std::endl;
        g_running = false;
    }
}

/**
 * @brief Helper function to read environment variables with fallback default.
 */
std::string getEnvOrDefault(const char* envVar, const std::string& defaultValue) {
    const char* val = std::getenv(envVar);
    return (val != nullptr && std::string(val).length() > 0) ? std::string(val) : defaultValue;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n"
              << "       TP-Link Edge IoT MQTT Router Management Agent     \n"
              << "========================================================" << std::endl;

    // Register signal handlers for graceful shutdown on edge devices
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Build configuration from environment or command line defaults
    edge_iot::AgentConfig config;
    config.brokerUri = (argc > 1) ? argv[1] : getEnvOrDefault("MQTT_BROKER_URI", "tcp://localhost:1883");
    config.deviceId  = (argc > 2) ? argv[2] : getEnvOrDefault("DEVICE_ID", "tplink-archer-ax50-001");
    config.username  = getEnvOrDefault("MQTT_USERNAME", "");
    config.password  = getEnvOrDefault("MQTT_PASSWORD", "");
    config.keepAliveIntervalSeconds = 60;
    config.cleanSession = true;
    config.automaticReconnect = true;
    config.qos = 1;

    std::cout << "[Main] Device ID   : " << config.deviceId << "\n"
              << "[Main] Broker URI  : " << config.brokerUri << "\n"
              << "[Main] LWT Topic   : " << config.getStatusTopic() << "\n"
              << "[Main] Command Sub : " << config.getCommandTopicFilter() << "\n"
              << "[Main] Telemetry   : " << config.getTelemetryTopic() << std::endl;

    // Instantiate Agent using default Paho MQTT client
    edge_iot::MqttRouterAgent agent(config);

    // Register custom reboot hook simulation
    agent.setRebootHook([&agent]() {
        std::cout << "[Main] [CALLBACK] Reboot hook invoked! Router will perform restart sequence." << std::endl;
    });

    // Start Agent
    if (!agent.start()) {
        std::cerr << "[Main] Failed to start MQTT Router Agent. Exiting." << std::endl;
        return 1;
    }

    std::cout << "[Main] Agent successfully started. Entering 10-second telemetry loop..." << std::endl;

    // Metrics simulation state
    std::mt19937 rng(1337);
    std::uniform_real_distribution<double> cpuDist(10.0, 45.0);
    std::uniform_real_distribution<double> memDist(35.0, 60.0);
    std::uniform_int_distribution<uint64_t> bytesDist(10240, 51200);

    uint64_t rxAccumulator = 10485760; // 10 MB initial
    uint64_t txAccumulator = 5242880;  // 5 MB initial
    uint64_t uptimeCounter = 0;

    auto lastTelemetryTime = std::chrono::steady_clock::now();

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTelemetryTime).count();

        if (elapsed >= 10) {
            lastTelemetryTime = now;
            uptimeCounter += 10;
            rxAccumulator += bytesDist(rng);
            txAccumulator += bytesDist(rng);

            edge_iot::TelemetryData telemetry;
            telemetry.cpu_load_percent = cpuDist(rng);
            telemetry.memory_usage_percent = memDist(rng);
            telemetry.memory_total_mb = 512.0;
            telemetry.memory_used_mb = (telemetry.memory_usage_percent / 100.0) * telemetry.memory_total_mb;
            telemetry.bandwidth_rx_bytes = rxAccumulator;
            telemetry.bandwidth_tx_bytes = txAccumulator;
            telemetry.uptime_seconds = uptimeCounter;

            agent.publishTelemetry(telemetry);
        }

        // Sleep in small 100ms intervals for responsive signal handling
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Main] Stopping MQTT Router Agent..." << std::endl;
    agent.stop();
    std::cout << "[Main] Agent stopped gracefully. Exiting." << std::endl;

    return 0;
}
