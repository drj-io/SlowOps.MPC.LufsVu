// Smoothed output gain with an exact unity path.
// When the smoothed gain has settled at exactly 1.0 (Trim 0.0 dB, or Bypass),
// samples are copied untouched so the output is bit-identical to the input.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

class Trim
{
public:
    void prepare(double sampleRate, double smoothMs = 20.0)
    {
        // One-pole smoother reaching ~99.9 % in smoothMs.
        coeff_ = std::exp(-6.9 / (smoothMs * 0.001 * sampleRate));
        gain_ = target_;
    }

    static float dbToGain(float db) { return db == 0.0f ? 1.0f : (float)std::pow(10.0, (double)db / 20.0); }

    void setTargetDb(float db) { target_ = dbToGain(db); }
    void setTargetGain(float g) { target_ = g; }
    void snap() { gain_ = target_; }

    bool settledAtUnity() const { return gain_ == 1.0 && target_ == 1.0f; }

    // Advance one sample and return the gain to apply.
    inline float next()
    {
        if (gain_ != (double)target_)
        {
            // Smoothed in double so the ramp cannot stall a few ULPs short of the target;
            // it snaps exactly onto the target so unity becomes a true bit-exact copy.
            gain_ = (double)target_ + coeff_ * (gain_ - (double)target_);
            if (std::fabs(gain_ - (double)target_) < 1e-5) gain_ = (double)target_;
        }
        return (float)gain_;
    }

    float current() const { return (float)gain_; }
    float target() const { return target_; }

private:
    double coeff_ = 0.0;
    double gain_ = 1.0;
    float target_ = 1.0f;
};

}} // namespace slowops::vumeter
