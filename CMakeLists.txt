cmake_minimum_required(VERSION 3.22)
# Deployment target and architectures must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
project(ShepardToneBuilder VERSION 0.2.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(STB_BUILD_PLUGIN "Build the plug-in (needs JUCE)" ON)
set(STB_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# core: no JUCE
add_library(stb_core STATIC core/Tonal.cpp core/Tempo.cpp core/Midi.cpp)
target_include_directories(stb_core PUBLIC core)
set_target_properties(stb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(stb_core PRIVATE -ffp-contract=off -Wall -Wextra)

add_executable(shepard_cli cli/shepard_main.cpp)
target_link_libraries(shepard_cli PRIVATE stb_core)
add_executable(stb_tests tests/core_test.cpp)
target_link_libraries(stb_tests PRIVATE stb_core)

# ---- plug-in ----------------------------------------------------------------
if(STB_BUILD_PLUGIN)
    if(STB_JUCE_PATH)
        add_subdirectory(${STB_JUCE_PATH} juce)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(juce)
    endif()

    set(STB_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

    # Shepard Tone Builder: instrument (Logic: Software Instrument -> AU Instruments -> Teilormusic)
    juce_add_plugin(ShepardToneBuilder
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.shepardtonebuilder"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE Shpd
        FORMATS AU VST3 Standalone
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        PRODUCT_NAME "Shepard Tone Builder"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Tools)
    target_sources(ShepardToneBuilder PRIVATE
        plugin/Processor.cpp plugin/Editor.cpp plugin/Look.cpp)
    target_include_directories(ShepardToneBuilder PRIVATE plugin)
    target_compile_definitions(ShepardToneBuilder PUBLIC ${STB_DEFS})
    target_link_libraries(ShepardToneBuilder
        PRIVATE stb_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endif()

# ---- plug-in tests: state, preview, drag file and editor snapshots (needs a display) ----
option(STB_BUILD_TESTS "Build the plug-in test runner" OFF)
if(STB_BUILD_PLUGIN AND STB_BUILD_TESTS)
    juce_add_console_app(StbPluginTests PRODUCT_NAME "StbPluginTests")
    target_sources(StbPluginTests PRIVATE tests/plugin_test.cpp
        plugin/Processor.cpp plugin/Editor.cpp plugin/Look.cpp)
    target_include_directories(StbPluginTests PRIVATE plugin)
    target_compile_definitions(StbPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Shepard Tone Builder" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        STB_NO_PLUGIN_ENTRY=1)
    target_link_libraries(StbPluginTests PRIVATE stb_core juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
