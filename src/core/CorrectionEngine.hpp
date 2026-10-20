#pragma once
#include "Config.hpp"
#include "Predictor.hpp"
#include "Randomizer.hpp"
#include "Types.hpp"

namespace ff {

enum class Reason {
    Inactive,             // mod off / practice-only outside practice / 0 ticks
    OriginalOk,           // Smart: reference timing already survives
    SmartShift,           // Smart: smallest surviving shift chosen
    NoCandidateSurvives,  // Smart: nothing in the window helps -> unchanged
    PredictionUnavailable,
    PredictionUncertain,
    IneligibleType,       // Random: press/release filtered out
    RandomSkipped,        // Random: probability roll failed
    RandomNoRange,        // Random: no allowed non-zero shift
    RandomShift,
};

inline const char* reasonText(Reason r) {
    switch (r) {
        case Reason::Inactive: return "inactive";
        case Reason::OriginalOk: return "original timing ok";
        case Reason::SmartShift: return "smart shift";
        case Reason::NoCandidateSurvives: return "no candidate survives";
        case Reason::PredictionUnavailable: return "prediction unavailable";
        case Reason::PredictionUncertain: return "prediction uncertain";
        case Reason::IneligibleType: return "input type not eligible";
        case Reason::RandomSkipped: return "random: probability roll failed";
        case Reason::RandomNoRange: return "random: no allowed shift";
        case Reason::RandomShift: return "random shift";
    }
    return "?";
}

struct Decision {
    int shift = 0;  // negative = earlier than reference, positive = later
    Reason reason = Reason::Inactive;
};

// Smart: smallest |shift| whose prediction survives the whole horizon.
//  - Candidates are tried in order 0, -1, +1, -2, +2, ... (ties go EARLIER).
//  - Shifts outside [-early, +late] are never evaluated.
//  - Unavailable/uncertain baseline prediction => unchanged.
//  - Uncertain candidates are skipped, never chosen.
inline Decision decideSmart(const InputEvent& ev, const Limits& lim, IPredictor& pred) {
    const int horizon = pred.horizonTicks();
    PredictionResult base = pred.evaluate(ev, lim.baseDelay);
    if (base.status == PredictionResult::Status::Unavailable) return {0, Reason::PredictionUnavailable};
    if (base.status == PredictionResult::Status::Uncertain) return {0, Reason::PredictionUncertain};
    if (base.survivalTicks >= horizon) return {0, Reason::OriginalOk};

    const int reach = lim.early > lim.late ? lim.early : lim.late;
    for (int m = 1; m <= reach; ++m) {
        for (int s : {-m, +m}) {
            if (s < 0 && m > lim.early) continue;
            if (s > 0 && m > lim.late) continue;
            PredictionResult r = pred.evaluate(ev, lim.baseDelay + s);
            if (r.status != PredictionResult::Status::Ok) continue;
            if (r.survivalTicks >= horizon) return {s, Reason::SmartShift};
        }
    }
    return {0, Reason::NoCandidateSurvives};
}

// Random: every ELIGIBLE event consumes exactly two draws (roll, pick),
// whatever the outcome, so the stream stays aligned for a given seed.
//  - probability: chance (percent) that an eligible event gets a shift at all.
//  - pick: uniform over the allowed NON-ZERO shifts in [-early, +late].
//    This is uniform over shifts, not over directions: with early=1, late=5,
//    "later" is five times as likely as "earlier".
inline Decision decideRandom(const InputEvent& ev, const Config& cfg, const Limits& lim, Randomizer& rng) {
    const bool eligible = cfg.affect == Affect::All ||
                          (cfg.affect == Affect::PressesOnly && ev.down) ||
                          (cfg.affect == Affect::ReleasesOnly && !ev.down);
    if (!eligible) return {0, Reason::IneligibleType};

    const uint32_t roll = rng.below(1000);
    const uint64_t pick = rng.next();

    const int pct = cfg.probabilityPct < 0 ? 0 : (cfg.probabilityPct > 100 ? 100 : cfg.probabilityPct);
    const int n = lim.early + lim.late;
    if (n <= 0) return {0, Reason::RandomNoRange};
    if (roll >= static_cast<uint32_t>(pct) * 10u) return {0, Reason::RandomSkipped};

    const int k = static_cast<int>(pick % static_cast<uint64_t>(n));
    const int s = k < lim.early ? -(lim.early - k) : (k - lim.early + 1);
    return {s, Reason::RandomShift};
}

inline Decision decide(const InputEvent& ev, const Config& cfg, const Limits& lim,
                       Randomizer& rng, IPredictor& pred) {
    switch (cfg.mode) {
        case Mode::Smart: return decideSmart(ev, lim, pred);
        case Mode::Random: return decideRandom(ev, cfg, lim, rng);
        case Mode::Disabled: break;
    }
    return {0, Reason::Inactive};
}

} // namespace ff
