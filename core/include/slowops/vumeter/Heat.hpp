// "React" lighting: how hot the program is, for the dial illumination.
//
// heatFromVU() maps a VU reading onto 0..1 along a thermal palette
// (deep blue -> ice -> amber -> red -> magenta -> purple), with the anchors set so that
// quiet program glows blue, a healthy 0 VU mix glows red and pinning into the red zone
// glows purple. HeatFollower adds glowing-filament ballistics on top of the needle:
// it warms up quickly and cools down slowly, so the colour breathes with the music
// instead of flickering with it. Purely cosmetic; never touches audio.
// Standard library only.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

// Piecewise-linear anchors (VU -> heat). Below the first anchor the dial is coldest blue.
struct HeatAnchor { double vu, heat; };
constexpr HeatAnchor kHeatAnchors[] = {
    { -20.0, 0.00 },   // deep blue: quiet
    { -10.0, 0.22 },   // ice blue
    {  -5.0, 0.42 },   // amber: getting warm
    {   0.0, 0.66 },   // red: 0 VU, hot enough
    {  +2.0, 0.86 },   // magenta
    {  +3.0, 1.00 },   // purple: pinned
};

inline double heatFromVU(double vu)
{
    constexpr int n = (int)(sizeof(kHeatAnchors) / sizeof(kHeatAnchors[0]));
    if (!(vu > kHeatAnchors[0].vu)) return 0.0;     // also catches NaN
    for (int i = 1; i < n; ++i)
        if (vu <= kHeatAnchors[i].vu)
        {
            const HeatAnchor& a = kHeatAnchors[i - 1];
            const HeatAnchor& b = kHeatAnchors[i];
            return a.heat + (vu - a.vu) / (b.vu - a.vu) * (b.heat - a.heat);
        }
    return 1.0;
}

class HeatFollower
{
public:
    // Time constants in seconds: warming (attack) and cooling (release).
    void setTimes(double attackS, double releaseS) { attack_ = attackS; release_ = releaseS; }
    void reset() { heat_ = 0.0; }

    // Advance by dt seconds towards the heat for `vu`; returns the current heat 0..1.
    double update(double vu, double dt)
    {
        const double target = heatFromVU(vu);
        const double tau = target > heat_ ? attack_ : release_;
        const double k = tau > 0.0 ? 1.0 - std::exp(-dt / tau) : 1.0;
        heat_ += (target - heat_) * k;
        if (!(heat_ >= 0.0)) heat_ = 0.0;
        if (heat_ > 1.0) heat_ = 1.0;
        return heat_;
    }

    double heat() const { return heat_; }

private:
    double attack_ = 0.12, release_ = 0.9;
    double heat_ = 0.0;
};

}} // namespace slowops::vumeter
