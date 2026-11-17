// Command line for Shepard Tone Builder: the same options as the plug-in, writes a .mid file and a short report.
//   shepard_cli tonal out.mid bars=8 cycles=2 dir=up set=chromatic interval=1 art=staccato output=single ...
//   shepard_cli tempo out.mid bars=8 cycles=2 method=smooth stages=4 dir=accel layers=4 preset=0 ...
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include "Shepard.h"

using namespace shp;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: shepard_cli tonal|tempo out.mid [key=value ...]\n"); return 2; }
    const std::string mode = argv[1], out = argv[2];
    std::map<std::string, std::string> kv;
    for (int i = 3; i < argc; ++i) {
        const char* eq = std::strchr(argv[i], '=');
        if (eq) kv[std::string(argv[i], (size_t)(eq - argv[i]))] = eq + 1;
    }
    auto num = [&](const char* k, double d) { auto it = kv.find(k); return it == kv.end() ? d : std::atof(it->second.c_str()); };
    auto str = [&](const char* k, const char* d) { auto it = kv.find(k); return it == kv.end() ? std::string(d) : it->second; };
    Transport tr;
    tr.num = (int)num("num", 4); tr.den = (int)num("den", 4); tr.bpm = num("bpm", 120);
    Score sc;
    if (mode == "tonal") {
        TonalSettings s;
        s.direction = str("dir", "up") == "down" ? dirDown : dirUp;
        const std::string set = str("set", "chromatic");
        for (int i = 0; i < psNumSets; ++i) if (set == pitchSetName(i)) s.pitchSet = i;
        s.tonic = (int)num("tonic", 0); s.interval = (int)num("interval", 1);
        s.low = (int)num("low", 24); s.high = (int)num("high", 108); s.center = (int)num("center", -1);
        s.widthOct = num("width", 0); s.shape = str("shape", "cosine") == "gauss" ? bellGauss : bellCosine;
        s.articulation = str("art", "staccato") == "legato" ? artLegato : artStaccato;
        s.gatePct = (int)num("gate", 0);
        const std::string sp = str("spread", "together");
        s.spread = sp == "up" ? spreadUp : sp == "down" ? spreadDown : sp == "random" ? spreadRandom : spreadTogether;
        s.spreadPct = (int)num("spreadpct", 50);
        s.vmin = (int)num("vmin", 12); s.vmax = (int)num("vmax", 120); s.gamma = num("gamma", 1.0);
        s.threshold = (int)num("threshold", 6); s.jitter = (int)num("jitter", 0); s.seed = (int)num("seed", 1);
        const std::string o = str("output", "single");
        s.output = o == "register" ? outRegister : o == "octave" ? outOctave : outSingle;
        s.registerTracks = (int)num("regtracks", 3);
        s.cc11 = num("cc11", 0) != 0; s.ccVelocity = (int)num("ccvel", 96);
        s.lengthBars = (int)num("bars", 8); s.cycles = (int)num("cycles", 2); s.stepTicks = (int)num("step", 240);
        { const std::string l = str("layout", "chord"); s.layout = l == "voices" ? layoutVoices : l == "gliss" ? layoutGliss : layoutChord; }
        s.stagger = num("stagger", 0) != 0; s.humTime = (int)num("humt", 0); s.humVel = (int)num("humv", 0);
        s.taperOct = num("taper", 0); s.tiltDb = num("tilt", 0);
        const std::string ct = str("control", "vel");
        s.control = ct == "cc1" ? ctlCc1 : ct == "cc11" ? ctlCc11 : ctlVelocity;
        s.edgeFade = num("edge", 1) != 0;
        s.glide = num("glide", 1) != 0;
        s.bendRange = num("bend", 0.5);
        s.glideShare = (int)num("share", 100);
        if (s.layout != layoutChord && num("whole", 1) != 0) s.lengthBars = tonalWholeCycleBars(s, tr, s.lengthBars);
        sc = buildTonal(s, tr);
    } else if (mode == "tempo") {
        TempoSettings s;
        s.method = str("method", "smooth") == "stepped" ? methodStepped : methodSmooth;
        s.stages = (int)num("stages", 4);
        s.direction = str("dir", "accel") == "rit" ? tempoRit : tempoAccel;
        s.layers = (int)num("layers", 4);
        const auto& pr = tempoPresets();
        const int pi = (int)num("preset", 0);
        if (pi >= 0 && pi < (int)pr.size()) s.grid = pr[(size_t)pi].grid;
        s.note = (int)num("note", 60);
        const std::string st = str("start", "auto");
        s.startMode = st == "exact" ? startExact : st == "aligned" ? startAligned : startAuto;
        s.lengthBars = (int)num("bars", 8); s.cycles = (int)num("cycles", 2); s.cycleBars = (int)num("cyclebars", 4);
        s.vmin = (int)num("vmin", 8); s.vmax = (int)num("vmax", 122); s.gamma = num("gamma", 1.0);
        s.ampFloor = num("floor", 0.03);
        s.lengthMode = str("length", "fixed") == "prop" ? lenProportional : lenFixed;
        s.gatePct = (int)num("gate", 50); s.fixedTicks = (int)num("ticks", 40); s.maxPerSec = (int)num("maxps", 0);
        s.output = str("output", "single") == "layers" ? toPerLayer : toSingle;
        sc = buildTempo(s, tr);
    } else { std::fprintf(stderr, "mode must be tonal or tempo\n"); return 2; }
    const auto bytes = writeMidi(sc);
    std::ofstream f(out, std::ios::binary);
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    std::printf("%s\n%zu notes in %zu tracks, %zu bytes\n", sc.info.c_str(), sc.noteCount(), sc.tracks.size(), bytes.size());
    for (auto& t : sc.tracks) std::printf("  %s: %zu notes, %zu cc\n", t.name.c_str(), t.notes.size(), t.ccs.size());
    return 0;
}
