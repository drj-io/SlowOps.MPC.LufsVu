// Slow Ops VU Meter: thin mpc::Plugin adapter around slowops::vumeter::VuEngine.
//
// Audio: in -> output trim (smoothed; bit-exact copy at 0.0 dB or Bypass) -> out.
// Meters: L, R and the true mono sum M = 0.5*(L+R) run continuously on the output signal.
//
// Display: each needle is drawn by several "sector" filmstrips, each covering a slice of the
// sweep (a small transparent sprite instead of one face-sized sprite, which keeps decoded
// image memory low). Frame 0 of every sector strip is blank; exactly one sector of a needle
// shows a needle at any time. Only the visible view's displays are published, at most 30
// times per second and only when a value actually changes.
#include "mpc_plugin.h"
#include "../../core/include/slowops/vumeter/VuEngine.hpp"
#include "../../core/include/slowops/vumeter/Heat.hpp"
#include "../../core/include/slowops/vumeter/HueCycle.hpp"

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
// Glow filmstrip (React and Cycle lighting share one strip): frames 0..23 are React heat
// steps (blue -> red -> purple), frames 24..59 are 36 hue steps (10 degrees each) for Cycle.
constexpr int kHeatFrames = 24;
constexpr int kHueFrames = 36;
constexpr int kGlowFrames = kHeatFrames + kHueFrames;

static_assert(kStereoFrames % kStereoSectors == 0 && kMonoFrames % kMonoSectors == 0, "sectors must divide frames");

const char* const kRefNames[] = { "-20 dBFS", "-18 dBFS", "-14 dBFS" };
constexpr double kRefDbfs[]   = { -20.0, -18.0, -14.0 };
const char* const kLightNames[] = { "Warm", "Cool", "Off", "React", "Cycle" };
constexpr int kNumLights = 5;
constexpr int kLightReact = 3;
constexpr int kLightCycle = 4;

} // namespace

class SlowOpsVUMeter : public mpc::Plugin
{
public:
    // Permanent parameter indices. Never reorder; append new parameters at the end.
    enum : int
    {
        kReserved = 0,   // the MPC nudges index 0 on every prepare, so it does nothing
        kMode,           // Stereo / Mono
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
        kClipIn,         // L or R clipped (shown in MONO)
        kNeedleL0,                                   // L sectors
        kNeedleR0 = kNeedleL0 + kStereoSectors,      // R sectors
        kNeedleM0 = kNeedleR0 + kStereoSectors,      // mono sectors
        kGlow = kNeedleM0 + kMonoSectors,            // React/Cycle glow frame 0..kGlowFrames-1
        kNumParams
    };

    SlowOpsVUMeter() : mpc::Plugin(makeInfo())
    {
        mpc::Param reserved = mpc::floatParam("reserved", "Reserved", 0.0f, 1.0f, 0.0f);
        reserved.automatable = false;
        addParam(reserved);
        addParam(mpc::choiceParam("mode", "Mode", { "Stereo", "Mono" }, 0));
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
        if (numParams() != kNumParams) mpc::log("SlowOpsVUMeter: parameter count mismatch");
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "VU Meter";
        info.vendor = "Slow Ops";
        info.uniqueId = MPC_FOURCC('S', 'o', 'V', 'M');
        info.version = 100;            // 0.1.0
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
        displayAge_ = 0;
        lastMode_ = getInt(kMode);   // a loaded mode is not a view switch; the next tick refreshes it
        if (getNormalised(kResetClip) >= 0.5f) setValue(kResetClip, 0.0f);
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

        displayAge_ += n;
        const int mode = getInt(kMode);
        if (displayAge_ >= displayEvery_ || mode != lastMode_)   // a newly shown view updates at once
        {
            lastMode_ = mode;
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
            if (f < kHeatFrames) std::snprintf(buf, sizeof(buf), "heat %d%%", (int)std::lround(f * 100.0 / (kHeatFrames - 1)));
            else                 std::snprintf(buf, sizeof(buf), "hue %d", (f - kHeatFrames) * 360 / kHueFrames);
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
    }

    void publishView() { setValue(kView, (float)(getInt(kMode) * kNumLights + getInt(kLight)) / (float)(2 * kNumLights - 1)); }

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
        const double vu = getInt(kMode) == 0
            ? deflectionToVU(std::fmax(engine_.deflection(kLeft), engine_.deflection(kRight)))
            : deflectionToVU(engine_.deflection(kMono));
        const double heat = heat_.update(vu, dt);
        const int light = getInt(kLight);
        int glow = -1;
        if (light == kLightReact) glow = (int)std::lround(heat * (kHeatFrames - 1));
        if (light == kLightCycle) glow = kHeatFrames + (int)std::lround(hue_.phase() * kHueFrames) % kHueFrames;
        if (glow >= 0) setValue(kGlow, (float)glow / (float)(kGlowFrames - 1));

        const bool clipL = engine_.consumeClip(kLeft);
        const bool clipR = engine_.consumeClip(kRight);
        if (getInt(kMode) == 0)
        {
            publishNeedle(kNeedleL0, kStereoSectors, kStereoPer, deflectionToFrame(engine_.deflection(kLeft), kStereoFrames));
            publishNeedle(kNeedleR0, kStereoSectors, kStereoPer, deflectionToFrame(engine_.deflection(kRight), kStereoFrames));
            setValue(kClipL, clipL ? 1.0f : 0.0f);
            setValue(kClipR, clipR ? 1.0f : 0.0f);
        }
        else
        {
            publishNeedle(kNeedleM0, kMonoSectors, kMonoPer, deflectionToFrame(engine_.deflection(kMono), kMonoFrames));
            setValue(kClipIn, (clipL || clipR) ? 1.0f : 0.0f);
        }
    }

    VuEngine engine_;
    HeatFollower heat_;
    HueCycle hue_;
    TempoInfo tempo_;
    int displayAge_ = 0;
    int displayEvery_ = 1470;
    int lastMode_ = 0;
};

MPC_PLUGIN_ENTRY(SlowOpsVUMeter)
