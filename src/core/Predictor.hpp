#pragma once
#include "Types.hpp"

namespace ff {

struct PredictionResult {
    enum class Status { Unavailable, Uncertain, Ok };
    Status status = Status::Unavailable;
    // Ticks the player is predicted to survive after the input is applied,
    // counted up to the predictor's horizon. Only meaningful when status == Ok.
    int survivalTicks = 0;
};

// Prediction is deliberately separate from scheduling so either can be
// replaced or tested alone. An implementation must NOT mutate live gameplay
// state.
//
// deliveryOffsetTicks: how many ticks after the event's arrival the input
// would be applied in this hypothetical (0 = at once, never negative).
class IPredictor {
public:
    virtual ~IPredictor() = default;
    virtual int horizonTicks() const = 0;
    virtual PredictionResult evaluate(const InputEvent& ev, int deliveryOffsetTicks) = 0;
};

// What ships today. There is no verified shadow simulation of Geometry Dash's
// physics in this project, so Smart mode reports "unavailable" and leaves
// every input unchanged. That is intentional: no invented physics.
class NullPredictor final : public IPredictor {
public:
    int horizonTicks() const override { return 0; }
    PredictionResult evaluate(const InputEvent&, int) override { return {}; }
};

} // namespace ff
