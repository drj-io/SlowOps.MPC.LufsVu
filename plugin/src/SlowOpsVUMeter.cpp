// Slow Ops LUFS VU (formerly "VU Meter"): thin mpc::Plugin adapter around the slowops::vumeter core.
//
// Audio (every stage is optional; with AUTO off, TRIM 0.0 dB and LIMITER off the output is a
// bit-exact copy of the input with zero latency):
//
//   in --+--> input loudness (AUTO's estimator; never the output, so no feedback loop)
//        |
//        +--> gain = AUTO + TRIM (one smoothed gain for L and R; unity on Bypass)
//               --> LIMITER (true-peak look-ahead; adds latency only while on)
//                     --> out --> VU needles, clip lamps, phase, LUFS M/S/I, true-peak max
//
// All metering reads the output (post-processing), so the METER and LUFS tabs agree.
// Stereo only (the v0.1 MONO view was retired in v0.2; its parameter slots stay so saved
// projects and indices remain valid).
//
// Display: each needle is drawn by several "sector" filmstrips, each covering a slice of the
// sweep (a small transparent sprite instead of one face-sized sprite, which keeps decoded
// image memory low). Frame 0 of every sector strip is blank; exactly one sector of a needle
// shows a needle at any time. Displays are published at most 30 times per second and only
// when a value actually changes.
#include "mpc_plugin.h"
#include "../../core/include/slowops/vumeter/VuEngine.hpp"
#include "../../core/include/slowops/vumeter/Heat.hpp"
#include "../../core/include/slowops/vumeter/HueCycle.hpp"
#include "../../core/include/slowops/vumeter/AutoLoudness.hpp"
#include "../../core/include/slowops/vumeter/LoudnessMeter.hpp"
#include "../../core/include/slowops/vumeter/TruePeak.hpp"
#include "../../core/include/slowops/vumeter/TruePeakLimiter.hpp"
#include "../../core/include/slowops/vumeter/Trim.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace slowops::vumeter;

namespace {

// Needle resolution. The skin generator reads sectors/frames back from params.json.
constexpr int kStereoSectors = 4, kStereoFrames = 128;   // per stereo needle
constexpr int kMonoSectors   = 8, kMonoFrames   = 256;   // the large mono needle
constexpr int kStereoPer = kStereoFrames / kStereoSectors;
constexpr int kMonoPer   = kMonoFrames / kMonoSectors;
// Glow filmstrip (React and Cycle lighting share one strip):
//   [palette 0 heat 0..15][palette 1 heat 0..15][palette 2 heat 0..15]  React (Thermal, Neon, Amber)
//   [hue 0..119]                                                        Cycle (3 degrees per step: smooth even at 8 bars)
//   [flash]                                                             white-hot clip flash
constexpr int kHeatFrames = 16;
constexpr int kPalettes = 3;
constexpr int kHueFrames = 120;
constexpr int kHueFirst = kHeatFrames * kPalettes;
constexpr int kFlashFrame = kHueFirst + kHueFrames;
constexpr int kGlowFrames = kFlashFrame + 1;

constexpr int kCorrSteps = 21;                // correlation -1..+1 in 21 steps (frame 0 = no signal)
constexpr float kMaxFloorVU = -40.0f;        // "cleared" max reading

static_assert(kStereoFrames % kStereoSectors == 0 && kMonoFrames % kMonoSectors == 0, "sectors must divide frames");

// -10 and -4 were added in v0.3 for dense, heavily compressed masters. They are appended, so a
// saved index (the SDK restores the plain choice index when the range changes) keeps its meaning.
const char* const kRefNames[] = { "-20 dBFS", "-18 dBFS", "-14 dBFS", "-10 dBFS", "-4 dBFS" };
constexpr double kRefDbfs[]   = { -20.0, -18.0, -14.0, -10.0, -4.0 };
const char* const kLightNames[] = { "Warm", "Cool", "Off", "React", "Cycle" };
constexpr int kNumLights = 5;
constexpr int kLightReact = 3;
constexpr int kLightCycle = 4;
constexpr double kClipThrDbfs[] = { 0.0, -0.3, -1.0, -3.0 };
constexpr double kCycleBarValues[] = { 1.0, 2.0, 4.0, 8.0 };

// LUFS tab
constexpr float kTargetMin = -23.0f, kTargetMax = -9.0f, kTargetDef = -14.0f, kTargetStep = 0.5f;
constexpr float kCeilMin = -6.0f, kCeilMax = -0.1f, kCeilDef = -1.0f, kCeilStep = 0.1f;
constexpr float kNoReading = -99.0f;          // readout floor: shown as "--.-"
constexpr double kClipRiskHoldS = 3.0;
constexpr double kLimitedGrDb = 1.0;          // AUTO says LIMITED above this average gain reduction
const char* const kAutoStateNames[] = { "AUTO OFF", "AUTO LEARNING", "AUTO ADJUSTING", "AUTO IN RANGE",
                                        "AUTO HOLD (SILENCE)", "AUTO GAIN LIMIT", "AUTO LIMITED", "CLIP RISK" };
constexpr int kClipRiskState = 7;

} // namespace

