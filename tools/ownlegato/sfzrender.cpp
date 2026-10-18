// sfzrender: kthost's event files rendered offline through sfizz (libsfizz, https://sfz.tools/sfizz), so the own-legato
// pilot's SFZ plays exactly the events Kontakt + SSO got (tools/ownlegato/, the heard check's kthost format).
//
//   sfzrender <instrument.sfz> <events.txt> <out.wav> [--tail s] [--block n] [--voices n] [--floor] [--stream]
//   events: lines "time on|off|cc|pb a b" (seconds; pb a = 0..16383, 8192 centre, as kthost); 44.1 kHz stereo float WAV.
// --floor: an event at frame floor(t * rate), as kthost places them (its default mode; else the nearest frame).
// Events are sample-accurate (sfizz's delay within the block).
// Whole samples in RAM (default): free-wheeling alone does NOT prevent streaming underruns. sfizz 1.2.3's
// renderBlock waits only for loading jobs its dispatcher thread has already picked up (FilePool::waitForBackgroundLoading),
// so a voice started in the block before can find its file still queued and play silence after the preloaded
// 8192 frames (measured 2026-10-17: blind-test clips cut 0.186 s after a region start, 1 of 16 repeat renders under
// load). So the SFZ is loaded with only the regions these events can trigger (key played, and sw_previous absent,
// equal to the key, or a previous key that note-on actually had), each preloaded whole: nothing streams. sfizz's
// sequence counters are per region and count note-ons of their own key, so dropping other keys' and pairs' regions
// leaves every kept region's take choice unchanged. --stream: the old behaviour (whole SFZ, 8192-frame preload).
//
// Build: g++ -O2 -std=c++17 sfzrender.cpp -I<sfizz>/include -L<sfizz>/lib -lsfizz -o sfzrender
#include <sfizz.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct Ev { long long frame; int type; int a; int b; };   // 0 on 1 off 2 cc 3 pb

static void writeWav(const std::string& path, const std::vector<float>& lr, int rate)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    const uint32_t n = uint32_t(lr.size() * 4);
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + n); fwrite("WAVEfmt ", 1, 8, f); u32(16); u16(3); u16(2); u32(rate);
    u32(rate * 8); u16(8); u16(32); fwrite("data", 1, 4, f); u32(n);
    fwrite(lr.data(), 4, lr.size(), f);
    fclose(f);
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: sfzrender <sfz> <events> <out.wav> [--tail s] [--block n] [--voices n]\n"); return 2; }
    double tail = 3.0; int block = 256; int voices = 128; bool floorFrames = false; bool stream = false;
    for (int i = 4; i < argc; ++i) {
        if (!strcmp(argv[i], "--tail") && i + 1 < argc) tail = atof(argv[++i]);
        else if (!strcmp(argv[i], "--block") && i + 1 < argc) block = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--voices") && i + 1 < argc) voices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--floor")) floorFrames = true;
        else if (!strcmp(argv[i], "--stream")) stream = true;
    }
    const int rate = 44100;
    std::vector<Ev> evs;
    {
        std::ifstream f(argv[2]);
        std::string line;
        while (std::getline(f, line)) {
            std::istringstream ss(line);
            double t; std::string ty; int a = 0, b = 0;
            if (!(ss >> t >> ty >> a)) continue;
            ss >> b;
            int type = ty == "on" ? 0 : ty == "off" ? 1 : ty == "cc" ? 2 : ty == "pb" ? 3 : -1;
            if (type < 0) continue;
            if (type == 0 && b == 0) type = 1;
            evs.push_back({ floorFrames ? (long long)(t * rate) : (long long)llround(t * rate), type, a, b });
        }
        std::stable_sort(evs.begin(), evs.end(), [](const Ev& x, const Ev& y) { return x.frame < y.frame; });
    }
    sfz::Sfizz synth;
    synth.setSampleRate(rate);
    synth.setSamplesPerBlock(block);
    synth.setNumVoices(voices);
    synth.enableFreeWheeling();
    bool ok;
    if (stream) {
        ok = synth.loadSfzFile(argv[1]);
    } else {
        // (key, previous note-on key) pairs these events play; -1 = no previous note-on
        std::set<std::pair<int, int>> pairs; std::set<int> keys;
        int prev = -1;
        for (const Ev& e : evs) if (e.type == 0) { pairs.insert({ e.a, prev }); keys.insert(e.a); prev = e.a; }
        auto opInt = [](const std::string& line, const char* name, int dflt) {
            const std::string key = std::string(" ") + name + "=";
            const size_t p = line.find(key);
            return p == std::string::npos ? dflt : atoi(line.c_str() + p + key.size());
        };
        std::ifstream f(argv[1]);
        std::string line, text; size_t kept = 0, dropped = 0;
        while (std::getline(f, line)) {
            if (line.rfind("<region>", 0) == 0) {
                if (line.find(" lokey=") != std::string::npos || line.find(" hikey=") != std::string::npos
                    || line.find(" key=") == std::string::npos) {
                    fprintf(stderr, "--stream needed: region without a single key= (%s)\n", line.c_str()); return 1;
                }
                const int k = opInt(line, "key", -1), sp = opInt(line, "sw_previous", -2);
                bool keep = keys.count(k) && (sp == -2 || sp == k);
                if (!keep && keys.count(k)) for (const auto& pr : pairs) if (pr.first == k && pr.second == sp) keep = true;
                if (!keep) { ++dropped; continue; }
                ++kept;
            }
            text += line; text += '\n';
        }
        synth.setPreloadSize(1u << 30);     // more than any sample: preloadFile loads min(frames, size + offset)
        ok = synth.loadSfzString(argv[1], text);
        fprintf(stderr, "whole samples in RAM: %zu regions kept, %zu dropped\n", kept, dropped);
    }
    if (!ok) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    fprintf(stderr, "%d regions, %zu events\n", synth.getNumRegions(), evs.size());
    const long long end = (evs.empty() ? 0 : evs.back().frame) + (long long)(tail * rate);
    std::vector<float> L(block), R(block), out;
    out.reserve(size_t(end) * 2);
    size_t ei = 0;
    for (long long pos = 0; pos < end; pos += block) {
        const int n = int(std::min<long long>(block, end - pos));
        while (ei < evs.size() && evs[ei].frame < pos + n) {
            const Ev& e = evs[ei++];
            const int d = int(std::max<long long>(0, e.frame - pos));
            switch (e.type) {
            case 0: synth.noteOn(d, e.a, e.b); break;
            case 1: synth.noteOff(d, e.a, 0); break;
            case 2: synth.cc(d, e.a, e.b); break;
            case 3: synth.pitchWheel(d, e.a - 8192); break;
            }
        }
        float* bufs[2] = { L.data(), R.data() };
        synth.renderBlock(bufs, size_t(n), 1);
        for (int i = 0; i < n; ++i) { out.push_back(L[i]); out.push_back(R[i]); }
    }
    writeWav(argv[3], out, rate);
    fprintf(stderr, "wrote %s (%.1f s)\n", argv[3], double(end) / rate);
    return 0;
}
