// Core engine tests for the Slow Ops VU Meter (no SDK dependency).
// Implements the pass/fail table of the spec, section 8.1, plus trim/bypass checks.
#include "slowops/vumeter/VuEngine.hpp"
#include "slowops/vumeter/Heat.hpp"
#include "slowops/vumeter/HueCycle.hpp"
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

int main()
{
    testHeat();
    testCycle();
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
