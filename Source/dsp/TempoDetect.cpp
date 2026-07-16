#include "TempoDetect.h"

#include <regex>
#include <cmath>
#include <algorithm>

namespace Nebula2
{
    std::vector<BpmCandidate> nameBpmCandidates(const std::string& name)
    {
        std::vector<BpmCandidate> out;
        if (name.empty()) return out;

        const auto alreadyHave = [&out](float v)
        {
            for (const auto& o : out) if (std::abs(o.value - v) < 1.0e-6f) return true;
            return false;
        };

        // "140bpm" — say no more.
        try
        {
            const std::regex reExplicit(R"((\d{2,3}(?:\.\d+)?)\s*bpm)", std::regex::icase);
            std::smatch m;
            if (std::regex_search(name, m, reExplicit))
            {
                const float v = std::stof(m[1].str());
                if (v >= 60.0f && v <= 200.0f) out.push_back({ v, true });
            }
        }
        catch (...) {}

        // Every bare number: BB140, _077_, 128, ... (2-3 digits only — a "1" isn't a tempo).
        try
        {
            const std::regex reNum(R"(\d+)");
            for (auto it = std::sregex_iterator(name.begin(), name.end(), reNum);
                 it != std::sregex_iterator(); ++it)
            {
                const std::string t = it->str();
                if (t.length() < 2 || t.length() > 3) continue;
                const float v = (float) std::stoi(t);
                if (v >= 60.0f && v <= 200.0f && ! alreadyHave(v)) out.push_back({ v, false });
            }
        }
        catch (...) {}

        return out;
    }

    float fitsDuration(float bpm, double dur)
    {
        if (bpm <= 0.0f) return 0.0f;
        const double barSec = (4.0 * 60.0) / (double) bpm;
        const double bars = dur / barSec;
        const int near = (int) std::lround(bars);
        if (near < 1 || near > 32) return 0.0f;

        const double err = std::abs(bars - (double) near) / (double) near;
        if (err > 0.02) return 0.0f;   // it simply isn't that tempo

        const float tidy = (near == 1 || near == 2 || near == 4 || near == 8 || near == 16) ? 1.0f
                         : (near == 3 || near == 6 || near == 12) ? 0.6f : 0.3f;   // 7-bar loops are rare
        return (float) (tidy * (1.0 - (err / 0.02) * 0.5));
    }

    float scoreCandidate(float bpm, int bars, float rough)
    {
        double s = 0.0;
        s += std::exp(-std::pow((double) bpm - 120.0, 2.0) / (2.0 * 38.0 * 38.0)) * 3.0;   // typical range

        const double nearest = std::round((double) bpm);
        s += 1.6 * std::exp(-std::abs((double) bpm - nearest) * 8.0);                       // round tempos likelier
        if (std::abs((double) bpm - nearest) < 0.06 && ((long) nearest % 5) == 0) s += 0.5;

        switch (bars)
        {
            case 1:  s += 0.9;  break;
            case 2:  s += 1.0;  break;
            case 4:  s += 0.9;  break;
            case 8:  s += 0.55; break;
            case 16: s += 0.2;  break;
            case 3:  s += 0.15; break;
            case 6:  s += 0.15; break;
            default: break;
        }

        if (rough > 0.0f)                       // agree with onsets, tolerating octave errors
        {
            for (const double mult : { 0.5, 1.0, 2.0 })
            {
                const double r = (double) rough * mult;
                if (r > 0.0 && std::abs((double) bpm - r) / (double) bpm < 0.05)
                {
                    s += 1.5 * (1.0 - std::abs((double) bpm - r) / (double) bpm / 0.05) * 0.8;
                    break;
                }
            }
        }
        return (float) s;
    }

    TempoResult detectTempo(double dur, const std::string& name, float rough)
    {
        TempoResult res;
        if (dur < 0.2) return res;

        struct Cand { double bpm; int bars; double sc; const char* source; };
        std::vector<Cand> cands;

        // 1. the loop's own length — the honest evidence.
        for (const int bars : { 1, 2, 3, 4, 6, 8, 12, 16 })
        {
            const double bpm = ((double) bars * 4.0 * 60.0) / dur;
            if (bpm < 60.0 || bpm > 200.0) continue;
            cands.push_back({ bpm, bars, (double) scoreCandidate((float) bpm, bars, rough), "loop length" });
        }

        // 2. the filename — but only where it AGREES with that length.
        for (const auto& c : nameBpmCandidates(name))
        {
            const float fit = fitsDuration(c.value, dur);
            if (fit <= 0.0f) continue;                 // "077" can't explain a 6.86s loop -> discarded
            const int bars = std::max(1, (int) std::lround(dur / ((4.0 * 60.0) / (double) c.value)));
            const double sc = (double) scoreCandidate(c.value, bars, rough)
                            + 2.2 * (double) fit
                            + (c.isExplicit ? 1.0 : 0.0);
            cands.push_back({ (double) c.value, bars, sc, "filename" });
        }

        if (cands.empty()) return res;

        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.sc > b.sc; });
        const auto& best = cands.front();

        res.bpm = (float) (std::round(best.bpm * 10.0) / 10.0);
        res.bars = std::min(16, best.bars);
        res.source = best.source;
        res.valid = true;
        return res;
    }
}
