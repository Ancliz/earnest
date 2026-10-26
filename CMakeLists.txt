cmake_minimum_required(VERSION 3.16)
project(smarthome LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(nlohmann_json 3.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML REQUIRED IMPORTED_TARGET yaml-0.1)
add_library(command-parser
    src/parser/parser.cpp
    src/parser/speech_text.cpp
    src/parser/device_names.cpp
    src/parser/phrase_rules.cpp
    src/parser/config_yaml.cpp
    src/home_assistant/control_policy.cpp)
target_compile_definitions(command-parser PRIVATE SMARTHOME_PHRASE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/phrases.yaml")
target_link_libraries(command-parser PUBLIC nlohmann_json::nlohmann_json PRIVATE PkgConfig::YAML)
target_include_directories(command-parser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(command-parser PRIVATE -Wall -Wextra -Wpedantic)

add_library(device-aliases src/home_assistant/device_aliases.cpp)
target_link_libraries(device-aliases PUBLIC command-parser)
target_include_directories(device-aliases PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(device-aliases PRIVATE -Wall -Wextra -Wpedantic)

add_library(voice-support src/app/settings.cpp src/audio/utterance_buffer.cpp)
target_link_libraries(voice-support PUBLIC command-parser)
target_compile_options(voice-support PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(parser-test tests/parser_test.cpp)
    target_link_libraries(parser-test PRIVATE command-parser)
    add_test(NAME parser COMMAND parser-test)
    add_executable(device-aliases-test tests/device_aliases_test.cpp)
    target_link_libraries(device-aliases-test PRIVATE device-aliases command-parser)
    add_test(NAME device-aliases COMMAND device-aliases-test)
    add_executable(phrase-config-test tests/phrase_config_test.cpp)
    target_link_libraries(phrase-config-test PRIVATE command-parser device-aliases)
    add_test(NAME phrase-config COMMAND phrase-config-test)
    add_executable(utterance-buffer-test tests/utterance_buffer_test.cpp)
    target_link_libraries(utterance-buffer-test PRIVATE voice-support)
    add_test(NAME utterance-buffer COMMAND utterance-buffer-test)
endif()

option(SMARTHOME_BUILD_VOICE "Build the Whisper microphone application" ON)
if(SMARTHOME_BUILD_VOICE)
    find_package(Threads REQUIRED)
    pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
    include(FetchContent)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG 4979e04f5dcaccb36057e059bbaed8a2f5288315) # v1.8.2
    FetchContent_MakeAvailable(whisper)
    add_library(home-assistant-client src/home_assistant/home_assistant.cpp)
    target_link_libraries(home-assistant-client PUBLIC command-parser device-aliases Threads::Threads)
    target_compile_options(home-assistant-client PRIVATE -Wall -Wextra -Wpedantic)

    if(BUILD_TESTING)
        add_executable(home-assistant-test tests/home_assistant_test.cpp)
        target_link_libraries(home-assistant-test PRIVATE home-assistant-client)
        add_test(NAME home-assistant COMMAND home-assistant-test)
        set_tests_properties(home-assistant PROPERTIES TIMEOUT 20)
    endif()

    add_executable(voice-test src/app/main.cpp src/audio/microphone.cpp)
    target_compile_options(voice-test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(voice-test PRIVATE home-assistant-client voice-support command-parser device-aliases whisper PkgConfig::PORTAUDIO Threads::Threads)
endif()
