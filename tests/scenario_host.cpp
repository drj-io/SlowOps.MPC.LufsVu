// scenario_host.cpp - drives the real native VU Meter build through the VST2 ABI (like the MPC)
// and checks the spec's integration requirements (section 8.2).
//
// Build: c++ -std=c++17 -O2 -I third_party/mpc-plugin-builder-sdk/framework tests/scenario_host.cpp -o build/scenario_host
// Run:   build/scenario_host plugin/build/native/vu-meter.dylib [out_dir]
#include "mpc_vst2_abi.h"

#include <dlfcn.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; std::printf("  PASS "); } else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); } \
                              std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

// Parameter indices (plugin/src/SlowOpsVUMeter.cpp). Permanent.
enum P { kReserved = 0, kMode, kRef, kPeak, kLight, kBypass, kTrim, kResetClip,
         kView, kClipL, kClipR, kClipIn, kNeedleL0, kNeedleR0 = kNeedleL0 + 4, kNeedleM0 = kNeedleR0 + 4,
         kGlow = kNeedleM0 + 8,
         kAutoTrim, kAutoBusy, kCorr, kMaxL, kMaxR, kMaxM, kClipThr, kCycleBars, kPalette,
         kLufsAuto, kTarget, kLimiter, kCeiling, kMeasure, kLufsClear, kTargetDown, kTargetUp, kCeilDown, kCeilUp,
         kIntegrated, kShortTerm, kMomentary, kTpMax, kGainRed, kAutoState, kAutoGain, kElapsed, kNumParams };
// Glow frames: 3 palettes x 16 heat steps, then 120 hue steps, then the flash frame.
static constexpr int kHues = 120, kHueFirst = 48, kFlash = kHueFirst + kHues, kGlowMax = kFlash;
static float light(int i) { return (float)i / 4.0f; }   // Warm, Cool, Off, React, Cycle
static long viewOf(float v) { return std::lround(v * 9); }
// Pseudo-indices for the needle decoder.
enum Needle { kNeedleL = 0, kNeedleR, kNeedleM };
struct NeedleLayout { int first, sectors, per, frames; };
static const NeedleLayout kLayout[3] = { { kNeedleL0, 4, 32, 128 }, { kNeedleR0, 4, 32, 128 }, { kNeedleM0, 8, 32, 256 } };
static constexpr double kPi = 3.14159265358979323846;

// ---- host side ----------------------------------------------------------------------
struct Automate { int count[kNumParams] = {}; float value[kNumParams] = {}; int ioChanged = 0; };
static std::map<AEffect*, Automate> g_auto;
static VstTimeInfo g_time{};
static VstIntPtr hostCallback(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr, void* ptr, float opt)
{
    switch (opcode)
    {
        case audioMasterAutomate:
            if (index >= 0 && index < kNumParams) { ++g_auto[e].count[index]; g_auto[e].value[index] = opt; }
            return 0;
        case audioMasterIOChanged: ++g_auto[e].ioChanged; return 1;
        case audioMasterVersion: return 2400;
        case audioMasterGetTime: return (VstIntPtr)&g_time;
        case audioMasterGetVendorString: if (ptr) std::strcpy((char*)ptr, "MPC 3"); return 1;
        case audioMasterGetProductString: if (ptr) std::strcpy((char*)ptr, "MPC 3"); return 1;
        default: return 0;
    }
}

typedef AEffect* (*MainProc)(audioMasterCallback);
static MainProc g_main = nullptr;

using Source = std::function<void(int64_t t, float& l, float& r)>;

struct Plug
{
    AEffect* e = nullptr;
    double sr;
    int64_t t = 0;
    Source input = [](int64_t, float& l, float& r) { l = r = 0.0f; };
    bool inPlace = true;
    bool transport = false;        // provide tempo / song position to the plugin
    bool rolling = false;          // song position advances
    double bpm = 120.0, ppq = 0.0;
    int num = 4, den = 4;
    bool bitExact = true;          // output == input for every processed sample so far
    double maxGainErr = 0.0;       // vs an expected gain, when `expectGain` > 0
    float expectGain = 0.0f;
    double outPeak = 0.0;          // largest |output| sample so far

    VstIntPtr disp(VstInt32 op, VstInt32 idx = 0, VstIntPtr v = 0, void* p = nullptr, float o = 0.0f) { return e->dispatcher(e, op, idx, v, p, o); }

    explicit Plug(double sampleRate = 44100.0, int maxBlock = 512) : sr(sampleRate)
    {
        e = g_main(hostCallback);
        disp(effSetSampleRate, 0, 0, nullptr, (float)sr);   // MPC-like load + prepare sequence
        disp(effSetBlockSize, 0, maxBlock);
        disp(effOpen);
        disp(effOpen);
        disp(effSetProgram, 0, 0);
        disp(effMainsChanged, 0, 1);
        const float p0 = e->getParameter(e, 0);             // parameter-0 nudge quirk
        e->setParameter(e, 0, p0 >= 0.5f ? 0.0f : 1.0f);
        e->setParameter(e, 0, p0);
        disp(effStartProcess);
    }
    ~Plug() { disp(effStopProcess); disp(effMainsChanged, 0, 0); disp(effClose); }

