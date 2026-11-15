// Core engine tests for the Slow Ops VU Meter (no SDK dependency).
// Implements the pass/fail table of the spec, section 8.1, plus trim/bypass checks.
#include "slowops/vumeter/VuEngine.hpp"
#include "slowops/vumeter/Heat.hpp"
#include "slowops/vumeter/HueCycle.hpp"
#include "slowops/vumeter/AutoLoudness.hpp"
#include "slowops/vumeter/LoudnessMeter.hpp"
#include "slowops/vumeter/TruePeak.hpp"
#include "slowops/vumeter/TruePeakLimiter.hpp"
#include <random>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace slowops::vumeter;

static int g_pass = 0, g_fail = 0;
static void CHECK(bool ok, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    std::printf("  %s ", ok ? "PASS" : "FAIL");
    std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap);
    (ok ? g_pass : g_fail)++;
}

using Gen = std::function<void(long n, float& l, float& r)>;   // n = absolute sample index

struct Rig
{
    VuEngine e;
    double sr;
    long pos = 0;
    std::vector<float> L, R;
    explicit Rig(double sampleRate, double ref = -18.0) : sr(sampleRate)
    {
        e.prepare(sr); e.setReferenceDbfs(ref);
        L.resize(4096); R.resize(4096);
    }
    // Run `seconds` of audio through the engine using a repeating irregular block pattern.
    void run(double seconds, const Gen& g, std::initializer_list<int> blocks = {512})
    {
        long total = (long)std::llround(seconds * sr);
        std::vector<int> bs(blocks);
        size_t k = 0;
        while (total > 0)
        {
            int n = (int)std::min<long>(bs[k++ % bs.size()], total);
            for (int i = 0; i < n; ++i) g(pos + i, L[(size_t)i], R[(size_t)i]);
            e.process(L.data(), R.data(), L.data(), R.data(), n);
            pos += n; total -= n;
        }
    }
};

static double dbToAmp(double db) { return std::pow(10.0, db / 20.0); }

static Gen sine(double sr, double peakDb, double hz = 1000.0, double polL = 1.0, double polR = 1.0)
{
    const double a = dbToAmp(peakDb);
    return [=](long n, float& l, float& r) {
        const double s = a * std::sin(2.0 * kPi * hz * (double)n / sr);
        l = (float)(polL * s); r = (float)(polR * s);
    };
}

static void testCalibration()
{
    std::printf("Reference calibration (1 kHz sine at reference peak -> 0 VU, +-0.2)\n");
    for (double ref : {-20.0, -18.0, -14.0})
    {
        Rig rig(44100, ref);
        rig.run(2.0, sine(44100, ref));
        CHECK(std::fabs(rig.e.vu(kLeft)) <= 0.2 && std::fabs(rig.e.vu(kRight)) <= 0.2,
              "ref %.0f dBFS: L %.3f VU, R %.3f VU", ref, rig.e.vu(kLeft), rig.e.vu(kRight));
    }
}

static void testScale()
{
    std::printf("Scale calibration (+-0.3 VU)\n");
    for (double off : {-20.0, -10.0, -7.0, -3.0, +3.0})
    {
        Rig rig(44100, -18.0);
        rig.run(2.0, sine(44100, -18.0 + off));
        CHECK(std::fabs(rig.e.vu(kLeft) - off) <= 0.3, "%+.0f dB re ref reads %+.3f VU", off, rig.e.vu(kLeft));
    }
}

static void testStep()
{
    std::printf("Step dynamics (0 -> 1 normalised drive)\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        NeedleDynamics n; n.prepare(sr);
        double t99 = -1, mx = 0;
        for (long i = 0; i < (long)(sr * 2); ++i)
        {
            n.step(1.0);
            const double y = n.rawDeflection();
            if (t99 < 0 && y >= 0.99) t99 = (double)(i + 1) / sr;
            if (y > mx) mx = y;
        }
        const double os = (mx - 1.0) * 100.0;
        CHECK(std::fabs(t99 - 0.300) <= 0.015, "%.0f Hz: 99%% at %.1f ms (300 +-15)", sr, t99 * 1000.0);
        CHECK(os >= 1.0 && os <= 1.5, "%.0f Hz: overshoot %.3f%% (1.0..1.5)", sr, os);
    }

    std::printf("Return dynamics (1 -> 0)\n");
    NeedleDynamics n; n.prepare(44100);
    for (int i = 0; i < 44100 * 2; ++i) n.step(1.0);
    double t01 = -1, minDisplayed = 1e9, prev = n.deflection();
    double maxDrop = 0;
    for (int i = 0; i < 44100 * 2; ++i)
    {
        n.step(0.0);
        const double y = n.deflection();
        if (t01 < 0 && y <= 0.01) t01 = (double)(i + 1) / 44100.0;
        if (y < minDisplayed) minDisplayed = y;
        maxDrop = std::max(maxDrop, prev - y);
        prev = y;
    }
    CHECK(std::fabs(t01 - 0.300) <= 0.015, "falls to 1%% in %.1f ms (symmetric with rise)", t01 * 1000.0);
    CHECK(minDisplayed >= 0.0, "displayed deflection never negative (min %.4f)", minDisplayed);
    CHECK(maxDrop < 0.01, "no instant snap (largest per-sample drop %.6f)", maxDrop);
}

