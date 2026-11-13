#pragma once

#include "Spectrum.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

// Spectrum analysis off the audio thread.
//
// The audio thread only copies samples into a lock-free FIFO (push()). This
// thread runs 8192-point FFTs every 4096 samples on the pre-EQ input and the
// post-EQ output (L and R power averaged), and keeps:
//   * a ~300 ms smoothed live spectrum of each, for the analyser display;
//   * two long-term averages that can be started/stopped: CAPTURE (a reference
//     playing through the plugin) and LEARN (your mix, for the match EQ).
// Silent passages (below -60 dBFS) are left out of the long-term averages.
class AnalysisEngine final : public juce::Thread
{
public:
    enum Average { capture = 0, learn = 1, numAverages };

    AnalysisEngine();
    ~AnalysisEngine() override;

    void setSampleRate (double sr);   // call from prepareToPlay

    // audio thread, lock-free; drops samples if the analysis thread falls behind
    void push (const float* inL, const float* inR, const float* outL, const float* outR, int n) noexcept;

    // message thread
    bool getDisplay (spectrum::Curve& pre, spectrum::Curve& post);
    void start (Average);
    bool stop (Average, spectrum::Curve& result);   // false if nothing was heard
    bool isRunning (Average) const;
    double secondsHeard (Average) const;

    void run() override;

private:
    void analyseFrame();
    void fftPower (const float* history, int writePos, float* binPower);

    static constexpr int fifoSize = 1 << 16;
    static constexpr int hop = spectrum::fftSize / 2;

    juce::AbstractFifo fifo { fifoSize };
    std::array<std::vector<float>, 4> fifoData;

    std::atomic<double> sampleRate { 0.0 };
    double preparedRate = 0.0;
    spectrum::BandMapper mapper;
    juce::dsp::FFT fft { spectrum::fftOrder };
    std::vector<float> window, fftBuffer, binPowerA, binPowerB;
    std::array<std::vector<float>, 4> history;   // ring buffers, fftSize each
    int historyPos = 0, pendingHop = 0;
    float windowGain = 1.0f;

    // results
    mutable juce::CriticalSection lock;
    std::array<double, spectrum::numBands> displayPre {}, displayPost {};
    bool haveDisplay = false;

    struct Accumulator
    {
        bool running = false;
        std::array<double, spectrum::numBands> sum {};
        int frames = 0;
        double seconds = 0.0;
    };
    std::array<Accumulator, numAverages> averages {};
};
