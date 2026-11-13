cmake_minimum_required(VERSION 3.22)
project(AlbionDrums VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- JUCE -------------------------------------------------------------------
set(ALBION_JUCE_TAG "8.0.4" CACHE STRING "JUCE git tag to build against")
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${ALBION_JUCE_TAG}
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# --- CLAP (must come AFTER JUCE) -------------------------------------------
FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        6bde108329ff9fc24404b79b8b397261a75cf6a8)   # pinned known-good
FetchContent_MakeAvailable(clap_juce_extensions)

# --- The plugin -------------------------------------------------------------
juce_add_plugin(AlbionDrums
    COMPANY_NAME             "Albion Road Audio"
    COMPANY_WEBSITE          "https://www.albionroadaudio.co.uk"
    BUNDLE_ID                "com.albionroadaudio.drums"
    PLUGIN_MANUFACTURER_CODE Albr
    PLUGIN_CODE              ArDr
    PRODUCT_NAME             "Albion Road Audio Drums"   # bundle file name
    PLUGIN_NAME              "ARA Drums"                 # name shown in the host
    DESCRIPTION              "Drum kit player for DrumGizmo kits"
    FORMATS                  VST3 Standalone                   # CLAP is added below
    VST3_CATEGORIES          Instrument Drum Sampler
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(AlbionDrums PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/AlbionLookAndFeel.cpp
    src/KitViews.cpp
    src/Kit.cpp
    src/MappedWav.cpp
    src/DrumEngine.cpp)

target_compile_definitions(AlbionDrums PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AlbionDrums
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET AlbionDrums
    CLAP_ID       "com.albionroadaudio.drums"
    CLAP_FEATURES instrument drum sampler stereo)

# --- portable Linux release builds (cmake -DALBION_PORTABLE_LINUX=ON) -----------
# Static C++ runtime + pinning a few libc/libm symbols to old versions, so binaries
# built on a new distro also load on older ones (see portable/glibc_compat.c).
option(ALBION_PORTABLE_LINUX "Build release binaries that run on older Linux distributions" OFF)
if(ALBION_PORTABLE_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(AlbionDrums PRIVATE portable/glibc_compat.c)
    set_source_files_properties(portable/glibc_compat.c PROPERTIES COMPILE_OPTIONS "-fno-lto")
    target_link_options(AlbionDrums PUBLIC -static-libstdc++ -static-libgcc
        "LINKER:--wrap=atan2f"
        "LINKER:--wrap=log10f"
        "LINKER:--wrap=sqrtf"
        "LINKER:--wrap=fmod"
        "LINKER:--wrap=__isoc23_strtol"
        "LINKER:--wrap=__isoc23_strtoll"
        "LINKER:--wrap=__isoc23_strtoul"
        "LINKER:--wrap=__isoc23_strtoull"
        "LINKER:--wrap=__isoc23_sscanf"
        "LINKER:--wrap=__isoc23_vsscanf"
        "LINKER:--wrap=arc4random")
endif()

# --- dev tool: offscreen UI snapshot (cmake -DALBION_BUILD_TOOLS=ON) ----------
option(ALBION_BUILD_TOOLS "Build developer tools" OFF)
if(ALBION_BUILD_TOOLS)
    # Links straight against the plugin's shared-code library, so it renders
    # exactly the code that ships.
    add_executable(AlbionDrumsSnapshot tools/Snapshot.cpp)
    target_include_directories(AlbionDrumsSnapshot PRIVATE $<TARGET_PROPERTY:AlbionDrums,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AlbionDrumsSnapshot PRIVATE $<TARGET_PROPERTY:AlbionDrums,COMPILE_DEFINITIONS>)
    target_link_libraries(AlbionDrumsSnapshot PRIVATE AlbionDrums)

    # Engine check for the DRUMS mixer's per-drum Bleed and Tune (needs a kit on disk).
    add_executable(AlbionDrumsEngineTest tools/EngineTest.cpp)
    target_include_directories(AlbionDrumsEngineTest PRIVATE $<TARGET_PROPERTY:AlbionDrums,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AlbionDrumsEngineTest PRIVATE $<TARGET_PROPERTY:AlbionDrums,COMPILE_DEFINITIONS>)
    target_link_libraries(AlbionDrumsEngineTest PRIVATE AlbionDrums)
endif()