    void set(int i, float v) { e->setParameter(e, i, v); }
    float get(int i) { return e->getParameter(e, i); }
    std::string text(int i) { char buf[256] = {0}; disp(effGetParamDisplay, i, 0, buf); return buf; }
    // Global needle frame from the sector displays; -1 if not exactly one sector shows a needle.
    int frame(int which)
    {
        const NeedleLayout& n = kLayout[which];
        int found = -1, shown = 0;
        for (int k = 0; k < n.sectors; ++k)
        {
            const int local = (int)std::lround(get(n.first + k) * n.per);
            if (local > 0) { ++shown; found = k * n.per + local - 1; }
        }
        return shown == 1 ? found : -1;
    }
    Automate& automate() { return g_auto[e]; }

    void block(int n)
    {
        std::vector<float> l((size_t)n), r((size_t)n), ol((size_t)n, -9.0f), orr((size_t)n, -9.0f);
        for (int i = 0; i < n; ++i) input(t + i, l[(size_t)i], r[(size_t)i]);
        const std::vector<float> il = l, ir = r;
        std::memset(&g_time, 0, sizeof(g_time));
        if (transport)
        {
            g_time.sampleRate = sr; g_time.tempo = bpm; g_time.ppqPos = ppq;
            g_time.timeSigNumerator = num; g_time.timeSigDenominator = den;
            g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid | (rolling ? kVstTransportPlaying : 0);
        }
        float* in[2] = { l.data(), r.data() };
        float* outs[2] = { ol.data(), orr.data() };
        if (inPlace) e->processReplacing(e, in, in, n);
        else         e->processReplacing(e, in, outs, n);
        const float* oL = inPlace ? l.data() : ol.data();
        const float* oR = inPlace ? r.data() : orr.data();
        for (int i = 0; i < n; ++i)
        {
            outPeak = std::max(outPeak, (double)std::max(std::fabs(oL[i]), std::fabs(oR[i])));
            if (std::memcmp(&oL[i], &il[(size_t)i], 4) != 0 || std::memcmp(&oR[i], &ir[(size_t)i], 4) != 0) bitExact = false;
            if (expectGain > 0.0f && il[(size_t)i] != 0.0f)
                maxGainErr = std::max(maxGainErr, std::fabs((double)oL[i] / il[(size_t)i] - expectGain));
        }
        t += n;
        if (rolling) ppq += (double)n / sr * bpm / 60.0;
    }
    void runFor(double s, std::vector<int> blocks = { 512 })
    {
        int64_t end = t + (int64_t)std::llround(s * sr);
        size_t k = 0;
        while (t < end) block((int)std::min<int64_t>(blocks[k++ % blocks.size()], end - t));
    }
    std::vector<uint8_t> save()
    {
        void* p = nullptr;
        const VstIntPtr n = disp(effGetChunk, 0, 0, &p);
        return std::vector<uint8_t>((const uint8_t*)p, (const uint8_t*)p + n);
    }
    void load(std::vector<uint8_t>& c) { disp(effSetChunk, 0, (VstIntPtr)c.size(), c.data()); }
};

static Source sine(double sr, double peakDb, double polR = 1.0, double hz = 1000.0)
{
    const double a = std::pow(10.0, peakDb / 20.0);
    return [=](int64_t t, float& l, float& r) {
        const double s = a * std::sin(2.0 * kPi * hz * (double)t / sr);
        l = (float)s; r = (float)(polR * s);
    };
}
static Source program(double sr)   // stereo, decorrelated, moderate level, deterministic
{
    return [=](int64_t t, float& l, float& r) {
        const double x = (double)t / sr;
        l = (float)(0.20 * std::sin(2 * kPi * 220 * x) + 0.05 * std::sin(2 * kPi * 3170 * x));
        r = (float)(0.15 * std::sin(2 * kPi * 330 * x + 1.0) + 0.07 * std::sin(2 * kPi * 51 * x));
    };
}
static int vuFrame(double vu, int frames = 128) { return (int)std::lround(std::pow(10.0, vu / 20.0) / std::pow(10.0, 3.0 / 20.0) * (frames - 1)); }

// ---- scenarios ------------------------------------------------------------------------
static void transparency()
{
    std::printf("Audio transparency (trim 0.0 dB)\n");
    for (int inPlace = 1; inPlace >= 0; --inPlace)
        for (int light = 0; light < 5; ++light)
        {
            Plug p; p.inPlace = inPlace; p.set(kLight, (float)light / 4.0f); p.input = program(p.sr);
            p.runFor(1.0, { 512, 37, 128, 1, 300 });
            CHECK(p.bitExact, "%s, light %d: output bit-identical to input", inPlace ? "in place" : "separate buffers", light);
        }
    Plug p; p.input = program(p.sr);
    p.runFor(0.3); p.set(kMode, 1.0f); p.runFor(0.3); p.set(kMode, 0.0f); p.runFor(0.3);
    CHECK(p.bitExact, "toggling the retired Mode slot while playing: output unchanged");
    CHECK(p.text(kTrim) == "0.0 dB", "default trim text is \"%s\"", p.text(kTrim).c_str());
}