class SlowOpsVUMeter : public mpc::Plugin
{
public:
    // Permanent parameter indices. Never reorder; append new parameters at the end.
    enum : int
    {
        kReserved = 0,   // the MPC nudges index 0 on every prepare, so it does nothing
        kMode,           // retired (was Stereo / Mono); always stereo
        kRef,            // -20 / -18 / -14 / -10 / -4 dBFS
        kPeak,           // Off / On (2 s hold)
        kLight,          // Warm / Cool / Off / React / Cycle (cosmetic). React colours the dial by program
                         // heat; Cycle travels round the hue wheel once every two bars, locked to the tempo
        kBypass,
        kTrim,           // -12..+12 dB
        kResetClip,      // one-shot trigger (tap a CLIP lamp)
        // ---- read-only displays ----
        kView,           // mode*5 + light: selects the face artwork
        kClipL,
        kClipR,
        kClipIn,         // retired (MONO input clip)
        kNeedleL0,                                   // L sectors
        kNeedleR0 = kNeedleL0 + kStereoSectors,      // R sectors
        kNeedleM0 = kNeedleR0 + kStereoSectors,      // retired mono sectors
        kGlow = kNeedleM0 + kMonoSectors,            // React/Cycle glow frame 0..kGlowFrames-1
        // ---- v0.2 (appended) ----
        kAutoTrim,       // retired in v0.3 (was the one-shot 0 VU "AUTO" trim)
        kAutoBusy,       // retired in v0.3
        kCorr,           // display: phase correlation frame (0 = no signal, 1..21 = -1..+1)
        kMaxL,           // display: highest VU since the last reset (lamp tap)
        kMaxR,
        kMaxM,           // retired (MONO max)
        kClipThr,        // CLIP threshold 0 / -0.3 / -1 / -3 dBFS
        kCycleBars,      // Cycle light: 1 / 2 / 4 / 8 bars per trip round the hue wheel
        kPalette,        // React light colours: Thermal / Neon / Amber
        // ---- v0.3 (appended): LUFS tab ----
        kLufsAuto,       // Off / On: steer integrated loudness towards Target
        kTarget,         // -23..-9 LUFS in 0.5 steps
        kLimiter,        // Off / On: true-peak limiter (adds latency only while on)
        kCeiling,        // -6.0..-0.1 dBTP in 0.1 steps
        kMeasure,        // Run / Hold: Hold freezes Integrated, True Peak Max and elapsed time
        kLufsClear,      // one-shot triggers (buttons)
        kTargetDown,
        kTargetUp,
        kCeilDown,
        kCeilUp,
        kIntegrated,     // ---- read-only displays ----
        kShortTerm,
        kMomentary,
        kTpMax,
        kGainRed,        // limiter gain reduction, dB >= 0
        kAutoState,
        kAutoGain,
        kElapsed,        // seconds since CLEAR while running
        kNumParams
    };

