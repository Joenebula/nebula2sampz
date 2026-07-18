#include "SliceAnalysis.h"
#include "SampleLayer.h"

#include <cmath>

namespace Nebula2
{
    const char* sliceKindName(SliceKind k) noexcept
    {
        switch (k)
        {
            case SliceKind::Kick:  return "kick";
            case SliceKind::Snare: return "snare";
            case SliceKind::Hat:   return "hat";
            case SliceKind::Tonal: return "tonal";
            case SliceKind::Perc:
            default:               return "perc";
        }
    }

    SliceInfo analyseSlice(const float* d, int len, double sampleRate) noexcept
    {
        SliceInfo out;
        if (d == nullptr || len < 8 || sampleRate <= 0.0) return out;

        double rms = 0.0;
        int zc = 0;
        float prev = 0.0f, peak = 0.0f;
        int peakAt = 0;

        for (int k = 0; k < len; ++k)
        {
            const float v = d[k];
            rms += (double) v * v;
            if ((v >= 0.0f) != (prev >= 0.0f)) ++zc;
            prev = v;
            const float av = std::abs(v);
            if (av > peak) { peak = av; peakAt = k; }
        }
        rms = std::sqrt(rms / len);

        // Zero-crossing rate as a rough brightness in Hz.
        const double bright = (double) zc / len * sampleRate * 0.5;

        // Decay: how fast energy falls away from the peak. High = sustained.
        const int mid = juce::jmin(len, peakAt + (int) (len * 0.25));
        double head = 0.0, tail = 0.0;
        for (int k = peakAt; k < mid; ++k) head += (double) d[k] * d[k];
        for (int k = mid; k < len; ++k)    tail += (double) d[k] * d[k];
        const double decay = tail / (head + 1.0e-9);

        // Low-band energy via a crude one-pole: this is what separates a kick from a hat.
        double lp = 0.0, lowE = 0.0;
        for (int k = 0; k < len; ++k) { lp += (d[k] - lp) * 0.02; lowE += lp * lp; }
        lowE = std::sqrt(lowE / len);
        const double lowRatio = lowE / (rms + 1.0e-9);

        out.rms      = (float) rms;
        out.brightHz = (float) bright;
        out.decay    = (float) decay;
        out.lowRatio = (float) lowRatio;

        // The prototype's thresholds, in its order — the order matters, since a slice can
        // satisfy more than one of these.
        if (lowRatio > 0.55 && bright < 900.0)        out.kind = SliceKind::Kick;
        else if (bright > 4200.0 && decay < 0.35)     out.kind = SliceKind::Hat;
        else if (bright > 1400.0 && lowRatio < 0.4)   out.kind = SliceKind::Snare;
        else if (decay > 0.8)                         out.kind = SliceKind::Tonal;
        else                                          out.kind = SliceKind::Perc;

        return out;
    }

    std::vector<SliceInfo> analyseSlices(const juce::AudioBuffer<float>& audio,
                                         const std::vector<int>& sliceStarts,
                                         double sampleRate)
    {
        std::vector<SliceInfo> out;
        if (audio.getNumChannels() <= 0 || sliceStarts.size() < 2) return out;

        const auto* d = audio.getReadPointer(0);
        const int total = audio.getNumSamples();

        for (size_t i = 0; i + 1 < sliceStarts.size(); ++i)
        {
            const int a = juce::jlimit(0, total, sliceStarts[i]);
            const int b = juce::jlimit(0, total, sliceStarts[i + 1]);
            out.push_back(analyseSlice(d + a, b - a, sampleRate));
        }
        return out;
    }

    std::vector<int> musicalSliceOrder(const std::vector<SliceInfo>& info, int count,
                                       juce::Random& rng)
    {
        const int n = juce::jlimit(0, SampleLayer::maxSlices, count);
        if (n <= 1 || (int) info.size() < n)
            return shuffledSliceOrder(n, rng);      // nothing known — be honest, just shuffle

        // Sort the available slices into rough roles.
        std::vector<int> kicks, snares, hats, others;
        for (int i = 0; i < n; ++i)
        {
            switch (info[(size_t) i].kind)
            {
                case SliceKind::Kick:  kicks.push_back(i);  break;
                case SliceKind::Snare: snares.push_back(i); break;
                case SliceKind::Hat:   hats.push_back(i);   break;
                default:               others.push_back(i); break;
            }
        }

        // No drums identified at all: a "musical" arrangement would be a fiction.
        if (kicks.empty() && snares.empty())
            return shuffledSliceOrder(n, rng);

        auto takeRandom = [&rng](std::vector<int>& from) -> int
        {
            if (from.empty()) return -1;
            const int idx = rng.nextInt((int) from.size());
            const int v = from[(size_t) idx];
            from.erase(from.begin() + idx);
            return v;
        };

        // Four steps to the bar at this count; the downbeat of each beat wants a kick, and
        // the backbeats (2 and 4) want a snare. That's the skeleton every breakbeat shares.
        const int stepsPerBeat = juce::jmax(1, n / 4);

        std::vector<int> order((size_t) n, -1);
        std::vector<int> kickPool = kicks, snarePool = snares, hatPool = hats, otherPool = others;

        auto refill = [](std::vector<int>& pool, const std::vector<int>& src)
        {
            if (pool.empty()) pool = src;      // reuse rather than leave a hole
        };

        for (int beat = 0; beat < 4; ++beat)
        {
            const int pos = beat * stepsPerBeat;
            if (pos >= n) break;

            const bool backbeat = (beat % 2) == 1;
            if (backbeat && ! snares.empty())
            {
                refill(snarePool, snares);
                order[(size_t) pos] = takeRandom(snarePool);
            }
            else if (! kicks.empty())
            {
                refill(kickPool, kicks);
                order[(size_t) pos] = takeRandom(kickPool);
            }
        }

        // Everything else: hats and percussion fill the gaps. Tonal slices are in `others`
        // and get placed like anything else — but they're never forced onto a downbeat,
        // which is the part that would sound wrong.
        std::vector<int> fill;
        for (int i : hats)   fill.push_back(i);
        for (int i : others) fill.push_back(i);
        for (int i : kicks)  fill.push_back(i);
        for (int i : snares) fill.push_back(i);

        std::vector<int> fillPool = fill;
        for (int i = 0; i < n; ++i)
        {
            if (order[(size_t) i] >= 0) continue;
            refill(fillPool, fill);
            const int v = takeRandom(fillPool);
            order[(size_t) i] = (v >= 0) ? v : (i % n);
        }

        // Nothing may be left unset, and nothing may point outside the break.
        for (int i = 0; i < n; ++i)
            if (order[(size_t) i] < 0 || order[(size_t) i] >= n) order[(size_t) i] = i;

        return order;
    }
}
