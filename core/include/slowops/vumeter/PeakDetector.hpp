// Sample-peak clip detector with an optional sample-counted hold latch.
// This is NOT a VU measurement and NOT true-peak (no oversampling).
#pragma once
#include <cmath>
#include <cstdint>

namespace slowops { namespace vumeter {

class PeakDetector
{
public:
    void prepare(double sampleRate) { holdLength_ = (int64_t)std::llround(2.0 * sampleRate); reset(); }
    void reset() { hit_ = false; holdLeft_ = 0; peak_ = 0.0f; }

    // holdEnabled: the 2 s latch. Without it the indicator lights for the UI frame in which a clip occurred.
    void setHold(bool enabled) { hold_ = enabled; if (!enabled) holdLeft_ = 0; }

    inline void process(float x)
    {
        const float a = std::fabs(x);              // NaN compares false below: not counted as a clip
        if (a > peak_ && a < 1e30f) peak_ = a;
        if (a >= 1.0f)
        {
            hit_ = true;
            if (hold_) holdLeft_ = holdLength_;
        }
        else if (holdLeft_ > 0)
        {
            --holdLeft_;
        }
    }

    // Called once per UI frame: is the indicator lit? Consumes the per-frame hit flag.
    bool consume()
    {
        const bool lit = hit_ || holdLeft_ > 0;
        hit_ = false;
        return lit;
    }

    bool lit() const { return hit_ || holdLeft_ > 0; }
    float peakSinceReset() const { return peak_; }
    void clearPeak() { peak_ = 0.0f; }

private:
    bool hold_ = true;
    bool hit_ = false;
    int64_t holdLeft_ = 0;
    int64_t holdLength_ = 88200;
    float peak_ = 0.0f;
};

}} // namespace slowops::vumeter
