#include "Shepard.h"
#include "Util.h"
#include <cstdio>

namespace shp {

namespace {
const double kLn2 = 0.69314718055994530942;
const double kPi = 3.14159265358979323846;
}  // namespace

const std::vector<TempoPreset>& tempoPresets() {
    static const std::vector<TempoPreset> p = {
        {"Clock, four beats", {3, 2, 2, 2}},
        {"One hit per bar", {3}},
        {"Eighths", {3, 1, 2, 1, 2, 1, 2, 1}},
        {"Sixteenths", {3, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1}},
        {"Triplets", {3, 1, 1, 2, 1, 1, 2, 1, 1, 2, 1, 1}},
        {"Tresillo", {3, 0, 0, 3, 0, 0, 3, 0}},
        {"Son clave 3-2", {3, 0, 0, 2, 0, 0, 2, 0, 0, 0, 2, 0, 2, 0, 0, 0}},
    };
    return p;
}

TempoCycle tempoCycle(const TempoSettings& s) {
    TempoCycle c;
    const int cycles = clampInt(s.cycles, 0, 64);
    const int bars = clampInt(s.lengthBars, 1, kMaxBars);
    c.cycleBars = cycles > 0 ? (double)bars / cycles : (double)clampInt(s.cycleBars, 1, kMaxBars);
    c.stages = s.method == methodStepped ? clampInt(s.stages, 2, 8) : 0;
    const double exact = c.stages ? c.cycleBars * (3.0 * c.stages - 1.0) / (2.0 * c.stages) : c.cycleBars / kLn2;
    int mode = s.startMode;
    if (mode == startAuto) mode = c.stages ? startExact : startAligned;
    if (mode == startAligned) {
        c.C = std::max(1.0, std::floor(exact + 0.5));
        c.rho = c.C / exact;
    } else {
        c.C = exact;
        c.rho = 1.0;
    }
    return c;
}

Score buildTempo(const TempoSettings& in, const Transport& tr) {
    TempoSettings s = in;
    s.layers = clampInt(s.layers, 2, 8);
    s.lengthBars = clampInt(s.lengthBars, 1, kMaxBars);
    s.cycles = clampInt(s.cycles, 0, 64);
    s.vmin = clampInt(s.vmin, 1, 127);
    s.vmax = clampInt(s.vmax, s.vmin, 127);
    s.note = clampInt(s.note, 0, 127);
    if (s.grid.empty() || s.grid.size() > 64) s.grid = {3, 2, 2, 2};
    const int g = (int)s.grid.size();

    Score sc;
    sc.tr = tr;
    const long long bar = tr.barTicks();
    const long long regionTicks = (long long)s.lengthBars * bar;
    sc.lengthTicks = regionTicks;
    const TempoCycle cy = tempoCycle(s);
    const double T = cy.cycleBars;
    const double cycleTicksD = T * (double)bar;
    const int N = s.layers;
    const double barSec = (double)bar / kTpb * 60.0 / std::max(20.0, tr.bpm);

    // the global stream: hit number phi (pattern repeats of layer 0 from the start) -> time tau in bars.
    struct Ev { double tau; int level; };
    std::vector<Ev> ev;
    const double endTau = N * T;
    const int S = cy.stages;
    std::vector<double> stageStartPhi, stageRate;   // stepped only
    if (S) {
        double phi = 0;
        for (int m = 0; m < N * S; ++m) {
            const double rate = cy.rho * std::ldexp(1.0, m / S) * (S + m % S) / S;
            stageStartPhi.push_back(phi);
            stageRate.push_back(rate);
            phi += rate * T / S;
        }
        stageStartPhi.push_back(phi);
    }
    size_t m = 0;
    bool full = false;
    for (long long n = 0; !full; ++n) {
        for (int i = 0; i < g; ++i) {
            const double phi = (double)n + (double)i / g;
            double tau;
            if (!S) {
                tau = T * std::log2(1.0 + phi / cy.C);
            } else {
                while (m + 1 < stageRate.size() && phi >= stageStartPhi[m + 1]) ++m;
                if (phi >= stageStartPhi.back()) { tau = endTau + 1; }
                else tau = (double)m * T / S + (phi - stageStartPhi[m]) / stageRate[m];
            }
            if (!s.grid[(size_t)i]) { if (tau >= endTau) { full = true; break; } continue; }
            ev.push_back({tau, s.grid[(size_t)i]});
            if (tau >= endTau) { full = true; break; }   // one hit past the end, used for the gap of the last
            if (ev.size() > (size_t)kMaxNotes) { full = true; break; }
        }
    }

    const std::string what = std::string("Shepard tempo ") + (s.method == methodStepped ? "stepped " + std::to_string(cy.stages) + " " : std::string("smooth ")) +
                             (s.direction == tempoRit ? "ritardando" : "accelerando");
    std::map<int, Track> tracks;
    auto trackFor = [&](int key) -> Track& {
        auto it = tracks.find(key);
        if (it == tracks.end()) {
            Track t;
            t.name = what + (s.output == toPerLayer ? " L" + std::to_string(key + 1) : "");
            it = tracks.emplace(key, std::move(t)).first;
        }
        return it->second;
    };
    static const double lvf[4] = {0, 0.55, 0.78, 1.0};

    // layer k carries the hits with tau in [k T, (k+1) T); the cycle repeats over the region
    const long long cyclesToWrite = s.cycles > 0 ? s.cycles : (regionTicks + (long long)cycleTicksD - 1) / std::max(1LL, (long long)cycleTicksD);
    size_t made = 0;
    for (size_t e = 0; e + 1 < ev.size() && made < (size_t)kMaxNotes; ++e) {
        const double tau = ev[e].tau;
        if (tau >= endTau) break;
        const double u = tau / T;
        const double a = std::sin(kPi * u / N) * std::sin(kPi * u / N);
        if (a < s.ampFloor || a < 1e-6) continue;
        const int k = std::min(N - 1, (int)std::floor(u));
        double frac = u - std::floor(u);
        const double gapAfter = (ev[e + 1].tau - tau);
        const double gapBefore = e > 0 ? (tau - ev[e - 1].tau) : gapAfter;
        const double gap = s.direction == tempoRit ? gapBefore : gapAfter;
        if (s.maxPerSec > 0 && std::min(gapAfter, gapBefore) * barSec < 1.0 / s.maxPerSec) continue;
        if (s.direction == tempoRit) frac = frac == 0.0 ? 0.0 : 1.0 - frac;
        const int level = clampInt(ev[e].level, 1, 3);
        const int vel = clampInt((int)std::lround(s.vmin + (s.vmax - s.vmin) * std::pow(a * lvf[level], s.gamma)), 1, 127);
        const long long gapTicks = (long long)std::floor(gap * (double)bar);
        int len = s.lengthMode == lenFixed ? std::max(1, s.fixedTicks)
                                           : (int)std::max(1LL, std::llround(gap * (double)bar * clampInt(s.gatePct, 5, 100) / 100.0));
        len = (int)std::max(1LL, std::min((long long)len, std::max(1LL, gapTicks - 1)));
        const long long inCycle = std::llround(frac * cycleTicksD);
        for (long long c = 0; c < cyclesToWrite; ++c) {
            const long long off = s.cycles > 0 ? divRound(c * regionTicks, s.cycles) : c * (long long)cycleTicksD;
            const long long tick = off + inCycle;
            if (tick >= regionTicks) continue;
            Track& t = trackFor(s.output == toPerLayer ? k : 0);
            t.notes.push_back({tick, len, s.note, vel});
            ++made;
        }
    }
    for (auto& kv : tracks) {
        if (kv.second.notes.empty() && kv.second.name.empty()) continue;
        tidyTrack(kv.second, regionTicks);
        if (!kv.second.name.empty()) sc.tracks.push_back(std::move(kv.second));
    }
    sc.name = what;
    char buf[160];
    std::snprintf(buf, sizeof buf, ", %d layers, %d bars, cycle %.3g bars, start speed %.3f, C %.3f", N, s.lengthBars, T, cy.rho, cy.C);
    sc.info = what + buf;
    return sc;
}

}  // namespace shp