static void trimAndBypass()
{
    std::printf("Output trim and bypass\n");
    Plug p; p.input = program(p.sr);
    p.set(kTrim, 18.0f / 24.0f);                 // +6.0 dB
    p.runFor(0.2);
    p.expectGain = std::pow(10.0f, 6.0f / 20.0f); p.maxGainErr = 0.0;
    p.runFor(0.5);
    CHECK(p.maxGainErr < 1e-5, "trim +6.0 dB: gain %.5f (max error %.2e)", (double)p.expectGain, p.maxGainErr);
    p.expectGain = 0.0f;
    p.set(kBypass, 1.0f); p.runFor(0.2);
    p.bitExact = true; p.runFor(0.5);
    CHECK(p.bitExact, "bypass on (trim +6): bit-identical after the 20 ms ramp");
    p.set(kBypass, 0.0f); p.set(kTrim, 0.5f); p.runFor(0.2);
    p.bitExact = true; p.runFor(0.5);
    CHECK(p.bitExact, "back to trim 0.0 dB: bit-identical again");
}

static void meters()
{
    std::printf("Needle displays\n");
    {
        Plug p; p.input = sine(p.sr, -18.0);
        p.runFor(2.0);
        const int f0 = vuFrame(0.0);
        CHECK(std::abs(p.frame(kNeedleL) - f0) <= 1 && std::abs(p.frame(kNeedleR) - f0) <= 1,
              "-18 dBFS sine at ref -18: L/R frames %d/%d (0 VU = %d)", p.frame(kNeedleL), p.frame(kNeedleR), f0);
        const int s0 = vuFrame(0.0) / 32;
        CHECK(p.text(kNeedleL0 + s0) == "+0.0 VU", "needle sector text \"%s\"", p.text(kNeedleL0 + s0).c_str());
        p.set(kRef, 0.0f);   // -20 dBFS reference -> same signal reads +2 VU
        p.runFor(1.5);
        CHECK(std::abs(p.frame(kNeedleL) - vuFrame(2.0)) <= 1, "ref -20: reads +2 VU (frame %d, expect %d)", p.frame(kNeedleL), vuFrame(2.0));
    }
    {
        Plug p; p.input = sine(p.sr, -18.0, -1.0);      // anti-phase
        p.runFor(1.5);
        CHECK(std::abs(p.frame(kNeedleL) - vuFrame(0.0)) <= 1, "anti-phase, STEREO: L at 0 VU");
        p.set(kMode, 1.0f); p.runFor(1.0);             // retired Mono slot: still stereo
        CHECK(std::abs(p.frame(kNeedleL) - vuFrame(0.0)) <= 1 && std::abs(p.frame(kNeedleR) - vuFrame(0.0)) <= 1,
              "retired Mode = Mono is ignored: L/R needles keep metering");
        int monoWrites = 0;
        for (int i = kNeedleM0; i < kNeedleM0 + 8; ++i) monoWrites += p.automate().count[i];
        CHECK(monoWrites == 0 && p.automate().count[kClipIn] == 0 && p.automate().count[kMaxM] == 0,
              "retired mono displays are never published");
    }
}

static void displayBudget()
{
    std::printf("Display update budget\n");
    for (int mode = 0; mode < 1; ++mode)       // stereo only (Mode is retired)
    {
        Plug p;
        Source prog = program(p.sr);
        p.input = [prog, sr = p.sr](int64_t t, float& l, float& r) {   // level swings at 1 Hz: needles keep moving
            prog(t, l, r);
            const float g = (float)(0.55 + 0.45 * std::sin(2 * kPi * 1.0 * (double)t / sr));
            l *= g; r *= g;
        };
        p.runFor(0.5);
        Automate before = p.automate();
        p.runFor(3.0, { 128 });
        int needles = 0, hidden = 0, total = 0;
        for (int i = 0; i < kNumParams; ++i) total += p.automate().count[i] - before.count[i];
        for (int i = kNeedleL0; i < kGlow; ++i)
        {
            const int d = p.automate().count[i] - before.count[i];
            const bool visible = mode == 0 ? i < kNeedleM0 : i >= kNeedleM0;
            (visible ? needles : hidden) += d;
        }
        const int maxNeedles = (mode == 0 ? 2 : 1) * 3 * 30 * 2;   // <= 2 writes per needle per tick (sector change)
        CHECK(needles <= maxNeedles && needles > 0, "%s: %d needle writes in 3 s (<= %d)", mode ? "MONO" : "STEREO", needles, maxNeedles);
        CHECK(hidden == 0, "%s: hidden view's needles not published (%d writes)", mode ? "MONO" : "STEREO", hidden);
        bool oneSector = true;
        for (int k = 0; k < 90; ++k) { p.runFor(1.0 / 30.0, { 128 }); for (int w : { kNeedleL, kNeedleR, kNeedleM }) if ((mode == 0) == (w != kNeedleM) && p.frame(w) < 0) oneSector = false; }
        CHECK(oneSector, "%s: exactly one sector shows each visible needle at every tick", mode ? "MONO" : "STEREO");
        CHECK(total <= 3 * 230, "%s: %d host notifications in 3 s (MPC comfortable at ~230/s)", mode ? "MONO" : "STEREO", total);
    }
    Plug p; p.runFor(1.0);
    Automate before = p.automate();
    p.runFor(2.0);
    int total = 0;
    for (int i = 0; i < kNumParams; ++i) if (i != kElapsed) total += p.automate().count[i] - before.count[i];
    CHECK(total == 0, "silence at rest: no redundant writes (%d; the elapsed clock ticks once a second)", total);
    CHECK(p.automate().count[kElapsed] - before.count[kElapsed] <= 3, "elapsed clock: at most one write a second");
}

