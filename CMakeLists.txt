cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(MqttRouterAgent
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "TP-Link Edge IoT MQTT Router Management Agent"
)

# ---------------------------------------------------------------------------
# 全局编译选项 & 核心设置
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 动态开关：生产环境默认关闭测试，本地开发可手动开启 (-DBUILD_TESTING=ON)
option(BUILD_TESTING "Build the testing tree" OFF)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(CREATE_LINK 
        "${CMAKE_BINARY_DIR}/compile_commands.json" 
        "${CMAKE_SOURCE_DIR}/compile_commands.json" 
        SYMBOLIC
    )
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# 系统基础依赖 (必须由系统提供)
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include(FetchContent)

# ---------------------------------------------------------------------------
# 1. Dependency: nlohmann_json (先找系统库，找不到再 Fetch)
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
# 2. Dependency: Eclipse Paho MQTT C and C++ (先找系统库，找不到再 Fetch)
# ---------------------------------------------------------------------------
# 尝试寻找系统中的 Paho 库 (Buildroot 环境下会在这里成功)
find_package(eclipse-paho-mqtt-c QUIET)
find_package(PahoMqttCpp QUIET)

# 定义统一的链接目标变量，抹平 Find 和 Fetch 产生的 Target 命名差异
if(PahoMqttCpp_FOUND)
    message(STATUS "Found Paho MQTT in system.")
    set(PAHO_CPP_TARGET PahoMqttCpp::paho-mqttpp3)
else()
    message(STATUS "Paho MQTT not found locally; fetching via FetchContent...")
    set(PAHO_BUILD_SHARED TRUE CACHE BOOL "" FORCE)
    set(PAHO_BUILD_STATIC FALSE CACHE BOOL "" FORCE)
    set(PAHO_BUILD_DOCUMENTATION FALSE CACHE BOOL "" FORCE)
    set(PAHO_BUILD_SAMPLES FALSE CACHE BOOL "" FORCE)
    set(PAHO_ENABLE_TESTING FALSE CACHE BOOL "" FORCE)
    set(PAHO_WITH_SSL TRUE CACHE BOOL "" FORCE)
    set(PAHO_HIGH_PERFORMANCE TRUE CACHE BOOL "" FORCE)

    FetchContent_Declare(paho_mqtt_c GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git GIT_TAG v1.3.14)
    FetchContent_Declare(paho_mqtt_cpp GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.cpp.git GIT_TAG v1.4.1)

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
    
    # 本地 Fetch 编译出的 Target 名称
    set(PAHO_CPP_TARGET paho-mqttpp3)
endif()

# ---------------------------------------------------------------------------
# 3. Core Library Target: mqtt_router_lib
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
    ${PAHO_CPP_TARGET}
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# ---------------------------------------------------------------------------
# 4. Main Executable: mqtt_router_agent
# ---------------------------------------------------------------------------
add_executable(mqtt_router_agent
    src/main.cpp
)

target_link_libraries(mqtt_router_agent PRIVATE
    mqtt_router_lib
)

# Tell CMake to install the program to the system's bin directory
install(TARGETS mqtt_router_agent DESTINATION bin)

# ---------------------------------------------------------------------------
# 5. Unit Tests: 仅在 BUILD_TESTING=ON 时生效
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    enable_testing()

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
endif()
