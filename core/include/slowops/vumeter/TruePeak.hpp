// True-peak (inter-sample peak) estimation per ITU-R BS.1770 Annex 2: oversample, then take
// the absolute maximum. One channel per instance.
//
// The signal is interpolated by a polyphase windowed-sinc FIR (Kaiser window) with
// kTapsPerPhase taps per phase: x4 below 88.2 kHz, x2 below 176.4 kHz, none above (where
// the sample grid is already fine enough). The result is never below the sample peak.
// process() returns the peak of the newest interpolation interval, which lags the input by
// delay() samples; maxPeak() holds the largest value since clearMax().
// Standard library only; realtime safe (coefficients are computed in prepare()).
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

class TruePeakDetector
{
public:
    static constexpr int kTapsPerPhase = 16;
    static constexpr int kMaxFactor = 4;

    static int factorFor(double sampleRate) { return sampleRate < 88200.0 ? 4 : (sampleRate < 176400.0 ? 2 : 1); }

    void prepare(double sampleRate)
    {
        factor_ = factorFor(sampleRate);
        const int n = factor_ * kTapsPerPhase;
        const double c = (n - 1) * 0.5;
        const double pi = 3.14159265358979323846;
        const double cutoff = 1.0;                     // fraction of the original Nyquist kept flat
        const double beta = 8.0;
        for (int p = 0; p < factor_; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k)
            {
                const int i = k * factor_ + p;
                const double t = ((double)i - c) / (double)factor_;
                const double x = pi * t * cutoff;
                const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(x) / x;
                const double r = ((double)i - c) / (c + 0.5);
                const double w = bessel0(beta * std::sqrt(std::fmax(0.0, 1.0 - r * r))) / bessel0(beta);
                coef_[p][k] = sinc * w;
                sum += coef_[p][k];
            }
            for (int k = 0; k < kTapsPerPhase; ++k) coef_[p][k] /= sum;   // unity gain at DC per phase
        }
        delay_ = factor_ > 1 ? (int)std::ceil(c / (double)factor_) : 0;
        reset();
    }

    void reset()
    {
        for (double& h : hist_) h = 0.0;
        pos_ = 0;
        for (double& d : direct_) d = 0.0;
        clearMax();
    }

    void clearMax() { max_ = 0.0; }

    // Feed one sample; returns the true-peak estimate (linear magnitude) of the newest interval.
    inline double process(float in)
    {
        const double x = std::isfinite(in) ? (double)in : 0.0;
        if (factor_ == 1)
        {
            const double a = std::fabs(x);
            if (a > max_) max_ = a;
            return a;
        }
        pos_ = (pos_ + kTapsPerPhase - 1) % kTapsPerPhase;
        hist_[pos_] = x;
        hist_[pos_ + kTapsPerPhase] = x;                 // mirror: the taps read one contiguous run
        const double* h = &hist_[pos_];
        double peak = 0.0;
        for (int p = 0; p < factor_; ++p)
        {
            const double* c = coef_[p];
            double y = 0.0;
            for (int k = 0; k < kTapsPerPhase; ++k) y += c[k] * h[k];
            const double a = std::fabs(y);
            if (a > peak) peak = a;
        }
        // never report less than the (aligned) sample peak
        direct_[dpos_] = std::fabs(x);
        const double sampleAligned = direct_[(dpos_ + kDirect - delay_) % kDirect];
        dpos_ = (dpos_ + 1) % kDirect;
        if (sampleAligned > peak) peak = sampleAligned;
        if (peak > max_) max_ = peak;
        return peak;
    }

    double maxPeak() const { return max_; }
    int delay() const { return delay_; }
    int factor() const { return factor_; }

private:
    static double bessel0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }

    static constexpr int kDirect = kTapsPerPhase;
    int factor_ = 4;
    int delay_ = 0;
    double coef_[kMaxFactor][kTapsPerPhase] = {};
    double hist_[2 * kTapsPerPhase] = {};
    int pos_ = 0;
    double direct_[kDirect] = {};
    int dpos_ = 0;
    double max_ = 0.0;
};

}} // namespace slowops::vumeter