static void clips()
{
    std::printf("Clip lamps\n");
    Plug p;
    p.input = [](int64_t t, float& l, float& r) { l = (t == 1000) ? 1.0f : 0.0f; r = 0.0f; };
    p.runFor(0.2);
    CHECK(p.get(kClipL) >= 0.5f && p.get(kClipR) < 0.5f, "STEREO: one full-scale L sample lights L CLIP only");
    p.runFor(1.5);
    CHECK(p.get(kClipL) >= 0.5f, "still lit at ~1.7 s (2 s hold)");
    p.set(kResetClip, 1.0f); p.runFor(0.1);
    CHECK(p.get(kClipL) < 0.5f && p.get(kResetClip) < 0.5f, "RESET CLIP clears it and the trigger returns to Off");

    Plug m;
    m.input = [](int64_t t, float& l, float& r) { l = (t % 500 == 0) ? 1.0f : 0.0f; r = -l; };
    m.runFor(0.5);
    CHECK(m.get(kClipL) >= 0.5f && m.get(kClipR) >= 0.5f, "L=+1/R=-1 impulses light both CLIP lamps");
}

static void state()
{
    std::printf("Save / reload\n");
    Plug a; a.input = sine(a.sr, -6.0);
    a.set(kRef, 1.0f); a.set(kPeak, 0.0f); a.set(kLight, light(1)); a.set(kTrim, 0.75f);
    a.runFor(1.0);
    a.set(kResetClip, 1.0f);                       // saved in the "on" position: must not fire on load
    auto chunk = a.save();
    Plug b; b.load(chunk);
    b.input = [](int64_t, float& l, float& r) { l = r = 0.0f; };
    b.runFor(0.1);
    CHECK(b.get(kRef) == 1.0f && b.get(kPeak) == 0.0f && std::lround(b.get(kLight) * 4) == 1 &&
          b.text(kTrim) == "+6.0 dB", "ref/peak/light/trim restored (trim \"%s\")", b.text(kTrim).c_str());
    CHECK(viewOf(b.get(kView)) == 1, "view = Cool (%ld)", viewOf(b.get(kView)));
    CHECK(b.get(kResetClip) < 0.5f, "a saved reset trigger is cleared, not fired as a setting");
    CHECK(b.frame(kNeedleL) == 0, "needle shows the live (silent) reading, not the saved one");
}

static void react()
{
    std::printf("React lighting\n");
    auto heatStep = [](Plug& p) { return (int)std::lround(p.get(kGlow) * kGlowMax); };
    Plug p; p.set(kLight, light(3));   // React
    p.runFor(0.3);
    CHECK(viewOf(p.get(kView)) == 3, "view = Stereo React");
    CHECK(heatStep(p) == 0, "silence glows coldest blue (step %d)", heatStep(p));
    p.input = sine(p.sr, -18.0);   // 0 VU
    p.runFor(3.0);
    CHECK(std::abs(heatStep(p) - 10) <= 1, "0 VU glows red (step %d of 15)", heatStep(p));
    p.input = sine(p.sr, -15.0);   // +3 VU
    p.runFor(3.0);
    CHECK(heatStep(p) == 15, "+3 VU glows full purple (step %d)", heatStep(p));
    p.input = [](int64_t, float& l, float& r) { l = r = 0.0f; };
    p.runFor(0.2);
    const int warmish = heatStep(p);
    CHECK(warmish > 6, "cools slowly: still step %d 0.2 s after the music stops", warmish);
    p.runFor(4.0);
    CHECK(heatStep(p) == 0, "back to blue after the glow decays");
    p.set(kMode, 1.0f); p.runFor(0.2);
    CHECK(viewOf(p.get(kView)) == 3, "retired Mode slot does not change the view");
    p.bitExact = true; p.input = program(p.sr); p.runFor(0.5);
    CHECK(p.bitExact, "React lighting never touches the audio");

    Plug q; q.input = sine(q.sr, -15.0); q.runFor(2.0);   // Warm: heat is not published
    CHECK(q.automate().count[kGlow] == 0, "glow is only published while Light = React or Cycle");
}

