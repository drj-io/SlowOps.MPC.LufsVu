// lufs_crosscheck.cpp - renders varied programme material to WAV files and prints this
// project's Integrated loudness and true-peak maximum for each, so scripts/lufs_crosscheck.py
// can compare them with an independent implementation (FFmpeg's ebur128 filter).
//
// Build: c++ -std=c++17 -O2 -I core/include tests/lufs_crosscheck.cpp -o build/lufs_crosscheck
// Run:   build/lufs_crosscheck <out_dir>      -> prints "<file> <I LUFS> <TP dBTP>" per line
#include "slowops/vumeter/LoudnessMeter.hpp"
#include "slowops/vumeter/TruePeak.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace slowops::vumeter;

static void writeWav(const std::string& path, int sr, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::perror(path.c_str()); std::exit(1); }
    const uint32_t frames = (uint32_t)l.size(), dataBytes = frames * 8;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(3); u16(2); u32((uint32_t)sr); u32((uint32_t)sr * 8); u16(8); u16(32);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite(&l[i], 4, 1, f); std::fwrite(&r[i], 4, 1, f); }
    std::fclose(f);
}

struct Programme { const char* name; int sr; double seconds; std::function<void(long, double, float&, float&)> gen; };

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const double pi = 3.14159265358979323846;
    std::mt19937 rng(1931);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::uniform_real_distribution<double> ud(0.0, 1.0);

    // 1) band-limited noise whose level jumps every 2 s between -45 and -8 dB (stereo, partly correlated)
    std::vector<double> lv(64);
    for (double& v : lv) v = -45.0 + 37.0 * ud(rng);
    double pl = 0, pr = 0;
    // 2) tones and silences
    // 3) a drum loop with sharp, hot transients and bright hats (true-peak test)
    const Programme progs[] = {
        { "noise_levels_48k", 48000, 60.0, [&](long n, double sr, float& l, float& r) {
              const double a = std::pow(10.0, lv[(size_t)(n / (long)(2 * sr)) % lv.size()] / 20.0);
              const double c = nd(rng);
              pl += 0.25 * (0.7 * c + 0.3 * nd(rng) - pl);
              pr += 0.25 * (0.7 * c + 0.3 * nd(rng) - pr);
              l = (float)(a * 3.0 * pl); r = (float)(a * 3.0 * pr); } },
        { "tones_gaps_44k", 44100, 45.0, [&](long n, double sr, float& l, float& r) {
              const double t = n / sr;
              const int seg = (int)(t / 3.0);
              const bool gap = seg % 4 == 3;
              const double f = 60.0 * std::pow(2.0, (seg % 9) * 0.9);
              const double a = gap ? 0.0 : std::pow(10.0, (-30.0 + (seg % 5) * 5.0) / 20.0);
              l = (float)(a * std::sin(2 * pi * f * t));
              r = (float)(a * std::sin(2 * pi * f * 1.003 * t + 0.4)); } },
        { "drums_hot_44k", 44100, 30.0, [&](long n, double sr, float& l, float& r) {
              const long beat = (long)(sr * 0.5);
              const long k = n % beat;
              const double kick = 1.1 * std::exp(-k / 3000.0) * std::sin(2 * pi * 55 * k / sr * (1 + 2.5 * std::exp(-k / 400.0)));
              const long h = n % (beat / 2);
              const double hat = 0.6 * std::exp(-h / 300.0) * nd(rng);
              const double hf = 0.35 * std::sin(2 * pi * 14700.0 * n / sr + 0.6);   // near fs/3: big inter-sample overs
              l = (float)(kick + hat + hf); r = (float)(kick - 0.5 * hat + hf); } },
        { "quiet_44k", 44100, 30.0, [&](long n, double sr, float& l, float& r) {
              const double a = (n / (long)sr) % 2 ? 0.003 : 0.0003;
              l = (float)(a * nd(rng)); r = (float)(a * nd(rng)); } },
    };

    for (const Programme& p : progs)
    {
        const long len = std::lround(p.seconds * p.sr);
        std::vector<float> l((size_t)len), r((size_t)len);
        LoudnessMeter m; m.prepare(p.sr);
        TruePeakDetector tl, tr; tl.prepare(p.sr); tr.prepare(p.sr);
        for (long n = 0; n < len; ++n)
        {
            p.gen(n, (double)p.sr, l[(size_t)n], r[(size_t)n]);
            m.process(l[(size_t)n], r[(size_t)n]);
            tl.process(l[(size_t)n]); tr.process(r[(size_t)n]);
        }
        const std::string path = dir + "/" + p.name + ".wav";
        writeWav(path, p.sr, l, r);
        const double tp = 20.0 * std::log10(std::fmax(tl.maxPeak(), tr.maxPeak()));
        std::printf("%s %.2f %.2f\n", path.c_str(), m.integrated(), tp);
    }
    return 0;
}
