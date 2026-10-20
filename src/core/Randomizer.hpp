#pragma once
#include <cstdint>

namespace ff {

// SplitMix64. Own implementation on purpose: std::uniform_int_distribution is
// implementation-defined, so the same seed could differ across platforms.
class Randomizer {
public:
    explicit Randomizer(uint64_t seed = 0) { reseed(seed); }

    void reseed(uint64_t seed) { m_state = seed; }

    uint64_t next() {
        uint64_t z = (m_state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Value in [0, n). Modulo bias is below n / 2^64, which is negligible here.
    uint32_t below(uint32_t n) { return n == 0 ? 0u : static_cast<uint32_t>(next() % n); }

private:
    uint64_t m_state = 0;
};

} // namespace ff