static void testMono()
{
    std::printf("Mono sum (M = 0.5*(L+R) before rectification)\n");
    {
        Rig rig(44100); rig.run(2.0, sine(44100, -18.0));
        CHECK(std::fabs(rig.e.vu(kMono)) <= 0.2, "L=R=ref: mono %.3f VU (expect 0)", rig.e.vu(kMono));
    }
    {
        Rig rig(44100);
        auto s = sine(44100, -18.0);
        rig.run(2.0, [&](long n, float& l, float& r) { s(n, l, r); r = 0.0f; });
        CHECK(std::fabs(rig.e.vu(kMono) + 6.0206) <= 0.2, "L=ref, R=0: mono %.3f VU (expect -6.02)", rig.e.vu(kMono));
    }
    {
        Rig rig(44100); rig.run(2.0, sine(44100, -18.0, 1000.0, 1.0, -1.0));
        CHECK(std::fabs(rig.e.vu(kLeft)) <= 0.2 && std::fabs(rig.e.vu(kRight)) <= 0.2,
              "anti-phase: stereo L %.3f / R %.3f VU", rig.e.vu(kLeft), rig.e.vu(kRight));
        CHECK(rig.e.deflection(kMono) < 1e-6, "anti-phase: mono at rest (deflection %.2e)", rig.e.deflection(kMono));
    }
    {
        Rig a(44100), b(44100);
        a.run(2.0, sine(44100, -18.0));
        b.run(2.0, sine(44100, -18.0, 1000.0, -1.0, -1.0));
        CHECK(std::fabs(a.e.vu(kLeft) - b.e.vu(kLeft)) < 0.01 && std::fabs(a.e.vu(kMono) - b.e.vu(kMono)) < 0.01,
              "polarity inversion: %.4f vs %.4f VU", a.e.vu(kLeft), b.e.vu(kLeft));
    }
}

static void testWaveform()
{
    std::printf("Non-sinusoidal program (average-responding, not RMS)\n");
    Rig rig(44100);
    const double a = dbToAmp(-18.0);
    rig.run(2.0, [&](long n, float& l, float& r) {
        l = r = (float)(((n / 22) % 2) ? a : -a);       // ~1 kHz square, same peak as reference sine
    });
    const double expect = 20.0 * std::log10(kPi / 2.0);  // +3.92 VU; an RMS meter would read +3.01
    CHECK(std::fabs(rig.e.vu(kLeft) - expect) <= 0.1, "square reads %+.3f VU (avg-responding %+.2f, RMS would be +3.01)",
          rig.e.vu(kLeft), expect);
}

static void testSilenceAndSafety()
{
    std::printf("Silence and safety\n");
    {
        Rig rig(44100); rig.run(1.0, sine(44100, -10.0));
        rig.run(3.0, [](long, float& l, float& r) { l = r = 0.0f; });
        bool rest = true;
        for (int m = 0; m < kNumMeters; ++m) rest = rest && rig.e.deflection(m) < 1e-4;
        CHECK(rest, "needles return to rest after silence");
        CHECK(deflectionToFrame(rig.e.deflection(kLeft), 128) == 0, "rest maps to frame 0");
    }
    {
        Rig rig(44100);
        const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity() };
        auto s = sine(44100, -18.0);
        rig.run(1.0, [&](long n, float& l, float& r) { s(n, l, r); if (n % 1000 == 0) { l = bad[(n / 1000) % 3]; r = l; } });
        bool finite = true;
        for (int m = 0; m < kNumMeters; ++m) finite = finite && std::isfinite(rig.e.deflection(m));
        CHECK(finite, "NaN/Inf input keeps meter state finite");
    }
}

static void testPeaks()
{
    std::printf("Sample-peak clip indicators\n");
    {
        Rig rig(44100); rig.e.setPeakHold(true);
        rig.run(0.5, [](long n, float& l, float& r) { l = (n == 100) ? 1.0f : 0.0f; r = 0.0f; });
        CHECK(rig.e.consumeClip(kLeft) && !rig.e.consumeClip(kRight), "single full-scale L sample latches L only");
        CHECK(rig.e.deflection(kLeft) < 0.05, "...while VU barely moves (%.4f)", rig.e.deflection(kLeft));
        rig.run(1.4, [](long, float& l, float& r) { l = r = 0.0f; });
        CHECK(rig.e.consumeClip(kLeft), "latch still lit at 1.9 s");
        rig.run(0.2, [](long, float& l, float& r) { l = r = 0.0f; });
        CHECK(!rig.e.consumeClip(kLeft), "latch released after 2 s (sample-counted)");
    }
    {
        Rig rig(44100); rig.e.setPeakHold(false);
        rig.run(0.1, [](long n, float& l, float& r) { l = (n == 10) ? -1.2f : 0.0f; r = 0.0f; });
        CHECK(rig.e.consumeClip(kLeft), "hold off: lit for the frame containing the clip");
        CHECK(!rig.e.consumeClip(kLeft), "hold off: dark on the next frame");
    }
    {
        Rig rig(44100); rig.e.setPeakHold(true);
        rig.run(0.5, [](long n, float& l, float& r) { l = (n % 100 == 0) ? 1.0f : 0.0f; r = -l; });
        const bool lit = rig.e.clipLit(kLeft) || rig.e.clipLit(kRight);
        CHECK(rig.e.deflection(kMono) < 1e-9, "L=+1/R=-1 impulses: mono VU cancels");
        CHECK(lit, "...but INPUT CLIP (L or R) still lights");
        rig.e.resetClips();
        CHECK(!rig.e.clipLit(kLeft) && !rig.e.clipLit(kRight), "reset clears latches");
    }
}

