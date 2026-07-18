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
    Source/WaveformView.cpp
    Source/WaveformView.h
    Source/GridView.cpp
    Source/GridView.h
    Source/MorphPadView.cpp
    Source/MorphPadView.h
    Source/RackView.cpp
    Source/RackView.h
    Source/Nebula2LookAndFeel.cpp
    Source/Nebula2LookAndFeel.h
    Source/Theme.h
    Source/Parameters.cpp
    Source/Parameters.h
    Source/ParameterIDs.h
    Source/GridPresets.cpp
    Source/GridPresets.h
    Source/Presets.cpp
    Source/Presets.h
    Source/MasterProcessor.cpp
    Source/MasterProcessor.h
    Source/Transport.h
    Source/dsp/ParametricEq.cpp
    Source/dsp/ParametricEq.h
    Source/dsp/Saturator.cpp
    Source/dsp/Saturator.h
    Source/dsp/Colour.cpp
    Source/dsp/Colour.h
    Source/dsp/Reverb.cpp
    Source/dsp/Reverb.h
    Source/dsp/Delay.cpp
    Source/dsp/Delay.h
    Source/dsp/DrumSynth.cpp
    Source/dsp/DrumSynth.h
    Source/dsp/TempoDetect.cpp
    Source/dsp/TempoDetect.h
    Source/dsp/Slicer.cpp
    Source/dsp/Slicer.h
    Source/dsp/DrumKit.cpp
    Source/dsp/DrumKit.h
    Source/dsp/ColourChain.cpp
    Source/dsp/ColourChain.h
    Source/dsp/SpaceProcessor.cpp
    Source/dsp/SpaceProcessor.h
    Source/dsp/SampleLayer.cpp
    Source/dsp/SampleLayer.h
    Source/dsp/FxGrid.cpp
    Source/dsp/FxGrid.h
    Source/dsp/StepFx.cpp
    Source/dsp/Resonator.cpp
    Source/dsp/StepFx.h
    Source/dsp/MorphPad.cpp
    Source/dsp/MorphPad.h
    Source/dsp/MorphEngine.cpp
    Source/dsp/MorphEngine.h
    Source/dsp/RackGraph.cpp
    Source/dsp/RackGraph.h
    Source/dsp/RackModules.cpp
    Source/dsp/RackModules.h
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

# Compile sources as UTF-8 on MSVC. Without this, MSVC reads source in the system codepage
# and any non-ASCII literal turns to mojibake in the plugin UI.
#
# This MUST be target_compile_options, not add_compile_options. add_compile_options only
# affects targets created AFTER the call — and juce_add_plugin(Nebula2) is at the top of
# this file, so the flag landed on the TEST BINARY ONLY. The plugin shipped mojibake the
# whole time this comment claimed the bug was fixed; the user found it in the rack's status
# strip ("Main Out â□□ the dry beat").
#
# The lesson: a fix isn't verified until you've observed the thing it fixes, on the
# artefact that actually ships.
if (MSVC)
    target_compile_options(Nebula2 PRIVATE /utf-8)
endif()

# --- Phase 1 test target: parameter round-trip + automation, run via ctest ---
enable_testing()

juce_add_console_app(Nebula2Tests PRODUCT_NAME "Nebula2Tests")

target_sources(Nebula2Tests PRIVATE
    Tests/TestMain.cpp
    Source/Parameters.cpp
    Source/Parameters.h
    Source/ParameterIDs.h
    Source/GridPresets.cpp
    Source/GridPresets.h
    Source/Presets.cpp
    Source/Presets.h
    Source/MasterProcessor.cpp
    Source/MasterProcessor.h
    Source/Transport.h
    Source/dsp/ParametricEq.cpp
    Source/dsp/ParametricEq.h
    Source/dsp/Saturator.cpp
    Source/dsp/Saturator.h
    Source/dsp/Colour.cpp
    Source/dsp/Colour.h
    Source/dsp/Reverb.cpp
    Source/dsp/Reverb.h
    Source/dsp/Delay.cpp
    Source/dsp/Delay.h
    Source/dsp/DrumSynth.cpp
    Source/dsp/DrumSynth.h
    Source/dsp/TempoDetect.cpp
    Source/dsp/TempoDetect.h
    Source/dsp/Slicer.cpp
    Source/dsp/Slicer.h
    Source/dsp/DrumKit.cpp
    Source/dsp/DrumKit.h
    Source/dsp/ColourChain.cpp
    Source/dsp/ColourChain.h
    Source/dsp/SpaceProcessor.cpp
    Source/dsp/SpaceProcessor.h
    Source/dsp/SampleLayer.cpp
    Source/dsp/SampleLayer.h
    Source/dsp/FxGrid.cpp
    Source/dsp/FxGrid.h
    Source/dsp/StepFx.cpp
    Source/dsp/Resonator.cpp
    Source/dsp/StepFx.h
    Source/dsp/MorphPad.cpp
    Source/dsp/MorphPad.h
    Source/dsp/MorphEngine.cpp
    Source/dsp/MorphEngine.h
    Source/dsp/RackGraph.cpp
    Source/dsp/RackGraph.h
    Source/dsp/RackModules.cpp
    Source/dsp/RackModules.h
)

target_compile_definitions(Nebula2Tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(Nebula2Tests PRIVATE
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

add_test(NAME Nebula2Tests COMMAND Nebula2Tests)

if (MSVC)
    target_compile_options(Nebula2Tests PRIVATE /utf-8)
endif()