    SlowOpsVUMeter() : mpc::Plugin(makeInfo())
    {
        mpc::Param reserved = mpc::floatParam("reserved", "Reserved", 0.0f, 1.0f, 0.0f);
        reserved.automatable = false;
        addParam(reserved);
        mpc::Param mode = mpc::choiceParam("mode", "Unused", { "Stereo", "Mono" }, 0);   // retired slot
        mode.automatable = false;
        addParam(mode);
        addParam(mpc::choiceParam("ref", "0 VU", { kRefNames[0], kRefNames[1], kRefNames[2], kRefNames[3], kRefNames[4] }, 1));
        addParam(mpc::choiceParam("peak", "Peak Hold", { "Off", "On" }, 1));
        addParam(mpc::choiceParam("light", "Light", { kLightNames[0], kLightNames[1], kLightNames[2], kLightNames[3], kLightNames[4] }, 0));
        addParam(mpc::boolParam("bypass", "Bypass", false));
        addParam(mpc::floatParam("trim", "Trim", -12.0f, 12.0f, 0.0f, "", 1));
        mpc::Param reset = mpc::boolParam("resetclip", "Reset Clip", false);
        reset.automatable = false;
        addParam(reset);
        addDisplay(mpc::choiceParam("view", "View", { "Stereo Warm", "Stereo Cool", "Stereo Off", "Stereo React", "Stereo Cycle",
                                                      "Mono Warm", "Mono Cool", "Mono Off", "Mono React", "Mono Cycle" }, 0));
        addDisplay(mpc::boolParam("clipL", "L Clip", false));
        addDisplay(mpc::boolParam("clipR", "R Clip", false));
        addDisplay(mpc::boolParam("clipIn", "Input Clip", false));
        addSectors("needleL", "L Needle", kStereoSectors, kStereoPer);
        addSectors("needleR", "R Needle", kStereoSectors, kStereoPer);
        addSectors("needleM", "M Needle", kMonoSectors, kMonoPer);
        addDisplay(mpc::intParam("glow", "Glow", 0, kGlowFrames - 1, 0));
        addDisplay(mpc::boolParam("autotrim", "Unused 2", false));     // retired slot
        addDisplay(mpc::boolParam("autobusy", "Unused 3", false));     // retired slot
        addDisplay(mpc::intParam("corr", "Correlation", 0, kCorrSteps, 0));
        addDisplay(mpc::floatParam("maxL", "L Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addDisplay(mpc::floatParam("maxR", "R Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addDisplay(mpc::floatParam("maxM", "M Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addParam(mpc::choiceParam("clipthr", "Clip Level", { "0 dBFS", "-0.3 dBFS", "-1 dBFS", "-3 dBFS" }, 0));
        addParam(mpc::choiceParam("cyclebars", "Cycle Rate", { "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 1));
        addParam(mpc::choiceParam("palette", "Palette", { "Thermal", "Neon", "Amber" }, 0));
        addParam(mpc::choiceParam("lufsauto", "LUFS Auto", { "Off", "On" }, 0));
        addParam(mpc::floatParam("target", "Target", kTargetMin, kTargetMax, kTargetDef, "", 1));
        addParam(mpc::choiceParam("limiter", "Limiter", { "Off", "On" }, 0));
        addParam(mpc::floatParam("ceiling", "Ceiling", kCeilMin, kCeilMax, kCeilDef, "", 1));
        addParam(mpc::choiceParam("measure", "Measure", { "Run", "Hold" }, 0));
        addTrigger("lufsclear", "Clear");
        addTrigger("targetdn", "Target Down");
        addTrigger("targetup", "Target Up");
        addTrigger("ceildn", "Ceiling Down");
        addTrigger("ceilup", "Ceiling Up");
        addDisplay(mpc::floatParam("integrated", "Integrated", kNoReading, 10.0f, kNoReading, "", 1));
        addDisplay(mpc::floatParam("shortterm", "Short-term", kNoReading, 10.0f, kNoReading, "", 1));
        addDisplay(mpc::floatParam("momentary", "Momentary", kNoReading, 10.0f, kNoReading, "", 1));
        addDisplay(mpc::floatParam("tpmax", "True Peak Max", kNoReading, 20.0f, kNoReading, "", 1));
        addDisplay(mpc::floatParam("gr", "Gain Reduction", 0.0f, 40.0f, 0.0f, "", 1));
        addDisplay(mpc::intParam("autostate", "Auto State", 0, 7, 0));
        addDisplay(mpc::floatParam("autogain", "Auto Gain", -12.0f, 12.0f, 0.0f, "", 1));
        addDisplay(mpc::floatParam("elapsed", "Elapsed", 0.0f, 359999.0f, 0.0f, "", 0));
        if (numParams() != kNumParams) mpc::log("SlowOpsVUMeter: parameter count mismatch");
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "LUFS VU";              // renamed from "VU Meter" in v0.3 (uid and parameter ids unchanged)
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'V', 'M');
        info.version = 300;            // 0.3.0
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 1;          // no audio tail
        info.latencySamples = 0;       // LIMITER off by default (see updateLatency)
        return info;
    }

    void prepare(double sampleRate, int) override
    {
        engine_.prepare(sampleRate);
        engine_.setTrimDb(0.0f, true);      // the VU engine only meters; gain is applied below
        gain_.prepare(sampleRate);
        gain_.snap();
        limiter_.prepare(sampleRate);
        auto_.prepare(sampleRate);
        outMeter_.prepare(sampleRate);
        tpL_.prepare(sampleRate);
        tpR_.prepare(sampleRate);
        heat_.setTimes(0.12, 0.9);     // warms in ~120 ms, cools in ~0.9 s: a glowing filament
        displayEvery_ = (int)(sampleRate / 30.0);
        if (displayEvery_ < 1) displayEvery_ = 1;
        updateLatency(getBool(kLimiter));
    }

    // Runs on resume and after every state load: the meter movements, clip latches and
    // loudness statistics restart from rest, and a trigger saved in the "on" position is
    // cleared without firing. Measurements are never restored from a project, only settings.
    // Needle/clip displays are deliberately not written here: any stale value from a saved
    // project is replaced by the live reading on the first UI tick (<= 33 ms), and a view that
    // becomes visible is published immediately, so saved needle/clip values never act as state.
    void reset() override
    {
        applyControls();
        engine_.reset();
        gain_.snap();
        limiter_.reset();
        auto_.reset();
        outMeter_.reset();
        tpL_.reset(); tpR_.reset();
        tpMax_ = 0.0;
        grAvg_ = 0.0;
        clipRiskLeft_ = 0;
        heat_.reset();
        hue_.reset();
        flashLeft_ = 0;
        lastHits_ = engine_.clipHits();
        displayAge_ = 0;
        tick_ = 0;
        for (int t : { (int)kResetClip, (int)kAutoTrim, (int)kLufsClear, (int)kTargetDown, (int)kTargetUp, (int)kCeilDown, (int)kCeilUp })
            if (getNormalised(t) >= 0.5f) setValue(t, 0.0f);
        publishView();
    }

    // LIMITER on/off changes the plugin's latency. Called from the host's thread (parameter
    // change, state load, prepare), never from process().
    void parameterChanged(int index, float plain) override
    {
        if (index == kLimiter) updateLatency(plain >= 0.5f);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* outL = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const bool monoIn = !(ctx.numInputs > 1 && ctx.inputs[1] != nullptr);
        const float* inR = monoIn ? inL : ctx.inputs[1];
        if (inL == nullptr || n <= 0) return;   // nothing to meter; outputs were cleared by the SDK

        handleTriggers();
        applyControls();
        auto_.setMonoInput(monoIn);
        outMeter_.setMonoInput(monoIn);
        publishView();

        const bool limiterOn = getBool(kLimiter);
        const bool running = getInt(kMeasure) == 0;
        float bl[kChunk], br[kChunk];
        for (int done = 0; done < n; )
        {
            const int m = n - done < kChunk ? n - done : kChunk;
            for (int i = 0; i < m; ++i)
            {
                const float xl = inL[done + i], xr = inR[done + i];   // read input before any output write
                auto_.feed(std::isfinite(xl) ? xl : 0.0f, std::isfinite(xr) ? xr : 0.0f);
                const float g = gain_.next();
                float yl = xl * g, yr = xr * g;                       // x * 1.0f == x: bit-exact at unity
                limiter_.process(yl, yr);
                bl[i] = yl; br[i] = yr;
            }
            for (int i = 0; i < m; ++i)
            {
                if (outL != nullptr) outL[done + i] = bl[i];
                if (outR != nullptr) outR[done + i] = br[i];
                outMeter_.process(bl[i], br[i]);
                const double pk = std::fmax(tpL_.process(bl[i]), monoIn ? 0.0 : tpR_.process(br[i]));
                if (running && pk > tpMax_) tpMax_ = pk;
                if (!limiterOn && pk > 1.0) clipRiskLeft_ = (int64_t)(kClipRiskHoldS * sampleRate());
            }
            engine_.process(bl, br, bl, br, m);                       // VU needles, clips, phase (meter only)
            done += m;
        }
        if (clipRiskLeft_ > 0) clipRiskLeft_ -= n;

        // AUTO: limiter pressure, then move the gain (applied from the next block on)
        grAvg_ += (limiter_.gainReductionDb() - grAvg_) * std::fmin(1.0, (double)n / (3.0 * sampleRate()));
        auto_.setLimited(limiterOn && grAvg_ > kLimitedGrDb);
        auto_.advance(n);
        updateGainTarget();

        tempo_.tempoValid = ctx.transport.valid && ctx.transport.tempoValid;
        tempo_.bpm = ctx.transport.bpm;
        tempo_.ppqValid = ctx.transport.valid && ctx.transport.ppqValid;
        tempo_.ppq = ctx.transport.ppqPos;
        tempo_.timeSigNumerator = ctx.transport.timeSigNumerator;
        tempo_.timeSigDenominator = ctx.transport.timeSigDenominator;
        hue_.advance(tempo_, n, sampleRate());

        // Clip flash (React/Cycle): any new clipped sample lights the dials white for one beat.
        const uint64_t hits = engine_.clipHits();
        if (hits != lastHits_)
        {
            lastHits_ = hits;
            const double beat = tempo_.tempoValid && tempo_.bpm > 1.0 ? 60.0 / tempo_.bpm : 0.25;
            flashLeft_ = (int64_t)(beat * sampleRate());
        }
        else if (flashLeft_ > 0)
        {
            flashLeft_ -= n;
        }

        displayAge_ += n;
        if (displayAge_ >= displayEvery_)
        {
            const double dt = (double)displayAge_ / sampleRate();
            displayAge_ = 0;
            publishDisplays(dt);
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        if (index == kGlow)
        {
            const int f = (int)std::lround(plain);
            char buf[32];
            if (f < kHueFirst)        std::snprintf(buf, sizeof(buf), "heat %d%%", (int)std::lround((f % kHeatFrames) * 100.0 / (kHeatFrames - 1)));
            else if (f < kFlashFrame) std::snprintf(buf, sizeof(buf), "hue %d", (f - kHueFirst) * 360 / kHueFrames);
            else                      std::snprintf(buf, sizeof(buf), "flash");
            out = buf; return true;
        }
        if (index == kMaxL || index == kMaxR || index == kMaxM)
        {
            char buf[32];
            if (plain <= kMaxFloorVU + 0.05f) std::snprintf(buf, sizeof(buf), "MAX --");
            else                              std::snprintf(buf, sizeof(buf), "MAX %+.1f VU", (double)plain);
            out = buf; return true;
        }
        if (index == kCorr)
        {
            const int f = (int)std::lround(plain);
            char buf[32];
            if (f <= 0) std::snprintf(buf, sizeof(buf), "--");
            else        std::snprintf(buf, sizeof(buf), "%+.1f", (f - 1) * 2.0 / (kCorrSteps - 1) - 1.0);
            out = buf; return true;
        }
        if (index >= kNeedleL0 && index < kGlow)
        {
            const bool mono = index >= kNeedleM0;
            const int per = mono ? kMonoPer : kStereoPer, frames = mono ? kMonoFrames : kStereoFrames;
            const int sector = mono ? index - kNeedleM0 : (index - kNeedleL0) % kStereoSectors;
            const int local = (int)std::lround(plain);
            if (local <= 0) { out = "-"; return true; }
            const int frame = sector * per + local - 1;
            const double y = (double)frame / (double)(frames - 1) * fullScaleDeflection();
            if (y <= 0.0) { out = "rest"; return true; }
            char buf[32]; std::snprintf(buf, sizeof(buf), "%+.1f VU", deflectionToVU(y));
            out = buf; return true;
        }
        if (index == kTarget || index == kCeiling)
        {
            char buf[32];
            if (index == kTarget) std::snprintf(buf, sizeof(buf), "%.1f LUFS", (double)snapTarget(plain));
            else                  std::snprintf(buf, sizeof(buf), "%.1f dBTP", (double)snapCeiling(plain));
            out = buf; return true;
        }
        if (index == kIntegrated || index == kShortTerm || index == kMomentary || index == kTpMax)
        {
            char buf[32];
            if (plain <= kNoReading + 0.05f) std::snprintf(buf, sizeof(buf), "--.-");
            else                             std::snprintf(buf, sizeof(buf), "%.1f", (double)plain);
            if (index == kMomentary) { out = std::string("M ") + buf; return true; }
            out = buf; return true;
        }
        if (index == kGainRed)
        {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.1f", (double)std::fabs(plain));
            out = buf; return true;
        }
        if (index == kAutoState)
        {
            const int s = (int)std::lround(plain);
            out = kAutoStateNames[s < 0 ? 0 : (s > kClipRiskState ? kClipRiskState : s)];
            return true;
        }
        if (index == kAutoGain)
        {
            char buf[32];
            if (std::fabs(plain) < 0.05f) std::snprintf(buf, sizeof(buf), "0.0 dB");
            else                          std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)plain);
            out = buf; return true;
        }
        if (index == kElapsed)
        {
            const long t = std::lround(plain < 0.0f ? 0.0f : plain);
            char buf[32];
            if (t >= 3600) std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
            else           std::snprintf(buf, sizeof(buf), "%02ld:%02ld", t / 60, t % 60);
            out = buf; return true;
        }
        if (index == kTrim)
        {
            const float db = snapTrim(plain);
            char buf[32];
            if (db == 0.0f) std::snprintf(buf, sizeof(buf), "0.0 dB");
            else            std::snprintf(buf, sizeof(buf), "%+.1f dB", (double)db);
            out = buf; return true;
        }
        return false;
    }

private:
    static constexpr int kChunk = 256;

    void addDisplay(mpc::Param p) { p.automatable = false; addParam(p); }
    void addTrigger(const char* id, const char* name)
    {
        mpc::Param p = mpc::boolParam(id, name, false);
        p.automatable = false;
        addParam(p);
    }

    void addSectors(const char* id, const char* name, int sectors, int per)
    {
        for (int s = 0; s < sectors; ++s)
        {
            const std::string sid = std::string(id) + std::to_string(s + 1);
            const std::string sname = std::string(name) + " " + std::to_string(s + 1);
            addDisplay(mpc::intParam(sid.c_str(), sname.c_str(), 0, per, s == 0 ? 1 : 0));   // rest = sector 1, frame 1
        }
    }

    // Change-only write that also tells the host (which is what redraws the MPC screen).
    void setValue(int i, float norm)
    {
        if (getNormalised(i) == norm) return;
        setNormalised(i, norm);
        notifyHostParameterChanged(i);
    }

    static float snapTrim(float db) { return std::round(db * 10.0f) / 10.0f; }
    static float snapTarget(float v) { const float s = std::round(v / kTargetStep) * kTargetStep; return s < kTargetMin ? kTargetMin : (s > kTargetMax ? kTargetMax : s); }
    static float snapCeiling(float v) { const float s = std::round(v * 10.0f) / 10.0f; return s < kCeilMin ? kCeilMin : (s > kCeilMax ? kCeilMax : s); }

    bool takeTrigger(int i)
    {
        if (getNormalised(i) < 0.5f) return false;
        setValue(i, 0.0f);
        return true;
    }

    void handleTriggers()
    {
        if (takeTrigger(kResetClip)) { engine_.resetClips(); engine_.clearMax(); }
        takeTrigger(kAutoTrim);                                   // retired: ignored
        if (takeTrigger(kLufsClear))
        {
            outMeter_.clear();
            auto_.clear();
            tpMax_ = 0.0;
            clipRiskLeft_ = 0;
        }
        if (takeTrigger(kTargetDown)) setValue(kTarget, param(kTarget).toNormalised(snapTarget(snapTarget(get(kTarget)) - kTargetStep)));
        if (takeTrigger(kTargetUp))   setValue(kTarget, param(kTarget).toNormalised(snapTarget(snapTarget(get(kTarget)) + kTargetStep)));
        if (takeTrigger(kCeilDown))   setValue(kCeiling, param(kCeiling).toNormalised(snapCeiling(snapCeiling(get(kCeiling)) - kCeilStep)));
        if (takeTrigger(kCeilUp))     setValue(kCeiling, param(kCeiling).toNormalised(snapCeiling(snapCeiling(get(kCeiling)) + kCeilStep)));
    }

    void applyControls()
    {
        engine_.setReferenceDbfs(kRefDbfs[getInt(kRef)]);
        engine_.setPeakHold(getInt(kPeak) == 1);
        engine_.setClipThresholdDbfs(kClipThrDbfs[getInt(kClipThr)]);
        hue_.setBars(kCycleBarValues[getInt(kCycleBars)]);
        const bool bypass = getBool(kBypass);
        limiter_.setCeilingDb(snapCeiling(get(kCeiling)));
        limiter_.setEnabled(getBool(kLimiter));
        limiter_.setBypass(bypass);
        auto_.setEnabled(getBool(kLufsAuto));
        auto_.setTargetLufs(snapTarget(get(kTarget)));
        outMeter_.setRunning(getInt(kMeasure) == 0);
        updateGainTarget();
    }

    // One gain for L and R: AUTO + TRIM, or exactly unity on Bypass (then bit-exact).
    void updateGainTarget()
    {
        if (getBool(kBypass)) { gain_.setTargetGain(1.0f); return; }
        const double db = auto_.gainDb() + (double)snapTrim(get(kTrim));
        gain_.setTargetGain(db == 0.0 ? 1.0f : (float)std::pow(10.0, db / 20.0));
    }

    void updateLatency(bool limiterOn)
    {
        const int lat = limiterOn ? limiter_.latencySamples() : 0;
        if (lat != info().latencySamples) setLatency(lat);
    }

    // The louder of the two needles (drives React heat).
    double visibleDeflection() const { return std::fmax(engine_.deflection(kLeft), engine_.deflection(kRight)); }

    void publishMax(int index, int meter)
    {
        double vu = deflectionToVU(engine_.maxDeflection(meter));
        if (vu < kMaxFloorVU) vu = kMaxFloorVU;
        if (vu > 10.0) vu = 10.0;
        setValue(index, param(index).toNormalised(std::round((float)vu * 10.0f) / 10.0f));
    }

    // View = light (the stereo half of the original Mode x Light choices).
    void publishView() { setValue(kView, (float)getInt(kLight) / (float)(2 * kNumLights - 1)); }

    // Show the needle in the sector that holds `frame`, then blank the others (show-before-hide,
    // so a sector change never leaves an empty dial for a redraw).
    void publishNeedle(int first, int sectors, int per, int frame)
    {
        const int s = frame / per < sectors ? frame / per : sectors - 1;
        setValue(first + s, (float)(frame - s * per + 1) / (float)per);
        for (int k = 0; k < sectors; ++k) if (k != s) setValue(first + k, 0.0f);
    }

    void publishDisplays(double dt)
    {
        // React heat follows the loudest needle of the visible view; it runs whatever the
        // Light setting, so switching to React shows the true current colour at once.
        const double heat = heat_.update(deflectionToVU(visibleDeflection()), dt);
        const int light = getInt(kLight);
        int glow = -1;
        if (light == kLightReact) glow = getInt(kPalette) * kHeatFrames + (int)std::lround(heat * (kHeatFrames - 1));
        if (light == kLightCycle) glow = kHueFirst + (int)std::lround(hue_.phase() * kHueFrames) % kHueFrames;
        if (glow >= 0 && flashLeft_ > 0) glow = kFlashFrame;
        if (glow >= 0) setValue(kGlow, (float)glow / (float)(kGlowFrames - 1));

        setValue(kGainRed, param(kGainRed).toNormalised(round1(limiter_.takePeakGainReductionDb())));
        if (tick_++ % 3 == 0) publishLoudness();     // numeric readouts at 10 Hz
        const Correlation& c = engine_.correlation();
        const int corr = c.hasSignal() ? 1 + (int)std::lround((c.value() + 1.0) * 0.5 * (kCorrSteps - 1)) : 0;
        setValue(kCorr, (float)corr / (float)kCorrSteps);

        const bool clipL = engine_.consumeClip(kLeft);
        const bool clipR = engine_.consumeClip(kRight);
        publishNeedle(kNeedleL0, kStereoSectors, kStereoPer, deflectionToFrame(engine_.deflection(kLeft), kStereoFrames));
        publishNeedle(kNeedleR0, kStereoSectors, kStereoPer, deflectionToFrame(engine_.deflection(kRight), kStereoFrames));
        setValue(kClipL, clipL ? 1.0f : 0.0f);
        setValue(kClipR, clipR ? 1.0f : 0.0f);
        publishMax(kMaxL, kLeft);
        publishMax(kMaxR, kRight);
    }

    static float round1(double v) { return std::isfinite(v) ? std::round((float)v * 10.0f) / 10.0f : kNoReading; }

    void publishReading(int index, double lufs)
    {
        float v = std::isfinite(lufs) ? round1(lufs) : kNoReading;
        const mpc::Param& p = param(index);
        if (v < p.min) v = p.min;
        if (v > p.max) v = p.max;
        setValue(index, p.toNormalised(v));
    }

    void publishLoudness()
    {
        publishReading(kIntegrated, outMeter_.integrated());
        publishReading(kShortTerm, outMeter_.shortTerm());
        publishReading(kMomentary, outMeter_.momentary());
        publishReading(kTpMax, tpMax_ > 0.0 ? 20.0 * std::log10(tpMax_) : LoudnessMeter::noValue());
        const int state = clipRiskLeft_ > 0 ? kClipRiskState : (int)auto_.state();
        setValue(kAutoState, (float)state / (float)kClipRiskState);
        float ag = round1(auto_.gainDb());
        setValue(kAutoGain, param(kAutoGain).toNormalised(ag));
        setValue(kElapsed, param(kElapsed).toNormalised(std::floor((float)outMeter_.elapsedSeconds())));
    }

    VuEngine engine_;
    HeatFollower heat_;
    HueCycle hue_;
    TempoInfo tempo_;
    Trim gain_;                    // AUTO + TRIM, smoothed over 20 ms
    TruePeakLimiter limiter_;
    AutoLoudness auto_;
    LoudnessMeter outMeter_;
    TruePeakDetector tpL_, tpR_;
    double tpMax_ = 0.0;           // linear, since CLEAR (frozen while HOLD)
    double grAvg_ = 0.0;
    int64_t clipRiskLeft_ = 0;
    unsigned tick_ = 0;
    uint64_t lastHits_ = 0;
    int64_t flashLeft_ = 0;
    int displayAge_ = 0;
    int displayEvery_ = 1470;
};

MPC_PLUGIN_ENTRY(SlowOpsVUMeter)
