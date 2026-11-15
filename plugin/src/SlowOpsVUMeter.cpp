// Slow Ops VU Meter: thin mpc::Plugin adapter around slowops::vumeter::VuEngine.
//
// Audio: in -> output trim (smoothed; bit-exact copy at 0.0 dB or Bypass) -> out.
// Meters: L and R needles on the output signal (stereo only; the v0.1 MONO view was retired in
// v0.2 -- its parameter slots stay so saved projects and indices remain valid, but are unused).
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
#include "../../core/include/slowops/vumeter/AutoTrim.hpp"

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

constexpr double kAutoTrimSeconds = 3.0;
constexpr int kCorrSteps = 21;                // correlation -1..+1 in 21 steps (frame 0 = no signal)
constexpr float kMaxFloorVU = -40.0f;        // "cleared" max reading

static_assert(kStereoFrames % kStereoSectors == 0 && kMonoFrames % kMonoSectors == 0, "sectors must divide frames");

const char* const kRefNames[] = { "-20 dBFS", "-18 dBFS", "-14 dBFS" };
constexpr double kRefDbfs[]   = { -20.0, -18.0, -14.0 };
const char* const kLightNames[] = { "Warm", "Cool", "Off", "React", "Cycle" };
constexpr int kNumLights = 5;
constexpr int kLightReact = 3;
constexpr int kLightCycle = 4;
constexpr double kClipThrDbfs[] = { 0.0, -0.3, -1.0, -3.0 };
constexpr double kCycleBarValues[] = { 1.0, 2.0, 4.0, 8.0 };

} // namespace

