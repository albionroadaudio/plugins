#include "Analysis.h"

using namespace spectrum;

AnalysisEngine::AnalysisEngine() : juce::Thread ("Albion Reference analysis")
{
    for (auto& d : fifoData) d.assign ((size_t) fifoSize, 0.0f);
    for (auto& h : history)  h.assign ((size_t) fftSize, 0.0f);
    window.resize ((size_t) fftSize);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) fftSize,
                                                              juce::dsp::WindowingFunction<float>::hann, false);
    float sum = 0.0f;
    for (auto w : window) sum += w;
    windowGain = 2.0f / sum;   // full-scale sine -> 0 dB at its bin

    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
    binPowerA.assign ((size_t) fftSize / 2 + 1, 0.0f);
    binPowerB.assign ((size_t) fftSize / 2 + 1, 0.0f);
    startThread (juce::Thread::Priority::low);
}

AnalysisEngine::~AnalysisEngine()
{
    stopThread (2000);
}

void AnalysisEngine::setSampleRate (double sr)
{
    sampleRate = sr;
}

void AnalysisEngine::push (const float* inL, const float* inR, const float* outL, const float* outR, int n) noexcept
{
    const auto scope = fifo.write (n);
    const float* src[4] = { inL, inR, outL, outR };
    for (int c = 0; c < 4; ++c)
    {
        if (scope.blockSize1 > 0) std::copy (src[c], src[c] + scope.blockSize1, fifoData[(size_t) c].data() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy (src[c] + scope.blockSize1, src[c] + scope.blockSize1 + scope.blockSize2,
                                             fifoData[(size_t) c].data() + scope.startIndex2);
    }
}

void AnalysisEngine::run()
{
    while (! threadShouldExit())
    {
        const auto sr = sampleRate.load();
        if (sr <= 0.0)
        {
            wait (20);
            continue;
        }
        if (! juce::approximatelyEqual (sr, preparedRate))
        {
            mapper.prepare (sr);
            preparedRate = sr;
        }

        const auto ready = fifo.getNumReady();
        if (ready == 0)
        {
            wait (10);
            continue;
        }

        const auto take = juce::jmin (ready, hop - pendingHop);
        const auto scope = fifo.read (take);
        auto copyIn = [this] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                for (int c = 0; c < 4; ++c)
                    history[(size_t) c][(size_t) historyPos] = fifoData[(size_t) c][(size_t) (start + i)];
                historyPos = (historyPos + 1) & (fftSize - 1);
            }
        };
        copyIn (scope.startIndex1, scope.blockSize1);
        copyIn (scope.startIndex2, scope.blockSize2);
        pendingHop += take;

        if (pendingHop >= hop)
        {
            pendingHop = 0;
            analyseFrame();
        }
    }
}

void AnalysisEngine::fftPower (const float* h, int writePos, float* binPower)
{
    // unroll the ring buffer, oldest sample first, and window it
    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] = h[(writePos + i) & (fftSize - 1)] * window[(size_t) i];
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const auto m = fftBuffer[(size_t) k] * windowGain;
        binPower[k] = m * m;
    }
}

void AnalysisEngine::analyseFrame()
{
    std::array<float, numBands> pre {}, post {};

    // pre-EQ input: average L and R power
    fftPower (history[0].data(), historyPos, binPowerA.data());
    fftPower (history[1].data(), historyPos, binPowerB.data());
    for (size_t k = 0; k < binPowerA.size(); ++k)
        binPowerA[k] = 0.5f * (binPowerA[k] + binPowerB[k]);
    mapper.bandsFromBins (binPowerA.data(), pre.data());

    // post-EQ output
    fftPower (history[2].data(), historyPos, binPowerA.data());
    fftPower (history[3].data(), historyPos, binPowerB.data());
    for (size_t k = 0; k < binPowerA.size(); ++k)
        binPowerA[k] = 0.5f * (binPowerA[k] + binPowerB[k]);
    mapper.bandsFromBins (binPowerA.data(), post.data());

    // loudness gate for the long-term averages (time-domain RMS of the input)
    double ms = 0.0;
    for (int c = 0; c < 2; ++c)
        for (auto x : history[(size_t) c])
            ms += (double) x * x;
    ms /= (2.0 * fftSize);
    const bool audible = ms > 1.0e-6;   // -60 dBFS

    const auto hopSeconds = (double) hop / preparedRate;
    const auto a = std::exp (-hopSeconds / 0.3);

    const juce::ScopedLock sl (lock);
    for (int b = 0; b < numBands; ++b)
    {
        displayPre[(size_t) b]  = haveDisplay ? a * displayPre[(size_t) b]  + (1.0 - a) * pre[(size_t) b]  : pre[(size_t) b];
        displayPost[(size_t) b] = haveDisplay ? a * displayPost[(size_t) b] + (1.0 - a) * post[(size_t) b] : post[(size_t) b];
    }
    haveDisplay = true;

    if (audible)
        for (auto& acc : averages)
            if (acc.running)
            {
                for (int b = 0; b < numBands; ++b)
                    acc.sum[(size_t) b] += pre[(size_t) b];
                ++acc.frames;
                acc.seconds += hopSeconds;
            }
}

bool AnalysisEngine::getDisplay (Curve& pre, Curve& post)
{
    const juce::ScopedLock sl (lock);
    if (! haveDisplay)
        return false;
    for (int b = 0; b < numBands; ++b)
    {
        pre[(size_t) b]  = 10.0f * std::log10 ((float) displayPre[(size_t) b]  + 1.0e-14f);
        post[(size_t) b] = 10.0f * std::log10 ((float) displayPost[(size_t) b] + 1.0e-14f);
    }
    return true;
}

void AnalysisEngine::start (Average which)
{
    const juce::ScopedLock sl (lock);
    averages[(size_t) which] = {};
    averages[(size_t) which].running = true;
}

bool AnalysisEngine::stop (Average which, Curve& result)
{
    const juce::ScopedLock sl (lock);
    auto& acc = averages[(size_t) which];
    acc.running = false;
    if (acc.frames < 4)
        return false;
    for (int b = 0; b < numBands; ++b)
        result[(size_t) b] = 10.0f * std::log10 ((float) (acc.sum[(size_t) b] / acc.frames) + 1.0e-14f);
    return true;
}

bool AnalysisEngine::isRunning (Average which) const
{
    const juce::ScopedLock sl (lock);
    return averages[(size_t) which].running;
}

double AnalysisEngine::secondsHeard (Average which) const
{
    const juce::ScopedLock sl (lock);
    return averages[(size_t) which].seconds;
}
