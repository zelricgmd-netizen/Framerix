#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace ff {

constexpr int kMaxWindow = 10;

enum class Mode { Smart, Random, Disabled };
enum class Direction { Both, EarlierOnly, LaterOnly };
enum class Affect { All, PressesOnly, ReleasesOnly };

struct Config {
    bool enabled = true;
    Mode mode = Mode::Smart;
    bool practiceOnly = true;
    int window = 1;           // used when symmetric == true
    bool symmetric = true;
    int maxEarly = 1;         // used when symmetric == false
    int maxLate = 1;
    int probabilityPct = 25;  // Random mode: chance an eligible input gets a shift
    Direction direction = Direction::Both;
    Affect affect = Affect::All;
    uint32_t seed = 12345;
    bool reseedOnRestart = true;
    bool debug = false;
};

// Effective limits after symmetric/direction rules are applied.
//
// early/late are measured against the REFERENCE timing, which is
// (arrival tick + baseDelay). baseDelay == early, because an input can never be
// delivered before it arrives; "earlier" is only possible by holding every
// input back by `early` ticks first. See README "Timing model".
struct Limits {
    int early = 0;
    int late = 0;
    int baseDelay = 0;
};

inline int clampWindow(int v) { return std::clamp(v, 0, kMaxWindow); }

inline Limits resolveLimits(const Config& c) {
    Limits l;
    if (!c.enabled || c.mode == Mode::Disabled) return l;
    l.early = clampWindow(c.symmetric ? c.window : c.maxEarly);
    l.late = clampWindow(c.symmetric ? c.window : c.maxLate);
    if (c.mode == Mode::Random) {
        if (c.direction == Direction::LaterOnly) l.early = 0;
        if (c.direction == Direction::EarlierOnly) l.late = 0;
    }
    l.baseDelay = l.early;
    return l;
}

// Zero ticks in both directions == truly inactive: no latency, pure passthrough.
inline bool isActive(const Config& c, bool inPractice) {
    if (c.practiceOnly && !inPractice) return false;
    Limits l = resolveLimits(c);
    return (l.early + l.late) > 0;
}

// Strings must match the "one-of" lists in mod.json.
inline Mode parseMode(const std::string& s) {
    if (s == "Random") return Mode::Random;
    if (s == "Disabled") return Mode::Disabled;
    return Mode::Smart;
}
inline Direction parseDirection(const std::string& s) {
    if (s == "Earlier only") return Direction::EarlierOnly;
    if (s == "Later only") return Direction::LaterOnly;
    return Direction::Both;
}
inline Affect parseAffect(const std::string& s) {
    if (s == "Presses only") return Affect::PressesOnly;
    if (s == "Releases only") return Affect::ReleasesOnly;
    return Affect::All;
}

} // namespace ff
