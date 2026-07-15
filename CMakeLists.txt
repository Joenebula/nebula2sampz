cmake_minimum_required(VERSION 3.22)

project(Nebula2VST VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Phase 0: empty instrument plugin that loads and makes no sound.
# COMPANY_NAME / PLUGIN_MANUFACTURER_CODE are placeholders — update before Phase 6 (release).
juce_add_plugin(Nebula2
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE Nbla
    PLUGIN_CODE Neb2
    FORMATS VST3
    PRODUCT_NAME "Nebula2"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(Nebula2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Parameters.cpp
    Source/Parameters.h
    Source/ParameterIDs.h
)

target_compile_definitions(Nebula2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Nebula2
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
