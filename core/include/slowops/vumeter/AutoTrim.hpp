// AUTO TRIM: listen to the program for a few seconds, then suggest the trim change that
// makes it average 0 VU on the meter.
//
// The input is the needle deflection the user is looking at (linear, 0 VU = 1). Moments
// below the gate (-30 VU: gaps, silence, fade tails) are ignored, so a loop with breaks is
// judged on its music. The average is taken in the linear deflection domain, like the eye
// averages a moving needle, then converted to VU. Silence produces no change.
// Standard library only; realtime safe.
#pragma once
#include <cmath>
#include <cstdint>

namespace slowops { namespace vumeter {

class AutoTrim
{
public:
    static constexpr double kGateVU = -30.0;

    void start(double seconds, double sampleRate)
    {
        remaining_ = (int64_t)std::llround(seconds * sampleRate);
        sum_ = 0.0; count_ = 0; active_ = remaining_ > 0; done_ = false;
    }
    void cancel() { active_ = false; done_ = false; remaining_ = 0; }

    bool active() const { return active_; }

    // Feed one block: `deflection` is the needle reading at the end of the block (it is a
    // slow, smooth signal, so block-rate sampling is plenty). Weighted by block length.
    void feed(double deflection, int n)
    {
        if (!active_ || n <= 0) return;
        const double gate = std::pow(10.0, kGateVU / 20.0);
        if (deflection > gate && std::isfinite(deflection)) { sum_ += deflection * n; count_ += n; }
        remaining_ -= n;
        if (remaining_ <= 0) { active_ = false; done_ = true; }
    }

    // After listening: true once, with the correction in dB (0 if nothing was above the gate).
    bool takeResult(double& correctionDb)
    {
        if (!done_) return false;
        done_ = false;
        correctionDb = count_ > 0 ? -20.0 * std::log10(sum_ / (double)count_) : 0.0;
        return true;
    }

    bool heardSignal() const { return count_ > 0; }

private:
    int64_t remaining_ = 0;
    double sum_ = 0.0;
    int64_t count_ = 0;
    bool active_ = false, done_ = false;
};

}} // namespace slowops::vumeter
