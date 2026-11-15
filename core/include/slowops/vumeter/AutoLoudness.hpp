// AUTO loudness: gently steers the programme's integrated loudness towards a target.
//
// It listens to the plugin INPUT with its own LoudnessMeter (never to its own output, so
// there is no feedback loop) and applies one common gain to L and R:
//     desired = clamp(target - inputIntegrated, -12 dB, +12 dB)
// re-evaluated once a second and approached at no more than 0.5 dB/s, with a deadband
// (starts moving beyond 0.3 LU of error, stops within 0.05). It never chases every
// 3-second window: the reference is the gated programme loudness since CLEAR.
//
// States: LEARNING  fewer than 3 s of gated programme since CLEAR / switch-on: gain held
//         HOLD      silence or a passage far below the programme (input momentary below
//                   -70 LUFS or more than 20 LU under it): gain held, noise is never boosted
//         GAIN LIMIT the target needs more than +-12 dB
//         LIMITED   the limiter is doing heavy work (reported by the caller)
//         ADJUSTING / IN RANGE
// Switching AUTO off returns its gain to 0 dB at 2 dB/s. clear() restarts the estimate but
// keeps the current gain (no jump). A best-effort live estimate, not a guarantee: the
// future of the programme is unknown.
// Standard library only.
#pragma once
#include "LoudnessMeter.hpp"
#include <cmath>
#include <cstdint>

namespace slowops { namespace vumeter {

class AutoLoudness
{
public:
    enum State { kOff = 0, kLearning, kAdjusting, kInRange, kHold, kGainLimit, kLimited, kNumStates };

    static constexpr double kMaxGainDb = 12.0;
    static constexpr double kRateDbPerS = 0.5;
    static constexpr double kOffRateDbPerS = 2.0;
    static constexpr double kLearnS = 3.0;
    static constexpr double kStartLU = 0.3, kStopLU = 0.05;
    static constexpr double kSilenceLU = 20.0;

    void prepare(double sampleRate) { sr_ = sampleRate; meter_.prepare(sampleRate); reset(); }

    void reset()
    {
        meter_.reset();
        gainDb_ = goalDb_ = 0.0;
        moving_ = false;
        lastHop_ = 0;
        state_ = enabled_ ? kLearning : kOff;
    }

    // Restart the loudness estimate; the gain stays where it is.
    void clear() { meter_.clear(); moving_ = false; lastHop_ = meter_.hops(); goalDb_ = gainDb_; }

    void setEnabled(bool on)
    {
        if (on == enabled_) return;
        enabled_ = on;
        moving_ = false;
        if (on) { meter_.clear(); goalDb_ = gainDb_; state_ = kLearning; }
        else    { goalDb_ = 0.0; state_ = kOff; }
    }
    void setTargetLufs(double lufs) { target_ = lufs; }
    void setLimited(bool limited) { limited_ = limited; }
    void setMonoInput(bool mono) { meter_.setMonoInput(mono); }

    inline void feed(double l, double r) { meter_.process(l, r); }

    // After feeding a block of n samples: update the state and move the gain.
    void advance(int n)
    {
        while (lastHop_ < meter_.hops()) { ++lastHop_; evaluate(lastHop_ % 10 == 0); }
        const double rate = enabled_ ? kRateDbPerS : kOffRateDbPerS;
        const double step = rate * (double)n / sr_;
        if (gainDb_ < goalDb_) gainDb_ = std::fmin(goalDb_, gainDb_ + step);
        else if (gainDb_ > goalDb_) gainDb_ = std::fmax(goalDb_, gainDb_ - step);
    }

    double gainDb() const { return gainDb_; }
    State state() const { return state_; }
    const LoudnessMeter& inputMeter() const { return meter_; }

private:
    void evaluate(bool recompute)
    {
        if (!enabled_) { state_ = kOff; goalDb_ = 0.0; return; }
        if (meter_.gatedSeconds() < kLearnS) { state_ = kLearning; goalDb_ = gainDb_; return; }
        const double in = meter_.integrated();
        const double m = meter_.momentary();
        if (!std::isfinite(in) || !(m > LoudnessMeter::kAbsGate) || m < in - kSilenceLU)
        {
            state_ = kHold; goalDb_ = gainDb_; moving_ = false;
            return;
        }
        if (recompute || state_ == kLearning || state_ == kHold)
        {
            const double raw = target_ - in;
            gainLimit_ = std::fabs(raw) > kMaxGainDb;
            desired_ = raw > kMaxGainDb ? kMaxGainDb : (raw < -kMaxGainDb ? -kMaxGainDb : raw);
        }
        const double err = desired_ - gainDb_;
        if (moving_ && std::fabs(err) < kStopLU) moving_ = false;
        else if (!moving_ && std::fabs(err) > kStartLU) moving_ = true;
        goalDb_ = moving_ ? desired_ : gainDb_;
        state_ = gainLimit_ ? kGainLimit : (limited_ ? kLimited : (moving_ ? kAdjusting : kInRange));
    }

    double sr_ = 48000.0;
    LoudnessMeter meter_;
    bool enabled_ = false, limited_ = false, moving_ = false, gainLimit_ = false;
    double target_ = -14.0, desired_ = 0.0, gainDb_ = 0.0, goalDb_ = 0.0;
    uint64_t lastHop_ = 0;
    State state_ = kOff;
};

}} // namespace slowops::vumeter
