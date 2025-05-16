# MQTT Router Agent

A lightweight C++17 service for embedded Linux routers (such as TP-Link devices). It periodically sends device telemetry to an MQTT broker and listens for remote management commands like device reboot.

## How it works

- **Telemetry**: Periodically collects CPU load, memory usage, network traffic, and uptime, formats them into JSON, and publishes to `router/{device_id}/telemetry`.
- **Remote Commands**: Subscribes to `router/{device_id}/command/#`. When a command like `{"action": "reboot"}` arrives, it parses the JSON and triggers the reboot sequence.
- **Connection & State**: Connects over TCP or TLS with automatic reconnect. Sets an MQTT Last Will and Testament (LWT) on `router/{device_id}/status` with payload `{"status": "offline"}` to report unexpected disconnects, and publishes `{"status": "online"}` upon connecting.
- **Testability**: Uses an `IMqttClient` interface with dependency injection, allowing the MQTT client to be fully mocked with Google Mock for offline unit testing.

## MQTT Topics

| Topic | Direction | Payload Example | Notes |
| :--- | :--- | :--- | :--- |
| `router/{device_id}/status` | Outgoing | `{"status": "online"}` / `{"status": "offline"}` | Retained; used for LWT and presence |
| `router/{device_id}/telemetry` | Outgoing | `{"device_id": "...", "metrics": {...}}` | Published periodically |
| `router/{device_id}/command/#` | Incoming | `{"action": "reboot"}` | Command topic filter |
| `router/{device_id}/response` | Outgoing | `{"action": "reboot", "status": "in_progress"}` | Command acknowledgments |

## Project Structure

- `include/`: Public headers (`MqttRouterAgent.hpp`, `IMqttClient.hpp`, `PahoMqttClient.hpp`, `AgentConfig.hpp`, `TelemetryData.hpp`).
- `src/`: Implementation files (`MqttRouterAgent.cpp`, `PahoMqttClient.cpp`, `main.cpp`).
- `tests/`: Unit tests and mock client (`test_router_agent.cpp`, `MockMqttClient.hpp`).
- `CMakeLists.txt`: Build configuration. Automatically fetches Paho MQTT C/C++, GoogleTest, and nlohmann/json if they are not installed locally.

## Requirements

- C++17 compiler (GCC 9+, Clang 10+)
- CMake 3.16+
- OpenSSL (`libssl-dev` or `openssl-devel`)

## Building

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

## Running the Agent

Pass the broker URI and device ID as arguments:

```bash
./build/mqtt_router_agent "tcp://localhost:1883" "router-01"
```

Or configure via environment variables:

```bash
export MQTT_BROKER_URI="ssl://broker.example.com:8883"
export DEVICE_ID="router-01"
export MQTT_USERNAME="user"
export MQTT_PASSWORD="password"
./build/mqtt_router_agent
```
