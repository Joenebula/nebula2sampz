#pragma once

#include <string>
#include <vector>

namespace Nebula2
{
    // Tempo detection ported from the prototype. The law it encodes (prototype pitfall 6):
    // a filename is a HINT, the audio's duration is the EVIDENCE. Gather every plausible
    // number from the name, then keep only those that explain the loop's actual length as
    // a whole number of bars. "VEC1 Loops BB140 077.wav" -> 140 (4 bars), never 77.

    struct BpmCandidate
    {
        float value = 0.0f;
        bool isExplicit = false;   // matched "...140bpm" rather than a bare number
    };

    // Every 2-3 digit number in 60..200 found in the name, plus an explicit "<n>bpm" match.
    std::vector<BpmCandidate> nameBpmCandidates(const std::string& name);

    // How well does this BPM explain the loop's length? 0 = not at all (wrong bar count).
    float fitsDuration(float bpm, double durationSeconds);

    // Plausibility score: near 120, round numbers, tidy bar counts, agreement with an
    // onset-derived rough BPM (0 = none available), tolerating octave errors.
    float scoreCandidate(float bpm, int bars, float roughBpm);

    struct TempoResult
    {
        float bpm = 0.0f;
        int bars = 0;
        std::string source;   // "loop length" | "filename"
        bool valid = false;
    };

    // Scores loop-length candidates and name candidates together; best evidence wins.
    // roughBpm is an optional onset-analysis hint (0 = none).
    TempoResult detectTempo(double durationSeconds, const std::string& name, float roughBpm = 0.0f);
}
