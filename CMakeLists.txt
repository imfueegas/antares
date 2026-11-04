cmake_minimum_required(VERSION 3.22)

# Runtime estático en Windows: el .vst3 no necesita instalar nada más en el PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(Entonador VERSION 1.0.0 LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Entonador
    COMPANY_NAME "Entonador"
    PLUGIN_MANUFACTURER_CODE Entn
    PLUGIN_CODE Etn1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Entonador Vocal"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Entonador PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Entonador PRIVATE cxx_std_17)

target_compile_definitions(Entonador PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Entonador
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Prueba del motor sin JUCE: cmake --build build --target test_core
add_executable(test_core EXCLUDE_FROM_ALL Tests/test_core.cpp)
target_compile_features(test_core PRIVATE cxx_std_17)
