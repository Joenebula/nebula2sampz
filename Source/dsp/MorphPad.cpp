#include "MorphPad.h"

#include <cmath>

namespace Nebula2
{
    void morphMotionOffset(MorphMotion mode, double phase, float size, float& dx, float& dy) noexcept
    {
        dx = dy = 0.0f;
        if (mode == MorphMotion::Off || size <= 0.0f) return;

        const double tau = phase * juce::MathConstants<double>::twoPi;
        const float  s   = juce::jlimit(0.0f, 1.0f, size);

        switch (mode)
        {
            case MorphMotion::Circle:
                dx = (float) std::sin(tau) * s;
                dy = (float) std::cos(tau) * s;
                break;

            case MorphMotion::Fig8:                 // lissajous 1:2 — a figure eight
                dx = (float) std::sin(tau) * s;
                dy = (float) std::sin(tau * 2.0) * s;
                break;

            case MorphMotion::Square:               // trace the square's edge, one side per quarter
            {
                const double p = phase - std::floor(phase);
                const double seg = p * 4.0;
                const int    e   = (int) seg;        // 0..3
                const float  f   = (float) (seg - (double) e) * 2.0f - 1.0f;  // -1..1 along the edge
                switch (e)
                {
                    case 0:  dx =  f;   dy = -1.0f; break;   // top edge, L->R
                    case 1:  dx =  1.0f; dy =  f;   break;   // right edge, T->B
                    case 2:  dx = -f;   dy =  1.0f; break;   // bottom edge, R->L
                    default: dx = -1.0f; dy = -f;   break;   // left edge, B->T
                }
                dx *= s; dy *= s;
                break;
            }

            case MorphMotion::Drift:                // two slow, coprime lobes = a wandering path
                dx = (float) (std::sin(tau) * 0.6 + std::sin(tau * 0.37 + 1.3) * 0.4) * s;
                dy = (float) (std::cos(tau * 0.83) * 0.6 + std::sin(tau * 1.29) * 0.4) * s;
                break;

            default: break;
        }
    }

    std::array<MorphScene, 4> defaultMorphScenes()
    {
        // Ported from the prototype's padSeedVariations().
        std::array<MorphScene, 4> s;
        s[0] = { 16000.0f, 0.7f,  6.0f,  0.0f,  0.0f,  0.0f, 100.0f,  6.0f };   // A — open
        s[1] = {  6500.0f, 2.2f, 62.0f, 18.0f,  0.0f,  0.0f, 115.0f, 12.0f };   // B — dirty
        s[2] = {   700.0f, 9.0f, 28.0f,  0.0f, 52.0f,  0.0f,  88.0f, 26.0f };   // C — dark, resonant
        s[3] = {  9000.0f, 1.2f, 22.0f, 58.0f, 30.0f, 48.0f, 165.0f, 66.0f };   // D — wide, wet, broken
        return s;
    }

    std::array<MorphScene, 4> randomMorphScenes(juce::Random& rng)
    {
        auto r = [&rng](float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };

        std::array<MorphScene, 4> s;

        // A — open: bright, barely touched.
        s[0] = { r(7000.0f, 16000.0f), r(0.6f, 2.0f),   r(0.0f, 20.0f),  r(0.0f, 15.0f),
                 r(0.0f, 20.0f),       0.0f,            r(95.0f, 120.0f),  r(0.0f, 20.0f) };

        // B — dirty: driven hard, still open enough to hear the drive.
        s[1] = { r(2500.0f, 8000.0f),  r(1.0f, 4.0f),   r(50.0f, 95.0f), r(0.0f, 35.0f),
                 r(0.0f, 25.0f),       r(0.0f, 25.0f),  r(90.0f, 130.0f),  r(5.0f, 25.0f) };

        // C — dark and resonant: filter right down, resonance right up.
        s[2] = { r(300.0f, 1400.0f),   r(6.0f, 16.0f),  r(15.0f, 55.0f), r(0.0f, 20.0f),
                 r(30.0f, 80.0f),      0.0f,            r(70.0f, 100.0f),  r(15.0f, 45.0f) };

        // D — wide, wet, broken: flanged, shattered, drenched.
        s[3] = { r(5000.0f, 14000.0f), r(0.7f, 3.0f),   r(10.0f, 40.0f), r(35.0f, 85.0f),
                 r(20.0f, 60.0f),      r(30.0f, 80.0f), r(130.0f, 190.0f), r(45.0f, 85.0f) };

        return s;
    }

