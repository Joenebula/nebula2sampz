#include "DrumSynth.h"

#include <cmath>
#include <array>
#include <algorithm>

namespace Nebula2::Drums
{
    namespace
    {
        constexpr double PI = 3.14159265358979323846;

        // Deterministic RNG — mulberry32, ported exactly (uint32 wrap == JS imul/|0/>>>).
        struct Mulberry32
        {
            uint32_t a;
            explicit Mulberry32(uint32_t seed) : a(seed) {}
            float next()
            {
                a += 0x6D2B79F5u;
                uint32_t t = (a ^ (a >> 15)) * (1u | a);
                t = (t + (t ^ (t >> 7)) * (61u | t)) ^ t;
                return (float) ((double) (t ^ (t >> 14)) / 4294967296.0);
            }
        };

        std::vector<float> noise(int n, Mulberry32& rnd)
        {
            std::vector<float> o((size_t) n);
            for (int i = 0; i < n; ++i) o[(size_t) i] = rnd.next() * 2.0f - 1.0f;
            return o;
        }

        std::vector<float> adsr(int n, double a, float dCurve, double sr)
        {
            const int at = std::max(1, (int) (a * sr));
            std::vector<float> o((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                if (i < at) o[(size_t) i] = (float) i / (float) at;
                else { const float t = (float) (i - at) / (float) std::max(1, n - at); o[(size_t) i] = std::pow(1.0f - t, dCurve); }
            }
            return o;
        }

        std::vector<float> expEnv(int n, float decay, double sr)
        {
            std::vector<float> o((size_t) n);
            for (int i = 0; i < n; ++i) o[(size_t) i] = (float) std::exp(-((double) i / sr) * decay);
            return o;
        }

        // RBJ biquad -> normalised {b0,b1,b2,a1,a2}.
        std::array<float, 5> biquad(char type, float f, float Q, float gainDB, double sr)
        {
            const double w0 = 2.0 * PI * f / sr, cw = std::cos(w0), sw = std::sin(w0), al = sw / (2.0 * Q);
            const double A = std::pow(10.0, (double) gainDB / 40.0);
            double b0, b1, b2, a0, a1, a2;
            if (type == 'l')      { b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = b0; a0 = 1 + al;     a1 = -2 * cw; a2 = 1 - al; }
            else if (type == 'h') { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al;     a1 = -2 * cw; a2 = 1 - al; }
            else if (type == 'b') { b0 = al;           b1 = 0;         b2 = -al; a0 = 1 + al;    a1 = -2 * cw; a2 = 1 - al; }
            else                  { b0 = 1 + al * A;   b1 = -2 * cw;   b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
            return { (float) (b0 / a0), (float) (b1 / a0), (float) (b2 / a0), (float) (a1 / a0), (float) (a2 / a0) };
        }

        std::vector<float> runBiquad(const std::vector<float>& x, const std::array<float, 5>& c)
        {
            std::vector<float> y(x.size());
            float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const float xi = x[i];
                const float yi = c[0] * xi + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
                x2 = x1; x1 = xi; y2 = y1; y1 = yi; y[i] = yi;
            }
            return y;
        }

        float nyq(float f, double sr) { return std::min(f, (float) (sr / 2.0 - 200.0)); }
        std::vector<float> lp(const std::vector<float>& x, float f, double sr)          { return runBiquad(x, biquad('l', nyq(f, sr), 0.707f, 0, sr)); }
        std::vector<float> hp(const std::vector<float>& x, float f, double sr)          { return runBiquad(x, biquad('h', nyq(f, sr), 0.707f, 0, sr)); }
        std::vector<float> bp(const std::vector<float>& x, float f, float q, double sr) { return runBiquad(x, biquad('b', nyq(f, sr), q, 0, sr)); }

        std::vector<float> sat(const std::vector<float>& x, float k)
        {
            const float t = std::tanh(k);
            std::vector<float> o(x.size());
            for (size_t i = 0; i < x.size(); ++i) o[i] = std::tanh(x[i] * k) / t;
            return o;
        }

        std::vector<float> scale(const std::vector<float>& a, float g)
        {
            std::vector<float> o(a.size());
            for (size_t i = 0; i < a.size(); ++i) o[i] = a[i] * g;
            return o;
        }

        std::vector<float> mode(float freq, double dur, float decay, float amp, double sr)
        {
            const int n = (int) (dur * sr);
            std::vector<float> o((size_t) n);
            for (int i = 0; i < n; ++i)
                o[(size_t) i] = amp * (float) (std::sin(2.0 * PI * freq * i / sr) * std::exp(-((double) i / sr) * decay));
            return o;
        }

        void addModal(std::vector<float>& out, std::initializer_list<std::array<float, 3>> modes, double dur, double sr)
        {
            for (const auto& m : modes)
            {
                const auto mm = mode(m[0], dur, m[1], m[2], sr);
                for (size_t i = 0; i < mm.size() && i < out.size(); ++i) out[i] += mm[i];
            }
        }
    } // namespace

