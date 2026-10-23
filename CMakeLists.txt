cmake_minimum_required(VERSION 3.25)
project(LensaLAN VERSION 0.3.0 LANGUAGES CXX)

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
    pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
    add_library(opennvr_storage STATIC libs/storage/event_store.cpp)
    target_link_libraries(opennvr_storage PUBLIC opennvr_core PkgConfig::SQLITE)
    add_library(opennvr_media STATIC libs/media/probe.cpp libs/media/event_writer.cpp)
    target_link_libraries(opennvr_media PUBLIC opennvr_core PkgConfig::GSTREAMER)
    target_include_directories(opennvr_media PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
    add_library(opennvr_detection STATIC libs/detection/person_detector.cpp libs/detection/person_stream.cpp libs/detection/scheduler.cpp)
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
        add_executable(opennvr_storage_tests tests/integration/storage_tests.cpp)
        target_link_libraries(opennvr_storage_tests PRIVATE opennvr_storage)
        add_test(NAME storage.lifecycle_recovery_retention COMMAND opennvr_storage_tests "${CMAKE_CURRENT_BINARY_DIR}/test-output")
        add_test(NAME media.synthetic_event COMMAND opennvr_probe --self-test
            --output "${CMAKE_CURRENT_BINARY_DIR}/test-output")
        set_tests_properties(media.synthetic_event PROPERTIES TIMEOUT 45)
    endif()
endif()

if(OPENNVR_BUILD_DESKTOP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
    qt_standard_project_setup(REQUIRES 6.8)
    add_library(opennvr_platform STATIC libs/platform/credentials.cpp libs/platform/credentials.hpp
        libs/platform/local_pipe.cpp libs/platform/local_pipe.hpp libs/platform/camera_catalog.cpp)
    target_include_directories(opennvr_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
    target_link_libraries(opennvr_platform PUBLIC Qt6::Core Qt6::Network crypt32 advapi32)
    add_library(opennvr_engine_host STATIC libs/engine/host.cpp libs/engine/live_hub.cpp)
    target_link_libraries(opennvr_engine_host PUBLIC opennvr_media opennvr_storage opennvr_platform)
    add_executable(opennvr_engine apps/service/main.cpp)
    target_link_libraries(opennvr_engine PRIVATE opennvr_engine_host)
    if(BUILD_TESTING)
        add_executable(opennvr_multicamera_tests tests/integration/multicamera_tests.cpp)
        target_link_libraries(opennvr_multicamera_tests PRIVATE opennvr_engine_host)
        add_test(NAME engine.multicamera_components COMMAND opennvr_multicamera_tests)
        add_executable(opennvr_pipe_tests tests/integration/pipe_tests.cpp)
        target_link_libraries(opennvr_pipe_tests PRIVATE opennvr_platform)
        add_test(NAME platform.local_pipe COMMAND opennvr_pipe_tests)
        set_tests_properties(platform.local_pipe PROPERTIES TIMEOUT 20)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME engine.synthetic_lifecycle COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_engine.py"
                --engine "$<TARGET_FILE:opennvr_engine>" --probe "$<TARGET_FILE:opennvr_probe>"
                --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/test-output")
            set_tests_properties(engine.synthetic_lifecycle PROPERTIES TIMEOUT 100)
            add_test(NAME engine.multicamera_live_grid COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_multicamera.py"
                --engine "$<TARGET_FILE:opennvr_engine>" --probe "$<TARGET_FILE:opennvr_probe>"
                --desktop "$<TARGET_FILE:opennvr>" --root "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(engine.multicamera_live_grid PROPERTIES TIMEOUT 200)
        endif()
        add_test(NAME desktop.camera_profiles COMMAND "$<TARGET_FILE:opennvr>" --profile-test)
        set_tests_properties(desktop.camera_profiles PROPERTIES TIMEOUT 20 WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    qt_add_executable(opennvr WIN32 apps/desktop/main.cpp apps/desktop/backend.cpp
        apps/desktop/backend.hpp apps/desktop/engine_client.cpp apps/desktop/engine_client.hpp
        apps/desktop/live_video.cpp apps/desktop/live_video.hpp)
    qt_add_qml_module(opennvr URI OpenNVR VERSION 1.0
        QML_FILES apps/desktop/Main.qml)
    target_link_libraries(opennvr PRIVATE opennvr_media opennvr_detection Qt6::Quick Qt6::QuickControls2
        Qt6::Network opennvr_platform)
    if(WIN32)
        target_link_libraries(opennvr PRIVATE crypt32)
    endif()
endif()
