cmake_minimum_required(VERSION 3.22)

# Statische MSVC-Runtime: die VST3 braucht keine Visual-C++-Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(PWKlon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PWKlon
    COMPANY_NAME              "Eigenbau"
    PLUGIN_MANUFACTURER_CODE  Eigb
    PLUGIN_CODE               PwKl
    FORMATS                   VST3
    PRODUCT_NAME              "PW-Klon"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx Dynamics)

target_sources(PWKlon PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PWKlon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PWKlon
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# Optionaler Test-Host (nur zum Pruefen, nicht fuer FL noetig)
option(PWKLON_TESTS "Build test host" OFF)
if(PWKLON_TESTS)
    juce_add_console_app(PWKlonHostTest PRODUCT_NAME "PWKlonHostTest")
    target_sources(PWKlonHostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(PWKlonHostTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PWKlonHostTest PRIVATE juce::juce_audio_processors juce::juce_gui_basics PUBLIC juce::juce_recommended_config_flags)

    juce_add_console_app(PWKlonGuiShot PRODUCT_NAME "PWKlonGuiShot")
    target_sources(PWKlonGuiShot PRIVATE tests/GuiShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(PWKlonGuiShot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="PW-Klon" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(PWKlonGuiShot PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()