static void testInvariance()
{
    std::printf("Block-size and sample-rate invariance\n");
    double ref = -1;
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        Rig rig(sr);
        rig.run(0.150, sine(sr, -18.0, 997.0), {1, 7, 333, 64, 1024, 5});
        const double y = rig.e.deflection(kLeft);
        if (ref < 0) ref = y;
        CHECK(std::fabs(y - ref) / ref < 0.01, "%.0f Hz, irregular blocks: mid-rise deflection %.4f (ref %.4f)", sr, y, ref);
    }
    Rig a(44100), b(44100);
    a.run(0.5, sine(44100, -12.0), {512});
    b.run(0.5, sine(44100, -12.0), {1, 3, 17, 100, 2048});
    CHECK(a.e.rawDeflection(kLeft) == b.e.rawDeflection(kLeft), "same rate, different blocks: bit-identical state");
}

static void testTrim()
{
    std::printf("Output trim / bypass\n");
    {
        VuEngine e; e.prepare(44100); e.setTrimDb(0.0f, false);
        std::vector<float> in(1000), out(1000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = std::sin((float)i * 0.37f) * 0.731f;
        e.process(in.data(), in.data(), out.data(), out.data(), (int)in.size());
        CHECK(std::memcmp(in.data(), out.data(), sizeof(float) * in.size()) == 0, "trim 0.0 dB: bit-exact copy");
    }
    {
        Rig rig(44100);
        rig.e.setTrimDb(6.0f, false);
        rig.run(2.0, sine(44100, -18.0));
        CHECK(std::fabs(rig.e.vu(kLeft) - 6.0) <= 0.2, "trim +6 dB: meter reads post-trim %+.3f VU", rig.e.vu(kLeft));
    }
    {
        VuEngine e; e.prepare(44100); e.setTrimDb(6.0f, false);
        std::vector<float> buf(44100, 0.25f);
        e.process(buf.data(), buf.data(), buf.data(), buf.data(), (int)buf.size());
        CHECK(std::fabs(buf.back() - 0.25f * Trim::dbToGain(6.0f)) < 1e-6f, "trim +6 dB: settled gain %.5f", buf.back() / 0.25f);
        CHECK(std::fabs(buf[1] - buf[0]) < 0.01f, "trim change is smoothed (no step at block start)");
        e.setTrimDb(6.0f, true);                       // bypass
        std::vector<float> warm(44100, 0.5f);
        e.process(warm.data(), warm.data(), warm.data(), warm.data(), (int)warm.size());
        std::vector<float> in(512), out(512);
        for (size_t i = 0; i < in.size(); ++i) in[i] = std::cos((float)i * 0.11f) * 0.4f;
        e.process(in.data(), in.data(), out.data(), out.data(), (int)in.size());
        CHECK(std::memcmp(in.data(), out.data(), sizeof(float) * in.size()) == 0, "bypass (trim +6): settles to bit-exact copy");
    }
}

static void testHeat()
{
    std::printf("React lighting heat\n");
    CHECK(heatFromVU(-40.0) == 0.0 && heatFromVU(-20.0) == 0.0, "quiet program is coldest blue (0)");
    CHECK(std::fabs(heatFromVU(0.0) - 0.66) < 1e-9, "0 VU sits in the red (0.66)");
    CHECK(heatFromVU(3.0) == 1.0 && heatFromVU(10.0) == 1.0, "+3 VU and beyond is full purple (1)");
    CHECK(heatFromVU(std::numeric_limits<double>::quiet_NaN()) == 0.0, "NaN reads cold");
    bool mono = true;
    for (double v = -30.0; v < 6.0; v += 0.05) mono = mono && heatFromVU(v + 0.05) >= heatFromVU(v);
    CHECK(mono, "heat rises monotonically with level");

    HeatFollower h; h.reset();
    double t63 = -1, t = 0;
    while (t < 2.0) { t += 1.0 / 30.0; if (h.update(3.0, 1.0 / 30.0) >= 0.632 && t63 < 0) t63 = t; }
    CHECK(t63 > 0.08 && t63 < 0.2, "warms up fast (63%% in %.0f ms)", t63 * 1000.0);
    double c63 = -1; t = 0;
    while (t < 5.0) { t += 1.0 / 30.0; if (h.update(-40.0, 1.0 / 30.0) <= 0.368 && c63 < 0) c63 = t; }
    CHECK(c63 > 0.7 && c63 < 1.2, "cools down slowly (63%% in %.0f ms)", c63 * 1000.0);
    HeatFollower a, b;
    for (int i = 0; i < 30; ++i) a.update(0.0, 1.0 / 30.0);
    for (int i = 0; i < 60; ++i) b.update(0.0, 1.0 / 60.0);
    CHECK(std::fabs(a.heat() - b.heat()) < 1e-9, "independent of the update rate");
}

// Drive a HueCycle like a host: blocks of `block` samples, song position advancing if `rolling`.
static double runCycle(HueCycle& c, TempoInfo& t, double seconds, bool rolling, int block = 512, double sr = 44100.0)
{
    long total = (long)std::llround(seconds * sr);
    double ph = c.phase();
    while (total > 0)
    {
        const int n = (int)std::min<long>(block, total);
        ph = c.advance(t, n, sr);
        if (rolling) t.ppq += (double)n / sr * t.bpm / 60.0;
        total -= n;
    }
    return ph;
}

static double hueDist(double a, double b) { double d = std::fabs(a - b); return std::min(d, 1.0 - d); }

static void testCycle()
{
    std::printf("Cycle lighting (hue every two bars)\n");
    CHECK(HueCycle::cycleQuarters(4, 4) == 8.0 && HueCycle::cycleQuarters(3, 4) == 6.0 && HueCycle::cycleQuarters(6, 8) == 6.0,
          "two bars = 8 quarters in 4/4, 6 in 3/4 and 6/8");
    {
        HueCycle c; c.reset();
        TempoInfo t; t.tempoValid = t.ppqValid = true; t.bpm = 120.0; t.ppq = 0.0;
        runCycle(c, t, 1.0, true);                       // lock on
        bool ok = true; double worst = 0;
        for (int i = 0; i < 40; ++i)
        {
            const double ph = runCycle(c, t, 0.1, true);
            const double want = std::fmod(t.ppq, 8.0) / 8.0;
            worst = std::max(worst, hueDist(ph, want)); ok = ok && hueDist(ph, want) < 0.002;
        }
        CHECK(ok, "120 BPM 4/4, rolling: locked to song position (worst %.4f of a cycle)", worst);
        CHECK(c.locked(), "reports locked while rolling");
    }
    {
        HueCycle c; c.reset();
        TempoInfo t; t.tempoValid = t.ppqValid = true; t.bpm = 90.0; t.ppq = 0.0;
        t.timeSigNumerator = 3; t.timeSigDenominator = 4;
        runCycle(c, t, 1.0, true);
        const double a = c.phase();
        const double b = runCycle(c, t, 4.0, true);      // 6 quarters at 90 BPM = 4 s = one cycle
        CHECK(hueDist(a, b) < 0.002, "90 BPM 3/4: one full cycle every 4.0 s (drift %.4f)", hueDist(a, b));
    }
    {
        HueCycle c; c.reset();
        TempoInfo t; t.tempoValid = t.ppqValid = true; t.bpm = 120.0; t.ppq = 37.25;   // stopped: position frozen
        const double a = runCycle(c, t, 1.0, false);
        const double b = runCycle(c, t, 1.0, false);
        CHECK(std::fabs(hueDist(a, b) - 0.25) < 0.002, "stopped: free-runs at tempo (1 s = 1/4 cycle at 120 BPM, got %.4f)", hueDist(a, b));
        TempoInfo none;                                   // no transport at all: 120 BPM
        const double c0 = c.phase(), c1 = runCycle(c, none, 2.0, false);
        CHECK(std::fabs(hueDist(c0, c1) - 0.5) < 0.002, "no transport: still cycles at 120 BPM");
    }
    {
        HueCycle c; c.reset();
        TempoInfo t; t.tempoValid = t.ppqValid = true; t.bpm = 120.0; t.ppq = 3.0;
        runCycle(c, t, 0.5, false);                       // free-running, out of phase with the song
        const double jump = hueDist(c.phase(), std::fmod(t.ppq, 8.0) / 8.0);
        double maxStep = 0, prev = c.phase();
        for (int i = 0; i < 60; ++i)
        {
            const double ph = runCycle(c, t, 1.0 / 30.0, true);
            maxStep = std::max(maxStep, hueDist(ph, prev)); prev = ph;
        }
        const double want = std::fmod(t.ppq, 8.0) / 8.0;
        CHECK(hueDist(c.phase(), want) < 0.002, "press Play: lands on the song position");
        CHECK(jump > 0.2 && maxStep < 0.25 * jump, "...gliding: largest step per frame %.3f of a cycle, an instant lock would jump %.3f", maxStep, jump);
    }
    {
        HueCycle a, b; a.reset(); b.reset();
        TempoInfo ta, tb; ta.tempoValid = ta.ppqValid = tb.tempoValid = tb.ppqValid = true;
        ta.bpm = tb.bpm = 128.0;
        runCycle(a, ta, 3.0, true, 512);
        runCycle(b, tb, 3.0, true, 96);
        CHECK(hueDist(a.phase(), b.phase()) < 0.003, "block-size independent (%.4f)", hueDist(a.phase(), b.phase()));
    }
}

static void testV02()
{
    std::printf("Cycle rate\n");
    for (double bars : {1.0, 4.0, 8.0})
    {
        HueCycle c; c.reset(); c.setBars(bars);
        TempoInfo t; t.tempoValid = t.ppqValid = true; t.bpm = 120.0;
        runCycle(c, t, 1.0, true);
        bool ok = true;
        for (int i = 0; i < 20; ++i)
        {
            const double ph = runCycle(c, t, 0.25, true);
            ok = ok && hueDist(ph, std::fmod(t.ppq, 4.0 * bars) / (4.0 * bars)) < 0.002;
        }
        CHECK(ok, "%.0f-bar cycle locks to the song position", bars);
    }

    std::printf("Clip threshold\n");
    {
        Rig rig(44100); rig.e.setClipThresholdDbfs(-1.0);
        rig.run(0.1, [](long n, float& l, float& r) { l = (n == 10) ? 0.90f : 0.0f; r = (n == 10) ? 0.88f : 0.0f; });
        CHECK(rig.e.consumeClip(kLeft) && !rig.e.consumeClip(kRight), "-1 dBFS: 0.90 lights, 0.88 does not (threshold 0.891)");
        rig.e.setClipThresholdDbfs(0.0);
        rig.e.resetClips();
        rig.run(0.1, [](long n, float& l, float& r) { l = (n == 10) ? 0.999f : 0.0f; r = 0.0f; });
        CHECK(!rig.e.consumeClip(kLeft), "0 dBFS: 0.999 does not light");
    }

    std::printf("Max VU tracking\n");
    {
        Rig rig(44100);
        rig.run(1.5, sine(44100, -15.0));   // +3 VU
        rig.run(2.0, sine(44100, -24.0));   // -6 VU
        CHECK(std::fabs(deflectionToVU(rig.e.maxDeflection(kLeft)) - 3.1) < 0.15,
              "max holds the highest reading (+%.2f VU incl. overshoot) after the level drops", deflectionToVU(rig.e.maxDeflection(kLeft)));
        rig.e.clearMax();
        rig.run(1.0, sine(44100, -24.0));
        CHECK(std::fabs(deflectionToVU(rig.e.maxDeflection(kLeft)) + 6.0) < 0.2, "clear restarts from the current level (%.2f VU)",
              deflectionToVU(rig.e.maxDeflection(kLeft)));
    }

    std::printf("Phase correlation\n");
    {
        Rig a(44100); a.run(1.0, sine(44100, -18.0));
        CHECK(a.e.correlation().hasSignal() && a.e.correlation().value() > 0.99, "identical L/R: +1 (%.3f)", a.e.correlation().value());
        Rig b(44100); b.run(1.0, sine(44100, -18.0, 1000.0, 1.0, -1.0));
        CHECK(b.e.correlation().value() < -0.99, "anti-phase: -1 (%.3f)", b.e.correlation().value());
        Rig c(44100);
        c.run(2.0, [](long n, float& l, float& r) {
            l = (float)(0.1 * std::sin(2 * kPi * 440.0 * n / 44100.0)); r = (float)(0.1 * std::sin(2 * kPi * 631.0 * n / 44100.0)); });
        CHECK(std::fabs(c.e.correlation().value()) < 0.1, "unrelated tones: about 0 (%.3f)", c.e.correlation().value());
        Rig d(44100); d.run(1.0, [](long, float& l, float& r) { l = r = 0.0f; });
        CHECK(!d.e.correlation().hasSignal(), "silence: no reading");
        Rig e(44100); e.run(1.0, [](long n, float& l, float& r) { l = (float)(0.1 * std::sin(n * 0.1)); r = 0.0f; });
        CHECK(e.e.correlation().hasSignal() && std::fabs(e.e.correlation().value()) < 1e-6, "one side silent: 0");
    }
}

// ---------------------------------------------------------------------------- v0.3: LUFS
// A sequence of stereo 1 kHz sine sections (peak level dBFS, seconds) through a LoudnessMeter.
struct Section { double db, s; };
static void feedSections(LoudnessMeter& m, double sr, std::initializer_list<Section> secs, double hz = 1000.0)
{
    long n = 0;
    for (const Section& sec : secs)
    {
        const double a = sec.db <= -200.0 ? 0.0 : dbToAmp(sec.db);
        const long len = std::lround(sec.s * sr);
        for (long i = 0; i < len; ++i, ++n)
        {
            const double x = a * std::sin(2.0 * kPi * hz * (double)n / sr);
            m.process(x, x);
        }
    }
}

static void testLoudness()
{
    std::printf("Loudness (EBU Tech 3341 stereo cases, +-0.1 LU)\n");
    for (double sr : {44100.0, 48000.0})
    {
        { LoudnessMeter m; m.prepare(sr); feedSections(m, sr, {{-23, 20}});
          CHECK(std::fabs(m.momentary() + 23) <= 0.1 && std::fabs(m.shortTerm() + 23) <= 0.1 && std::fabs(m.integrated() + 23) <= 0.1,
                "%.0f Hz case 1 (-23 dBFS 20 s): M %.2f S %.2f I %.2f", sr, m.momentary(), m.shortTerm(), m.integrated()); }
        { LoudnessMeter m; m.prepare(sr); feedSections(m, sr, {{-33, 20}});
          CHECK(std::fabs(m.momentary() + 33) <= 0.1 && std::fabs(m.shortTerm() + 33) <= 0.1 && std::fabs(m.integrated() + 33) <= 0.1,
                "%.0f Hz case 2 (-33 dBFS 20 s): M %.2f S %.2f I %.2f", sr, m.momentary(), m.shortTerm(), m.integrated()); }
        { LoudnessMeter m; m.prepare(sr); feedSections(m, sr, {{-36, 10}, {-23, 60}, {-36, 10}});
          CHECK(std::fabs(m.integrated() + 23) <= 0.1, "%.0f Hz case 3 (relative gate): I %.2f", sr, m.integrated()); }
        { LoudnessMeter m; m.prepare(sr); feedSections(m, sr, {{-72, 10}, {-36, 10}, {-23, 60}, {-36, 10}, {-72, 10}});
          CHECK(std::fabs(m.integrated() + 23) <= 0.1, "%.0f Hz case 4 (absolute + relative gate): I %.2f", sr, m.integrated()); }
        { LoudnessMeter m; m.prepare(sr); feedSections(m, sr, {{-26, 20}, {-20, 20.1}, {-26, 20}});
          CHECK(std::fabs(m.integrated() + 23) <= 0.1, "%.0f Hz case 5: I %.2f", sr, m.integrated()); }
        {   // case 9: 1.34 s at -20 / 1.66 s at -30, repeated: Short-term is -23 throughout once 3 s have passed
            LoudnessMeter m; m.prepare(sr);
            feedSections(m, sr, {{-20, 1.34}, {-30, 1.66}});
            double lo = 0, hi = -100;
            for (int k = 0; k < 5; ++k)
            {
                const uint64_t h0 = m.hops();
                LoudnessMeter* mp = &m;
                (void)h0; (void)mp;
                for (int part = 0; part < 30; ++part)    // 0.1 s steps across one period
                {
                    // continue the alternating pattern in 0.1 s slices with the right levels
                    const double t = 3.0 + k * 3.0 + part * 0.1;
                    const double ph = std::fmod(t, 3.0);
                    (void)ph;
                }
            }
            // simpler and exact: feed the whole pattern once more and sample S at every hop
            LoudnessMeter s; s.prepare(sr);
            const long len = std::lround(15.0 * sr);
            const long first = std::lround(1.34 * sr), period = std::lround(3.0 * sr);
            uint64_t lastHop = 0;
            for (long i = 0; i < len; ++i)
            {
                const double a = (i % period) < first ? dbToAmp(-20) : dbToAmp(-30);
                const double x = a * std::sin(2.0 * kPi * 1000.0 * (double)i / sr);
                s.process(x, x);
                if (s.hops() != lastHop)
                {
                    lastHop = s.hops();
                    if (i > (long)(3.05 * sr)) { lo = std::fmin(lo, s.shortTerm()); hi = std::fmax(hi, s.shortTerm()); }
                }
            }
            CHECK(lo >= -23.1 && hi <= -22.9, "%.0f Hz case 9 (Short-term of a 3 s pattern): %.2f .. %.2f", sr, lo, hi);
        }
    }
    {
        LoudnessMeter m; m.prepare(48000); feedSections(m, 48000, {{-300, 5}});
        CHECK(!std::isfinite(m.momentary()) && !std::isfinite(m.shortTerm()) && !std::isfinite(m.integrated())
              && !std::isnan(m.integrated()), "silence: no value (never NaN)");
        feedSections(m, 48000, {{-80, 5}});
        CHECK(!std::isfinite(m.integrated()) && m.shortTerm() < -75, "-80 dBFS: below the absolute gate, I has no value (S %.1f)", m.shortTerm());
    }
    {
        LoudnessMeter m; m.prepare(48000);
        long n = 0;
        for (; n < 48000 * 5; ++n) { const double x = 0.1 * std::sin(n * 0.13); m.process(x, 0.0); }
        const double oneSide = m.integrated();
        LoudnessMeter b; b.prepare(48000);
        for (n = 0; n < 48000 * 5; ++n) { const double x = 0.1 * std::sin(n * 0.13); b.process(x, x); }
        CHECK(std::fabs(b.integrated() - oneSide - 3.01) < 0.05, "channel energies add: identical L+R reads +3.01 LU over one side (%.2f)",
              b.integrated() - oneSide);
        LoudnessMeter c; c.prepare(48000); c.setMonoInput(true);
        for (n = 0; n < 48000 * 5; ++n) { const double x = 0.1 * std::sin(n * 0.13); c.process(x, x); }
        CHECK(std::fabs(c.integrated() - oneSide) < 0.01, "a single physical channel is counted once");
    }
    {
        LoudnessMeter m; m.prepare(48000);
        feedSections(m, 48000, {{-20, 10}});
        m.setRunning(false);
        feedSections(m, 48000, {{-40, 10}});
        CHECK(std::fabs(m.integrated() + 20) < 0.1 && std::fabs(m.elapsedSeconds() - 10.0) < 0.15 && m.shortTerm() < -35,
              "HOLD freezes I (%.2f) and elapsed (%.1f s); S stays live (%.1f)", m.integrated(), m.elapsedSeconds(), m.shortTerm());
        m.setRunning(true);
        m.clear();
        feedSections(m, 48000, {{-30, 5}});
        CHECK(std::fabs(m.integrated() + 30) < 0.1 && std::fabs(m.elapsedSeconds() - 5.0) < 0.15, "CLEAR restarts I (%.2f) and elapsed",
              m.integrated());
    }
}

static void testTruePeak()
{
    std::printf("True peak\n");
    for (double sr : {44100.0, 48000.0})
    {
        TruePeakDetector d; d.prepare(sr);
        double sp = 0;
        for (int n = 0; n < 20000; ++n)
        {
            const float x = (float)std::sin(0.5 * kPi * n + 0.25 * kPi);
            sp = std::fmax(sp, std::fabs(x));
            d.process(x);
        }
        const double tp = 20 * std::log10(d.maxPeak());
        CHECK(std::fabs(20 * std::log10(sp) + 3.01) < 0.02 && tp > -0.4 && tp < 0.2,
              "%.0f Hz fs/4 at 45 deg: sample peak %.2f dBFS, true peak %+.2f dBTP (within +0.2/-0.4)", sr, 20 * std::log10(sp), tp);
    }
    {
        double worst = 0;
        for (double f = 100; f <= 20000; f *= 1.17)
        {
            TruePeakDetector d; d.prepare(44100);
            for (int n = 0; n < 44100; ++n)                 // 1 s with a 50 ms fade-in (no onset transient)
            {
                const double env = n < 2205 ? n / 2205.0 : 1.0;
                d.process((float)(env * 0.5 * std::sin(2 * kPi * f * n / 44100.0 + 0.7)));
            }
            worst = std::fmax(worst, std::fabs(20 * std::log10(d.maxPeak() / 0.5)));
        }
        CHECK(worst < 0.2, "steady sines 100 Hz..20 kHz: true peak within %.3f dB of the true amplitude", worst);
    }
    {
        TruePeakDetector d; d.prepare(48000);
        for (int n = 0; n < 4800; ++n) d.process(n == 100 ? 0.9f : 0.0f);
        for (int n = 0; n < 48000; ++n) d.process((float)(0.01 * std::sin(n * 0.05)));
        CHECK(d.maxPeak() >= 0.899, "the maximum holds after quieter audio (%.3f)", d.maxPeak());
        d.clearMax();
        d.process(0.0f);
        CHECK(d.maxPeak() < 0.02, "clearMax restarts it");
    }
}

using StereoGen = std::function<void(long n, float& l, float& r)>;

// Run a limiter over `seconds` of a generator; returns output true peak (dBTP, both channels).
static double runLimiter(TruePeakLimiter& lim, double sr, double seconds, const StereoGen& g, double* maxGr = nullptr,
                         double* ratioErr = nullptr)
{
    TruePeakDetector ml, mr; ml.prepare(sr); mr.prepare(sr);
    const long len = std::lround(seconds * sr);
    for (long n = 0; n < len; ++n)
    {
        float l, r; g(n, l, r);
        lim.process(l, r);
        ml.process(l); mr.process(r);
        if (maxGr) *maxGr = std::fmax(*maxGr, lim.gainReductionDb());
        if (ratioErr && std::fabs(r) > 1e-3f) *ratioErr = std::fmax(*ratioErr, std::fabs(l / r - 2.0f));
    }
    return 20 * std::log10(std::fmax(ml.maxPeak(), mr.maxPeak()));
}

static void testLimiter()
{
    std::printf("True-peak limiter\n");
    const double sr = 44100;
    std::mt19937 rng(7);
    std::normal_distribution<double> nd(0, 1);
    struct Case { const char* name; StereoGen g; };
    const Case cases[] = {
        { "1 kHz sine +12 dB", [&](long n, float& l, float& r) { l = r = (float)(4 * std::sin(2 * kPi * 1000 * n / sr)); } },
        { "fs/4 at 45 deg (inter-sample overs)", [&](long n, float& l, float& r) { l = r = (float)(2 * std::sin(0.5 * kPi * n + 0.25 * kPi)); } },
        { "100 Hz square", [&](long n, float& l, float& r) { l = r = ((n / 221) % 2) ? 2.0f : -2.0f; } },
        { "noise bursts", [&](long n, float& l, float& r) { const double e = ((n / 5000) % 3 == 0) ? 3.0 : 0.3; l = (float)(e * nd(rng)); r = (float)(e * nd(rng)); } },
        { "kick drum", [&](long n, float& l, float& r) { const long k = n % 22050; l = r = (float)(3 * std::exp(-k / 2000.0) * std::sin(2 * kPi * 60 * k / sr * (1 + 3 * std::exp(-k / 300.0)))); } },
    };
    for (double ceil : {-1.0, -0.1, -6.0})
        for (const Case& c : cases)
        {
            TruePeakLimiter lim; lim.prepare(sr); lim.setCeilingDb(ceil); lim.setEnabled(true); lim.reset();
            double gr = 0;
            const double tp = runLimiter(lim, sr, 3.0, c.g, &gr);
            CHECK(tp <= ceil + 0.05 && gr > 1.0, "ceiling %.1f, %s: output %.2f dBTP, max GR %.1f dB", ceil, c.name, tp, gr);
        }
    {
        TruePeakLimiter lim; lim.prepare(sr); lim.setEnabled(true); lim.reset();
        double err = 0, gr = 0;
        runLimiter(lim, sr, 2.0, [&](long n, float& l, float& r) { const double s = 2 * std::sin(2 * kPi * 3000 * n / sr); l = (float)s; r = (float)(0.5 * s); }, &gr, &err);
        CHECK(gr > 3.0 && err < 1e-4, "stereo-linked: L = 2 R stays 2:1 while limiting %.1f dB (error %.1e)", gr, err);
    }
    {
        TruePeakLimiter lim; lim.prepare(sr); lim.setEnabled(true); lim.reset();
        long at = -1;
        for (long n = 0; n < 2000; ++n) { float l = n == 10 ? 0.1f : 0.0f, r = l; lim.process(l, r); if (l != 0.0f && at < 0) at = n - 10; }
        CHECK(at == lim.latencySamples() && lim.latencySamples() > 100 && lim.latencySamples() < 200,
              "latency: an impulse comes out exactly %ld samples later (reported %d, ~3.2 ms)", at, lim.latencySamples());
    }
    {
        TruePeakLimiter lim; lim.prepare(sr); lim.reset();
        bool exact = true;
        for (long n = 0; n < 10000; ++n) { const float x = (float)(3 * std::sin(n * 0.01)); float l = x, r = -x; lim.process(l, r); exact = exact && l == x && r == -x; }
        CHECK(exact && lim.gainReductionDb() == 0.0, "off: untouched, no delay, no gain reduction");
    }
    {   // switch on and off mid-stream: the crossfade keeps every step small
        TruePeakLimiter lim; lim.prepare(sr); lim.reset();
        float prev = 0; double worst = 0;
        for (long n = 0; n < (long)(sr * 2); ++n)
        {
            if (n == (long)(sr * 0.5)) lim.setEnabled(true);
            if (n == (long)(sr * 1.5)) lim.setEnabled(false);
            const float x = (float)(0.3 * std::sin(2 * kPi * 100 * n / sr));
            float l = x, r = x; lim.process(l, r);
            if (n > 0) worst = std::fmax(worst, std::fabs(l - prev));
            prev = l;
        }
        const double maxStep = 0.3 * 2 * kPi * 100 / sr;
        CHECK(worst < 3.0 * maxStep, "on/off crossfade: largest sample step %.4f (signal's own %.4f)", worst, maxStep);
    }
    {   // bypass with the limiter on: the delayed input, bit for bit
        TruePeakLimiter lim; lim.prepare(sr); lim.setEnabled(true); lim.setBypass(true); lim.reset();
        std::vector<float> in;
        bool exact = true;
        const int d = lim.latencySamples();
        for (long n = 0; n < 20000; ++n)
        {
            const float x = (float)(3 * std::sin(n * 0.01));
            in.push_back(x);
            float l = x, r = x; lim.process(l, r);
            if (n >= d) exact = exact && l == in[(size_t)(n - d)];
        }
        CHECK(exact, "bypass with LIMITER on: output is the input delayed by the latency, bit-exact");
    }
}

static void testAutoLoudness()
{
    std::printf("AUTO loudness\n");
    const double sr = 48000;
    const int block = 480;
    // stereo 1 kHz sine at `db` dBFS peak (= db LUFS), fed through AUTO; the output meter reads x * gain.
    struct Run
    {
        AutoLoudness a; LoudnessMeter out; double sr; long n = 0; double maxSlew = 0;
        explicit Run(double s) : sr(s) { a.prepare(s); out.prepare(s); a.setTargetLufs(-14.0); a.setEnabled(true); }
        void go(double seconds, double db)
        {
            const double amp = db <= -200 ? 0.0 : std::pow(10.0, db / 20.0);
            for (long k = 0; k < std::lround(seconds * sr / block); ++k)
            {
                const double g = std::pow(10.0, a.gainDb() / 20.0);
                for (int i = 0; i < block; ++i, ++n)
                {
                    const double x = amp * std::sin(2.0 * kPi * 1000.0 * (double)n / sr);
                    a.feed(x, x);
                    out.process(x * g, x * g);
                }
                const double before = a.gainDb();
                a.advance(block);
                maxSlew = std::fmax(maxSlew, std::fabs(a.gainDb() - before) / (block / sr));
            }
        }
    };
    {
        Run r(sr);
        r.go(2.0, -24.0);
        CHECK(r.a.state() == AutoLoudness::kLearning && r.a.gainDb() == 0.0, "first 3 s: LEARNING, gain held");
        r.go(38.0, -24.0);
        CHECK(std::fabs(r.a.gainDb() - 10.0) < 0.35 && r.maxSlew <= 0.5 + 1e-9 && r.a.state() == AutoLoudness::kInRange,
              "-24 LUFS towards -14: gain %+.2f dB, slew <= %.2f dB/s, IN RANGE", r.a.gainDb(), r.maxSlew);
        r.out.clear();
        r.go(10.0, -24.0);
        CHECK(std::fabs(r.out.integrated() + 14.0) < 0.5, "output integrated %.2f LUFS (target -14)", r.out.integrated());
        const double g = r.a.gainDb();
        r.go(10.0, -300.0);
        CHECK(r.a.gainDb() == g && r.a.state() == AutoLoudness::kHold, "silence: HOLD, gain unchanged (%+.2f)", r.a.gainDb());
        r.a.clear();
        r.go(0.5, -24.0);
        CHECK(r.a.gainDb() == g && r.a.state() == AutoLoudness::kLearning, "CLEAR: relearns without moving the gain");
        r.a.setEnabled(false);
        r.go(4.0, -24.0);
        CHECK(std::fabs(r.a.gainDb() - std::fmax(0.0, g - 8.0)) < 0.05, "AUTO off: returns at 2 dB/s (%+.2f after 4 s)", r.a.gainDb());
        r.go(2.0, -24.0);
        CHECK(r.a.gainDb() == 0.0 && r.a.state() == AutoLoudness::kOff, "...and lands exactly on 0 dB");
    }
    {
        Run r(sr);
        r.go(10.0, -6.0);
        CHECK(r.a.gainDb() < 0.0 && r.a.state() == AutoLoudness::kAdjusting, "a louder programme turns down (%+.2f dB, ADJUSTING)", r.a.gainDb());
    }
    {
        Run r(sr);
        r.go(40.0, -40.0);
        CHECK(std::fabs(r.a.gainDb() - 12.0) <= AutoLoudness::kStopLU + 1e-9 && r.a.state() == AutoLoudness::kGainLimit, "needs +26 dB: stops at +12 within the deadband (GAIN LIMIT; %+.2f, state %d)",
              r.a.gainDb(), (int)r.a.state());
    }
    {
        Run r(sr);
        r.go(10.0, -24.0);
        const double g0 = r.a.gainDb();
        r.a.setTargetLufs(-18.0);
        r.go(1.5, -24.0);
        CHECK(r.maxSlew <= 0.5 + 1e-9 && r.a.gainDb() != g0, "a TARGET change ramps (slew %.2f dB/s)", r.maxSlew);
    }
}

int main()
{
    testHeat();
    testCycle();
    testV02();
    testLoudness();
    testTruePeak();
    testLimiter();
    testAutoLoudness();
    testCalibration();
    testScale();
    testStep();
    testMono();
    testWaveform();
    testSilenceAndSafety();
    testPeaks();
    testInvariance();
    testTrim();
    std::printf("\ncore_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
