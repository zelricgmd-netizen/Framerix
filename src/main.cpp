// FrameFix: Geode glue.
//
// STATUS: NOT COMPILED, NOT LOADED, NOT TESTED IN-GAME. This file was written
// without access to the Geode headers/bindings (the authoring sandbox had no
// network). Every hook name and signature below comes from memory of the
// bindings and must be checked by a real build. If a signature is wrong the
// compiler will say so; fix it against the bindings for your GD version.
//
// Assumptions to verify (see docs/REPORT.md, "Unverified assumptions"):
//   A1  void GJBaseGameLayer::handleButton(bool down, int button, bool isPlayer1)
//   A2  void GJBaseGameLayer::processCommands(float dt), called once per physics step
//   A3  PlayLayer::m_isPracticeMode is a bool that reliably reflects Practice Mode
//   A4  void PlayLayer::resetLevel(), onQuit(), levelComplete() exist as written
//   A5  Calling layer->handleButton(...) from outside a hook re-enters our hook
//       (hence the `delivering` guard)

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "core/Config.hpp"
#include "core/CorrectionEngine.hpp"
#include "core/InputScheduler.hpp"
#include "core/Predictor.hpp"
#include "core/Randomizer.hpp"

using namespace geode::prelude;

namespace {

struct Runtime {
    ff::Config cfg;
    ff::InputScheduler sched;
    ff::Randomizer rng;
    ff::NullPredictor predictor;  // no verified physics predictor exists yet
    int64_t tick = 0;             // our own count of processCommands calls this attempt
    uint64_t nextSeq = 0;
    uint32_t lastSeed = 0;
    bool delivering = false;
};

Runtime& rt() {
    static Runtime r;
    return r;
}

// Settings are polled (on restart and every 30 ticks) instead of using change
// listeners, to avoid depending on listener APIs that differ between SDK versions.
void loadConfig() {
    auto* m = Mod::get();
    auto& r = rt();
    auto& c = r.cfg;
    c.enabled = m->getSettingValue<bool>("enabled");
    c.mode = ff::parseMode(m->getSettingValue<std::string>("mode"));
    c.practiceOnly = m->getSettingValue<bool>("practice-only");
    c.window = static_cast<int>(m->getSettingValue<int64_t>("window"));
    c.symmetric = m->getSettingValue<bool>("symmetric");
    c.maxEarly = static_cast<int>(m->getSettingValue<int64_t>("max-early"));
    c.maxLate = static_cast<int>(m->getSettingValue<int64_t>("max-late"));
    c.probabilityPct = static_cast<int>(m->getSettingValue<int64_t>("random-probability"));
    c.direction = ff::parseDirection(m->getSettingValue<std::string>("random-direction"));
    c.affect = ff::parseAffect(m->getSettingValue<std::string>("random-affect"));
    c.seed = static_cast<uint32_t>(m->getSettingValue<int64_t>("random-seed"));
    c.reseedOnRestart = m->getSettingValue<bool>("reseed-on-restart");
    c.debug = m->getSettingValue<bool>("debug-log");
    if (c.seed != r.lastSeed) {
        r.lastSeed = c.seed;
        r.rng.reseed(c.seed);
    }
}

bool inPractice() {
    auto* pl = PlayLayer::get();
    return pl && pl->m_isPracticeMode;  // A3
}

// Only act on the live PlayLayer, never on the editor or other base-layer users.
bool isOurLayer(GJBaseGameLayer* layer) {
    auto* pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

void deliver(GJBaseGameLayer* layer, std::vector<ff::InputEvent> const& evs) {
    auto& r = rt();
    r.delivering = true;
    for (auto const& e : evs) layer->handleButton(e.down, e.button, e.player1);
    r.delivering = false;
}

} // namespace

$on_mod(Loaded) {
    loadConfig();
}

class $modify(FFBaseGameLayer, GJBaseGameLayer) {
    // Interception point. While FrameFix is inactive and nothing is pending
    // this forwards immediately, so there is no added latency.
    void handleButton(bool down, int button, bool isPlayer1) {
        auto& r = rt();
        if (r.delivering || !isOurLayer(this)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        ff::InputEvent ev;
        ev.origTick = r.tick;
        ev.down = down;
        ev.button = button;
        ev.player1 = isPlayer1;
        ev.seq = r.nextSeq++;

        const bool active = ff::isActive(r.cfg, inPractice());
        const ff::Limits lim = active ? ff::resolveLimits(r.cfg) : ff::Limits{};
        const ff::Decision d = active ? ff::decide(ev, r.cfg, lim, r.rng, r.predictor)
                                      : ff::Decision{0, ff::Reason::Inactive};

        const ff::Scheduled s = r.sched.submit(ev, d.shift, lim.baseDelay);

        if (r.cfg.debug) {
            log::debug("[FrameFix] {} btn={} p1={} origTick={} deliverTick={} shift={} latency={} clamped={} reason=\"{}\"",
                       down ? "press" : "release", button, isPlayer1, ev.origTick, s.deliverTick,
                       s.shift, s.latency, s.clamped, ff::reasonText(d.reason));
        }

        if (s.immediate) GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Tick clock + delivery point.
    void processCommands(float dt) {
        auto& r = rt();
        if (!isOurLayer(this)) {
            GJBaseGameLayer::processCommands(dt);
            return;
        }

        if (r.tick % 30 == 0) loadConfig();

        // If the mod became inactive (toggle, mode, practice off, window 0),
        // deliver everything still held back, in order, so nothing sticks.
        const bool active = ff::isActive(r.cfg, inPractice());
        auto evs = active ? r.sched.due(r.tick) : r.sched.flushAll();
        if (!evs.empty()) deliver(this, evs);

        GJBaseGameLayer::processCommands(dt);
        ++r.tick;
    }
};

class $modify(FFPlayLayer, PlayLayer) {
    void resetLevel() {
        auto& r = rt();
        loadConfig();
        // Drop pending inputs from the previous attempt. If the game was last
        // told "held" but the final scheduled state is "released", send the
        // release after the reset so the player cannot start the attempt stuck.
        auto comp = r.sched.clear(true);
        r.tick = 0;
        if (r.cfg.reseedOnRestart) r.rng.reseed(r.cfg.seed);
        PlayLayer::resetLevel();
        if (!comp.empty()) deliver(this, comp);
    }

    void onQuit() {
        rt().sched.clear(false);
        rt().tick = 0;
        PlayLayer::onQuit();
    }

    void levelComplete() {
        rt().sched.clear(false);
        PlayLayer::levelComplete();
    }
};