class SlowOpsVUMeter : public mpc::Plugin
{
public:
    // Permanent parameter indices. Never reorder; append new parameters at the end.
    enum : int
    {
        kReserved = 0,   // the MPC nudges index 0 on every prepare, so it does nothing
        kMode,           // retired (was Stereo / Mono); always stereo
        kRef,            // -20 / -18 / -14 dBFS
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
        kAutoTrim,       // one-shot trigger: listen 3 s, then set Trim so the program averages 0 VU
        kAutoBusy,       // display: AUTO is listening
        kCorr,           // display: phase correlation frame (0 = no signal, 1..21 = -1..+1)
        kMaxL,           // display: highest VU since the last reset (lamp tap)
        kMaxR,
        kMaxM,           // retired (MONO max)
        kClipThr,        // CLIP threshold 0 / -0.3 / -1 / -3 dBFS
        kCycleBars,      // Cycle light: 1 / 2 / 4 / 8 bars per trip round the hue wheel
        kPalette,        // React light colours: Thermal / Neon / Amber
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
        addParam(mpc::choiceParam("ref", "0 VU", { kRefNames[0], kRefNames[1], kRefNames[2] }, 1));
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
        mpc::Param autoTrim = mpc::boolParam("autotrim", "Auto Trim", false);
        autoTrim.automatable = false;
        addParam(autoTrim);
        addDisplay(mpc::boolParam("autobusy", "Auto Busy", false));
        addDisplay(mpc::intParam("corr", "Correlation", 0, kCorrSteps, 0));
        addDisplay(mpc::floatParam("maxL", "L Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addDisplay(mpc::floatParam("maxR", "R Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addDisplay(mpc::floatParam("maxM", "M Max", kMaxFloorVU, 10.0f, kMaxFloorVU, "", 1));
        addParam(mpc::choiceParam("clipthr", "Clip Level", { "0 dBFS", "-0.3 dBFS", "-1 dBFS", "-3 dBFS" }, 0));
        addParam(mpc::choiceParam("cyclebars", "Cycle Rate", { "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 1));
        addParam(mpc::choiceParam("palette", "Palette", { "Thermal", "Neon", "Amber" }, 0));
        if (numParams() != kNumParams) mpc::log("SlowOpsVUMeter: parameter count mismatch");
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "VU Meter";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'V', 'M');
        info.version = 200;            // 0.2.0
        info.isInstrument = false;
        info.numInputs = 2;
        info.numOutputs = 2;
        info.tailSamples = 1;          // no audio tail
        return info;
    }

    void prepare(double sampleRate, int) override
    {
        engine_.prepare(sampleRate);
        heat_.setTimes(0.12, 0.9);     // warms in ~120 ms, cools in ~0.9 s: a glowing filament
        displayEvery_ = (int)(sampleRate / 30.0);
        if (displayEvery_ < 1) displayEvery_ = 1;
    }

    // Runs on resume and after every state load: the meter movements and clip latches restart
    // from rest, and a trigger saved in the "on" position is cleared without firing.
    // Needle/clip displays are deliberately not written here: any stale value from a saved
    // project is replaced by the live reading on the first UI tick (<= 33 ms), and a view that
    // becomes visible is published immediately, so saved needle/clip values never act as state.
    void reset() override
    {
        applyControls();
        engine_.reset();
        heat_.reset();
        hue_.reset();
        autoTrim_.cancel();
        flashLeft_ = 0;
        lastHits_ = engine_.clipHits();
        displayAge_ = 0;
        if (getNormalised(kResetClip) >= 0.5f) setValue(kResetClip, 0.0f);
        if (getNormalised(kAutoTrim) >= 0.5f) setValue(kAutoTrim, 0.0f);
        publishView();
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int n = ctx.numFrames;
        float* outL = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float* inL = ctx.numInputs > 0 ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.numInputs > 1 && ctx.inputs[1] != nullptr ? ctx.inputs[1] : inL;
        if (inL == nullptr || n <= 0) return;   // nothing to meter; outputs were cleared by the SDK

        if (getNormalised(kResetClip) >= 0.5f)
        {
            setValue(kResetClip, 0.0f);
            engine_.resetClips();
            engine_.clearMax();
        }
        if (getNormalised(kAutoTrim) >= 0.5f)       // tap AUTO: start listening (tap again: cancel)
        {
            setValue(kAutoTrim, 0.0f);
            if (autoTrim_.active()) autoTrim_.cancel();
            else autoTrim_.start(kAutoTrimSeconds, sampleRate());
        }
        applyControls();
        publishView();
        engine_.process(inL, inR, outL, outR, n);

        tempo_.tempoValid = ctx.transport.valid && ctx.transport.tempoValid;
        tempo_.bpm = ctx.transport.bpm;
        tempo_.ppqValid = ctx.transport.valid && ctx.transport.ppqValid;
        tempo_.ppq = ctx.transport.ppqPos;
        tempo_.timeSigNumerator = ctx.transport.timeSigNumerator;
        tempo_.timeSigDenominator = ctx.transport.timeSigDenominator;
        hue_.advance(tempo_, n, sampleRate());

        if (autoTrim_.active())
        {
            autoTrim_.feed(visibleDeflection(), n);
            double correction = 0.0;
            if (autoTrim_.takeResult(correction) && autoTrim_.heardSignal())
            {
                // Meters read the trimmed output, so the correction adds to the current trim
                // (with Bypass on the meters read the raw input: start from 0 dB).
                const float base = getBool(kBypass) ? 0.0f : snapTrim(get(kTrim));
                float trim = snapTrim(base + (float)correction);
                trim = trim < -12.0f ? -12.0f : (trim > 12.0f ? 12.0f : trim);
                setValue(kTrim, param(kTrim).toNormalised(trim));
                applyControls();
            }
        }

        // Clip flash (React/Cycle): any new clipped sample lights the cabinet white for one beat.
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
    void addDisplay(mpc::Param p) { p.automatable = false; addParam(p); }

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

    void applyControls()
    {
        engine_.setReferenceDbfs(kRefDbfs[getInt(kRef)]);
        engine_.setPeakHold(getInt(kPeak) == 1);
        engine_.setTrimDb(snapTrim(get(kTrim)), getBool(kBypass));
        engine_.setClipThresholdDbfs(kClipThrDbfs[getInt(kClipThr)]);
        hue_.setBars(kCycleBarValues[getInt(kCycleBars)]);
    }

    // The louder of the two needles (drives React heat and AUTO trim).
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

        setValue(kAutoBusy, autoTrim_.active() ? 1.0f : 0.0f);
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

    VuEngine engine_;
    HeatFollower heat_;
    HueCycle hue_;
    TempoInfo tempo_;
    AutoTrim autoTrim_;
    uint64_t lastHits_ = 0;
    int64_t flashLeft_ = 0;
    int displayAge_ = 0;
    int displayEvery_ = 1470;
};

MPC_PLUGIN_ENTRY(SlowOpsVUMeter)
