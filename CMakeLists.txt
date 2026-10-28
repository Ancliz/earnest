cmake_minimum_required(VERSION 3.16)
project(smarthome LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
find_package(nlohmann_json 3.2 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

add_library(configuration src/config/configuration.cpp)
target_compile_definitions(configuration PRIVATE SMARTHOME_PHRASE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/phrases.yaml")
target_link_libraries(configuration PUBLIC nlohmann_json::nlohmann_json PRIVATE PkgConfig::YAML)
target_include_directories(configuration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(command-context src/parser/speech_text.cpp src/parser/device_names.cpp)
target_link_libraries(command-context PUBLIC nlohmann_json::nlohmann_json)
target_include_directories(command-context PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(command-parser src/parser/parser.cpp src/parser/phrase_rules.cpp)
target_link_libraries(command-parser PUBLIC command-context)

add_library(home-assistant-client
    src/home_assistant/home_assistant.cpp
    src/home_assistant/device_aliases.cpp
    src/home_assistant/control_policy.cpp)
target_link_libraries(home-assistant-client PUBLIC command-context Threads::Threads)

add_library(audio-support src/audio/utterance_buffer.cpp)
target_include_directories(audio-support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(app-settings src/app/settings.cpp)
target_link_libraries(app-settings PUBLIC configuration)

foreach(target configuration command-context command-parser home-assistant-client audio-support app-settings)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(BUILD_TESTING)
    add_executable(parser-test tests/parser_test.cpp)
    target_link_libraries(parser-test PRIVATE command-parser configuration)
    add_test(NAME parser COMMAND parser-test)

    add_executable(device-aliases-test tests/device_aliases_test.cpp)
    target_link_libraries(device-aliases-test PRIVATE home-assistant-client command-parser configuration)
    add_test(NAME device-aliases COMMAND device-aliases-test)

    add_executable(phrase-config-test tests/phrase_config_test.cpp)
    target_link_libraries(phrase-config-test PRIVATE command-parser configuration home-assistant-client)
    add_test(NAME phrase-config COMMAND phrase-config-test)

    add_executable(utterance-buffer-test tests/utterance_buffer_test.cpp)
    target_link_libraries(utterance-buffer-test PRIVATE audio-support)
    add_test(NAME utterance-buffer COMMAND utterance-buffer-test)

    add_executable(home-assistant-test tests/home_assistant_test.cpp)
    target_link_libraries(home-assistant-test PRIVATE home-assistant-client command-parser configuration)
    add_test(NAME home-assistant COMMAND home-assistant-test)
    set_tests_properties(home-assistant PROPERTIES TIMEOUT 20)

    add_executable(command-boundaries-test tests/command_boundaries_test.cpp)
    target_link_libraries(command-boundaries-test PRIVATE command-parser configuration app-settings audio-support)
    add_test(NAME command-boundaries COMMAND command-boundaries-test)
endif()

option(SMARTHOME_BUILD_VOICE "Build the Whisper microphone application" ON)
if(SMARTHOME_BUILD_VOICE)
    pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
    include(FetchContent)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG 4979e04f5dcaccb36057e059bbaed8a2f5288315) # v1.8.2
    FetchContent_MakeAvailable(whisper)
    add_executable(voice-test src/app/main.cpp src/audio/microphone.cpp)
    target_compile_options(voice-test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(voice-test PRIVATE home-assistant-client audio-support app-settings command-parser
        whisper PkgConfig::PORTAUDIO Threads::Threads)
endif()
