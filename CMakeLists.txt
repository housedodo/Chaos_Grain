cmake_minimum_required(VERSION 3.22)
project(ChaosGrain VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

# JUCE is downloaded automatically. To use a local copy instead:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ChaosGrain
    COMPANY_NAME "ChaosGrain"
    PRODUCT_NAME "ChaosGrain"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Chgr
    PLUGIN_CODE Cgr1
    FORMATS VST3 AU Standalone
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(ChaosGrain PRIVATE
    Source/GrainEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UnitView.cpp)

target_compile_definitions(ChaosGrain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ChaosGrain
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(CHAOSGRAIN_TESTS "Build the offline render test" OFF)
if(CHAOSGRAIN_TESTS)
    juce_add_console_app(RenderTest)
    target_sources(RenderTest PRIVATE Tests/RenderTest.cpp)
    # Reuse the plugin's compiled JUCE code; just borrow its include paths/defines.
    target_include_directories(RenderTest PRIVATE $<TARGET_PROPERTY:ChaosGrain,INCLUDE_DIRECTORIES>)
    target_compile_definitions(RenderTest PRIVATE $<TARGET_PROPERTY:ChaosGrain,COMPILE_DEFINITIONS>)
    target_link_libraries(RenderTest PRIVATE ChaosGrain)

    # Records a captioned demo video of the plugin (Linux; needs ffmpeg + a display).
    juce_add_console_app(DemoVideo)
    target_sources(DemoVideo PRIVATE Tests/DemoVideo.cpp)
    target_include_directories(DemoVideo PRIVATE $<TARGET_PROPERTY:ChaosGrain,INCLUDE_DIRECTORIES>)
    target_compile_definitions(DemoVideo PRIVATE $<TARGET_PROPERTY:ChaosGrain,COMPILE_DEFINITIONS>)
    target_link_libraries(DemoVideo PRIVATE ChaosGrain)
endif()
