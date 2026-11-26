cmake_minimum_required(VERSION 3.22)

project(MooniAnalyzer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static runtime: the installed plugin needs no Visual C++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # All sources are UTF-8 (Russian UI text). Without /utf-8 MSVC would read them in the
    # system ANSI code page and corrupt every Cyrillic string.
    add_compile_options(/utf-8 /MP)
endif()

option(MOONI_BUILD_TESTS "Build the DSP / analysis test runner" ON)
option(MOONI_BUILD_PLUGIN "Build the plugin (VST3 + Standalone)" ON)

# ---------------------------------------------------------------------------
# JUCE: use external/JUCE if present (git submodule / local clone), otherwise fetch.
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Embedded fonts (full Cyrillic coverage, SIL OFL)
# ---------------------------------------------------------------------------
juce_add_binary_data(MooniFonts
    HEADER_NAME MooniFonts.h
    NAMESPACE MooniFonts
    SOURCES
        Resources/Fonts/Nunito-Medium.ttf
        Resources/Fonts/Nunito-SemiBold.ttf
        Resources/Fonts/Nunito-ExtraBold.ttf
        Resources/Fonts/Unbounded-Bold.ttf)

set(MOONI_ENGINE_SOURCES
    Source/dsp/ParametricEQ.cpp
    Source/dsp/StereoImager.cpp
    Source/dsp/Maximizer.cpp
    Source/dsp/LoudnessMeter.cpp
    Source/analysis/Accumulator.cpp
    Source/analysis/Report.cpp
    Source/analysis/Diagnostics.cpp
    Source/analysis/LoudnessPlanner.cpp
    Source/analysis/AnalysisEngine.cpp
    Source/analysis/ReferenceTrack.cpp)

set(MOONI_PLUGIN_SOURCES
    Source/core/Params.cpp
    Source/core/PluginProcessor.cpp
    Source/core/History.cpp
    Source/gui/Theme.cpp
    Source/gui/MooniCharacter.cpp
    Source/gui/MooniBrain.cpp
    Source/gui/World.cpp
    Source/gui/Widgets.cpp
    Source/gui/EqGraph.cpp
    Source/gui/Rooms.cpp
    Source/gui/RoomsMore.cpp
    Source/gui/PluginEditor.cpp)

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
if(MOONI_BUILD_PLUGIN)
juce_add_plugin(MooniAnalyzer
    COMPANY_NAME "Ernomoon"
    COMPANY_WEBSITE "https://github.com/ernomoon"
    BUNDLE_ID com.ernomoon.mooni-analyzer
    PLUGIN_MANUFACTURER_CODE Ernm
    PLUGIN_CODE Mooa
    FORMATS VST3 Standalone
    PRODUCT_NAME "Mooni Analyzer"
    DESCRIPTION "AI-style mastering assistant by Ernomoon"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Analyzer Mastering
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MooniAnalyzer PRIVATE ${MOONI_ENGINE_SOURCES} ${MOONI_PLUGIN_SOURCES})
target_include_directories(MooniAnalyzer PRIVATE Source)

target_compile_definitions(MooniAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    MOONI_VERSION_STRING="${PROJECT_VERSION}")

if(UNIX AND NOT APPLE)
    # Linux build is used for headless verification: no ALSA/JACK headers needed.
    target_compile_definitions(MooniAnalyzer PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
endif()

target_link_libraries(MooniAnalyzer
    PRIVATE
        MooniFonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# Exact float comparisons are deliberate (flat-top clip detection, exact state checks).
# Source-file options come after JUCE's interface warning flags, so this one wins.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(${MOONI_ENGINE_SOURCES} ${MOONI_PLUGIN_SOURCES} Tests/TestMain.cpp Tests/Shots.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()

# ---------------------------------------------------------------------------
# Test runner (headless): DSP + analysis engine
# ---------------------------------------------------------------------------
if(MOONI_BUILD_TESTS)
    juce_add_console_app(MooniTests PRODUCT_NAME "MooniTests")
    target_sources(MooniTests PRIVATE Tests/TestMain.cpp ${MOONI_ENGINE_SOURCES})
    target_include_directories(MooniTests PRIVATE Source)
    target_compile_definitions(MooniTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1)
    target_link_libraries(MooniTests PRIVATE
        juce::juce_core juce::juce_data_structures juce::juce_audio_basics juce::juce_audio_formats juce::juce_dsp
        juce::juce_events
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Headless UI screenshot harness (Linux verification only)
# ---------------------------------------------------------------------------
option(MOONI_BUILD_SHOTS "Build the headless UI screenshot harness" OFF)
if(MOONI_BUILD_SHOTS)
    juce_add_console_app(MooniShots PRODUCT_NAME "MooniShots")
    target_sources(MooniShots PRIVATE Tests/Shots.cpp ${MOONI_ENGINE_SOURCES} ${MOONI_PLUGIN_SOURCES})
    target_include_directories(MooniShots PRIVATE Source)
    target_compile_definitions(MooniShots PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_FLAC=1
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_MODAL_LOOPS_PERMITTED=1 MOONI_VERSION_STRING="${PROJECT_VERSION}" JucePlugin_Name="Mooni Analyzer")
    target_link_libraries(MooniShots PRIVATE MooniFonts juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
