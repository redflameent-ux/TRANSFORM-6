cmake_minimum_required(VERSION 3.22)
project(TRANSFORM6 VERSION 1.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    # Static runtime: the plugin works without installing extra Microsoft files
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Use a local JUCE folder if given (-DJUCE_DIR=...), otherwise download it.
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(TRANSFORM6
    COMPANY_NAME "Pathfinder Pressings"
    BUNDLE_ID com.pathfinderpressings.transform6
    PLUGIN_MANUFACTURER_CODE Pfpr
    PLUGIN_CODE Trf6
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "TRANSFORM-6"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    # macOS: the standalone records from the audio input, which needs mic consent
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "TRANSFORM-6 records audio from your chosen input so you can turn it into MIDI."
    VST3_CATEGORIES Fx Analyzer Tools
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(TRANSFORM6 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EngineRunner.cpp)

target_compile_definitions(TRANSFORM6 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TRANSFORM6
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
