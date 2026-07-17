#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>

namespace Nebula2
{
    enum class DriveChar { Tube = 0, Fuzz, Fold };

    // The prototype's drive transfer function (WaveShaper curve), ported 1:1.
    //  tube: warm rounded soft-clip   ((1+k)x)/(1+k|x|), k = amt^2 * 90
    //  fuzz: hard-ish tanh clip        tanh(x * (1 + amt*24))
    //  fold: 4-pass wavefolder         metallic, folds back on itself
    // amt < 0.01 is a pass-through. Output is always within [-1, 1].
    float driveCurveSample(float x, float amt, DriveChar character) noexcept;

    // Drive (oversampled to suppress the aliasing the prototype's in-browser WaveShaper
    // left in) -> bit-crush (zero-order-hold downsample + reconstruction lowpass, the
    // prototype's anti-alias trick) -> mid/side width. Stereo, real-time safe.
    class Saturator
    {
    public:
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        // driveAmt/crushAmt in 0..1, width in 0..2.
        //
        // The two halves are separable BY DESIGN. The prototype's Colour order is
        // drive -> squeeze -> tone -> crush -> width, so ColourChain runs the drive, then
        // its own compressor and tone filter, THEN comes back for crush+width. The combined
        // process() does drive-then-crush+width back-to-back, which is the WRONG order for
        // the Colour chain — it's kept only for callers that genuinely want both at once.
        void processDrive(juce::AudioBuffer<float>& buffer,
                          float driveAmt, DriveChar character) noexcept;
        void processCrushWidth(juce::AudioBuffer<float>& buffer,
                               float crushAmt, float width) noexcept;

        void process(juce::AudioBuffer<float>& buffer,
                     float driveAmt, DriveChar character,
                     float crushAmt, float width) noexcept;

        int getLatencySamples() const noexcept;

    private:
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

        // Crush state (per channel).
        float holdL = 0.0f, holdR = 0.0f, lpL = 0.0f, lpR = 0.0f;
        int   sampleCounter = 0;

        double sampleRate = 44100.0;
    };
}
