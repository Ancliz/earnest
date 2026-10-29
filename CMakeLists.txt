cmake_minimum_required(VERSION 3.16)
project(smarthome LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
find_package(nlohmann_json 3.9 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)

# Header-only command vocabulary and the parser/executor interfaces every stage shares.
add_library(commands INTERFACE)
target_include_directories(commands INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(commands INTERFACE nlohmann_json::nlohmann_json)

add_library(configuration src/config/configuration.cpp)
target_compile_definitions(configuration PUBLIC SMARTHOME_PHRASE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/phrases.yaml")
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

add_library(ollama-parser src/ollama/ollama.cpp)
target_link_libraries(ollama-parser PUBLIC commands regex-parser Threads::Threads)

add_library(audio src/audio/utterance_buffer.cpp)
target_include_directories(audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(app-settings src/app/settings.cpp)
target_link_libraries(app-settings PUBLIC configuration)

foreach(target configuration regex-parser home-assistant audio app-settings)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

if(BUILD_TESTING)
    foreach(name parser control_policy configuration home_assistant utterance_buffer settings)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_compile_options(${name}_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(${name}_test PRIVATE regex-parser home-assistant configuration audio app-settings)
        add_test(NAME ${name} COMMAND ${name}_test)
    endforeach()

    set_tests_properties(home_assistant PROPERTIES TIMEOUT 20)
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
    add_executable(voice-test src/app/main.cpp src/audio/microphone.cpp src/speech/whisper_transcriber.cpp)
    target_compile_options(voice-test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(voice-test PRIVATE regex-parser ollama-parser home-assistant audio app-settings
        whisper PkgConfig::PORTAUDIO Threads::Threads)
endif()