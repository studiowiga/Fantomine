cmake_minimum_required(VERSION 3.22)

project(Fantomine VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS : binaire universel (Apple Silicon + Intel), compatible macOS 10.13+
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
# Windows : runtime C++ statique, aucun redistribuable Visual C++ à installer
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
if(DEFINED JUCE_LOCAL_PATH)
    add_subdirectory(${JUCE_LOCAL_PATH} JUCE)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Fantomine
    COMPANY_NAME "Studio Wiga"
    COMPANY_WEBSITE ""
    PLUGIN_MANUFACTURER_CODE Swga
    PLUGIN_CODE Fntm
    PRODUCT_NAME "Fantomine"
    DESCRIPTION "Theremin voice for guitar"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    BUNDLE_ID "com.studiowiga.fantomine")

juce_add_binary_data(FantomineAssets SOURCES
    background.png
    title_on.png
    led_on.png
    oct_handle.png
    Inter-SemiBold.otf
    Inter-Regular.otf)

target_sources(Fantomine PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(Fantomine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(Fantomine PRIVATE /utf-8)
endif()

target_link_libraries(Fantomine
    PRIVATE
        FantomineAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
