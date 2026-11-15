// Stereo-linked, true-peak-aware look-ahead limiter.
//
// Side chain (per sample n):
//   pk[n]  = max over L and R of the true-peak estimate (TruePeakDetector, oversampled)
//   r[n]   = min(req(pk[n]), req(pk[n-1])),  req(p) = min(1, ceiling / p)
//            (both samples around an inter-sample peak must be turned down)
//   h[n]   = min of r over the last W samples (W = look-ahead, 3 ms)
//   y[n]   = instant attack, program-dependent release towards h (fast after brief peaks,
//            slow while limiting is sustained, to avoid pumping and distortion)
//   g[n]   = mean of y over the last W samples (a smooth fade-in that still reaches the
//            required gain exactly when the peak arrives, because y <= h <= r there)
// The audio is delayed by W - 1 + D (D = the detector's group delay), so every output sample
// is multiplied by a gain no greater than its own requirement. One gain for both channels:
// the stereo image never moves.
//
// setEnabled() crossfades between the undelayed input and the delayed, limited signal over
// 20 ms (no click; the delay itself is reported to the host by the plugin). With setBypass()
// the limiter's gain glides to exactly 1, so the output is the delayed input, bit for bit.
// Standard library only; buffers are allocated in prepare(), never in process().
#pragma once
#include "TruePeak.hpp"
#include <cmath>
#include <vector>

namespace slowops { namespace vumeter {

class TruePeakLimiter
{
public:
    static constexpr double kLookaheadS = 0.003;
    static constexpr double kFadeS = 0.02;
    static constexpr double kMarginDb = 0.1;        // detection is an estimate: aim a little lower

    void prepare(double sampleRate)
    {
        sr_ = sampleRate;
        tpL_.prepare(sampleRate);
        tpR_.prepare(sampleRate);
        w_ = (int)std::lround(kLookaheadS * sampleRate);
        if (w_ < 2) w_ = 2;
        delay_ = w_ - 1 + tpL_.delay();        // >= 1
        holdV_.assign((size_t)w_ + 1, 1.0);
        holdI_.assign((size_t)w_ + 1, 0);
        box_.assign((size_t)w_, 1.0);
        dl_.assign((size_t)delay_, 0.0f);      // read-then-write ring: exactly delay_ samples
        dr_.assign((size_t)delay_, 0.0f);
        relFast_ = 1.0 - std::exp(-1.0 / (0.05 * sampleRate));
        relSlow_ = 1.0 - std::exp(-1.0 / (0.5 * sampleRate));
        sustainCoef_ = 1.0 - std::exp(-1.0 / (1.5 * sampleRate));
        fadeStep_ = 1.0 / (kFadeS * sampleRate);
        reset();
    }

    void reset()
    {
        tpL_.reset(); tpR_.reset();
        resetGain();
        for (float& v : dl_) v = 0.0f;
        for (float& v : dr_) v = 0.0f;
        dpos_ = 0;
        mix_ = enabled_ ? 1.0 : 0.0;
        bypassMix_ = bypass_ ? 1.0 : 0.0;
        peakGr_ = 0.0;
    }

    void setCeilingDb(double db) { ceiling_ = std::pow(10.0, (db - kMarginDb) / 20.0); }
    void setEnabled(bool on)
    {
        if (on && !enabled_ && mix_ <= 0.0) { tpL_.reset(); tpR_.reset(); resetGain(); }   // start from rest
        enabled_ = on;
    }
    void setBypass(bool on) { bypass_ = on; }

    bool enabled() const { return enabled_; }
    // True while any limited (delayed) signal reaches the output.
    bool active() const { return mix_ > 0.0; }
    int latencySamples() const { return delay_; }
    // Current gain reduction (dB, >= 0) and the largest since the last take.
    double gainReductionDb() const { return toDb(gain_); }
    double takePeakGainReductionDb() { const double g = peakGr_; peakGr_ = 0.0; return g; }

