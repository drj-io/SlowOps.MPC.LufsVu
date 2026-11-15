// "Cycle" lighting: the dial colour travels once round the hue wheel every two bars.
//
// While the host's song position is moving, the phase is locked to it:
//     phase = frac(ppq / (2 bars in quarter notes)),  bar = numerator * 4 / denominator quarters
// so the colour lands on the same hue at the same place in every two-bar phrase, in any
// time signature and tempo. Position is judged by whether ppq actually advances (the MPC's
// "playing" flag is not reliable). When the position stops, the cycle free-runs at the host
// tempo (120 BPM if none) from wherever it is, so the dial never freezes. Locking onto the
// song position glides over ~150 ms instead of jumping. Purely cosmetic; never touches audio.
// Standard library only.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

struct TempoInfo
{
    bool   tempoValid = false;
    double bpm = 120.0;
    bool   ppqValid = false;
    double ppq = 0.0;             // song position at the start of the block, quarter notes
    int    timeSigNumerator = 4;
    int    timeSigDenominator = 4;
};

class HueCycle
{
public:
    static constexpr double kBars = 2.0;      // one full hue cycle every two bars
    static constexpr double kGlideS = 0.15;   // lock-on glide time

    void reset() { phase_ = 0.0; hasLast_ = false; locked_ = false; }

    static double cycleQuarters(int num, int den)
    {
        if (num <= 0 || den <= 0) { num = 4; den = 4; }
        return kBars * (double)num * 4.0 / (double)den;
    }

    // Advance by one block of n samples; returns the phase 0..1 at the end of the block.
    double advance(const TempoInfo& t, int n, double sampleRate)
    {
        const double bpm = (t.tempoValid && t.bpm > 1.0 && t.bpm < 1000.0) ? t.bpm : 120.0;
        const double cycleQ = cycleQuarters(t.timeSigNumerator, t.timeSigDenominator);
        const double dq = (double)n / sampleRate * bpm / 60.0;       // quarters in this block
        const bool moving = t.ppqValid && hasLast_ && t.ppq != lastPpq_;
        hasLast_ = t.ppqValid;
        lastPpq_ = t.ppq;

        phase_ = wrap(phase_ + dq / cycleQ);                          // free-run at tempo
        if (moving)
        {
            const double target = wrap((t.ppq + dq) / cycleQ);
            double err = target - phase_;
            err -= std::floor(err + 0.5);                             // shortest way round
            const double k = locked_ ? 1.0 : 1.0 - std::exp(-((double)n / sampleRate) / kGlideS);
            phase_ = wrap(phase_ + err * k);
            if (std::fabs(err) < 1e-4) locked_ = true;               // on the beat: follow exactly
            else if (std::fabs(err) > 0.02) locked_ = false;         // locate / loop jump: glide again
        }
        else
        {
            locked_ = false;
        }
        return phase_;
    }

    double phase() const { return phase_; }
    bool locked() const { return locked_; }

private:
    static double wrap(double x) { x -= std::floor(x); return x >= 1.0 ? 0.0 : x; }

    double phase_ = 0.0;
    double lastPpq_ = 0.0;
    bool hasLast_ = false;
    bool locked_ = false;
};

}} // namespace slowops::vumeter
