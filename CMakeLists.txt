cmake_minimum_required(VERSION 3.22)
project(ChaosGrain VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

# Windows: link the C++ runtime statically so the plugin loads on PCs without the
# Visual C++ Redistributable installed (otherwise hosts silently skip it).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE is downloaded automatically. To use a local copy instead:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Two plugins from the same code:
#   ChaosGrain     - instrument: load a sample, play it with MIDI
#   ChaosGrain FX  - effect: mangles live input (freeze, feedback, mix)
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
    FORMATS VST3 AU
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_plugin(ChaosGrainFX
    COMPANY_NAME "ChaosGrain"
    PRODUCT_NAME "ChaosGrain FX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Chgr
    PLUGIN_CODE Cgf1
    FORMATS VST3 AU
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

foreach(target ChaosGrain ChaosGrainFX)
    target_sources(${target} PRIVATE
        Source/GrainEngine.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UnitView.cpp
        Source/KeyDetector.cpp
        Source/Resonator.cpp
        Source/Spectral.cpp
        Source/Evolve.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endforeach()

# HOIST - a small "weather and story" effect: TIDE (sway / pump / storm wobble) and DREAD
# (five stages from bright shimmer to the abyss), ANCHOR to hold the sound.
juce_add_plugin(Hoist
    COMPANY_NAME "ChaosGrain"
    PRODUCT_NAME "Hoist"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Chgr
    PLUGIN_CODE Hst1
    FORMATS VST3 AU
    VST3_CATEGORIES Fx Modulation Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Hoist PRIVATE
    Source/Hoist/HoistDSP.cpp
    Source/Hoist/HoistProcessor.cpp
    Source/Hoist/HoistEditor.cpp)
target_compile_definitions(Hoist PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Hoist
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
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

    # Offline test of the FX build: live input, freeze, feedback stability.
    juce_add_console_app(FxTest)
    target_sources(FxTest PRIVATE Tests/FxTest.cpp)
    target_include_directories(FxTest PRIVATE $<TARGET_PROPERTY:ChaosGrainFX,INCLUDE_DIRECTORIES>)
    target_compile_definitions(FxTest PRIVATE $<TARGET_PROPERTY:ChaosGrainFX,COMPILE_DEFINITIONS>)
    target_link_libraries(FxTest PRIVATE ChaosGrainFX)

    # Records a captioned demo video of ChaosGrain FX (Linux; needs ffmpeg + a display).
    juce_add_console_app(DemoFx)
    target_sources(DemoFx PRIVATE Tests/DemoFx.cpp)
    target_include_directories(DemoFx PRIVATE $<TARGET_PROPERTY:ChaosGrainFX,INCLUDE_DIRECTORIES>)
    target_compile_definitions(DemoFx PRIVATE $<TARGET_PROPERTY:ChaosGrainFX,COMPILE_DEFINITIONS>)
    target_link_libraries(DemoFx PRIVATE ChaosGrainFX)

    # Offline checks + demo recorder for HOIST
    juce_add_console_app(HoistTest)
    target_sources(HoistTest PRIVATE Tests/HoistTest.cpp)
    target_include_directories(HoistTest PRIVATE $<TARGET_PROPERTY:Hoist,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HoistTest PRIVATE $<TARGET_PROPERTY:Hoist,COMPILE_DEFINITIONS>)
    target_link_libraries(HoistTest PRIVATE Hoist)

    juce_add_console_app(HoistDemo)
    target_sources(HoistDemo PRIVATE Tests/HoistDemo.cpp)
    target_include_directories(HoistDemo PRIVATE $<TARGET_PROPERTY:Hoist,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HoistDemo PRIVATE $<TARGET_PROPERTY:Hoist,COMPILE_DEFINITIONS>)
    target_link_libraries(HoistDemo PRIVATE Hoist)

    # Soundtrack kit from one recording: stage loops, ambience, MIDI, demo video
    juce_add_console_app(HoistSoundtrack)
    target_sources(HoistSoundtrack PRIVATE Tests/HoistSoundtrack.cpp)
    target_include_directories(HoistSoundtrack PRIVATE $<TARGET_PROPERTY:Hoist,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HoistSoundtrack PRIVATE $<TARGET_PROPERTY:Hoist,COMPILE_DEFINITIONS>)
    target_link_libraries(HoistSoundtrack PRIVATE Hoist)

    # The game's stage-1 open-sea stems (game_audio/) through HOIST, as seamless loops
    juce_add_console_app(HoistLayers)
    target_sources(HoistLayers PRIVATE Tests/HoistLayers.cpp)
    target_include_directories(HoistLayers PRIVATE $<TARGET_PROPERTY:Hoist,INCLUDE_DIRECTORIES>)
    target_compile_definitions(HoistLayers PRIVATE $<TARGET_PROPERTY:Hoist,COMPILE_DEFINITIONS>)
    target_link_libraries(HoistLayers PRIVATE Hoist)
endif()