static void cycle()
{
    std::printf("Cycle lighting\n");
    auto glow = [](Plug& p) { return (int)std::lround(p.get(kGlow) * kGlowMax); };
    auto expectHue = [](double ppq, double cycleQ) { return kHueFirst + (int)std::lround(std::fmod(ppq, cycleQ) / cycleQ * kHues) % kHues; };
    auto hueDiff = [](int a, int b) { int d = std::abs(a - b) % kHues; return std::min(d, kHues - d); };
    {
        Plug p; p.set(kLight, light(4)); p.transport = p.rolling = true; p.bpm = 120.0; p.input = program(p.sr);
        p.runFor(1.0);
        CHECK(viewOf(p.get(kView)) == 4, "view = Stereo Cycle");
        int worst = 0; bool inRange = true;
        for (int i = 0; i < 24; ++i)
        {
            p.runFor(1.0 / 6.0, { 128 });
            worst = std::max(worst, hueDiff(glow(p), expectHue(p.ppq, 8.0)));
            inRange = inRange && glow(p) >= kHueFirst && glow(p) < kFlash;
        }
        CHECK(inRange, "Cycle only uses the hue frames (48..167)");
        CHECK(worst <= 1, "120 BPM 4/4: hue follows the song position, one cycle per 2 bars (worst %d step)", worst);
        CHECK(p.bitExact, "Cycle lighting never touches the audio");
    }
    {
        Plug p; p.set(kLight, light(4)); p.transport = p.rolling = true; p.bpm = 90.0; p.num = 6; p.den = 8; p.ppq = 10.0;
        p.runFor(1.0);
        const int a = glow(p);
        p.runFor(2.0);
        CHECK(hueDiff(glow(p), expectHue(p.ppq, 6.0)) <= 1 && std::abs(hueDiff(a, glow(p)) - kHues / 2) <= 1,
              "90 BPM 6/8: half a cycle in 2 s (two bars = 6 quarters = 4 s)");
    }
    {
        Plug p; p.set(kLight, light(4)); p.transport = true; p.rolling = false; p.bpm = 120.0; p.ppq = 3.0;
        p.runFor(0.5);
        const int a = glow(p);
        p.runFor(1.0);
        CHECK(std::abs(hueDiff(a, glow(p)) - kHues / 4) <= 1, "stopped: keeps cycling at the tempo (a quarter turn per second at 120 BPM)");
    }
}

