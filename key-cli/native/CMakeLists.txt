cmake_minimum_required(VERSION 3.21)
project(KeyCliNative LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(KeySysmon STATIC
    src/core/sysmon/parsers.cpp
    src/core/sysmon/cache_helpers.cpp
    src/core/sysmon/collector_system.cpp
    src/core/sysmon/collector_devices.cpp
    src/core/sysmon/collector_processes.cpp
    src/core/sysmon/gpu/gpu_manager.cpp
    src/core/sysmon/gpu/drm_helpers.cpp
    src/core/sysmon/gpu/nvidia_nvml_provider.cpp
    src/core/sysmon/sampler.cpp
    src/core/sysmon/serialization.cpp
)
target_include_directories(KeySysmon PUBLIC src/core)
target_link_libraries(KeySysmon PUBLIC Qt6::Core ${CMAKE_DL_LIBS})
target_compile_definitions(KeySysmon PRIVATE
    KEY_CPU_POWER_HELPER="${CMAKE_INSTALL_FULL_LIBEXECDIR}/key-cli/key-cpu-power")

add_executable(key-sysmon src/main.cpp src/commands/sysmon_command.cpp)
target_include_directories(key-sysmon PRIVATE src src/commands)
target_link_libraries(key-sysmon PRIVATE KeySysmon Qt6::Core)
set_target_properties(key-sysmon PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(key-cpu-power src/cpu_power.cpp)
set_target_properties(key-cpu-power PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

install(TARGETS key-sysmon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS key-cpu-power RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/key-cli)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(sysmon_core_tests tests/sysmon_core_test.cpp)
    target_link_libraries(sysmon_core_tests PRIVATE KeySysmon Qt6::Core Qt6::Test)
    add_test(NAME sysmon_core_tests COMMAND sysmon_core_tests)

    qt_add_executable(sysmon_integration_tests tests/sysmon_integration_test.cpp)
    target_link_libraries(sysmon_integration_tests PRIVATE Qt6::Core Qt6::Test)
    target_compile_definitions(sysmon_integration_tests PRIVATE
        KEY_SYSMON_EXECUTABLE="$<TARGET_FILE:key-sysmon>")
    add_dependencies(sysmon_integration_tests key-sysmon)
    add_test(NAME sysmon_integration_tests COMMAND sysmon_integration_tests)
endif()