    float peak(const std::vector<float>& x)
    {
        float p = 0.0f;
        for (const float v : x) p = std::max(p, std::abs(v));
        return p;
    }

    std::vector<float> vKick(float vel, uint32_t seed, double sr)
    {
        const float f_top = 170, f_bot = 50, bend = 40, beater = 0.5f, shell = 0.4f, drive = 1.6f, shell_f = 90;
        const double dur = 0.5;

        Mulberry32 rnd(seed);
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);
        double ph = 0.0;
        const float ft = f_top * (1.0f + 0.1f * vel);
        const auto env = adsr(n, 0.001, 2.2f, sr);

        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double pit = f_bot + (ft - f_bot) * std::exp(-t * bend);
            ph += 2.0 * PI * pit / sr;
            out[(size_t) i] = (float) std::sin(ph) * env[(size_t) i];
        }
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            out[(size_t) i] += (float) (std::sin(2.0 * PI * (f_bot * 0.82) * t) * std::exp(-t * 7.0) * 0.6);
        }

        const int bn = (int) (0.009 * sr);
        std::vector<float> cl((size_t) n, 0.0f);
        for (int c = 0; c < bn; ++c) cl[(size_t) c] = (rnd.next() * 2.0f - 1.0f) * std::pow(1.0f - (float) c / (float) bn, 2.0f);
        cl = lp(cl, 2600.0f + 4200.0f * vel, sr);

        const auto she = adsr(n, 0.002, 1.6f, sr);
        std::vector<float> shb((size_t) n, 0.0f);
        addModal(shb, { { shell_f, 14.0f, 1.0f }, { shell_f * 1.9f, 20.0f, 0.4f } }, dur, sr);

        for (int k = 0; k < n; ++k)
            out[(size_t) k] += cl[(size_t) k] * beater * (0.5f + 0.9f * vel) + shb[(size_t) k] * she[(size_t) k] * shell;

        return scale(sat(scale(out, 1.2f), drive), 0.98f);
    }

    std::vector<float> vSnare(float vel, uint32_t seed, double sr)
    {
        const float shellF[3] = { 190, 350, 470 };
        const double dur = 0.3;
        const float buzz = 1, body = 1, tone = 1, bright = 8000, drive = 1.6f, decay_buzz = 26;

        Mulberry32 rnd(seed);
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);
        const float det = 1.0f + (rnd.next() - 0.5f) * 0.016f;

        const auto she = adsr(n, 0.0006, 3.0f, sr);
        std::vector<float> shb((size_t) n, 0.0f);
        addModal(shb, { { shellF[0] * det, 30.0f, tone }, { shellF[1] * det, 42.0f, 0.6f * tone }, { shellF[2] * det, 55.0f, 0.35f * tone } }, dur, sr);
        for (int i = 0; i < n; ++i) out[(size_t) i] += shb[(size_t) i] * she[(size_t) i] * 0.8f;

        const auto th  = mode(shellF[0] * 0.85f, dur, 34.0f, 0.7f * body, sr);
        const auto the = adsr(n, 0.0006, 3.4f, sr);
        for (int i = 0; i < n; ++i) out[(size_t) i] += (i < (int) th.size() ? th[(size_t) i] : 0.0f) * the[(size_t) i];

        const float dk = decay_buzz * (1.35f - 0.5f * vel);
        const auto lo = bp(noise(n, rnd), 3400.0f, 0.6f, sr);
        const auto hi = bp(noise(n, rnd), bright * (0.5f + 0.28f * vel), 0.9f, sr);
        std::vector<float> wr((size_t) n, 0.0f);
        for (int w = 0; w < n; ++w)
        {
            const float e1 = (float) std::exp(-((double) w / sr) * dk);
            const float e2 = (float) std::exp(-((double) w / sr) * dk * 1.4);
            wr[(size_t) w] = (lo[(size_t) w] * e1 * 0.9f + hi[(size_t) w] * e2 * 0.45f * vel) * buzz;
        }
        wr = sat(scale(wr, 1.3f), 1.4f);

        const int cn = (int) (0.004 * sr);
        std::vector<float> ck((size_t) n, 0.0f);
        const auto cks = hp(noise(cn, rnd), 3000.0f, sr);
        for (int q = 0; q < cn; ++q) ck[(size_t) q] = cks[(size_t) q] * (1.0f - (float) q / (float) cn);

        for (int m = 0; m < n; ++m) out[(size_t) m] += wr[(size_t) m] * 0.9f + ck[(size_t) m] * (0.35f + 0.6f * vel);

        return scale(sat(out, drive), 0.85f);
    }

    std::vector<float> vHat(float vel, uint32_t seed, double sr, bool open)
    {
        const double dur = open ? 0.28 : 0.05;
        const float hi = 8600;
        const float base[5] = { 3200, 4400, 5900, 7600, 9400 };
        const float decay = (open ? 5.0f : 46.0f) * (1.25f - 0.4f * vel);

        Mulberry32 rnd(seed);
        const int n = (int) (dur * sr);
        std::vector<float> metal((size_t) n, 0.0f);
        for (int b = 0; b < 5; ++b)
        {
            const float f = base[b] * (1.0f + (rnd.next() - 0.5f) * 0.024f);
            for (int i = 0; i < n; ++i) metal[(size_t) i] += (float) std::sin(2.0 * PI * f * i / sr);
        }
        metal = hp(scale(metal, 1.0f / 5.0f), 3600.0f, sr);

        const auto hiss = bp(noise(n, rnd), hi * (0.55f + 0.22f * vel), 0.9f, sr);
        std::vector<float> out((size_t) n, 0.0f);
        for (int k = 0; k < n; ++k)
        {
            const float e = (float) std::exp(-((double) k / sr) * decay);
            out[(size_t) k] = metal[(size_t) k] * 0.7f * e
                            + hiss[(size_t) k] * (float) std::exp(-((double) k / sr) * decay * 0.8) * 0.4f * vel;
        }
        return scale(lp(out, 11500.0f, sr), open ? 0.26f : 0.2f);
    }

    std::vector<float> vClap(float vel, uint32_t seed, double sr)
    {
        const double dur = 0.28;
        Mulberry32 rnd(seed);
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);

        const double offs[4] = { 0.0, 0.011, 0.021, 0.03 };
        const float g[4] = { 1.0f, 0.8f, 0.65f, 0.5f };
        for (int j = 0; j < 4; ++j)
        {
            const int off = (int) (offs[j] * sr);
            const auto seg = bp(noise(n, rnd), 1300.0f, 0.9f, sr);
            const auto env = adsr(n, 0.0004, 8.0f, sr);
            for (int i = off; i < n; ++i)
                out[(size_t) i] += seg[(size_t) (i - off)] * env[(size_t) (i - off)] * g[j] * (0.6f + 0.4f * vel);
        }
        const auto tail = bp(noise(n, rnd), 1100.0f, 0.8f, sr);
        const auto te = expEnv(n, 22.0f, sr);
        for (int k = 0; k < n; ++k) out[(size_t) k] += tail[(size_t) k] * te[(size_t) k] * 0.3f;

        return scale(sat(out, 1.4f), 0.8f);
    }

    std::vector<float> vTom(float vel, uint32_t seed, double sr)
    {
        (void) seed;
        const float f_top = 200, f_bot = 110, bend = 16;
        const double dur = 0.42;
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);
        double ph = 0.0;
        const auto env = adsr(n, 0.001, 2.4f, sr);
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            const double pit = f_bot + (f_top - f_bot) * std::exp(-t * bend);
            ph += 2.0 * PI * pit / sr;
            out[(size_t) i] = (float) std::sin(ph) * env[(size_t) i];
        }
        const auto h = mode(f_bot * 2.6f, dur, 22.0f, 0.25f, sr);
        const auto he = adsr(n, 0.001, 3.0f, sr);
        for (int k = 0; k < n; ++k) out[(size_t) k] += (k < (int) h.size() ? h[(size_t) k] : 0.0f) * he[(size_t) k];

        return scale(sat(out, 1.5f), 0.8f);
    }

    std::vector<float> vRim(float vel, uint32_t seed, double sr)
    {
        (void) seed;
        const double dur = 0.09;
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);
        const auto env = adsr(n, 0.0004, 4.0f, sr);
        addModal(out, { { 1720.0f, 50.0f, 1.0f }, { 2550.0f, 60.0f, 0.6f }, { 3900.0f, 80.0f, 0.3f } }, dur, sr);
        for (int i = 0; i < n; ++i) out[(size_t) i] *= env[(size_t) i];
        return scale(sat(out, 2.2f), 0.6f * (0.6f + 0.4f * vel));
    }

    std::vector<float> vPerc(float vel, uint32_t seed, double sr)
    {
        const float f = 320, slap = 0.2f;
        const double dur = 0.26;
        Mulberry32 rnd(seed);
        const int n = (int) (dur * sr);
        std::vector<float> out((size_t) n, 0.0f);
        const auto env = adsr(n, 0.0008, 3.2f, sr);
        for (int i = 0; i < n; ++i) out[(size_t) i] = (float) std::sin(2.0 * PI * f * i / sr) * env[(size_t) i];

        const auto h = mode(f * 2.8f, dur, 26.0f, 0.3f, sr);
        const auto he = adsr(n, 0.001, 3.0f, sr);
        for (int k = 0; k < n; ++k) out[(size_t) k] += (k < (int) h.size() ? h[(size_t) k] : 0.0f) * he[(size_t) k];

        const auto sl = bp(noise(n, rnd), 1200.0f, 0.8f, sr);
        const auto se = adsr(n, 0.0004, 4.0f, sr);
        const int lim = std::min(n, (int) (0.02 * sr));
        for (int m = 0; m < lim; ++m) out[(size_t) m] += sl[(size_t) m] * se[(size_t) m] * (0.3f + slap + 0.3f * vel);

        return scale(sat(out, 1.6f), 0.72f);
    }
}
