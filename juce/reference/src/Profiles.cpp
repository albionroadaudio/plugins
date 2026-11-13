#include "Profiles.h"
#include <juce_dsp/juce_dsp.h>

using namespace spectrum;

namespace profiles
{
    namespace
    {
        // slope in dB/octave around 1 kHz, optional sub and air roll-offs
        Curve slopeCurve (float dbPerOct, bool rollOffs)
        {
            Curve c {};
            for (int b = 0; b < numBands; ++b)
            {
                const auto f = bandHz (b);
                auto db = dbPerOct * std::log2 (f / 1000.0f);
                if (rollOffs)
                {
                    db -= 10.0f * std::log10 (1.0f + std::pow (35.0f / f, 4.0f));      // 2nd-order sub roll-off
                    db -= 10.0f * std::log10 (1.0f + std::pow (f / 16000.0f, 4.0f));   // air roll-off
                }
                c[(size_t) b] = db;
            }
            return normalised (c);
        }

        juce::String safeFileName (const juce::String& name)
        {
            auto s = juce::File::createLegalFileName (name.trim());
            return s.isEmpty() ? juce::String ("Profile") : s;
        }
    }

    std::vector<Profile> builtIns()
    {
        return {
            { "Pink noise (-3 dB/oct)",        slopeCurve (-3.0f, false), true, {} },
            { "Generic bright (-3.5 dB/oct)",  slopeCurve (-3.5f, true),  true, {} },
            { "Generic balanced (-4.5 dB/oct)", slopeCurve (-4.5f, true), true, {} },
            { "Generic warm (-5.5 dB/oct)",    slopeCurve (-5.5f, true),  true, {} },
        };
    }

    juce::File userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Albion Road Audio").getChildFile ("Reference Profiles");
    }

    std::vector<Profile> loadUserProfiles()
    {
        std::vector<Profile> result;
        const auto folder = userFolder();
        if (! folder.isDirectory())
            return result;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.xml", juce::File::findFiles))
            if (auto xml = juce::XmlDocument::parse (entry.getFile()); xml != nullptr && xml->hasTagName ("AlbionReferenceProfile"))
            {
                Profile p;
                p.name = xml->getStringAttribute ("name", entry.getFile().getFileNameWithoutExtension());
                p.file = entry.getFile();
                if (fromString (xml->getStringAttribute ("curve"), p.curve))
                    result.push_back (std::move (p));
            }

        std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
        return result;
    }

    juce::File save (const juce::String& name, const Curve& curve)
    {
        const auto folder = userFolder();
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (safeFileName (name), ".xml", false);

        juce::XmlElement xml ("AlbionReferenceProfile");
        xml.setAttribute ("name", name.trim());
        xml.setAttribute ("version", 1);
        xml.setAttribute ("bands", "120 bands, 1/12 octave from 20 Hz, dB, loudness-normalised (100 Hz-8 kHz mean = 0)");
        xml.setAttribute ("curve", toString (normalised (curve)));
        xml.writeTo (file);
        return file;
    }

    bool analyseFile (const juce::File& file, Curve& result, juce::String& error,
                      std::atomic<float>& progress, const std::function<bool()>& keepGoing)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Can't read " + file.getFileName() + " (supported: WAV, AIFF, FLAC, OGG, MP3)";
            return false;
        }

        BandMapper mapper;
        mapper.prepare (reader->sampleRate);
        juce::dsp::FFT fft (fftOrder);
        std::vector<float> window ((size_t) fftSize), buffer ((size_t) fftSize * 2), power ((size_t) fftSize / 2 + 1), sumPower;
        juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false);
        float wsum = 0.0f;
        for (auto w : window) wsum += w;
        const auto gain = 2.0f / wsum;

        const int channels = juce::jmin (2, (int) reader->numChannels);
        juce::AudioBuffer<float> block (channels, fftSize);
        std::array<double, numBands> sum {};
        std::array<float, numBands> bands {};
        int frames = 0;
        const auto total = reader->lengthInSamples;

        for (juce::int64 pos = 0; pos + fftSize <= total; pos += fftSize / 2)
        {
            if (! keepGoing())
            {
                error = "Cancelled";
                return false;
            }
            reader->read (&block, 0, fftSize, pos, true, channels > 1);

            double ms = 0.0;
            std::fill (power.begin(), power.end(), 0.0f);
            for (int c = 0; c < channels; ++c)
            {
                const auto* x = block.getReadPointer (c);
                for (int i = 0; i < fftSize; ++i)
                {
                    ms += (double) x[i] * x[i];
                    buffer[(size_t) i] = x[i] * window[(size_t) i];
                }
                std::fill (buffer.begin() + fftSize, buffer.end(), 0.0f);
                fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
                for (int k = 0; k <= fftSize / 2; ++k)
                {
                    const auto m = buffer[(size_t) k] * gain;
                    power[(size_t) k] += m * m / (float) channels;
                }
            }
            if (ms / (channels * fftSize) < 1.0e-6)   // skip silence below -60 dBFS
                continue;

            mapper.bandsFromBins (power.data(), bands.data());
            for (int b = 0; b < numBands; ++b)
                sum[(size_t) b] += bands[(size_t) b];
            ++frames;
            progress = (float) ((double) pos / (double) total);
        }

        if (frames < 4)
        {
            error = file.getFileName() + " is too short or silent to analyse";
            return false;
        }
        for (int b = 0; b < numBands; ++b)
            result[(size_t) b] = 10.0f * std::log10 ((float) (sum[(size_t) b] / frames) + 1.0e-14f);
        progress = 1.0f;
        return true;
    }
}
