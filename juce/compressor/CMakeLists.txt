cmake_minimum_required(VERSION 3.22)
project(Albion76 VERSION 0.1.0 LANGUAGES C CXX)

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
# Pinned to a known-good commit for reproducible builds.
FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        6bde108329ff9fc24404b79b8b397261a75cf6a8)
FetchContent_MakeAvailable(clap_juce_extensions)

# --- The plugin -------------------------------------------------------------
juce_add_plugin(Albion76
    COMPANY_NAME             "Albion Road Audio"
    COMPANY_WEBSITE          "https://www.albionroadaudio.co.uk"
    BUNDLE_ID                "com.albionroadaudio.albion76"
    PLUGIN_MANUFACTURER_CODE Albr
    PLUGIN_CODE              Ar76
    PRODUCT_NAME             "Albion Road Audio 76 Compressor"   # bundle file name
    PLUGIN_NAME              "ARA 76 Compressor"                 # name shown in the host
    DESCRIPTION              "FET-style compressor"
    FORMATS                  VST3 Standalone                   # CLAP is added below
    VST3_CATEGORIES          Fx Dynamics
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(Albion76 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/GrMeterComponent.cpp
    src/AlbionLookAndFeel.cpp
    src/Presets.cpp)

target_compile_definitions(Albion76 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Albion76
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET Albion76
    CLAP_ID       "com.albionroadaudio.albion76"
    CLAP_FEATURES audio-effect compressor stereo mono)

# --- portable Linux release builds (cmake -DALBION_PORTABLE_LINUX=ON) -----------
# Static C++ runtime + pinning a few libc/libm symbols to old versions, so binaries
# built on a new distro also load on older ones (see portable/glibc_compat.c).
option(ALBION_PORTABLE_LINUX "Build release binaries that run on older Linux distributions" OFF)
if(ALBION_PORTABLE_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(Albion76 PRIVATE portable/glibc_compat.c)
    set_source_files_properties(portable/glibc_compat.c PROPERTIES COMPILE_OPTIONS "-fno-lto")
    target_link_options(Albion76 PUBLIC -static-libstdc++ -static-libgcc
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
    add_executable(Albion76Snapshot tools/Snapshot.cpp)
    target_include_directories(Albion76Snapshot PRIVATE $<TARGET_PROPERTY:Albion76,INCLUDE_DIRECTORIES>)
    target_compile_definitions(Albion76Snapshot PRIVATE $<TARGET_PROPERTY:Albion76,COMPILE_DEFINITIONS>)
    target_link_libraries(Albion76Snapshot PRIVATE Albion76)
endif()
