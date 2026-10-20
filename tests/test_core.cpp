// Host unit tests for src/core. No Geode, no game: this validates the
// scheduling/decision LOGIC only. It says nothing about in-game behavior.
#include "CorrectionEngine.hpp"
#include "InputScheduler.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <vector>

using namespace ff;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

// ---- helpers ---------------------------------------------------------------

struct FnPredictor : IPredictor {
    int horizon = 100;
    std::function<PredictionResult(const InputEvent&, int)> fn;
    std::vector<int> asked;  // offsets evaluated, in order
    int horizonTicks() const override { return horizon; }
    PredictionResult evaluate(const InputEvent& e, int off) override {
        asked.push_back(off);
        return fn(e, off);
    }
};

static PredictionResult ok(int surv) {
    PredictionResult r;
    r.status = PredictionResult::Status::Ok;
    r.survivalTicks = surv;
    return r;
}

// survives (full horizon) only at the listed shifts relative to reference
static FnPredictor survivesAtShifts(Limits lim, std::set<int> shifts) {
    FnPredictor p;
    p.fn = [lim, shifts, h = p.horizon](const InputEvent&, int off) {
        return ok(shifts.count(off - lim.baseDelay) ? h : 3);
    };
    return p;
}

static Config cfgWith(Mode m, int window) {
    Config c;
    c.mode = m;
    c.window = window;
    c.practiceOnly = false;
    return c;
}

static InputEvent ev(int64_t t, bool down, uint64_t seq, int button = 1, bool p1 = true) {
    InputEvent e;
    e.origTick = t;
    e.down = down;
    e.seq = seq;
    e.button = button;
    e.player1 = p1;
    return e;
}

// ---- config / limits -------------------------------------------------------

static void testLimitsAndActivation() {
    Config c = cfgWith(Mode::Smart, 4);
    Limits l = resolveLimits(c);
    CHECK(l.early == 4 && l.late == 4 && l.baseDelay == 4);

    c.symmetric = false; c.maxEarly = 2; c.maxLate = 7;
    l = resolveLimits(c);
    CHECK(l.early == 2 && l.late == 7 && l.baseDelay == 2);

    c.maxEarly = 99; c.maxLate = -3;  // out of range gets clamped
    l = resolveLimits(c);
    CHECK(l.early == kMaxWindow && l.late == 0);

    c = cfgWith(Mode::Smart, 0);
    CHECK(!isActive(c, true));                    // 0 ticks == truly off
    CHECK(resolveLimits(c).baseDelay == 0);       // and no latency

    c = cfgWith(Mode::Smart, 3);
    c.enabled = false;
    CHECK(!isActive(c, true));
    c.enabled = true; c.mode = Mode::Disabled;
    CHECK(!isActive(c, true));

    c = cfgWith(Mode::Smart, 3);
    c.practiceOnly = true;
    CHECK(!isActive(c, false));
    CHECK(isActive(c, true));

    c = cfgWith(Mode::Random, 5);
    c.direction = Direction::LaterOnly;
    l = resolveLimits(c);
    CHECK(l.early == 0 && l.late == 5 && l.baseDelay == 0);  // no latency when never early
    c.direction = Direction::EarlierOnly;
    l = resolveLimits(c);
    CHECK(l.early == 5 && l.late == 0 && l.baseDelay == 5);

    c.direction = Direction::LaterOnly; c.window = 0;
    CHECK(!isActive(c, true));
    c.direction = Direction::EarlierOnly; c.symmetric = false; c.maxEarly = 0; c.maxLate = 9;
    CHECK(!isActive(c, true));  // earlier-only with 0 early == nothing allowed

    CHECK(parseMode("Random") == Mode::Random && parseMode("x") == Mode::Smart);
    CHECK(parseDirection("Earlier only") == Direction::EarlierOnly);
    CHECK(parseAffect("Releases only") == Affect::ReleasesOnly);
}

// ---- smart -----------------------------------------------------------------

