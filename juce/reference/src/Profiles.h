#pragma once

#include "Spectrum.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <functional>

// Reference profiles: a name plus a loudness-normalised long-term spectrum.
//
// Built-ins are generic tonal-balance *slopes* (not measured genre data);
// user profiles are XML files in the user's application-data folder so they
// are shared by every project and every instance.
namespace profiles
{
    struct Profile
    {
        juce::String name;
        spectrum::Curve curve {};
        bool builtIn = false;
        juce::File file;
    };

    std::vector<Profile> builtIns();
    juce::File userFolder();
    std::vector<Profile> loadUserProfiles();
    juce::File save (const juce::String& name, const spectrum::Curve&);   // returns the file written

    // Long-term average spectrum of an audio file (WAV, AIFF, FLAC, OGG, MP3).
    bool analyseFile (const juce::File&, spectrum::Curve& result, juce::String& error,
                      std::atomic<float>& progress, const std::function<bool()>& keepGoing);
}