static void v02()
{
    auto norm = [](int i, int n) { return (float)i / (float)(n - 1); };
    auto glow = [](Plug& p) { return (int)std::lround(p.get(kGlow) * kGlowMax); };

    std::printf("Retired AUTO TRIM slot\n");
    {
        Plug p; p.input = sine(p.sr, -24.0); p.runFor(0.5);
        p.set(kAutoTrim, 1.0f); p.runFor(4.0);
        CHECK(p.get(kAutoTrim) < 0.5f && p.text(kTrim) == "0.0 dB" && p.bitExact, "tapping it does nothing (trim %s, audio untouched)",
              p.text(kTrim).c_str());
    }

    std::printf("Phase correlation\n");
    {
        auto corr = [](Plug& p) { return (int)std::lround(p.get(kCorr) * 21); };
        Plug p; p.runFor(0.5);
        CHECK(corr(p) == 0, "silence: no reading");
        p.input = sine(p.sr, -18.0); p.runFor(1.5);
        CHECK(corr(p) == 21 && p.text(kCorr) == "+1.0", "mono material: +1 (%s)", p.text(kCorr).c_str());
        p.input = sine(p.sr, -18.0, -1.0); p.runFor(2.0);
        CHECK(corr(p) == 1 && p.text(kCorr) == "-1.0", "phase-flipped: -1 (%s)", p.text(kCorr).c_str());
    }

    std::printf("Max VU readouts\n");
    {
        Plug p; p.input = sine(p.sr, -15.0); p.runFor(1.5);   // +3 VU
        p.input = sine(p.sr, -24.0); p.runFor(1.5);            // -6 VU
        CHECK(p.text(kMaxL) == "MAX +3.1 VU" && p.text(kMaxR) == "MAX +3.1 VU", "STEREO: L/R hold %s (incl. needle overshoot)", p.text(kMaxL).c_str());
        p.set(kResetClip, 1.0f); p.runFor(0.5);
        CHECK(p.text(kMaxL) == "MAX -6.0 VU", "lamp tap clears it (now %s)", p.text(kMaxL).c_str());
    }

    std::printf("CLIP threshold\n");
    {
        Plug p; p.set(kClipThr, norm(2, 4));                    // -1 dBFS
        p.input = [](int64_t t, float& l, float& r) { l = (t == 500) ? 0.9f : 0.0f; r = 0.0f; };
        p.runFor(0.2);
        CHECK(p.get(kClipL) >= 0.5f, "-1 dBFS: a 0.9 sample lights CLIP");
        Plug q; q.input = [](int64_t t, float& l, float& r) { l = (t == 500) ? 0.9f : 0.0f; r = 0.0f; };
        q.runFor(0.2);
        CHECK(q.get(kClipL) < 0.5f, "0 dBFS (default): it does not");
    }

    std::printf("Light extras\n");
    {
        Plug p; p.set(kLight, light(4)); p.set(kCycleBars, norm(3, 4));   // 8 bars
        p.transport = p.rolling = true; p.bpm = 120.0;
        p.runFor(1.0);
        int worst = 0;
        for (int i = 0; i < 12; ++i)
        {
            p.runFor(0.5);
            const int want = kHueFirst + (int)std::lround(std::fmod(p.ppq, 32.0) / 32.0 * kHues) % kHues;
            int d = std::abs(glow(p) - want) % kHues; worst = std::max(worst, std::min(d, kHues - d));
        }
        CHECK(worst <= 1, "Cycle rate 8 bars: one trip per 32 beats (worst %d step)", worst);
    }
    {
        Plug p; p.set(kLight, light(3)); p.set(kPalette, norm(1, 3));     // React, Neon
        p.input = sine(p.sr, -15.0); p.runFor(3.0);
        CHECK(glow(p) == 16 + 15, "React palette Neon: frame %d (palette offset 16)", glow(p));
        p.set(kPalette, norm(2, 3)); p.runFor(0.1);
        CHECK(glow(p) == 32 + 15, "palette Amber: frame %d", glow(p));
    }
    {
        Plug p; p.set(kLight, light(4)); p.transport = p.rolling = true; p.bpm = 120.0;
        p.input = [](int64_t t, float& l, float& r) { l = (t == 44100) ? 1.0f : 0.0f; r = 0.0f; };
        p.runFor(1.1);
        CHECK(glow(p) == kFlash, "clip: the cabinet flashes");
        p.runFor(0.35);
        CHECK(glow(p) == kFlash, "...for a beat (still flashing at ~0.45 s)");
        p.runFor(0.2);
        CHECK(glow(p) != kFlash && glow(p) >= kHueFirst, "...then back to the cycle after one beat at 120 BPM (0.5 s)");
        Plug q; q.input = [](int64_t t, float& l, float& r) { l = (t == 1000) ? 1.0f : 0.0f; r = 0.0f; };
        q.runFor(0.5);
        CHECK(q.automate().count[kGlow] == 0, "no flash (no glow writes) with a static light");
    }
}

static bool near(const std::string& text, double expect, double tol)
{
    char* end = nullptr;
    const double v = std::strtod(text.c_str() + (text.rfind("M ", 0) == 0 ? 2 : 0), &end);
    return end != nullptr && end != text.c_str() && std::fabs(v - expect) <= tol + 1e-9;
}