static void testSmartSmallestShift() {
    Config c = cfgWith(Mode::Smart, 5);
    Limits lim = resolveLimits(c);
    Randomizer rng(1);
    InputEvent e = ev(100, true, 0);

    // 1-tick fix exists (+1) and so do bigger ones: must pick +1, not +4/+5.
    auto p = survivesAtShifts(lim, {+1, +4, +5});
    Decision d = decide(e, c, lim, rng, p);
    CHECK(d.reason == Reason::SmartShift && d.shift == +1);

    // equal magnitude: tie goes earlier, deterministically
    p = survivesAtShifts(lim, {-2, +2});
    d = decide(e, c, lim, rng, p);
    CHECK(d.shift == -2);

    // reference timing fine -> unchanged, no further candidates evaluated
    p = survivesAtShifts(lim, {0, +1});
    d = decide(e, c, lim, rng, p);
    CHECK(d.reason == Reason::OriginalOk && d.shift == 0 && p.asked.size() == 1);

    // nothing helps -> unchanged
    p = survivesAtShifts(lim, {});
    d = decide(e, c, lim, rng, p);
    CHECK(d.reason == Reason::NoCandidateSurvives && d.shift == 0);
}

static void testSmartRespectsLimits() {
    Config c = cfgWith(Mode::Smart, 0);
    c.symmetric = false; c.maxEarly = 0; c.maxLate = 3;
    Limits lim = resolveLimits(c);
    Randomizer rng(1);
    InputEvent e = ev(10, true, 0);

    // only an earlier shift would help, but early limit is 0 -> must not use it
    auto p = survivesAtShifts(lim, {-1});
    Decision d = decide(e, c, lim, rng, p);
    CHECK(d.shift == 0 && d.reason == Reason::NoCandidateSurvives);
    for (int off : p.asked) CHECK(off >= lim.baseDelay && off <= lim.baseDelay + lim.late);

    // 4 would help but late limit is 3
    p = survivesAtShifts(lim, {+4});
    d = decide(e, c, lim, rng, p);
    CHECK(d.shift == 0);

    p = survivesAtShifts(lim, {+3});
    d = decide(e, c, lim, rng, p);
    CHECK(d.shift == +3);

    // early-only asymmetric: never asks for a positive offset beyond reference
    c.maxEarly = 4; c.maxLate = 0;
    lim = resolveLimits(c);
    p = survivesAtShifts(lim, {-4});
    d = decide(e, c, lim, rng, p);
    CHECK(d.shift == -4);
    for (int off : p.asked) CHECK(off >= 0 && off <= lim.baseDelay);
}

static void testSmartUnreliable() {
    Config c = cfgWith(Mode::Smart, 3);
    Limits lim = resolveLimits(c);
    Randomizer rng(1);
    InputEvent e = ev(10, true, 0);

    NullPredictor np;
    Decision d = decide(e, c, lim, rng, np);
    CHECK(d.shift == 0 && d.reason == Reason::PredictionUnavailable);

    FnPredictor p;
    p.fn = [](const InputEvent&, int) {
        PredictionResult r;
        r.status = PredictionResult::Status::Uncertain;
        return r;
    };
    d = decide(e, c, lim, rng, p);
    CHECK(d.shift == 0 && d.reason == Reason::PredictionUncertain);

    // baseline bad, one uncertain candidate, a later certain one -> skip uncertain
    FnPredictor q;
    q.fn = [lim](const InputEvent&, int off) {
        int s = off - lim.baseDelay;
        PredictionResult r;
        if (s == 0) return ok(2);
        if (s == -1) { r.status = PredictionResult::Status::Uncertain; return r; }
        if (s == +1) return ok(100);
        return ok(2);
    };
    d = decide(e, c, lim, rng, q);
    CHECK(d.shift == +1);
}

// ---- random ----------------------------------------------------------------

static std::vector<int> runRandom(const Config& c, uint32_t seed, int n) {
    Limits lim = resolveLimits(c);
    Randomizer rng(seed);
    NullPredictor np;
    std::vector<int> out;
    for (int i = 0; i < n; ++i) out.push_back(decide(ev(i, i % 2 == 0, i), c, lim, rng, np).shift);
    return out;
}

