// Stereo phase correlation, -1 .. +1, smoothed over ~300 ms.
//   +1  L and R identical (mono-safe)       0  unrelated (wide)
//   -1  L = -R: the mix cancels in mono
// r = <LR> / sqrt(<L^2><R^2>), each term a one-pole average. Below about -60 dBFS
// there is no meaningful reading and hasSignal() is false.
// Standard library only; realtime safe.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

class Correlation
{
public:
    void prepare(double sampleRate, double tauS = 0.3) { a_ = 1.0 - std::exp(-1.0 / (tauS * sampleRate)); reset(); }
    void reset() { lr_ = ll_ = rr_ = 0.0; }

    inline void process(double l, double r)
    {
        lr_ += a_ * (l * r - lr_);
        ll_ += a_ * (l * l - ll_);
        rr_ += a_ * (r * r - rr_);
        if (ll_ < 1e-30) ll_ = 0.0;
        if (rr_ < 1e-30) rr_ = 0.0;
        if (std::fabs(lr_) < 1e-30) lr_ = 0.0;
    }

    bool hasSignal() const
    {
        constexpr double kFloor = 1e-6;   // mean square of about -60 dBFS
        return ll_ > kFloor || rr_ > kFloor;
    }

    double value() const
    {
        const double d = std::sqrt(ll_ * rr_);
        if (d <= 1e-12) return 0.0;      // one side silent: unrelated
        double r = lr_ / d;
        if (r > 1.0) r = 1.0;
        if (r < -1.0) r = -1.0;
        return r;
    }

private:
    double a_ = 0.0001;
    double lr_ = 0.0, ll_ = 0.0, rr_ = 0.0;
};

}} // namespace slowops::vumeter