static void lufs()
{
    auto norm = [](float v, float lo, float hi) { return (v - lo) / (hi - lo); };
    std::printf("LUFS readouts (output, post-processing)\n");
    {
        Plug p(48000.0); p.input = sine(p.sr, -23.0);
        CHECK(p.text(kIntegrated) == "--.-" && p.text(kShortTerm) == "--.-" && p.text(kTpMax) == "--.-", "before audio: \"--.-\"");
        p.runFor(6.0);
        CHECK(near(p.text(kIntegrated), -23.0, 0.1) && near(p.text(kShortTerm), -23.0, 0.1) && near(p.text(kMomentary), -23.0, 0.1)
              && p.text(kMomentary).rfind("M ", 0) == 0,
              "-23 dBFS stereo sine: I %s, S %s, %s", p.text(kIntegrated).c_str(), p.text(kShortTerm).c_str(), p.text(kMomentary).c_str());
        CHECK(near(p.text(kTpMax), -23.0, 0.1) && p.text(kGainRed) == "0.0", "TP MAX %s dBTP, GR %s", p.text(kTpMax).c_str(), p.text(kGainRed).c_str());
        CHECK(p.text(kElapsed) == "00:05" || p.text(kElapsed) == "00:06", "elapsed %s", p.text(kElapsed).c_str());
        CHECK(p.text(kAutoState) == "AUTO OFF" && p.bitExact, "AUTO OFF, LIMITER off: audio bit-exact");

        p.set(kMeasure, 1.0f);                       // HOLD
        p.input = sine(p.sr, -13.0); p.runFor(4.0);
        CHECK(near(p.text(kIntegrated), -23.0, 0.1) && near(p.text(kShortTerm), -13.0, 0.1) && near(p.text(kTpMax), -23.0, 0.1)
              && (p.text(kElapsed) == "00:05" || p.text(kElapsed) == "00:06"),
              "HOLD: I %s and TP MAX %s frozen, S live (%s), elapsed %s", p.text(kIntegrated).c_str(), p.text(kTpMax).c_str(),
              p.text(kShortTerm).c_str(), p.text(kElapsed).c_str());
        p.set(kMeasure, 0.0f);
        p.set(kLufsClear, 1.0f); p.runFor(0.2);
        CHECK(p.get(kLufsClear) < 0.5f && p.text(kIntegrated) == "--.-" && p.text(kElapsed) == "00:00" && near(p.text(kTpMax), -13.0, 0.1),
              "CLEAR: I restarts (%s), elapsed %s, TP MAX from now (%s)", p.text(kIntegrated).c_str(), p.text(kElapsed).c_str(), p.text(kTpMax).c_str());
        p.runFor(3.0);
        CHECK(near(p.text(kIntegrated), -13.0, 0.1), "...then measures the new programme (%s)", p.text(kIntegrated).c_str());
    }
    std::printf("TARGET / CEILING arrows\n");
    {
        Plug p;
        CHECK(p.text(kTarget) == "-14.0 LUFS" && p.text(kCeiling) == "-1.0 dBTP", "defaults %s, %s", p.text(kTarget).c_str(), p.text(kCeiling).c_str());
        p.set(kTargetUp, 1.0f); p.runFor(0.05);
        CHECK(p.text(kTarget) == "-13.5 LUFS" && p.get(kTargetUp) < 0.5f, "TARGET > steps 0.5 (%s)", p.text(kTarget).c_str());
        for (int i = 0; i < 4; ++i) { p.set(kTargetDown, 1.0f); p.runFor(0.05); }
        CHECK(p.text(kTarget) == "-15.5 LUFS", "TARGET < (%s)", p.text(kTarget).c_str());
        p.set(kTarget, 0.99f);
        for (int i = 0; i < 3; ++i) { p.set(kTargetUp, 1.0f); p.runFor(0.05); }
        CHECK(p.text(kTarget) == "-9.0 LUFS", "TARGET stops at -9 (%s)", p.text(kTarget).c_str());
        p.set(kCeilUp, 1.0f); p.runFor(0.05);
        p.set(kCeilUp, 1.0f); p.runFor(0.05);
        CHECK(p.text(kCeiling) == "-0.8 dBTP", "CEILING > steps 0.1 (%s)", p.text(kCeiling).c_str());
        p.set(kCeiling, 0.0f); p.set(kCeilDown, 1.0f); p.runFor(0.05);
        CHECK(p.text(kCeiling) == "-6.0 dBTP", "CEILING stops at -6 (%s)", p.text(kCeiling).c_str());
    }
    std::printf("LIMITER\n");
    {
        Plug p; p.input = sine(p.sr, -3.0, 1.0, 997.0);
        const int io0 = p.automate().ioChanged;
        p.set(kLimiter, 1.0f);
        const int lat = p.e->initialDelay;
        CHECK(lat > 100 && lat < 200 && p.automate().ioChanged > io0, "on: latency %d samples (%.1f ms) reported, host told (IOChanged)",
              lat, lat * 1000.0 / p.sr);
        p.set(kTrim, norm(6.0f, -12.0f, 12.0f));     // +6 dB into a -1.0 dBTP ceiling
        p.runFor(0.5);
        p.set(kLufsClear, 1.0f);                     // TRUE PEAK MAX from here (the 20 ms switch-on fade is past)
        p.outPeak = 0.0; p.runFor(1.0);
        CHECK(p.outPeak <= std::pow(10.0, -1.0 / 20.0) && p.outPeak > std::pow(10.0, -1.5 / 20.0) && near(p.text(kGainRed), 4.1, 0.2),
              "-3 dBFS + 6 dB trim: output peak %.2f dBFS, GR %s dB", 20 * std::log10(p.outPeak), p.text(kGainRed).c_str());
        CHECK(near(p.text(kTpMax), -1.1, 0.1), "TRUE PEAK MAX (post-limiter) %s dBTP", p.text(kTpMax).c_str());
        p.set(kLimiter, 0.0f);
        CHECK(p.e->initialDelay == 0, "off: latency back to 0");
        p.runFor(4.0);
        CHECK(p.text(kAutoState) == "CLIP RISK" && near(p.text(kGainRed), 0.0, 0.0), "LIMITER off and over 0 dBTP: CLIP RISK, GR %s",
              p.text(kGainRed).c_str());
        p.outPeak = 0.0; p.runFor(0.5);
        CHECK(p.outPeak > 1.3, "no hidden limiting with LIMITER off (peak %.2f dBFS)", 20 * std::log10(p.outPeak));
        p.set(kTrim, 0.5f); p.input = sine(p.sr, -20.0);
        p.runFor(4.0);
        CHECK(p.text(kAutoState) == "AUTO OFF", "CLIP RISK clears after 3 s of safe level");
    }
    {
        Plug p; p.set(kLimiter, 1.0f); p.set(kBypass, 1.0f); p.input = sine(p.sr, 0.0);
        p.runFor(1.0);
        CHECK(p.e->initialDelay > 0 && p.outPeak <= 1.0 && p.outPeak > 0.99, "BYPASS with LIMITER on: no limiting (peak %.3f), latency kept",
              p.outPeak);
    }
    std::printf("AUTO loudness\n");
    {
        Plug p(48000.0); p.input = sine(p.sr, -24.0);
        p.set(kLufsAuto, 1.0f);
        p.runFor(2.0);
        CHECK(p.text(kAutoState) == "AUTO LEARNING" && p.text(kAutoGain) == "0.0 dB", "first seconds: %s, %s",
              p.text(kAutoState).c_str(), p.text(kAutoGain).c_str());
        p.runFor(30.0, { 480 });
        CHECK(near(p.text(kAutoGain), 10.0, 0.35) && p.text(kAutoState) == "AUTO IN RANGE", "-24 LUFS programme, TARGET -14: %s, %s",
              p.text(kAutoGain).c_str(), p.text(kAutoState).c_str());
        p.set(kLufsClear, 1.0f); p.runFor(6.0);
        CHECK(near(p.text(kIntegrated), -14.0, 0.4) && near(p.text(kAutoGain), 10.0, 0.35), "output integrated %s LUFS; CLEAR kept the gain (%s)",
              p.text(kIntegrated).c_str(), p.text(kAutoGain).c_str());
        p.set(kTrim, norm(1.0f, -12.0f, 12.0f));       // +1 dB on purpose
        p.set(kLufsClear, 1.0f); p.runFor(6.0);
        CHECK(near(p.text(kIntegrated), -13.0, 0.4), "TRIM +1 dB is an offset AUTO keeps: %s LUFS", p.text(kIntegrated).c_str());
        p.input = [](int64_t, float& l, float& r) { l = r = 0.0f; };
        const std::string g = p.text(kAutoGain);
        p.runFor(5.0);
        CHECK(p.text(kAutoState) == "AUTO HOLD (SILENCE)" && p.text(kAutoGain) == g, "silence: %s, gain stays %s",
              p.text(kAutoState).c_str(), p.text(kAutoGain).c_str());
        p.set(kLufsAuto, 0.0f); p.runFor(6.0);
        CHECK(p.text(kAutoGain) == "0.0 dB" && p.text(kAutoState) == "AUTO OFF", "AUTO off: gain glides back to %s", p.text(kAutoGain).c_str());
        p.set(kTrim, 0.5f); p.input = sine(p.sr, -24.0); p.runFor(0.2);
        p.bitExact = true; p.runFor(0.5);
        CHECK(p.bitExact, "...and the audio is bit-exact again");
    }
    std::printf("Settings saved, measurements not\n");
    {
        Plug a; a.input = sine(a.sr, -20.0);
        a.set(kTarget, norm(-16.0f, -23.0f, -9.0f)); a.set(kLimiter, 1.0f); a.set(kMeasure, 1.0f); a.set(kLufsClear, 1.0f);
        a.runFor(2.0);
        auto chunk = a.save();
        Plug b; b.load(chunk);
        CHECK(b.text(kTarget) == "-16.0 LUFS" && b.get(kLimiter) >= 0.5f && b.get(kMeasure) >= 0.5f && b.e->initialDelay > 0,
              "TARGET, LIMITER (and its latency), HOLD restored");
        b.runFor(0.3);
        CHECK(b.text(kIntegrated) == "--.-" && b.text(kTpMax) == "--.-" && b.get(kLufsClear) < 0.5f, "no saved readings; CLEAR not re-fired");
    }
}