static void testRandom() {
    Config c = cfgWith(Mode::Random, 4);
    c.probabilityPct = 100;
    auto a = runRandom(c, 777, 2000);
    auto b = runRandom(c, 777, 2000);
    auto d = runRandom(c, 778, 2000);
    CHECK(a == b);  // reproducible
    CHECK(a != d);  // seed matters

    std::set<int> seen;
    for (int s : a) {
        CHECK(s >= -4 && s <= 4 && s != 0);  // limit never exceeded; 100% => always shifted
        seen.insert(s);
    }
    CHECK(seen.size() == 8);  // every allowed non-zero shift occurs

    c.probabilityPct = 0;
    for (int s : runRandom(c, 5, 500)) CHECK(s == 0);

    c.probabilityPct = 25;
    int shifted = 0;
    for (int s : runRandom(c, 99, 20000)) shifted += (s != 0);
    double rate = shifted / 20000.0;
    CHECK(rate > 0.22 && rate < 0.28);

    // direction restrictions
    c.probabilityPct = 100; c.direction = Direction::LaterOnly;
    for (int s : runRandom(c, 3, 500)) CHECK(s >= 1 && s <= 4);
    c.direction = Direction::EarlierOnly;
    for (int s : runRandom(c, 3, 500)) CHECK(s <= -1 && s >= -4);

    // asymmetric limits
    c = cfgWith(Mode::Random, 0);
    c.symmetric = false; c.maxEarly = 1; c.maxLate = 6; c.probabilityPct = 100;
    for (int s : runRandom(c, 11, 2000)) CHECK(s >= -1 && s <= 6 && s != 0);

    // window 10 / window 1
    c = cfgWith(Mode::Random, 10); c.probabilityPct = 100;
    for (int s : runRandom(c, 12, 2000)) CHECK(s >= -10 && s <= 10);
    c = cfgWith(Mode::Random, 1); c.probabilityPct = 100;
    for (int s : runRandom(c, 12, 500)) CHECK(s == -1 || s == 1);
}

static void testRandomAffectFilterAndAlignment() {
    Config c = cfgWith(Mode::Random, 3);
    c.probabilityPct = 100; c.affect = Affect::PressesOnly;
    Limits lim = resolveLimits(c);
    Randomizer rng(42);
    NullPredictor np;
    Decision r = decide(ev(0, false, 0), c, lim, rng, np);
    CHECK(r.reason == Reason::IneligibleType && r.shift == 0);
    Decision p = decide(ev(1, true, 1), c, lim, rng, np);
    CHECK(p.reason == Reason::RandomShift);

    // ineligible events consume no draws: press results identical with or without releases between
    Randomizer r1(9), r2(9);
    Decision a1 = decide(ev(0, true, 0), c, lim, r1, np);
    decide(ev(1, false, 1), c, lim, r2, np);
    decide(ev(2, false, 2), c, lim, r2, np);
    Decision a2 = decide(ev(0, true, 0), c, lim, r2, np);
    CHECK(a1.shift == a2.shift);

    c.affect = Affect::ReleasesOnly;
    CHECK(decide(ev(0, true, 0), c, lim, rng, np).reason == Reason::IneligibleType);
    CHECK(decide(ev(0, false, 0), c, lim, rng, np).reason == Reason::RandomShift);
}

// ---- scheduler -------------------------------------------------------------

static void testSchedulerBasics() {
    InputScheduler s;
    // inactive passthrough: immediate, zero latency
    Scheduled r = s.submit(ev(50, true, 0), 0, 0);
    CHECK(r.immediate && r.latency == 0 && s.pendingCount() == 0);

    // window 3: baseDelay 3, shift 0 => 3 ticks latency, queued
    s = InputScheduler();
    r = s.submit(ev(50, true, 0), 0, 3);
    CHECK(!r.immediate && r.deliverTick == 53 && r.latency == 3 && r.shift == 0);
    CHECK(s.due(52).empty());
    auto d = s.due(53);
    CHECK(d.size() == 1 && d[0].down);

    // shift -3 with baseDelay 3 => delivered at arrival, immediate
    s = InputScheduler();
    r = s.submit(ev(10, true, 0), -3, 3);
    CHECK(r.immediate && r.latency == 0 && r.shift == -3);

    // shift beyond what base delay allows cannot go before arrival
    s = InputScheduler();
    r = s.submit(ev(10, true, 0), -5, 3);
    CHECK(r.deliverTick == 10 && r.clamped);
}

