#pragma once

#include <vector>
#include <cstdint>

namespace Nebula2::Drums
{
    // Modal-synthesis drum voices ported 1:1 from the prototype's DrumEngine (pure DSP,
    // no Web Audio). Each returns a mono one-shot at `sampleRate`, fully deterministic for
    // a given (velocity, seed) — the seed drives a mulberry32 RNG, so kits/variations are
    // reproducible under host automation and offline render.
    //
    // velocity is 0..1.
    std::vector<float> vKick(float velocity, uint32_t seed, double sampleRate);
    std::vector<float> vSnare(float velocity, uint32_t seed, double sampleRate);
    std::vector<float> vHat(float velocity, uint32_t seed, double sampleRate, bool open);
    std::vector<float> vClap(float velocity, uint32_t seed, double sampleRate);
    std::vector<float> vTom(float velocity, uint32_t seed, double sampleRate);
    std::vector<float> vRim(float velocity, uint32_t seed, double sampleRate);
    std::vector<float> vPerc(float velocity, uint32_t seed, double sampleRate);

    // Test/measurement helper: peak absolute value.
    float peak(const std::vector<float>& x);
}
