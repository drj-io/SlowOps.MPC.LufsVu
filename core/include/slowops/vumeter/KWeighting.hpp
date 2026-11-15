// ITU-R BS.1770 K-weighting: a high-shelf "pre-filter" (head effects) followed by the
// RLB high-pass. Coefficients are derived for any sample rate from the filters' analogue
// prototypes (centre frequency, Q and shelf gain as published for BS.1770; at 48 kHz they
// reproduce the coefficient table in the Recommendation).
// Standard library only; realtime safe.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0; }

    inline double process(double x)
    {
        const double y = b0 * x + z1;            // transposed direct form II
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void flushDenormals()
    {
        if (std::fabs(z1) < 1e-25) z1 = 0.0;
        if (std::fabs(z2) < 1e-25) z2 = 0.0;
    }
};

class KWeighting
{
public:
    void prepare(double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        {   // stage 1: high shelf, +4 dB above ~1.5 kHz
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(pi * f0 / sampleRate);
            const double Vh = std::pow(10.0, G / 20.0);
            const double Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf_.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf_.b1 = 2.0 * (K * K - Vh) / a0;
            shelf_.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf_.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // stage 2: RLB high-pass at ~38 Hz
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            hp_.b0 = 1.0; hp_.b1 = -2.0; hp_.b2 = 1.0;
            hp_.a1 = 2.0 * (K * K - 1.0) / a0;
            hp_.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }

    void reset() { shelf_.reset(); hp_.reset(); }

    inline double process(double x) { return hp_.process(shelf_.process(x)); }

    void flushDenormals() { shelf_.flushDenormals(); hp_.flushDenormals(); }

private:
    Biquad shelf_, hp_;
};

}} // namespace slowops::vumeter