static void testQuickTapKeepsOrder() {
    // press at 10, release at 12. press pushed late (+5), release pushed early (-3).
    InputScheduler s;
    const int B = 3;
    Scheduled p = s.submit(ev(10, true, 0), +5, B);   // wants 18
    Scheduled r = s.submit(ev(12, false, 1), -3, B);  // wants 12, must stay after press
    CHECK(p.deliverTick == 18);
    CHECK(r.deliverTick == 19 && r.clamped);          // one tick after the press
    auto out = s.flushAll();
    CHECK(out.size() == 2 && out[0].down && !out[1].down);
    CHECK(!s.deliveredDown(ev(0, true, 0).streamKey()));  // ends released

    // press and release on the same arrival tick keep their order
    s = InputScheduler();
    s.submit(ev(5, true, 0), +2, 2);
    s.submit(ev(5, false, 1), -2, 2);
    out = s.flushAll();
    CHECK(out.size() == 2 && out[0].down && !out[1].down);
}

static void testStreamsAreIndependent() {
    InputScheduler s;
    s.submit(ev(10, true, 0, 1, true), +4, 4);   // p1 jump delayed
    Scheduled other = s.submit(ev(10, true, 1, 1, false), 0, 0);  // p2 not affected
    CHECK(other.immediate);
}

static void testFlushOnDisable() {
    InputScheduler s;
    s.submit(ev(1, true, 0), 0, 3);
    s.submit(ev(2, false, 1), 0, 3);
    s.submit(ev(3, true, 2), 0, 3);
    CHECK(s.pendingCount() == 3);
    auto out = s.flushAll();
    CHECK(out.size() == 3 && out[0].down && !out[1].down && out[2].down);
    CHECK(s.pendingCount() == 0);
    CHECK(s.deliveredDown(ev(0, true, 0).streamKey()));
    // after disable, new input must not overtake nothing pending -> immediate
    Scheduled r = s.submit(ev(4, false, 3), 0, 0);
    CHECK(r.immediate);
}

static void testClearCompensatesRelease() {
    // game was told "down", release is still pending when the player dies
    InputScheduler s;
    s.submit(ev(1, true, 0), 0, 0);              // immediate press
    s.submit(ev(5, false, 1), 0, 3);             // release queued for tick 8
    CHECK(s.deliveredDown(ev(0, true, 0).streamKey()));
    auto comp = s.clear(true);
    CHECK(comp.size() == 1 && !comp[0].down && comp[0].button == 1 && comp[0].player1);
    CHECK(s.pendingCount() == 0);

    // dropped press+release pair, game never saw the press -> nothing to compensate
    s = InputScheduler();
    s.submit(ev(1, true, 0), 0, 3);
    s.submit(ev(2, false, 1), 0, 3);
    CHECK(s.clear(true).empty());

    // still-held at the end (final state down): no release is invented
    s = InputScheduler();
    s.submit(ev(1, true, 0), 0, 0);
    CHECK(s.clear(true).empty());

    // player 2 stream decodes correctly
    s = InputScheduler();
    s.submit(ev(1, true, 0, 2, false), 0, 0);
    s.submit(ev(3, false, 1, 2, false), 0, 2);
    comp = s.clear(true);
    CHECK(comp.size() == 1 && comp[0].button == 2 && !comp[0].player1);

    // compensate=false (level quit) returns nothing
    s = InputScheduler();
    s.submit(ev(1, true, 0), 0, 0);
    s.submit(ev(3, false, 1), 0, 2);
    CHECK(s.clear(false).empty());
}

