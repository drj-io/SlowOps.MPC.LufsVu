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

    // Clip threshold as a linear sample magnitude (1.0 = 0 dBFS, 0.891 = -1 dBFS ...).
    void setThreshold(float linear) { threshold_ = linear > 0.0f ? linear : 1.0f; }

    // holdEnabled: the 2 s latch. Without it the indicator lights for the UI frame in which a clip occurred.
    void setHold(bool enabled) { hold_ = enabled; if (!enabled) holdLeft_ = 0; }

    inline void process(float x)
    {
        const float a = std::fabs(x);              // NaN compares false below: not counted as a clip
        if (a > peak_ && a < 1e30f) peak_ = a;
        if (a >= threshold_)
        {
            hit_ = true;
            ++hits_;
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
    // Total clipped samples ever seen (never reset): lets a caller detect new clip events.
    uint64_t hits() const { return hits_; }
    float peakSinceReset() const { return peak_; }
    void clearPeak() { peak_ = 0.0f; }

private:
    bool hold_ = true;
    float threshold_ = 1.0f;
    uint64_t hits_ = 0;
    bool hit_ = false;
    int64_t holdLeft_ = 0;
    int64_t holdLength_ = 88200;
    float peak_ = 0.0f;
};

}} // namespace slowops::vumeter