    std::array<float, 4> morphWeights(float x, float y) noexcept
    {
        x = juce::jlimit(0.0f, 1.0f, x);
        y = juce::jlimit(0.0f, 1.0f, y);
        return { (1.0f - x) * (1.0f - y),   // A top-left
                 x * (1.0f - y),            // B top-right
                 (1.0f - x) * y,            // C bottom-left
                 x * y };                   // D bottom-right
    }

    MorphScene blendMorph(const std::array<MorphScene, 4>& c, float x, float y) noexcept
    {
        const auto w = morphWeights(x, y);

        const auto lin = [&w](float a, float b, float cc, float d)
        {
            return w[0] * a + w[1] * b + w[2] * cc + w[3] * d;
        };

        // Log-space blend for frequency: the ear hears pitch/brightness logarithmically,
        // so a linear blend of 16k and 700 would sit at 8.3k and feel like it had barely
        // left "open". Geometric lands at ~3.3k, which is what halfway actually sounds like.
        const auto logBlend = [&w](float a, float b, float cc, float d)
        {
            const auto L = [](float v) { return std::log(juce::jmax(1.0f, v)); };
            return std::exp(w[0] * L(a) + w[1] * L(b) + w[2] * L(cc) + w[3] * L(d));
        };

        MorphScene o;
        o.cut = logBlend(c[0].cut, c[1].cut, c[2].cut, c[3].cut);
        o.res = lin(c[0].res, c[1].res, c[2].res, c[3].res);
        o.drv = lin(c[0].drv, c[1].drv, c[2].drv, c[3].drv);
        o.flg = lin(c[0].flg, c[1].flg, c[2].flg, c[3].flg);
        o.phs = lin(c[0].phs, c[1].phs, c[2].phs, c[3].phs);
        o.sht = lin(c[0].sht, c[1].sht, c[2].sht, c[3].sht);
        o.wid = lin(c[0].wid, c[1].wid, c[2].wid, c[3].wid);
        o.spc = lin(c[0].spc, c[1].spc, c[2].spc, c[3].spc);
        return o;
    }

    juce::String morphScenesToString(const std::array<MorphScene, 4>& c)
    {
        juce::String out;
        for (int i = 0; i < 4; ++i)
        {
            const auto& s = c[(size_t) i];
            out << s.cut << " " << s.res << " " << s.drv << " " << s.flg << " "
                << s.phs << " " << s.sht << " " << s.wid << " " << s.spc;
            if (i < 3) out << ",";
        }
        return out;
    }

    std::array<MorphScene, 4> morphScenesFromString(const juce::String& str)
    {
        auto out = defaultMorphScenes();       // malformed input falls back to the seeds
        auto rows = juce::StringArray::fromTokens(str, ",", "");
        if (rows.size() != 4) return out;

        for (int i = 0; i < 4; ++i)
        {
            auto v = juce::StringArray::fromTokens(rows[i].trim(), " ", "");
            if (v.size() != 8) return defaultMorphScenes();
            MorphScene s;
            s.cut = v[0].getFloatValue(); s.res = v[1].getFloatValue();
            s.drv = v[2].getFloatValue(); s.flg = v[3].getFloatValue();
            s.phs = v[4].getFloatValue(); s.sht = v[5].getFloatValue();
            s.wid = v[6].getFloatValue(); s.spc = v[7].getFloatValue();
            out[(size_t) i] = s;
        }
        return out;
    }
}