static void testRapidTapsAndHolds() {
    // 30 rapid taps (press/release alternating each tick) under heavy random shifts
    Config c = cfgWith(Mode::Random, 5);
    c.probabilityPct = 100;
    Limits lim = resolveLimits(c);
    Randomizer rng(2024);
    NullPredictor np;
    InputScheduler s;
    std::vector<InputEvent> delivered;
    uint64_t seq = 0;
    for (int64_t t = 0; t < 80; ++t) {
        if (t < 60) {
            InputEvent e = ev(t, t % 2 == 0, seq++);
            Decision d = decide(e, c, lim, rng, np);
            Scheduled sc = s.submit(e, d.shift, lim.baseDelay);
            if (sc.immediate) delivered.push_back(e);
        }
        for (auto& e : s.due(t)) delivered.push_back(e);
    }
    CHECK(delivered.size() == 60);
    for (size_t i = 0; i < delivered.size(); ++i) CHECK(delivered[i].down == (i % 2 == 0));  // alternation intact
    CHECK(!s.deliveredDown(1 * 2));  // ends released, not stuck

    // long hold: press, many ticks, release
    s = InputScheduler();
    Scheduled p = s.submit(ev(0, true, 0), +3, 3);
    Scheduled r = s.submit(ev(500, false, 1), -3, 3);
    CHECK(p.deliverTick == 6 && r.deliverTick == 500);
    CHECK(r.latency == 0);
}

// ---- randomized invariant sweep -------------------------------------------

static void testInvariantFuzz() {
    std::mt19937 gen(1234);
    int runs = 0;
    for (int iter = 0; iter < 400; ++iter) {
        Config c;
        c.practiceOnly = false;
        c.mode = Mode::Random;
        c.symmetric = gen() % 2;
        c.window = gen() % 11;
        c.maxEarly = gen() % 11;
        c.maxLate = gen() % 11;
        c.probabilityPct = 20 + gen() % 81;
        c.direction = static_cast<Direction>(gen() % 3);
        c.affect = Affect::All;
        Limits lim = resolveLimits(c);

        Randomizer rng(gen());
        NullPredictor np;
        InputScheduler s;
        std::map<int, std::vector<InputEvent>> submitted, delivered;
        std::map<uint64_t, Scheduled> sched;
        std::map<int, bool> held;
        uint64_t seq = 0;
        const int T = 300;

        for (int64_t t = 0; t < T + 40; ++t) {
            if (t < T) {
                for (int key = 1; key <= 3; ++key) {
                    if (gen() % 100 < 12) {
                        InputEvent e = ev(t, !held[key], seq++, key, true);
                        held[key] = !held[key];
                        Decision d = decide(e, c, lim, rng, np);
                        Scheduled sc = s.submit(e, d.shift, lim.baseDelay);
                        submitted[e.streamKey()].push_back(e);
                        sched[e.seq] = sc;
                        if (sc.immediate) delivered[e.streamKey()].push_back(e);
                        CHECK(sc.deliverTick >= e.origTick);
                        CHECK(sc.deliverTick <= e.origTick + lim.baseDelay + lim.late);
                        CHECK(sc.shift >= -lim.early && sc.shift <= lim.late);
                        CHECK(sc.latency <= lim.baseDelay + lim.late);
                    }
                }
            }
            for (auto& e : s.due(t)) {
                delivered[e.streamKey()].push_back(e);
                CHECK(t >= e.origTick);
                CHECK(t == sched[e.seq].deliverTick);
            }
        }
        CHECK(s.pendingCount() == 0);
        for (auto& [key, sub] : submitted) {
            auto& del = delivered[key];
            CHECK(del.size() == sub.size());
            for (size_t i = 0; i < sub.size() && i < del.size(); ++i) CHECK(del[i].seq == sub[i].seq);  // same order
            for (size_t i = 1; i < del.size(); ++i) CHECK(del[i].down != del[i - 1].down);              // alternation
            if (!sub.empty()) CHECK(s.deliveredDown(key) == sub.back().down);                           // no stuck state
        }
        ++runs;
    }
    CHECK(runs == 400);
}

int main() {
    testLimitsAndActivation();
    testSmartSmallestShift();
    testSmartRespectsLimits();
    testSmartUnreliable();
    testRandom();
    testRandomAffectFilterAndAlignment();
    testSchedulerBasics();
    testQuickTapKeepsOrder();
    testStreamsAreIndependent();
    testFlushOnDisable();
    testClearCompensatesRelease();
    testRapidTapsAndHolds();
    testInvariantFuzz();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
