cmake_minimum_required(VERSION 3.20)

# All executables land in <build>/bin for every configuration (the setup
# embeds bin/AudioWatchdog.exe). The $<1:...> generator expression stops
# multi-config generators from appending a per-configuration subfolder.

# Single-config generators (Ninja, NMake) default to Release. Multi-config
# generators (Visual Studio) ignore this and use --config.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

project(AudioWatchdog
    VERSION 1.1.0
    DESCRIPTION "Prevents Windows audio endpoints from using exclusive mode"
    LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
                        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                        $<$<COMPILE_LANGUAGE:CXX>:/EHsc> $<$<COMPILE_LANGUAGE:CXX>:/sdl>)
    add_compile_definitions(_UNICODE UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra)
endif()

add_compile_definitions(_WIN32_WINNT=0x0A00)

# Single source of truth for the version (application, service, installer).
set(AW_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(src/version.h.in "${AW_GENERATED_DIR}/version.h" @ONLY)
file(WRITE "${AW_GENERATED_DIR}/payload.rc"
    "IDR_PAYLOAD RCDATA \"${CMAKE_BINARY_DIR}/bin/AudioWatchdog.exe\"
")

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
    src/audio/FormatManager.cpp
    src/audio/AudioFormatStore.cpp
    src/audio/AudioDeviceWatcher.cpp
    src/core/WatchdogEngine.cpp
    src/service/AudioWatchdogService.cpp
    src/cli/Cli.cpp
    src/cli/Diagnostic.cpp
)

target_include_directories(audiowatchdog_core PUBLIC src "${AW_GENERATED_DIR}")
target_link_libraries(audiowatchdog_core PUBLIC
    ole32
    oleaut32
    propsys
    advapi32
    user32
    shell32
)

# Main executable: tray app (default), Windows service (--service) and CLI.
add_executable(AudioWatchdog WIN32
    src/main.cpp
    src/app/TrayApp.cpp
    resources/app.rc
    resources/app.manifest)
target_include_directories(AudioWatchdog PRIVATE resources "${AW_GENERATED_DIR}")
target_link_libraries(AudioWatchdog PRIVATE audiowatchdog_core comctl32)
set_target_properties(AudioWatchdog PROPERTIES
    OUTPUT_NAME "AudioWatchdog"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")

enable_testing()

add_executable(audiowatchdog_tests
    tests/test_main.cpp
    tests/test_harness.cpp
    tests/test_strutil.cpp
    tests/test_config.cpp
    tests/test_logger.cpp
    tests/test_exclusivemode.cpp
    tests/test_format.cpp
    tests/test_watchdog.cpp
)
target_link_libraries(audiowatchdog_tests PRIVATE audiowatchdog_core)
target_include_directories(audiowatchdog_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(audiowatchdog_tests PROPERTIES
    OUTPUT_NAME "audiowatchdog_tests"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")

add_test(NAME audiowatchdog_tests COMMAND audiowatchdog_tests)

# Self-contained setup: embeds AudioWatchdog.exe as RCDATA, requires
# elevation, installs the service + tray app; doubles as Uninstall.exe.
add_executable(AudioWatchdogSetup WIN32
    installer/SetupMain.cpp
    installer/setup.rc
    installer/setup.manifest)
target_link_options(AudioWatchdogSetup PRIVATE
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
target_include_directories(AudioWatchdogSetup PRIVATE
    resources
    installer
    "${AW_GENERATED_DIR}"
    "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(AudioWatchdogSetup PRIVATE audiowatchdog_core comctl32)
add_dependencies(AudioWatchdogSetup AudioWatchdog)
# The payload is read by rc.exe; rebuild the setup whenever it changes.
set_source_files_properties(installer/setup.rc PROPERTIES
    OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/bin/AudioWatchdog.exe")
set_target_properties(AudioWatchdogSetup PROPERTIES
    OUTPUT_NAME "AudioWatchdog-Setup"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")

install(TARGETS AudioWatchdog RUNTIME DESTINATION .)
