// Second-order moving-coil needle model for a VU meter.
//
//   y'' + 2*zeta*wn*y' + wn^2*y = wn^2*u(t)
//
// y is the needle deflection in linear units (0 VU = 1.0). u is the full-wave
// rectified, calibrated drive. The ODE is discretised exactly (zero-order hold on
// u over one sample), so the step response is identical at every sample rate and
// independent of block size, and the update is unconditionally stable.
//
// Standard library only; no SDK dependency.
#pragma once
#include <cmath>

namespace slowops { namespace vumeter {

// Design targets (IEC 60268-17 style): ~1.3 % overshoot, 99 % reached in ~300 ms.
// zeta from overshoot: exp(-pi*zeta/sqrt(1-zeta^2)) = 0.013. wn tuned by core_tests.
constexpr double kDefaultZeta = 0.8102;
constexpr double kDefaultWn   = 13.43;   // rad/s

class NeedleDynamics
{
public:
    void prepare(double sampleRate, double zeta = kDefaultZeta, double wn = kDefaultWn)
    {
        const double T = 1.0 / sampleRate;
        const double sigma = zeta * wn;
        const double wd = wn * std::sqrt(1.0 - zeta * zeta);   // underdamped: zeta < 1
        const double e = std::exp(-sigma * T);
        const double c = std::cos(wd * T), s = std::sin(wd * T);
        // State-transition matrix Phi = exp(A*T), A = [[0,1],[-wn^2,-2*zeta*wn]].
        p11_ = e * (c + sigma / wd * s);
        p12_ = e * (s / wd);
        p21_ = e * (-wn * wn / wd * s);
        p22_ = e * (c - sigma / wd * s);
        // ZOH input vector: steady state for constant u is [u, 0], so
        // x[n+1] = Phi*x[n] + (I - Phi)*[u,0]  =>  gamma = [1 - p11, -p21].
        g1_ = 1.0 - p11_;
        g2_ = -p21_;
        reset();
    }

    void reset() { y_ = 0.0; v_ = 0.0; }

    inline void step(double u)
    {
        const double y = p11_ * y_ + p12_ * v_ + g1_ * u;
        const double v = p21_ * y_ + p22_ * v_ + g2_ * u;
        y_ = y; v_ = v;
        // Flush tiny values (denormal guard) and keep the state finite.
        if (!(std::fabs(y_) < 1e12) || !(std::fabs(v_) < 1e12)) { y_ = 0.0; v_ = 0.0; }
        if (std::fabs(y_) < 1e-20) y_ = 0.0;
        if (std::fabs(v_) < 1e-20) v_ = 0.0;
    }

    // Physical deflection; the needle rests on its stop, it never reads below 0.
    double deflection() const { return y_ > 0.0 ? y_ : 0.0; }
    double rawDeflection() const { return y_; }

private:
    double p11_ = 1, p12_ = 0, p21_ = 0, p22_ = 1, g1_ = 0, g2_ = 0;
    double y_ = 0, v_ = 0;
};

}} // namespace slowops::vumeter
