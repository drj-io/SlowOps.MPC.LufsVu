// EBU R 128 / ITU-R BS.1770 loudness meter (Momentary, Short-term, Integrated).
//
//   * K-weighting per channel; channel energies are SUMMED (weights 1.0 for L and R),
//     never the mono sum of the samples.
//   * The weighted energy is collected in 100 ms sub-blocks ("hops").
//     Momentary  = mean of the last 4 sub-blocks  (400 ms rectangular window, ungated)
//     Short-term = mean of the last 30 sub-blocks (3 s rectangular window, ungated)
//     Before a window has filled, the missing time counts as silence.
//   * Integrated: 400 ms blocks every 100 ms (75 % overlap), absolute gate -70 LUFS, then a
//     relative gate 10 LU below the absolute-gated loudness. Blocks are kept in a fixed
//     histogram (0.01 LU bins from -70 to +10 LUFS, count and energy per bin), so memory is
//     bounded however long the session runs; only the bin straddling the relative gate is
//     classified by its centre (error far below 0.01 LU).
//   * clear() restarts Integrated and the elapsed time; setRunning(false) (HOLD) freezes
//     them while M and S stay live.
// L = -0.691 + 10 log10(sum of mean squares). No value is reported as -infinity.
// Standard library only; buffers are allocated in prepare(), never in process().
#pragma once
#include "KWeighting.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace slowops { namespace vumeter {

class LoudnessMeter
{
public:
    static constexpr int kShortHops = 30;
    static constexpr int kMomentaryHops = 4;
    static constexpr double kAbsGate = -70.0;
    static constexpr double kHistTop = 10.0;
    static constexpr double kBinWidth = 0.01;
    static constexpr int kBins = (int)((kHistTop - kAbsGate) / kBinWidth + 0.5);

    static double noValue() { return -std::numeric_limits<double>::infinity(); }
    static double toLufs(double energy) { return energy > 0.0 ? -0.691 + 10.0 * std::log10(energy) : noValue(); }
    static double toEnergy(double lufs) { return std::pow(10.0, (lufs + 0.691) / 10.0); }

    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        hop_ = (int)std::lround(sampleRate * 0.1);
        if (hop_ < 1) hop_ = 1;
        kl_.prepare(sampleRate);
        kr_.prepare(sampleRate);
        count_.assign((size_t)kBins, 0u);
        energy_.assign((size_t)kBins, 0.0);
        reset();
    }

    // Everything back to silence (filters, live windows and statistics).
    void reset()
    {
        kl_.reset(); kr_.reset();
        for (double& s : ring_) s = 0.0;
        ringPos_ = 0;
        acc_ = 0.0; inHop_ = 0;
        hops_ = 0;
        clear();
    }

    // Restart Integrated and elapsed time. M and S keep running.
    void clear()
    {
        for (auto& c : count_) c = 0u;
        for (auto& e : energy_) e = 0.0;
        absCount_ = 0; absSum_ = 0.0;
        runHops_ = 0;
        elapsedHops_ = 0;
        dirty_ = true;
    }

    void setRunning(bool run) { if (run != running_) { running_ = run; runHops_ = 0; } }
    bool running() const { return running_; }

    // A single physical channel: count it once (r is ignored).
    void setMonoInput(bool mono) { mono_ = mono; }

    inline void process(double l, double r)
    {
        const double yl = kl_.process(l);
        acc_ += yl * yl;
        if (!mono_)
        {
            const double yr = kr_.process(r);
            acc_ += yr * yr;
        }
        if (++inHop_ >= hop_) finishHop();
    }

    // Number of completed 100 ms hops (lets a caller act once per hop).
    uint64_t hops() const { return hops_; }

    double momentary() const { return toLufs(windowMean(kMomentaryHops)); }
    double shortTerm() const { return toLufs(windowMean(kShortHops)); }

    double integrated() const
    {
        if (dirty_) { integrated_ = computeIntegrated(); dirty_ = false; }
        return integrated_;
    }

    // Seconds of programme that passed the absolute gate since clear() (block count x 100 ms).
    double gatedSeconds() const { return (double)absCount_ * 0.1; }
    double elapsedSeconds() const { return (double)elapsedHops_ * (double)hop_ / sampleRate_; }

private:
    double windowMean(int n) const
    {
        double s = 0.0;
        for (int k = 1; k <= n; ++k) s += ring_[(ringPos_ - k + kShortHops) % kShortHops];
        return s / (double)n;
    }

    void finishHop()
    {
        double sub = acc_ / (double)inHop_;
        if (!std::isfinite(sub)) { sub = 0.0; kl_.reset(); kr_.reset(); }   // NaN/inf input: drop it
        ring_[ringPos_] = sub;
        ringPos_ = (ringPos_ + 1) % kShortHops;
        acc_ = 0.0; inHop_ = 0;
        kl_.flushDenormals(); kr_.flushDenormals();
        ++hops_;
        if (!running_) return;
        ++elapsedHops_;
        if (++runHops_ >= kMomentaryHops) addBlock(windowMean(kMomentaryHops));
    }

    void addBlock(double e)
    {
        const double l = toLufs(e);
        if (!(l > kAbsGate)) return;
        int b = (int)((l - kAbsGate) / kBinWidth);
        if (b >= kBins) b = kBins - 1;
        if (b < 0) b = 0;
        ++count_[(size_t)b];
        energy_[(size_t)b] += e;
        ++absCount_;
        absSum_ += e;
        dirty_ = true;
    }

    double computeIntegrated() const
    {
        if (absCount_ == 0) return noValue();
        const double relGate = toLufs(absSum_ / (double)absCount_) - 10.0;
        double sum = 0.0;
        uint64_t n = 0;
        for (int b = 0; b < kBins; ++b)
        {
            if (count_[(size_t)b] == 0u) continue;
            const double centre = kAbsGate + ((double)b + 0.5) * kBinWidth;
            if (centre <= relGate) continue;
            sum += energy_[(size_t)b];
            n += count_[(size_t)b];
        }
        return n > 0 ? toLufs(sum / (double)n) : noValue();
    }

    double sampleRate_ = 48000.0;
    int hop_ = 4800;
    KWeighting kl_, kr_;
    bool mono_ = false;
    bool running_ = true;

    double ring_[kShortHops] = {};
    int ringPos_ = 0;
    double acc_ = 0.0;
    int inHop_ = 0;
    uint64_t hops_ = 0;
    uint64_t runHops_ = 0;
    uint64_t elapsedHops_ = 0;

    std::vector<uint32_t> count_;
    std::vector<double> energy_;
    uint64_t absCount_ = 0;
    double absSum_ = 0.0;
    mutable double integrated_ = noValue();
    mutable bool dirty_ = true;
};

}} // namespace slowops::vumeter
