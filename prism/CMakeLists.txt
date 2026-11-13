cmake_minimum_required(VERSION 3.22)

# PRISM targets one Intel Mac. These must be set before project().
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architecture" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS" FORCE)

project(PRISM VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PRISM_BUILD_TESTS "Build Catch2 tests and the benchmark" ON)
# Linux-only helper: build a VST3 purely so pluginval can exercise the
# processor/editor on a Linux CI box. Never used on the Mac (SPEC: AU only).
option(PRISM_LINUX_VST3_FOR_VALIDATION "Linux only: also build a VST3 for pluginval" OFF)
option(PRISM_ASAN "Build with AddressSanitizer" OFF)

if(PRISM_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

add_subdirectory(JUCE)

if(APPLE)
  set(PRISM_FORMATS AU Standalone)
else()
  set(PRISM_FORMATS Standalone)
  if(PRISM_LINUX_VST3_FOR_VALIDATION)
    list(APPEND PRISM_FORMATS VST3)
  endif()
endif()

juce_add_plugin(PRISM
  COMPANY_NAME "Asher"
  BUNDLE_ID com.asher.prism
  PLUGIN_MANUFACTURER_CODE Ashr
  PLUGIN_CODE Prsm
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth
  FORMATS ${PRISM_FORMATS}
  PRODUCT_NAME "PRISM")

file(GLOB_RECURSE PRISM_FACTORY_PRESETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/Factory/*.prismpreset")
juce_add_binary_data(PrismData
  NAMESPACE PrismBinary
  SOURCES
    Resources/Fonts/Inter-Regular.ttf
    Resources/Fonts/Inter-SemiBold.ttf
    ${PRISM_FACTORY_PRESETS})
set_target_properties(PrismData PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE PRISM_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Params/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/State/*.cpp")
file(GLOB_RECURSE PRISM_GUI_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/GUI/*.cpp")
set(PRISM_PLUGIN_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp")

set(PRISM_COMMON_DEFS
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_FLAC=0
  JUCE_USE_OGGVORBIS=0
  JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)

set(PRISM_JUCE_MODULES
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_audio_formats
  juce::juce_dsp
  juce::juce_gui_extra)

if(MSVC)
  set(PRISM_WARNINGS /W3)
else()
  set(PRISM_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

target_sources(PRISM PRIVATE ${PRISM_CORE_SOURCES} ${PRISM_GUI_SOURCES} ${PRISM_PLUGIN_SOURCES})
target_include_directories(PRISM PRIVATE Source)
target_compile_definitions(PRISM PUBLIC ${PRISM_COMMON_DEFS})
target_compile_options(PRISM PRIVATE ${PRISM_WARNINGS})
target_link_libraries(PRISM
  PRIVATE PrismData ${PRISM_JUCE_MODULES}
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

if(PRISM_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)

  # The core (processor + DSP + GUI) compiled into a static library that links
  # JUCE as an ordinary set of modules. Tests and the benchmark use it.
  add_library(PrismCore STATIC
    ${PRISM_CORE_SOURCES} ${PRISM_GUI_SOURCES} ${PRISM_PLUGIN_SOURCES})
  target_include_directories(PrismCore PUBLIC Source)
  target_compile_definitions(PrismCore PUBLIC
    ${PRISM_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=0
    PRISM_HEADLESS_CORE=1
    JucePlugin_Name="PRISM"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0)
  target_compile_options(PrismCore PRIVATE ${PRISM_WARNINGS})
  target_link_libraries(PrismCore
    PUBLIC PrismData ${PRISM_JUCE_MODULES}
           juce::juce_recommended_config_flags)

  file(GLOB PRISM_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp")
  add_executable(PrismTests ${PRISM_TEST_SOURCES})
  target_link_libraries(PrismTests PRIVATE PrismCore Catch2::Catch2)

  add_executable(PrismBench Tests/bench/Bench.cpp Tests/TestHelpers.cpp)
  target_link_libraries(PrismBench PRIVATE PrismCore)
  target_compile_definitions(PrismBench PRIVATE PRISM_BENCH_NO_CATCH=1)
  target_link_libraries(PrismBench PRIVATE Catch2::Catch2)

  add_executable(PrismTool Tests/tools/PrismTool.cpp Tests/TestHelpers.cpp)
  target_link_libraries(PrismTool PRIVATE PrismCore Catch2::Catch2)

  enable_testing()
  add_test(NAME PrismTests COMMAND PrismTests)
endif()