    // Process one stereo frame in place.
    inline void process(float& l, float& r)
    {
        const float inL = l, inR = r;
        // delay line (always written, so enabling has history ready)
        const float oldL = dl_[(size_t)dpos_], oldR = dr_[(size_t)dpos_];   // delayed by `delay_`
        dl_[(size_t)dpos_] = inL;
        dr_[(size_t)dpos_] = inR;
        dpos_ = dpos_ + 1 >= delay_ ? 0 : dpos_ + 1;

        const double target = enabled_ ? 1.0 : 0.0;
        if (mix_ != target) mix_ = mix_ < target ? std::fmin(target, mix_ + fadeStep_) : std::fmax(target, mix_ - fadeStep_);
        const double btarget = bypass_ ? 1.0 : 0.0;
        if (bypassMix_ != btarget)
            bypassMix_ = bypassMix_ < btarget ? std::fmin(btarget, bypassMix_ + fadeStep_) : std::fmax(btarget, bypassMix_ - fadeStep_);

        if (mix_ <= 0.0) { gain_ = 1.0; return; }        // off: untouched, zero latency

        const double g = sideChain(inL, inR);
        gain_ = g;
        const double gr = toDb(g);
        if (gr > peakGr_) peakGr_ = gr;
        const double ga = g + (1.0 - g) * bypassMix_;    // exactly 1 when bypassed
        const double wl = (double)oldL * ga, wr = (double)oldR * ga;
        if (mix_ >= 1.0) { l = (float)wl; r = (float)wr; return; }
        l = (float)((1.0 - mix_) * (double)inL + mix_ * wl);
        r = (float)((1.0 - mix_) * (double)inR + mix_ * wr);
    }

private:
    static double toDb(double g) { return g < 1.0 && g > 0.0 ? -20.0 * std::log10(g) : 0.0; }

    void resetGain()
    {
        for (double& v : box_) v = 1.0;
        boxSum_ = (double)w_;
        boxPos_ = 0;
        holdHead_ = holdTail_ = 0;
        n_ = 0;
        prevReq_ = 1.0;
        y_ = 1.0;
        sustain_ = 0.0;
        gain_ = 1.0;
    }

    inline double sideChain(float l, float r)
    {
        const double pk = std::fmax(tpL_.process(l), tpR_.process(r));
        const double req = pk > ceiling_ ? ceiling_ / pk : 1.0;
        const double rq = std::fmin(req, prevReq_);
        prevReq_ = req;

        // sliding minimum over the last w_ samples (monotonic queue in a ring of w_+1)
        const size_t cap = holdV_.size();
        while (holdHead_ != holdTail_)
        {
            const size_t last = (holdTail_ + cap - 1) % cap;
            if (holdV_[last] >= rq) holdTail_ = last; else break;
        }
        holdV_[holdTail_] = rq; holdI_[holdTail_] = n_;
        holdTail_ = (holdTail_ + 1) % cap;
        while (holdI_[holdHead_] + (long long)w_ <= n_) holdHead_ = (holdHead_ + 1) % cap;
        const double h = holdV_[holdHead_];
        ++n_;

        // instant attack, program-dependent release
        sustain_ += sustainCoef_ * ((y_ < 0.944 ? 1.0 : 0.0) - sustain_);   // limiting by more than 0.5 dB
        if (h < y_) y_ = h;
        else y_ += (h - y_) * (relFast_ + (relSlow_ - relFast_) * sustain_);

        // boxcar smoothing over w_ samples (exact re-sum once per lap: no drift)
        boxSum_ += y_ - box_[(size_t)boxPos_];
        box_[(size_t)boxPos_] = y_;
        if (++boxPos_ >= w_)
        {
            boxPos_ = 0;
            double s = 0.0;
            for (double v : box_) s += v;
            boxSum_ = s;
        }
        const double g = boxSum_ / (double)w_;
        return g > 1.0 ? 1.0 : g;
    }

    double sr_ = 48000.0;
    TruePeakDetector tpL_, tpR_;
    int w_ = 144;
    int delay_ = 151;
    double ceiling_ = 0.8810488730080141;   // -1.0 dBTP less the margin until setCeilingDb()
    bool enabled_ = false, bypass_ = false;
    double mix_ = 0.0, bypassMix_ = 0.0, fadeStep_ = 0.001;
    double relFast_ = 0.0, relSlow_ = 0.0, sustainCoef_ = 0.0, sustain_ = 0.0;
    double prevReq_ = 1.0, y_ = 1.0, gain_ = 1.0, peakGr_ = 0.0;

    std::vector<double> holdV_;
    std::vector<long long> holdI_;
    size_t holdHead_ = 0, holdTail_ = 0;
    long long n_ = 0;
    std::vector<double> box_;
    double boxSum_ = 0.0;
    int boxPos_ = 0;
    std::vector<float> dl_, dr_;
    int dpos_ = 0;
};

}} // namespace slowops::vumeter
