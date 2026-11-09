cmake_minimum_required(VERSION 3.16)
project(earnest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
find_package(nlohmann_json 3.9 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

# Header-only command vocabulary and the parser/executor interfaces every stage shares.
add_library(commands INTERFACE)
target_include_directories(commands INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(commands INTERFACE nlohmann_json::nlohmann_json)

add_library(configuration src/config/configuration.cpp)
target_compile_definitions(configuration PUBLIC SMARTHOME_PHRASE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/phrases.yaml"
                           SMARTHOME_HOME_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/home.yaml")
target_include_directories(configuration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(configuration PUBLIC nlohmann_json::nlohmann_json PRIVATE PkgConfig::YAML)

add_library(regex-parser
    src/parser/regex_parser.cpp
    src/parser/phrase_rules.cpp
    src/parser/device_names.cpp
    src/parser/speech_text.cpp)
target_link_libraries(regex-parser PUBLIC commands)

add_library(home-assistant src/home_assistant/home_assistant.cpp src/home_assistant/control_policy.cpp)
target_link_libraries(home-assistant PUBLIC commands Threads::Threads)

add_library(ollama-parser src/ollama/ollama.cpp src/ollama/ollama_parser.cpp)
target_link_libraries(ollama-parser PUBLIC commands regex-parser spdlog::spdlog Threads::Threads)

add_library(audio src/audio/utterance_buffer.cpp src/audio/audio.cpp)
target_include_directories(audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(app-settings src/app/settings.cpp src/app/logging.cpp)
target_link_libraries(app-settings PUBLIC configuration spdlog::spdlog)

add_library(spotify-executor src/spotify/spotify.cpp)
target_link_libraries(spotify-executor PUBLIC commands spdlog::spdlog Threads::Threads)

add_library(executor-router src/app/executor_router.cpp)
target_link_libraries(executor-router PUBLIC commands spdlog::spdlog)

add_library(command-worker src/app/command_worker.cpp)
target_link_libraries(command-worker PUBLIC commands configuration regex-parser audio spdlog::spdlog Threads::Threads)

foreach(target configuration regex-parser home-assistant spotify-executor executor-router audio app-settings command-worker)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(BUILD_TESTING)
    foreach(name parser control_policy configuration home_assistant spotify utterance_buffer settings command_worker)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_compile_options(${name}_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(${name}_test PRIVATE regex-parser home-assistant spotify-executor executor-router configuration audio app-settings command-worker)
        target_compile_definitions(${name}_test PRIVATE SMARTHOME_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()

    set_tests_properties(home_assistant spotify PROPERTIES TIMEOUT 20)
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
    add_executable(earnest src/app/main.cpp src/audio/microphone.cpp src/audio/portaudio_player.cpp src/speech/whisper_transcriber.cpp src/tts/chatterbox.cpp)
    target_compile_options(earnest PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(earnest PRIVATE regex-parser ollama-parser home-assistant spotify-executor executor-router audio app-settings command-worker
        whisper PkgConfig::PORTAUDIO Threads::Threads)
endif()