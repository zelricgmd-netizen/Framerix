#pragma once
// Geode-free core types. Everything under src/core builds and is unit-tested
// on a plain host compiler (see tests/).
#include <cstdint>

namespace ff {

// One button event as the game reported it.
// origTick = value of FrameFix's own tick counter when the event arrived.
struct InputEvent {
    int64_t origTick = 0;
    bool down = false;     // true = press, false = release
    int button = 1;        // game button id (1 = jump in classic mode)
    bool player1 = true;
    uint64_t seq = 0;      // arrival order, strictly increasing

    // Press/release ordering is only meaningful within one (button, player) stream.
    int streamKey() const { return button * 2 + (player1 ? 0 : 1); }
};

} // namespace ff
