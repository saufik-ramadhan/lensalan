cmake_minimum_required(VERSION 3.25)
project(OpenNVR VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENNVR_BUILD_MEDIA "Build the GStreamer feasibility probe" ON)
option(OPENNVR_BUILD_DESKTOP "Build the Qt Quick development console" ON)
set(OPENNVR_ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/onnxruntime-win-x64-gpu-1.23.2"
    CACHE PATH "Development ONNX Runtime SDK (run tools/bootstrap_ai.py)")

add_library(opennvr_core STATIC
    libs/domain/event.cpp
    libs/media/encoded_ring.cpp
)
target_include_directories(opennvr_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
if(MSVC)
    target_compile_options(opennvr_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(opennvr_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(opennvr_core_tests tests/unit/core_tests.cpp)
    target_link_libraries(opennvr_core_tests PRIVATE opennvr_core)
    add_test(NAME core.event_and_ring COMMAND opennvr_core_tests)
endif()

if(OPENNVR_BUILD_MEDIA OR OPENNVR_BUILD_DESKTOP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
        gstreamer-1.0>=1.24 gstreamer-app-1.0 gstreamer-video-1.0)
    add_library(opennvr_media STATIC libs/media/probe.cpp libs/media/event_writer.cpp)
    target_link_libraries(opennvr_media PUBLIC opennvr_core PkgConfig::GSTREAMER)
    target_include_directories(opennvr_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
    add_library(opennvr_detection STATIC libs/detection/person_detector.cpp libs/detection/person_stream.cpp)
    target_include_directories(opennvr_detection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
    target_link_libraries(opennvr_detection PUBLIC PkgConfig::GSTREAMER)
    if(WIN32 AND EXISTS "${OPENNVR_ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
        target_compile_definitions(opennvr_detection PRIVATE OPENNVR_HAS_ONNX=1)
        target_include_directories(opennvr_detection SYSTEM PRIVATE "${OPENNVR_ONNXRUNTIME_DIR}/include")
    endif()
    target_link_libraries(opennvr_media PUBLIC opennvr_detection)
    add_executable(opennvr_probe apps/probe/main.cpp)
    target_link_libraries(opennvr_probe PRIVATE opennvr_media)
    if(BUILD_TESTING)
        add_test(NAME media.synthetic_event COMMAND opennvr_probe --self-test
            --output "${CMAKE_CURRENT_BINARY_DIR}/test-output")
        set_tests_properties(media.synthetic_event PROPERTIES TIMEOUT 45)
    endif()
endif()

if(OPENNVR_BUILD_DESKTOP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
    qt_standard_project_setup(REQUIRES 6.8)
    qt_add_executable(opennvr WIN32 apps/desktop/main.cpp apps/desktop/backend.cpp
        apps/desktop/backend.hpp libs/platform/credentials.cpp libs/platform/credentials.hpp)
    qt_add_qml_module(opennvr URI OpenNVR VERSION 1.0
        QML_FILES apps/desktop/Main.qml)
    target_link_libraries(opennvr PRIVATE opennvr_media opennvr_detection Qt6::Quick Qt6::QuickControls2
        Qt6::Network)
    if(WIN32)
        target_link_libraries(opennvr PRIVATE crypt32)
    endif()
endif()
