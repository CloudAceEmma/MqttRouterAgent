#include "PahoMqttClient.hpp"
#include <iostream>
#include <chrono>

namespace edge_iot {

PahoMqttClient::PahoMqttClient() = default;

PahoMqttClient::~PahoMqttClient() {
    if (client_ && client_->is_connected()) {
        try {
            client_->disconnect()->wait_for(std::chrono::seconds(2));
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
}

void PahoMqttClient::setCallback(IMqttCallback* callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = callback;
}

bool PahoMqttClient::connect(const AgentConfig& config) {
    currentConfig_ = config;

    try {
        std::string serverUri = config.brokerUri;
        std::string clientId = config.getEffectiveClientId();

        std::cout << "[PahoMqttClient] Initializing client for " << clientId 
                  << " -> " << serverUri << std::endl;

        client_ = std::make_unique<mqtt::async_client>(serverUri, clientId);
        client_->set_callback(*this);

        mqtt::connect_options_builder connOptsBuilder;
        connOptsBuilder.clean_session(config.cleanSession)
                       .keep_alive_interval(std::chrono::seconds(config.keepAliveIntervalSeconds))
                       .automatic_reconnect(config.automaticReconnect);

        if (config.automaticReconnect) {
            connOptsBuilder.automatic_reconnect(
                std::chrono::milliseconds(config.minRetryIntervalMs),
                std::chrono::milliseconds(config.maxRetryIntervalMs)
            );
        }

        if (!config.username.empty()) {
            connOptsBuilder.user_name(config.username);
        }
        if (!config.password.empty()) {
            connOptsBuilder.password(config.password);
        }

        // Configure Last Will and Testament (LWT)
        std::string lwtTopic = config.getStatusTopic();
        std::string lwtPayload = config.getLwtPayload();
        mqtt::message willMsg(lwtTopic, lwtPayload, config.qos, true);
        mqtt::will_options willOpts(willMsg);
        connOptsBuilder.will(willOpts);

        std::cout << "[PahoMqttClient] Configured LWT on topic: " << lwtTopic 
                  << " with payload: " << lwtPayload << std::endl;

        // Configure TLS / SSL if required
        if (config.isTlsRequired()) {
            std::cout << "[PahoMqttClient] Enabling TLS/SSL security options..." << std::endl;
            mqtt::ssl_options_builder sslOptsBuilder;
            if (!config.caPath.empty()) {
                sslOptsBuilder.trust_store(config.caPath);
            }
            if (!config.clientCertPath.empty()) {
                sslOptsBuilder.key_store(config.clientCertPath);
            }
            if (!config.clientKeyPath.empty()) {
                sslOptsBuilder.private_key(config.clientKeyPath);
            }
            sslOptsBuilder.enable_server_cert_auth(config.verifyServerCert);
            connOptsBuilder.ssl(sslOptsBuilder.finalize());
        }

        mqtt::connect_options connOpts = connOptsBuilder.finalize();

        // Connect asynchronously with action listener
        std::cout << "[PahoMqttClient] Connecting asynchronously to broker..." << std::endl;
        client_->connect(connOpts, nullptr, *this);
        return true;

    } catch (const mqtt::exception& exc) {
        std::cerr << "[PahoMqttClient] Connection failed with exception: " 
                  << exc.what() << " [Code: " << exc.get_reason_code() << "]" << std::endl;
        return false;
    } catch (const std::exception& exc) {
        std::cerr << "[PahoMqttClient] Standard exception during connect: " 
                  << exc.what() << std::endl;
        return false;
    }
}

bool PahoMqttClient::disconnect(int timeoutMs) {
    if (!client_) {
        return true;
    }

    try {
        std::cout << "[PahoMqttClient] Disconnecting from broker..." << std::endl;
        auto token = client_->disconnect(timeoutMs);
        if (token) {
            token->wait_for(std::chrono::milliseconds(timeoutMs));
        }
        isConnected_ = false;
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "[PahoMqttClient] Disconnect error: " << exc.what() << std::endl;
        return false;
    }
}

bool PahoMqttClient::isConnected() const {
    return isConnected_ && client_ && client_->is_connected();
}

bool PahoMqttClient::publish(const std::string& topic, const std::string& payload, int qos, bool retained) {
    if (!client_) {
        std::cerr << "[PahoMqttClient] Cannot publish: client not initialized" << std::endl;
        return false;
    }

    try {
        auto msg = mqtt::make_message(topic, payload, qos, retained);
        client_->publish(msg, nullptr, *this);
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "[PahoMqttClient] Publish error on topic " << topic 
                  << ": " << exc.what() << std::endl;
        return false;
    }
}

bool PahoMqttClient::subscribe(const std::string& topicFilter, int qos) {
    if (!client_) {
        std::cerr << "[PahoMqttClient] Cannot subscribe: client not initialized" << std::endl;
        return false;
    }

    try {
        std::cout << "[PahoMqttClient] Subscribing to: " << topicFilter << " (QoS " << qos << ")" << std::endl;
        client_->subscribe(topicFilter, qos, nullptr, *this);
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "[PahoMqttClient] Subscribe error for " << topicFilter 
                  << ": " << exc.what() << std::endl;
        return false;
    }
}

bool PahoMqttClient::unsubscribe(const std::string& topicFilter) {
    if (!client_) {
        return false;
    }

    try {
        client_->unsubscribe(topicFilter, nullptr, *this);
        return true;
    } catch (const mqtt::exception& exc) {
        std::cerr << "[PahoMqttClient] Unsubscribe error: " << exc.what() << std::endl;
        return false;
    }
}

// mqtt::callback implementation
void PahoMqttClient::connected(const mqtt::string& cause) {
    std::string causeStr = cause;
    std::cout << "[PahoMqttClient] Connection established/reconnected. Cause: " 
              << (causeStr.empty() ? "initial connect" : causeStr) << std::endl;
    isConnected_ = true;

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_) {
        callback_->onConnected(causeStr);
    }
}

void PahoMqttClient::connection_lost(const mqtt::string& cause) {
    std::string causeStr = cause;
    std::cerr << "[PahoMqttClient] Connection lost! Cause: " << causeStr << std::endl;
    isConnected_ = false;

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_) {
        callback_->onConnectionLost(causeStr);
    }
}

void PahoMqttClient::message_arrived(mqtt::const_message_ptr msg) {
    if (!msg) return;

    std::string topic = msg->get_topic();
    std::string payload = msg->to_string();

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_) {
        callback_->onMessageArrived(topic, payload);
    }
}

void PahoMqttClient::delivery_complete(mqtt::delivery_token_ptr tok) {
    if (!tok) return;

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_) {
        callback_->onDeliveryComplete(tok->get_message_id());
    }
}

// mqtt::iaction_listener implementation
void PahoMqttClient::on_failure(const mqtt::token& tok) {
    std::cerr << "[PahoMqttClient] Async action failure (Token ID: " 
              << tok.get_message_id() << ", Reason Code: " 
              << tok.get_reason_code() << ")" << std::endl;
}

void PahoMqttClient::on_success(const mqtt::token& tok) {
    // Action succeeded (connect, publish, or subscribe token)
    if (tok.get_type() == mqtt::token::Type::CONNECT) {
        isConnected_ = true;
    }
}

} // namespace edge_iot
