cmake_minimum_required(VERSION 3.16)

# Ensure compatibility with older submodules while supporting modern CMake
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(MqttRouterAgent
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "TP-Link Edge IoT MQTT Router Management Agent"
)

# Standard C and C++ compiler configurations
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd, LSP, and IDE tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Create a symlink in the project root if it doesn't already exist
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(CREATE_LINK 
        "${CMAKE_BINARY_DIR}/compile_commands.json" 
        "${CMAKE_SOURCE_DIR}/compile_commands.json" 
        SYMBOLIC
    )
endif()

# Position Independent Code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# Threading & SSL
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. Dependency: nlohmann_json
# ---------------------------------------------------------------------------
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found locally; fetching via FetchContent...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---------------------------------------------------------------------------
# 2. Dependency: Eclipse Paho MQTT C and C++
# ---------------------------------------------------------------------------
set(PAHO_BUILD_SHARED TRUE CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC FALSE CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION FALSE CACHE BOOL "" FORCE)
set(PAHO_BUILD_SAMPLES FALSE CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING FALSE CACHE BOOL "" FORCE)
set(PAHO_WITH_SSL TRUE CACHE BOOL "" FORCE)
set(PAHO_HIGH_PERFORMANCE TRUE CACHE BOOL "" FORCE)

FetchContent_Declare(
    paho_mqtt_c
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
    GIT_TAG v1.3.14
)

FetchContent_Declare(
    paho_mqtt_cpp
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.cpp.git
    GIT_TAG v1.4.1
)

FetchContent_GetProperties(paho_mqtt_c)
if(NOT paho_mqtt_c_POPULATED)
    FetchContent_Populate(paho_mqtt_c)
endif()

FetchContent_GetProperties(paho_mqtt_cpp)
if(NOT paho_mqtt_cpp_POPULATED)
    FetchContent_Populate(paho_mqtt_cpp)
    file(MAKE_DIRECTORY "${paho_mqtt_cpp_SOURCE_DIR}/externals")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink 
        "${paho_mqtt_c_SOURCE_DIR}" 
        "${paho_mqtt_cpp_SOURCE_DIR}/externals/paho-mqtt-c"
    )
endif()

set(PAHO_WITH_MQTT_C TRUE CACHE BOOL "" FORCE)
add_subdirectory(${paho_mqtt_cpp_SOURCE_DIR} ${paho_mqtt_cpp_BINARY_DIR} EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# 3. Dependency: GoogleTest & GoogleMock
# ---------------------------------------------------------------------------
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found locally; fetching via FetchContent...")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ---------------------------------------------------------------------------
# 4. Core Library Target: mqtt_router_lib
# ---------------------------------------------------------------------------
add_library(mqtt_router_lib STATIC
    src/PahoMqttClient.cpp
    src/MqttRouterAgent.cpp
)

target_include_directories(mqtt_router_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(mqtt_router_lib PUBLIC
    paho-mqttpp3
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# ---------------------------------------------------------------------------
# 5. Main Executable: mqtt_router_agent
# ---------------------------------------------------------------------------
add_executable(mqtt_router_agent
    src/main.cpp
)

target_link_libraries(mqtt_router_agent PRIVATE
    mqtt_router_lib
)

# ---------------------------------------------------------------------------
# 6. Unit Tests: test_router_agent
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_router_agent
    tests/test_router_agent.cpp
)

target_include_directories(test_router_agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

target_link_libraries(test_router_agent PRIVATE
    mqtt_router_lib
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(test_router_agent)
