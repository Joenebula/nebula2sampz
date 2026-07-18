#include "AmpShape.h"

#include <cmath>

namespace Nebula2
{
    const char* ampShapeName(AmpShapeId s) noexcept
    {
        switch (s)
        {
            case AmpShapeId::Punch: return "Punch";
            case AmpShapeId::Gate:  return "Gate";
            case AmpShapeId::Swell: return "Swell";
            case AmpShapeId::Pluck: return "Pluck";
            case AmpShapeId::Flat:  return "Flat";
            default:                return "?";
        }
    }

    AmpShape makeAmpShape(AmpShapeId id)
    {
        AmpShape out {};
        for (int i = 0; i < ampShapePoints; ++i)
        {
            const float t = (float) i / (float) (ampShapePoints - 1);
            float v = 1.0f;
            switch (id)
            {
                case AmpShapeId::Punch: v = juce::jmax(0.02f, std::pow(1.0f - t, 3.2f)); break;
                case AmpShapeId::Gate:  v = t < 0.55f ? 1.0f : 0.03f;                    break;
                case AmpShapeId::Swell: v = std::pow(t, 1.6f);                           break;
                case AmpShapeId::Pluck: v = t < 0.06f ? t / 0.06f
                                            : std::pow(1.0f - ((t - 0.06f) / 0.94f), 2.2f); break;
                case AmpShapeId::Flat:
                default:                v = 1.0f;                                        break;
            }
            out[(size_t) i] = juce::jlimit(0.0f, 1.0f, v);
        }
        return out;
    }

    AmpShape randomAmpShape(AmpShapeId base, juce::Random& rng)
    {
        // Perturb a real curve rather than rolling 32 independent values. Independent
        // values give a jagged curve, which is a stutter effect, not an envelope — the
        // prototype multiplies its base shape by 0.75..1.25 for exactly this reason.
        auto shape = makeAmpShape(base);
        for (auto& v : shape)
            v = juce::jlimit(0.02f, 1.0f, v * (0.75f + rng.nextFloat() * 0.5f));
        return shape;
    }

    float ampShapeAt(const AmpShape& shape, double position01) noexcept
    {
        // Clamp, don't wrap: a voice running slightly past its own end must fade out rather
        // than jump back to the attack, which would click on every slice.
        const double p = juce::jlimit(0.0, 1.0, position01);
        const double x = p * (double) (ampShapePoints - 1);
        const int i0 = (int) x;
        const int i1 = juce::jmin(i0 + 1, ampShapePoints - 1);
        const float frac = (float) (x - (double) i0);
        return shape[(size_t) i0] + frac * (shape[(size_t) i1] - shape[(size_t) i0]);
    }

    juce::String ampShapeToString(const AmpShape& s)
    {
        juce::String out;
        for (int i = 0; i < ampShapePoints; ++i)
        {
            if (i > 0) out << ",";
            out << juce::String(s[(size_t) i], 3);
        }
        return out;
    }

    AmpShape ampShapeFromString(const juce::String& str)
    {
        auto toks = juce::StringArray::fromTokens(str, ",", "");
        // Wrong length or non-numeric means we don't know the curve, and the honest answer
        // to that is Flat — a shape that changes nothing — rather than a half-filled array
        // that silently gates every slice.
        if (toks.size() != ampShapePoints) return makeAmpShape(AmpShapeId::Flat);

        AmpShape out {};
        for (int i = 0; i < ampShapePoints; ++i)
        {
            const auto t = toks[i].trim();
            if (t.isEmpty() || ! t.containsOnly("-.0123456789"))
                return makeAmpShape(AmpShapeId::Flat);
            out[(size_t) i] = juce::jlimit(0.0f, 1.0f, t.getFloatValue());
        }
        return out;
    }
}
