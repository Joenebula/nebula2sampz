#include "Theme.h"
#include <BinaryData.h>

namespace Nebula2::Theme
{
    namespace
    {
        // The embedded faces, created once and shared. Typeface::Ptr is reference-counted,
        // so holding them here keeps them alive for the plugin's lifetime rather than
        // re-parsing a TTF every time a label is drawn.
        struct Faces
        {
            juce::Typeface::Ptr chakra400, chakra500, chakra600, chakra700;
            juce::Typeface::Ptr plex400, plex500;
            bool ok = false;

            Faces()
            {
                auto load = [] (const void* data, int size) -> juce::Typeface::Ptr
                {
                    return data != nullptr && size > 0
                         ? juce::Typeface::createSystemTypefaceFor (data, (size_t) size)
                         : nullptr;
                };

                chakra400 = load (BinaryData::ChakraPetchRegular_ttf,  BinaryData::ChakraPetchRegular_ttfSize);
                chakra500 = load (BinaryData::ChakraPetchMedium_ttf,   BinaryData::ChakraPetchMedium_ttfSize);
                chakra600 = load (BinaryData::ChakraPetchSemiBold_ttf, BinaryData::ChakraPetchSemiBold_ttfSize);
                chakra700 = load (BinaryData::ChakraPetchBold_ttf,     BinaryData::ChakraPetchBold_ttfSize);
                plex400   = load (BinaryData::IBMPlexMonoRegular_ttf,  BinaryData::IBMPlexMonoRegular_ttfSize);
                plex500   = load (BinaryData::IBMPlexMonoMedium_ttf,   BinaryData::IBMPlexMonoMedium_ttfSize);

                ok = chakra400 != nullptr && chakra600 != nullptr && plex400 != nullptr;
            }
        };

        const Faces& faces()
        {
            // Function-local static: constructed on first use, after JUCE is initialised.
            // A namespace-scope global would run before that and the typefaces would fail
            // to build - silently, leaving the whole UI on a fallback face.
            static const Faces f;
            return f;
        }

        // Pick the nearest embedded weight. The kit uses 400/500/600/700; asking for
        // anything else lands on the closest one we actually shipped rather than letting
        // JUCE synthesise a fake bold, which looks smeared at small sizes.
        juce::Typeface::Ptr chakraFor (int weight)
        {
            const auto& f = faces();
            if (weight >= 700) return f.chakra700 != nullptr ? f.chakra700 : f.chakra600;
            if (weight >= 600) return f.chakra600;
            if (weight >= 500) return f.chakra500 != nullptr ? f.chakra500 : f.chakra400;
            return f.chakra400;
        }
    }

    bool fontsLoaded() { return faces().ok; }

    juce::Font ui (float size, int weight)
    {
        if (auto tf = chakraFor (weight))
            return juce::Font (juce::FontOptions (tf).withHeight (size));

        // Named fallback, not a silent one. If this ever runs, the UI is wrong and should
        // look obviously wrong rather than subtly so.
        return juce::Font (juce::FontOptions ("Segoe UI", size,
                                              weight >= 600 ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font mono (float size, int weight)
    {
        const auto& f = faces();
        auto tf = weight >= 500 && f.plex500 != nullptr ? f.plex500 : f.plex400;

        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (size));

        return juce::Font (juce::FontOptions ("Consolas", size,
                                              weight >= 500 ? juce::Font::bold : juce::Font::plain));
    }
}
