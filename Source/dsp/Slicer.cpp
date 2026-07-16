#include "Slicer.h"

#include <cmath>
#include <algorithm>

namespace Nebula2
{
    int snapZero(const float* d, int n, int idx, int radius)
    {
        if (d == nullptr || n <= 0) return 0;
        idx = std::max(0, std::min(n - 1, idx));

        int best = idx;
        float bm = std::abs(d[idx]);

        for (int o = 0; o <= radius; ++o)
        {
            for (const int dir : { -1, 1 })
            {
                const int i = idx + dir * o;
                if (i <= 0 || i >= n) continue;
                if ((d[i - 1] <= 0.0f && d[i] > 0.0f) || (d[i - 1] >= 0.0f && d[i] < 0.0f))
                    return i;                                   // a real crossing wins outright
                const float m = std::abs(d[i]);
                if (m < bm) { bm = m; best = i; }
            }
        }
        return best;
    }

    std::vector<int> computeGridSlices(const float* d, int n, int numSlices, int snapRadius)
    {
        std::vector<int> starts;
        if (d == nullptr || n <= 0) return starts;

        const int num = std::max(1, numSlices);
        starts.reserve((size_t) num + 1);

        for (int i = 0; i <= num; ++i)
        {
            int b = (int) std::lround((double) i / (double) num * (double) n);
            if (i == 0)            b = 0;
            else if (i == num)     b = n;
            else                   b = snapZero(d, n, b, snapRadius);

            if (! starts.empty()) b = std::max(b, starts.back() + 1);
            starts.push_back(std::min(b, n));
        }
        return starts;
    }

    std::vector<int> detectTransients(const float* d, int n, double sr, float sensitivity)
    {
        if (d == nullptr || n <= 0) return {};

        const int hop = std::max(64, (int) (sr * 0.005));
        const int win = hop * 2;
        if (n <= win) return { 0, n };                          // too short to analyse

        const int nf = std::max(1, (n - win) / hop);

        std::vector<float> env((size_t) nf, 0.0f);
        for (int f = 0; f < nf; ++f)
        {
            double s = 0.0;
            const int st = f * hop;
            for (int i = 0; i < win; ++i) { const float v = d[st + i]; s += (double) v * v; }
            env[(size_t) f] = (float) std::sqrt(s / (double) win);
        }

        std::vector<float> flux((size_t) nf, 0.0f);
        for (int f = 1; f < nf; ++f) flux[(size_t) f] = std::max(0.0f, env[(size_t) f] - env[(size_t) (f - 1)]);

        double mean = 0.0; float mx = 0.0f;
        for (int f = 0; f < nf; ++f) { mean += flux[(size_t) f]; mx = std::max(mx, flux[(size_t) f]); }
        mean /= (double) nf;

        const double thr = mean + ((double) mx - mean) * (1.0 - (double) sensitivity) * 0.5 + 1.0e-7;
        const int minGapF = std::max(1, (int) (sr * 0.03 / hop));
        const int minGapS = minGapF * hop;

        std::vector<int> starts { 0 };
        int last = -minGapF * 2;

        for (int f = 1; f < nf - 1; ++f)
        {
            if (flux[(size_t) f] > thr
                && flux[(size_t) f] >= flux[(size_t) (f - 1)]
                && flux[(size_t) f] >= flux[(size_t) (f + 1)]
                && (f - last) >= minGapF)
            {
                const int pos = snapZero(d, n, f * hop, (int) (sr * 0.002));
                if (pos > starts.back() + minGapS) { starts.push_back(pos); last = f; }
            }
        }

        starts.push_back(n);
        return starts;
    }

    double derivedBpm(int bars, int numSamples, double sr)
    {
        if (sr <= 0.0 || numSamples <= 0) return 0.0;
        const double dur = (double) numSamples / sr;
        if (dur <= 0.0) return 0.0;
        return ((double) bars * 4.0) / dur * 60.0;
    }

    double tempoRatio(double playBpm, double nativeBpm)
    {
        if (nativeBpm <= 0.0 || playBpm <= 0.0) return 1.0;
        return playBpm / nativeBpm;
    }
}