static void instances()
{
    std::printf("Multiple instances\n");
    Plug a, b;
    a.set(kLight, light(3)); b.set(kRef, 2.0f);
    a.input = sine(a.sr, -18.0); b.input = sine(b.sr, -18.0, 1.0, 440.0);
    for (int i = 0; i < 60; ++i) { a.block(512); b.block(512); }
    CHECK(viewOf(a.get(kView)) == 3 && viewOf(b.get(kView)) == 0, "lights independent");
    CHECK(std::abs(b.frame(kNeedleL) - vuFrame(-4.0)) <= 1, "B at ref -14 reads -4 VU (frame %d)", b.frame(kNeedleL));
    CHECK(std::abs(a.frame(kNeedleL) - vuFrame(0.0)) <= 1, "A at ref -18 reads 0 VU (frame %d)", a.frame(kNeedleL));
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s <plugin.dylib|.so> [out_dir]\n", argv[0]); return 2; }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 2; }
    g_main = (MainProc)dlsym(lib, "VSTPluginMain");
    if (!g_main) { std::fprintf(stderr, "VSTPluginMain not found\n"); return 2; }
    {
        Plug p;
        CHECK(p.e->numParams == kNumParams, "parameter count %d", p.e->numParams);
        CHECK(p.e->numInputs == 2 && p.e->numOutputs == 2 && p.e->initialDelay == 0, "stereo I/O, zero latency");
    }
    transparency();
    trimAndBypass();
    meters();
    displayBudget();
    clips();
    state();
    react();
    cycle();
    v02();
    lufs();
    instances();
    std::printf("\nscenario_host: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
