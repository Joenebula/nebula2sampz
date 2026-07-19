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

# RELEASE IDENTITY — SETTLED 2026-07-19 by the user. Do NOT change the two codes.
#
# The VST3 class ID is built from PLUGIN_MANUFACTURER_CODE + PLUGIN_CODE and NOTHING ELSE.
# Verified, not assumed: the generated moduleinfo.json CID ends
# ...4E626C61 4E656232, which is "Nbla" "Neb2" in ASCII, and it was byte-identical before and
# after COMPANY_NAME changed from "YourCompany" to "Nebula Audio".
#
# Two consequences, and the second corrects a claim this file used to make:
#   - Changing either CODE changes the plugin's identity, and every project saved against
#     the old one shows Nebula2 as a missing plugin. So they are frozen.
#   - Changing COMPANY_NAME does NOT. This comment previously warned that setting the
#     release identity would orphan saved projects; that was true only of the codes, and
#     the codes were already their final values. The vendor string is display text.
#
# Codes kept as chosen: four ASCII characters, uppercase first letter - what JUCE requires,
# and what Audio Units would need if this is ever built for macOS.
juce_add_plugin(Nebula2
    COMPANY_NAME "Nebula Audio"
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
    Source/EqCurve.cpp
    Source/RackView.h
    Source/RackLayout.h
    Source/Nebula2LookAndFeel.cpp
    Source/Nebula2LookAndFeel.h
    Source/Theme.h
    Source/Theme.cpp
    Source/Parameters.cpp
    Source/Parameters.h
    Source/ParameterIDs.h
    Source/MidiLearn.cpp
    Source/MidiLearn.h
    Source/History.cpp
    Source/History.h
    Source/Randomise.cpp
    Source/Randomise.h
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
    Source/dsp/TempoDetect.cpp
    Source/dsp/TempoDetect.h
    Source/dsp/Slicer.cpp
    Source/dsp/Slicer.h
    Source/dsp/ColourChain.cpp
    Source/dsp/ColourChain.h
    Source/dsp/SpaceProcessor.cpp
    Source/dsp/SpaceProcessor.h
    Source/dsp/SampleLayer.cpp
    Source/dsp/SampleLayer.h
    Source/dsp/FxGrid.cpp
    Source/dsp/FxGrid.h
    Source/dsp/StepFx.cpp
    Source/dsp/AmpShape.cpp
    Source/dsp/AmpShape.h
    Source/dsp/SliceAnalysis.cpp
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

# --- embedded typefaces ---
#
# The Audio UI Kit specifies Chakra Petch and IBM Plex Mono. A plugin cannot fetch Google
# Fonts at runtime the way the design's HTML does - there is no browser and no network - so
# the faces are compiled into the binary and registered at startup. Without this the UI
# silently falls back to whatever the OS has, which is how it ends up looking nothing like
# the design while every colour is technically correct.
#
# Both families are SIL Open Font License, which permits embedding. The licences ship
# alongside them in Resources/fonts/ rather than living only in a commit message.
juce_add_binary_data(Nebula2Fonts SOURCES
    Resources/fonts/ChakraPetch-Regular.ttf
    Resources/fonts/ChakraPetch-Medium.ttf
    Resources/fonts/ChakraPetch-SemiBold.ttf
    Resources/fonts/ChakraPetch-Bold.ttf
    Resources/fonts/IBMPlexMono-Regular.ttf
    Resources/fonts/IBMPlexMono-Medium.ttf
)

target_link_libraries(Nebula2
    PRIVATE
        Nebula2Fonts
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
    Source/MidiLearn.cpp
    Source/MidiLearn.h
    Source/History.cpp
    Source/History.h
    Source/Randomise.cpp
    Source/Randomise.h
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
    Source/dsp/TempoDetect.cpp
    Source/dsp/TempoDetect.h
    Source/dsp/Slicer.cpp
    Source/dsp/Slicer.h
    Source/dsp/ColourChain.cpp
    Source/dsp/ColourChain.h
    Source/dsp/SpaceProcessor.cpp
    Source/dsp/SpaceProcessor.h
    Source/dsp/SampleLayer.cpp
    Source/dsp/SampleLayer.h
    Source/dsp/FxGrid.cpp
    Source/dsp/FxGrid.h
    Source/dsp/StepFx.cpp
    Source/dsp/AmpShape.cpp
    Source/dsp/AmpShape.h
    Source/dsp/SliceAnalysis.cpp
    Source/dsp/Resonator.cpp
    Source/dsp/StepFx.h
    Source/dsp/MorphPad.cpp
    Source/dsp/MorphPad.h
    Source/dsp/MorphEngine.cpp
    Source/dsp/MorphEngine.h
    Source/dsp/RackGraph.cpp
    Source/dsp/RackGraph.h
    Source/dsp/RackModules.cpp
    Source/RackLayout.h
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
