cmake_minimum_required(VERSION 3.22)
project(AlbionSlapback VERSION 0.1.0 LANGUAGES C CXX)

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
juce_add_plugin(AlbionSlapback
    COMPANY_NAME             "Albion Road Audio"
    COMPANY_WEBSITE          "https://www.albionroadaudio.co.uk"
    BUNDLE_ID                "com.albionroadaudio.slapback"
    PLUGIN_MANUFACTURER_CODE Albr
    PLUGIN_CODE              ArSb
    PRODUCT_NAME             "Albion Road Audio Slapback"   # bundle file name
    PLUGIN_NAME              "ARA Slapback"                 # name shown in the host
    DESCRIPTION              "Tape / BBD / digital slapback delay"
    FORMATS                  VST3 Standalone                   # CLAP is added below
    VST3_CATEGORIES          Fx Delay
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE)

target_sources(AlbionSlapback PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/AlbionLookAndFeel.cpp
    src/Presets.cpp)

target_compile_definitions(AlbionSlapback PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AlbionSlapback
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET AlbionSlapback
    CLAP_ID       "com.albionroadaudio.slapback"
    CLAP_FEATURES audio-effect delay stereo mono)

# --- portable Linux release builds (cmake -DALBION_PORTABLE_LINUX=ON) -----------
# Static C++ runtime + pinning a few libc/libm symbols to old versions, so binaries
# built on a new distro also load on older ones (see portable/glibc_compat.c).
option(ALBION_PORTABLE_LINUX "Build release binaries that run on older Linux distributions" OFF)
if(ALBION_PORTABLE_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(AlbionSlapback PRIVATE portable/glibc_compat.c)
    set_source_files_properties(portable/glibc_compat.c PROPERTIES COMPILE_OPTIONS "-fno-lto")
    target_link_options(AlbionSlapback PUBLIC -static-libstdc++ -static-libgcc
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
    add_executable(AlbionSlapbackSnapshot tools/Snapshot.cpp)
    target_include_directories(AlbionSlapbackSnapshot PRIVATE $<TARGET_PROPERTY:AlbionSlapback,INCLUDE_DIRECTORIES>)
    target_compile_definitions(AlbionSlapbackSnapshot PRIVATE $<TARGET_PROPERTY:AlbionSlapback,COMPILE_DEFINITIONS>)
    target_link_libraries(AlbionSlapbackSnapshot PRIVATE AlbionSlapback)
endif()
