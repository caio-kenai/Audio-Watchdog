cmake_minimum_required(VERSION 3.20)

project(AudioWatchdog
    VERSION 1.0.0
    DESCRIPTION "Prevents Windows audio endpoints from using exclusive mode"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W3 /utf-8 /Zc:__cplusplus /permissive- /EHsc /D_UNICODE /DUNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra)
endif()

add_compile_definitions(_WIN32_WINNT=0x0A00)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)

add_library(audiowatchdog_core STATIC
    src/util/StrUtil.cpp
    src/util/Paths.cpp
    src/util/ServiceUtil.cpp
    src/logging/Logger.cpp
    src/logging/EventLog.cpp
    src/config/Config.cpp
    src/audio/CoreAudioDeviceLister.cpp
    src/audio/ExclusiveModeManager.cpp
    src/audio/ExclusiveModeStore.cpp
    src/audio/AudioDeviceWatcher.cpp
    src/core/WatchdogEngine.cpp
    src/service/AudioWatchdogService.cpp
    src/cli/Cli.cpp
    src/cli/Diagnostic.cpp
)

target_include_directories(audiowatchdog_core PUBLIC src)
target_link_libraries(audiowatchdog_core PUBLIC
    ole32
    oleaut32
    propsys
    mmdevapi
    avrt
    advapi32
    user32
    ntdll
)

if(WIN32)
    add_executable(AudioWatchdog WIN32 src/main.cpp src/app/TrayApp.cpp resources/app.rc)
    set_source_files_properties(resources/app.rc PROPERTIES
        COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
    target_include_directories(AudioWatchdog PRIVATE resources)
else()
    add_executable(AudioWatchdog src/main.cpp src/app/TrayApp.cpp)
endif()
target_link_libraries(AudioWatchdog PRIVATE audiowatchdog_core shell32)
set_target_properties(AudioWatchdog PROPERTIES
    OUTPUT_NAME "AudioWatchdog"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

enable_testing()

add_executable(audiowatchdog_tests
    tests/test_main.cpp
    tests/test_harness.cpp
    tests/test_strutil.cpp
    tests/test_config.cpp
    tests/test_logger.cpp
    tests/test_exclusivemode.cpp
    tests/test_watchdog.cpp
)
target_link_libraries(audiowatchdog_tests PRIVATE audiowatchdog_core)
target_include_directories(audiowatchdog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(audiowatchdog_tests PROPERTIES
    OUTPUT_NAME "audiowatchdog_tests"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_test(NAME audiowatchdog_tests COMMAND audiowatchdog_tests)

if(WIN32)
    # Self-contained setup executable: embeds AudioWatchdog.exe as RCDATA,
    # requires elevation, installs the app and creates the logs folder.
    add_executable(AudioWatchdogSetup WIN32
        installer/SetupMain.cpp
        installer/setup.rc)
    target_link_options(AudioWatchdogSetup PRIVATE
        "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    set_source_files_properties(installer/setup.rc PROPERTIES
        COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\" /I\"${CMAKE_BINARY_DIR}/bin\"")
    target_include_directories(AudioWatchdogSetup PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources
        ${CMAKE_CURRENT_SOURCE_DIR}/installer)
    target_link_libraries(AudioWatchdogSetup PRIVATE ole32 oleaut32 shell32 advapi32 user32)
    add_dependencies(AudioWatchdogSetup AudioWatchdog)
    set_target_properties(AudioWatchdogSetup PROPERTIES
        OUTPUT_NAME "AudioWatchdogSetup"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_custom_command(TARGET AudioWatchdogSetup POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/bin/AudioWatchdogSetup.exe"
            "${CMAKE_CURRENT_SOURCE_DIR}/installer/AudioWatchdogSetup.exe")
endif()

install(TARGETS AudioWatchdog RUNTIME DESTINATION .)