// VU metering engine: output trim + full-wave average-responding VU ballistics
// for Left, Right and the true mono sum M = 0.5*(L+R), plus sample-peak clip detection.
//
// Audio path:  in -> trim (smoothed) -> out. Meters and clip detectors read the
// output signal. With trim settled at unity the output is a bit-exact copy.
// Standard library only; realtime safe (no allocation, locks or I/O in process()).
#pragma once
#include "Correlation.hpp"
#include "NeedleDynamics.hpp"
#include "PeakDetector.hpp"
#include "Trim.hpp"
#include <cmath>
#include <cstring>

namespace slowops { namespace vumeter {

constexpr double kPi = 3.14159265358979323846;

enum Meter { kLeft = 0, kRight = 1, kMono = 2, kNumMeters = 3 };

// Dial: the drawn sweep ends at +3 VU. Linear-voltage instrument.
constexpr double kFullScaleVU = 3.0;
inline double fullScaleDeflection() { return std::pow(10.0, kFullScaleVU / 20.0); }

// Linear deflection (0 VU = 1) <-> VU reading in dB.
inline double deflectionToVU(double y) { return y > 0.0 ? 20.0 * std::log10(y) : -200.0; }

// Filmstrip frame for a deflection: linear in voltage, 0 = rest, frames-1 = +3 VU stop.
inline int deflectionToFrame(double y, int frames)
{
    double t = y / fullScaleDeflection();
    if (!(t > 0.0)) t = 0.0;
    if (t > 1.0) t = 1.0;
    return (int)std::lround(t * (frames - 1));
}

class VuEngine
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        for (auto& n : needles_) n.prepare(sampleRate);
        for (auto& p : peaks_) p.prepare(sampleRate);
        corr_.prepare(sampleRate);
        trim_.prepare(sampleRate);
        trim_.snap();
        reset();
    }

    void reset()
    {
        for (auto& n : needles_) n.reset();
        for (auto& p : peaks_) p.reset();
        corr_.reset();
        clearMax();
        trim_.snap();
    }

    // reference: 0 VU = a sine with this peak level in dBFS (e.g. -18).
    void setReferenceDbfs(double refDbfs)
    {
        const double aref = std::pow(10.0, refDbfs / 20.0);
        drive_ = (kPi / 2.0) / aref;    // mean |sin| = 2/pi * peak -> Aref-peak sine averages to 1.0
    }

    void setTrimDb(float db, bool bypass) { trim_.setTargetGain(bypass ? 1.0f : Trim::dbToGain(db)); }
    void setPeakHold(bool on) { for (auto& p : peaks_) p.setHold(on); }
    void resetClips() { for (auto& p : peaks_) p.reset(); }
    void setClipThresholdDbfs(double db) { for (auto& p : peaks_) p.setThreshold((float)std::pow(10.0, db / 20.0)); }

    // Highest needle deflection since the last clearMax() (what the needle actually showed).
    double maxDeflection(int m) const { return max_[m]; }
    void clearMax() { for (double& m : max_) m = 0.0; }

    const Correlation& correlation() const { return corr_; }

    // inR may equal inL (mono input). outL/outR may alias inL/inR (in place) and outR may be null.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (trim_.settledAtUnity())
        {
            for (int i = 0; i < n; ++i) meterFrame(inL[i], inR[i]);
            if (outL != nullptr && outL != inL) std::memcpy(outL, inL, sizeof(float) * (size_t)n);
            if (outR != nullptr && outR != inR) std::memcpy(outR, inR, sizeof(float) * (size_t)n);
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];    // read input before any output write
            const float g = trim_.next();
            const float yl = xl * g, yr = xr * g;
            if (outL != nullptr) outL[i] = yl;
            if (outR != nullptr) outR[i] = yr;
            meterFrame(yl, yr);
        }
    }

    double deflection(int m) const { return needles_[m].deflection(); }
    double rawDeflection(int m) const { return needles_[m].rawDeflection(); }
    double vu(int m) const { return deflectionToVU(deflection(m)); }

    // Per-UI-frame clip state; consumes per-frame hits.
    bool consumeClip(int ch) { return peaks_[ch].consume(); }
    bool clipLit(int ch) const { return peaks_[ch].lit(); }

    const Trim& trim() const { return trim_; }

private:
    inline void meterFrame(float l, float r)
    {
        peaks_[kLeft].process(l);
        peaks_[kRight].process(r);
        // Sanitise for the meter state only (audio is never altered by this).
        const double dl = std::isfinite(l) ? (double)l : 0.0;
        const double dr = std::isfinite(r) ? (double)r : 0.0;
        const double m = 0.5 * (dl + dr);               // sum BEFORE rectification
        needles_[kLeft].step(std::fabs(dl) * drive_);
        needles_[kRight].step(std::fabs(dr) * drive_);
        needles_[kMono].step(std::fabs(m) * drive_);
        corr_.process(dl, dr);
        for (int k = 0; k < kNumMeters; ++k)
        {
            const double y = needles_[k].deflection();
            if (y > max_[k]) max_[k] = y;
        }
    }

    double sampleRate_ = 44100.0;
    double drive_ = (kPi / 2.0) / 0.12589254117941673;
    NeedleDynamics needles_[kNumMeters];
    PeakDetector peaks_[2];
    Correlation corr_;
    double max_[kNumMeters] = { 0.0, 0.0, 0.0 };
    Trim trim_;
};

}} // namespace slowops::vumeter
