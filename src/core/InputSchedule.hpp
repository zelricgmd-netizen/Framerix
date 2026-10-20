#pragma once
#include "Types.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace ff {

struct Scheduled {
    int64_t deliverTick = 0;
    int shift = 0;      // deliverTick - (origTick + baseDelay), after ordering clamp
    int latency = 0;    // deliverTick - origTick (what the player actually feels)
    bool immediate = false;  // deliver now; nothing was queued
    bool clamped = false;    // ordering rules moved it from the requested tick
};

// Holds inputs back and releases them at their delivery tick.
//
// Invariants (covered by tests/test_core.cpp):
//  - Events of one (button, player) stream are delivered in arrival order, so
//    a release can never overtake its press and nothing sticks.
//  - A release that arrived at least one tick after its press is delivered at
//    least one tick after it.
//  - Nothing is delivered before its arrival tick.
//  - No event is dropped except by clear(); flushAll() delivers everything.
class InputScheduler {
public:
    Scheduled submit(const InputEvent& ev, int shift, int baseDelay) {
        int64_t want = ev.origTick + baseDelay + shift;
        bool clamped = false;
        if (want < ev.origTick) {
            want = ev.origTick;
            clamped = true;
        }

        Stream& st = m_streams[ev.streamKey()];
        if (st.hasLast) {
            int64_t minTick = st.lastTick + (ev.origTick > st.lastOrig ? 1 : 0);
            if (want < minTick) {
                want = minTick;
                clamped = true;
            }
        }
        st.hasLast = true;
        st.lastTick = want;
        st.lastOrig = ev.origTick;
        st.lastDown = ev.down;

        Scheduled out;
        out.deliverTick = want;
        out.shift = static_cast<int>(want - (ev.origTick + baseDelay));
        out.latency = static_cast<int>(want - ev.origTick);
        out.clamped = clamped;

        if (want == ev.origTick && st.pending == 0) {
            out.immediate = true;
            st.deliveredDown = ev.down;
            return out;
        }

        Pending p{ev, want};
        auto it = std::upper_bound(m_pending.begin(), m_pending.end(), p,
                                   [](const Pending& a, const Pending& b) {
                                       return a.tick != b.tick ? a.tick < b.tick : a.ev.seq < b.ev.seq;
                                   });
        m_pending.insert(it, p);
        st.pending++;
        return out;
    }

    // Events whose delivery tick has been reached, in delivery order.
    std::vector<InputEvent> due(int64_t tick) {
        size_t n = 0;
        while (n < m_pending.size() && m_pending[n].tick <= tick) ++n;
        return take(n);
    }

    // Everything pending, in order. Used when the mod turns itself off.
    // Everything has been delivered ahead of its scheduled tick, so the
    // per-stream ordering floor is reset; otherwise the next input would be
    // held back behind ticks that no longer matter.
    std::vector<InputEvent> flushAll() {
        std::vector<InputEvent> out = take(m_pending.size());
        for (auto& [key, st] : m_streams) {
            st.hasLast = false;
            st.lastDown = st.deliveredDown;
        }
        return out;
    }

    // Drop everything (restart, death, quit). When `compensate` is true, returns
    // releases for streams the game was last told were held but whose final
    // scheduled state is released, so a dropped release cannot leave a button stuck.
    std::vector<InputEvent> clear(bool compensate) {
        std::vector<InputEvent> comp;
        if (compensate) {
            for (const auto& [key, st] : m_streams) {
                if (st.deliveredDown && !st.lastDown) {
                    InputEvent e;
                    e.down = false;
                    e.button = key / 2;
                    e.player1 = (key % 2 == 0);
                    comp.push_back(e);
                }
            }
        }
        m_pending.clear();
        m_streams.clear();
        return comp;
    }

    size_t pendingCount() const { return m_pending.size(); }
    bool hasPending(int streamKey) const {
        auto it = m_streams.find(streamKey);
        return it != m_streams.end() && it->second.pending > 0;
    }
    bool deliveredDown(int streamKey) const {
        auto it = m_streams.find(streamKey);
        return it != m_streams.end() && it->second.deliveredDown;
    }

private:
    struct Pending {
        InputEvent ev;
        int64_t tick;
    };
    struct Stream {
        bool hasLast = false;
        int64_t lastTick = 0;
        int64_t lastOrig = 0;
        bool lastDown = false;       // final scheduled state
        bool deliveredDown = false;  // state the game has actually been told
        int pending = 0;
    };

    std::vector<InputEvent> take(size_t n) {
        std::vector<InputEvent> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const InputEvent& e = m_pending[i].ev;
            out.push_back(e);
            Stream& st = m_streams[e.streamKey()];
            st.pending--;
            st.deliveredDown = e.down;
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<std::ptrdiff_t>(n));
        return out;
    }

    std::vector<Pending> m_pending;  // sorted by (tick, seq)
    std::map<int, Stream> m_streams;
};

} // namespace ff
