cmake_minimum_required(VERSION 3.22)

# SPEC §1: Intel only, macOS 11+. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

project(LENS VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(LENS_BUILD_TESTS "Build Catch2 tests and the headless benchmark" ON)

add_subdirectory(JUCE)

juce_add_plugin(LENS
    PRODUCT_NAME "LENS"
    COMPANY_NAME "Asher"
    BUNDLE_ID com.asher.lens
    PLUGIN_MANUFACTURER_CODE Ashr
    PLUGIN_CODE Lens
    FORMATS AU Standalone
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)

file(GLOB_RECURSE LENS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(LENS PRIVATE ${LENS_SOURCES})

target_include_directories(LENS PRIVATE Source)

file(GLOB LENS_RESOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/*.ttf
    ${CMAKE_CURRENT_SOURCE_DIR}/Resources/coach/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.lenspreset)
juce_add_binary_data(LensData SOURCES ${LENS_RESOURCES})
set_target_properties(LensData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(LENS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(LENS
    PRIVATE
        LensData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(LENS_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    # Tests reuse the plugin's compiled code (and its JUCE build) rather
    # than compiling JUCE a second time.
    function(lens_use_plugin_code target)
        target_link_libraries(${target} PRIVATE LENS)
        target_compile_definitions(${target} PRIVATE
            $<TARGET_PROPERTY:LENS,COMPILE_DEFINITIONS>)
        target_include_directories(${target} PRIVATE
            $<TARGET_PROPERTY:LENS,INCLUDE_DIRECTORIES>
            $<TARGET_PROPERTY:LensData,INTERFACE_INCLUDE_DIRECTORIES>
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests)
        target_compile_definitions(${target} PRIVATE LENS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    endfunction()

    add_library(lens_test_helpers STATIC Tests/helpers/OfflineRender.cpp Tests/helpers/Signals.cpp)
    lens_use_plugin_code(lens_test_helpers)

    file(GLOB LENS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp)
    add_executable(lens_tests ${LENS_TEST_SOURCES})
    lens_use_plugin_code(lens_tests)
    target_link_libraries(lens_tests PRIVATE lens_test_helpers Catch2::Catch2)

    add_executable(lens_bench Tests/bench/main.cpp)
    lens_use_plugin_code(lens_bench)
    target_link_libraries(lens_bench PRIVATE lens_test_helpers)

    add_executable(lens_snapshot Tests/tools/snapshot.cpp)
    lens_use_plugin_code(lens_snapshot)
    target_link_libraries(lens_snapshot PRIVATE lens_test_helpers)

    enable_testing()
    add_test(NAME lens_tests COMMAND lens_tests)
endif()
