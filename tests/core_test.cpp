// Core tests: loop closure, steps, bell, any length, tempo independence, CC11, tracks, Risset seams, stepped grid, MIDI round trip.
#include <algorithm>
#include <cstdio>
#include <tuple>
#include <cmath>
#include <map>
#include <set>
#include "Shepard.h"

using namespace shp;

static int failures = 0, checks = 0;
static void expect(bool ok, const std::string& what) {
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL %s\n", what.c_str()); }
}
static std::string str(int v) { return std::to_string(v); }

struct Key { long long tick; int note, vel; bool operator<(const Key& o) const { return tick != o.tick ? tick < o.tick : note != o.note ? note < o.note : vel < o.vel; } };
static std::vector<Key> keys(const Track& t) {
    std::vector<Key> k;
    for (auto& n : t.notes) k.push_back({n.tick, n.note, n.velocity});
    std::sort(k.begin(), k.end());
    return k;
}
static std::vector<Key> allKeys(const Score& s) {
    std::vector<Key> k;
    for (auto& t : s.tracks) for (auto& n : t.notes) k.push_back({n.tick, n.note, n.velocity});
    std::sort(k.begin(), k.end());
    return k;
}
static bool sameNotes(const Score& a, const Score& b) {
    if (a.tracks.size() != b.tracks.size()) return false;
    for (size_t i = 0; i < a.tracks.size(); ++i) {
        if (a.tracks[i].notes.size() != b.tracks[i].notes.size()) return false;
        for (size_t j = 0; j < a.tracks[i].notes.size(); ++j) {
            auto &x = a.tracks[i].notes[j], &y = b.tracks[i].notes[j];
            if (x.tick != y.tick || x.note != y.note || x.velocity != y.velocity || x.length != y.length) return false;
        }
    }
    return true;
}
// the second half of a two-cycle region equals the first half shifted
static bool halvesMatch(const std::vector<Key>& k, long long region) {
    std::vector<Key> a, b;
    for (auto& x : k) (x.tick < region / 2 ? a : b).push_back(x);
    if (a.size() != b.size() || a.empty()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].tick + region / 2 != b[i].tick || a[i].note != b[i].note || a[i].vel != b[i].vel) return false;
    return true;
}

static void testTonal() {
    std::printf("tonal\n");
    Transport tr;
    // loop closure for every set, interval, direction
    for (int set = 0; set < psNumSets; ++set)
        for (int iv = 1; iv <= 7; ++iv)
            for (int dir = 0; dir < 2; ++dir) {
                TonalSettings s;
                s.pitchSet = set; s.interval = iv; s.direction = dir; s.lengthBars = 8; s.cycles = 2;
                auto sc = buildTonal(s, tr);
                expect(sc.tracks.size() == 1 && halvesMatch(keys(sc.tracks[0]), sc.lengthTicks),
                       "closure set " + str(set) + " interval " + str(iv) + " dir " + str(dir));
            }
    // steps per loop
    {
        TonalSettings s;
        const int want[] = {12, 6, 4, 3, 12, 2, 12};
        for (int iv = 1; iv <= 7; ++iv) { s.interval = iv; expect(tonalStepsPerCycle(s) == want[iv - 1], "steps per loop interval " + str(iv)); }
        s.pitchSet = psMajor; s.interval = 1; expect(tonalStepsPerCycle(s) == 7, "diatonic: 7 steps");
        s.pitchSet = psWholeTone; s.interval = 2; expect(tonalStepsPerCycle(s) == 3, "whole tone by 2: 3 steps");
    }
    // pitch classes, direction, range, velocity
    {
        TonalSettings s; s.lengthBars = 4; s.cycles = 1; s.tonic = 0;
        auto sc = buildTonal(s, tr);
        std::map<long long, std::set<int>> byTick;
        for (auto& n : sc.tracks[0].notes) byTick[n.tick].insert(n.note % 12);
        expect(byTick.size() == 12, "12 steps in one loop");
        int step = 0; bool ok = true;
        for (auto& kv : byTick) { ok = ok && kv.second.size() == 1 && *kv.second.begin() == step % 12; ++step; }
        expect(ok, "chromatic up: pitch class rises by one each step");
        s.direction = dirDown;
        auto sd = buildTonal(s, tr);
        std::map<long long, std::set<int>> bd;
        for (auto& n : sd.tracks[0].notes) bd[n.tick].insert(n.note % 12);
        step = 0; ok = true;
        for (auto& kv : bd) { ok = ok && *kv.second.begin() == (12 - step % 12) % 12; ++step; }
        expect(ok, "chromatic down: pitch class falls by one each step");
        ok = true;
        for (auto& n : sc.tracks[0].notes) ok = ok && n.note >= s.low && n.note <= s.high && n.velocity >= s.threshold && n.velocity <= s.vmax;
        expect(ok, "notes inside the range and the velocity limits");
        ok = true;
        for (int p = s.low; p <= s.high; ++p) ok = ok && std::fabs(tonalBell(s, p) - tonalBell(s, s.low + s.high - p)) < 1e-9;
        expect(ok, "bell is symmetric around the middle");
        expect(tonalBell(s, s.low) == 0 && tonalBell(s, 66) > 0.99, "bell is zero at the edge and one in the middle");
        // tonic
        s.tonic = 5; s.direction = dirUp;
        auto st = buildTonal(s, tr);
        expect(st.tracks[0].notes[0].note % 12 == 5, "tonic sets the first pitch class");
    }
    // any length, any meter
    {
        const int bars[] = {1, 2, 3, 5, 7, 13, 64};
        for (int b : bars)
            for (int c : {1, 2, 3}) {
                TonalSettings s; s.lengthBars = b; s.cycles = c;
                auto sc = buildTonal(s, tr);
                bool ok = sc.lengthTicks == (long long)b * 3840 && !sc.tracks.empty();
                for (auto& n : sc.tracks[0].notes) ok = ok && n.tick >= 0 && n.tick < sc.lengthTicks && n.tick + n.length <= sc.lengthTicks;
                expect(ok, "region " + str(b) + " bars, " + str(c) + " loops fits exactly");
            }
        Transport t78; t78.num = 7; t78.den = 8;
        TonalSettings s; s.lengthBars = 4; s.cycles = 2;
        auto sc = buildTonal(s, t78);
        expect(sc.lengthTicks == 4 * 3360 && halvesMatch(keys(sc.tracks[0]), sc.lengthTicks), "7/8: bar of 3360 ticks, loop closes");
        s.cycles = 3; s.lengthBars = 9;
        auto s3 = buildTonal(s, tr);
        std::vector<Key> k = keys(s3.tracks[0]);
        const long long third = s3.lengthTicks / 3;
        std::set<std::tuple<long long, int, int>> first, second;
        for (auto& x : k) { if (x.tick < third) first.insert({x.tick, x.note, x.vel}); else if (x.tick < 2 * third) second.insert({x.tick - third, x.note, x.vel}); }
        expect(first == second, "three loops in nine bars repeat exactly");
    }
    // tempo independence
    {
        TonalSettings s;
        Transport a, b; a.bpm = 60; b.bpm = 200;
        expect(sameNotes(buildTonal(s, a), buildTonal(s, b)), "tonal ticks do not depend on the project tempo");
    }
    // articulation
    {
        TonalSettings s; s.lengthBars = 4; s.cycles = 1;
        const long long step = 4 * 3840 / 12;
        s.articulation = artStaccato;
        expect(buildTonal(s, tr).tracks[0].notes[0].length < step, "staccato is shorter than a step");
        s.articulation = artLegato;
        auto lg = buildTonal(s, tr);
        expect(lg.tracks[0].notes[0].length > step, "legato overlaps the next step");
        s.gatePct = 200; s.interval = 6;       // the same pitch returns every second step: no overlapping notes of one pitch
        auto g2 = buildTonal(s, tr);
        std::map<int, long long> endOf; bool ok = true;
        for (auto& n : g2.tracks[0].notes) { if (endOf.count(n.note) && endOf[n.note] > n.tick) ok = false; endOf[n.note] = n.tick + n.length; }
        expect(ok, "no overlap of one pitch at 200 percent gate");
    }
    // outputs
    {
        TonalSettings s; s.lengthBars = 4; s.cycles = 1;
        auto one = buildTonal(s, tr);
        s.output = outRegister; s.registerTracks = 3;
        auto reg = buildTonal(s, tr);
        expect(reg.tracks.size() == 3 && reg.noteCount() == one.noteCount(), "register output: three tracks, same notes");
        bool ok = true;
        for (size_t i = 0; i < reg.tracks.size(); ++i) for (auto& n : reg.tracks[i].notes) ok = ok && (n.note - s.low) * 3 / (s.high - s.low + 1) == (int)i;
        expect(ok, "register output: every note in its register");
        s.output = outOctave;
        auto oc = buildTonal(s, tr);
        expect(oc.noteCount() == one.noteCount() && oc.tracks.size() >= 5, "octave output: a track per octave");
        s.cc11 = true; s.ccVelocity = 90;
        auto cc = buildTonal(s, tr);
        ok = cc.noteCount() == one.noteCount();
        size_t ccs = 0;
        for (auto& t : cc.tracks) {
            ccs += t.ccs.size();
            for (auto& n : t.notes) ok = ok && n.velocity == 90;
            for (auto& c : t.ccs) ok = ok && c.cc == 11 && c.value >= 1 && c.value <= 127;
        }
        expect(ok && ccs == cc.noteCount(), "CC11: constant velocity, one CC per note");
        // the loudest lane has the largest CC
        int bestLane = -1, best = 0, lane = 0;
        for (auto& t : cc.tracks) { int m = 0; for (auto& c : t.ccs) m = std::max(m, c.value); if (m > best) { best = m; bestLane = lane; } ++lane; }
        expect(bestLane >= 2 && bestLane < (int)cc.tracks.size() - 2, "CC11: the middle octaves are the loudest");
        s.cc11 = false; s.output = outSingle;
    }
    // seeded details
    {
        TonalSettings s; s.jitter = 6; s.spread = spreadRandom; s.seed = 3;
        auto a = buildTonal(s, tr), b = buildTonal(s, tr);
        expect(sameNotes(a, b), "same seed, same notes");
        s.seed = 4;
        expect(!sameNotes(a, buildTonal(s, tr)), "another seed, other notes");
        s.jitter = 6; s.cycles = 2; s.lengthBars = 8; s.spread = spreadTogether;
        auto j = buildTonal(s, tr);
        expect(halvesMatch(keys(j.tracks[0]), j.lengthTicks), "velocity jitter repeats in every loop");
    }
    // free run
    {
        TonalSettings s; s.cycles = 0; s.stepTicks = 240; s.lengthBars = 2;
        auto sc = buildTonal(s, tr);
        expect(!sc.tracks.empty() && sc.tracks[0].notes.size() > 100, "free run with a fixed step");
    }
}

static TempoSettings example() {
    TempoSettings s;
    s.lengthBars = 4; s.cycles = 1; s.layers = 4; s.grid = {3, 2, 2, 2};
    s.startMode = startAligned; s.output = toPerLayer; s.ampFloor = 0.03;
    return s;
}

static void testTempo() {
    std::printf("tempo\n");
    Transport tr;
    {
        auto c = tempoCycle(example());
        expect(c.C == 6 && std::fabs(c.rho - 1.0397) < 0.001, "T=4: C=6, start speed 1.04");
        TempoSettings s; s.method = methodStepped; s.stages = 4; s.lengthBars = 8; s.cycles = 1;
        auto d = tempoCycle(s);
        expect(std::fabs(d.C - 11.0) < 1e-9 && d.rho == 1.0, "stepped S=4, T=8: ratio 1 exactly, C=11");
    }
    // the worked example: 302 hits over four layers
    auto ex = buildTempo(example(), tr);
    expect(ex.tracks.size() == 4, "one track per layer");
    expect(ex.noteCount() == 302, "worked example: 302 hits (got " + std::to_string(ex.noteCount()) + ")");
    {
        const size_t want[] = {0, 0, 0, 0};
        (void)want;
        size_t n0 = ex.tracks[0].notes.size(), n3 = ex.tracks[3].notes.size();
        expect(n3 > 2 * n0 && n0 > 10, "each layer is denser than the one below");
        bool ok = true;
        int peak = 0, peakLayer = 0, li = 0;
        for (auto& t : ex.tracks) { int m = 0; for (auto& n : t.notes) { m = std::max(m, n.velocity); ok = ok && n.velocity >= 1 && n.velocity <= 127; } if (m > peak) { peak = m; peakLayer = li; } ++li; }
        expect(ok && (peakLayer == 1 || peakLayer == 2), "velocity: loudest hits in the middle layers");
    }
    // seams between layers
    {
        const long long R = ex.lengthTicks;
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            auto &a = ex.tracks[(size_t)k].notes, &b = ex.tracks[(size_t)k + 1].notes;
            const double lastA = (double)a.back().tick, prevA = (double)a[a.size() - 2].tick;
            const double firstB = (double)b[0].tick, secondB = (double)b[1].tick;
            const double gap = (double)R - lastA + firstB;
            const double prevI = lastA - prevA, nextI = secondB - firstB;
            ok = ok && std::fabs(gap - prevI) / prevI < 0.08 && std::fabs(gap - nextI) / nextI < 0.08;
        }
        expect(ok, "no jump at the seam from layer k to layer k+1");
        // all layers share a downbeat at the start of the loop (C is an integer)
        bool down = true;
        for (int k = 1; k < 4; ++k) down = down && ex.tracks[(size_t)k].notes[0].tick == 0;
        expect(down, "layers 1..3 start together on the downbeat");
    }
    // two loops repeat exactly, any meter, any tempo
    {
        TempoSettings s = example(); s.lengthBars = 8; s.cycles = 2; s.output = toSingle;
        auto sc = buildTempo(s, tr);
        expect(halvesMatch(allKeys(sc), sc.lengthTicks), "two loops repeat exactly");
        Transport a, b; a.bpm = 55; b.bpm = 190;
        expect(sameNotes(buildTempo(s, a), buildTempo(s, b)), "tempo mode ticks do not depend on the project tempo");
        Transport t68; t68.num = 6; t68.den = 8;
        auto m = buildTempo(s, t68);
        expect(m.lengthTicks == 8 * 2880 && halvesMatch(allKeys(m), m.lengthTicks), "6/8: bar of 2880 ticks, loop closes");
        for (int bars : {1, 3, 5, 16, 64}) {
            TempoSettings q = example(); q.lengthBars = bars; q.cycles = 1; q.output = toSingle;
            auto z = buildTempo(q, tr);
            bool ok = z.lengthTicks == (long long)bars * 3840;
            for (auto& n : z.tracks[0].notes) ok = ok && n.tick >= 0 && n.tick + n.length <= z.lengthTicks;
            expect(ok, "tempo region of " + str(bars) + " bars fits");
        }
    }
    // direction mirrors the loop
    {
        TempoSettings s = example(); s.output = toSingle;
        auto up = buildTempo(s, tr);
        s.direction = tempoRit;
        auto down = buildTempo(s, tr);
        expect(std::llabs((long long)up.noteCount() - (long long)down.noteCount()) <= 2, "ritardando has the same hits as accelerando");
        // accelerando: gaps between hits shrink along each layer; ritardando: they grow
        TempoSettings p = example(); p.output = toPerLayer;
        auto a = buildTempo(p, tr);
        p.direction = tempoRit;
        auto r = buildTempo(p, tr);
        auto& la = a.tracks[1].notes; auto& lr = r.tracks[1].notes;
        const long long fa = la[1].tick - la[0].tick, ea = la.back().tick - la[la.size() - 2].tick;
        const long long fr = lr[1].tick - lr[0].tick, er = lr.back().tick - lr[lr.size() - 2].tick;
        expect(fa > ea, "accelerando: gaps shrink");
        expect(fr < er, "ritardando: gaps grow");
    }
    // density cap
    {
        TempoSettings s = example(); s.output = toSingle;
        const size_t full = buildTempo(s, tr).noteCount();
        s.maxPerSec = 10;
        const size_t capped = buildTempo(s, tr).noteCount();
        expect(capped < full && capped > 0, "density cap drops the fastest hits");
    }
    // stepped: hits on the tuplet grid of the stages
    {
        TempoSettings s; s.method = methodStepped; s.stages = 2; s.lengthBars = 4; s.cycles = 1; s.layers = 2;
        s.grid = {3}; s.startMode = startExact; s.ampFloor = 0; s.vmin = 1; s.output = toPerLayer;
        auto sc = buildTempo(s, tr);
        const long long want[] = {3840, 7680, 10240, 12800};
        bool ok = sc.tracks.size() == 2 && sc.tracks[0].notes.size() == 4;
        for (int i = 0; ok && i < 4; ++i) ok = sc.tracks[0].notes[(size_t)i].tick == want[i];
        expect(ok, "stepped 1 -> 3/2 -> 2: hits at 1, 2, 2 2/3, 3 1/3 bars");
        // five on the grid per stage: S=4 stages rates 1, 5/4, 3/2, 7/4: gaps inside a stage are equal
        TempoSettings q; q.method = methodStepped; q.stages = 4; q.lengthBars = 8; q.cycles = 1; q.layers = 3;
        q.grid = {3}; q.startMode = startExact; q.ampFloor = 0; q.vmin = 1; q.output = toPerLayer;
        auto z = buildTempo(q, tr);
        // layer 0, stage 1 (bars 2..4): rate 5/4 bars per bar -> a hit every 4/5 bar = 3072 ticks
        std::vector<long long> inStage;
        for (auto& n : z.tracks[0].notes) if (n.tick >= 7680 && n.tick < 15360) inStage.push_back(n.tick);
        bool eq = inStage.size() >= 3;
        for (size_t i = 1; i < inStage.size(); ++i) eq = eq && std::llabs((inStage[i] - inStage[i - 1]) - 3072) <= 1;
        expect(eq, "stepped: stage 5/4 puts a hit every 4/5 bar");
    }
    // degenerate input never hangs
    {
        TempoSettings s; s.grid = {0, 0, 0};
        expect(buildTempo(s, tr).noteCount() == 0, "a grid of rests gives no hits and ends");
        s.method = methodStepped; s.grid = {0};
        expect(buildTempo(s, tr).noteCount() == 0, "stepped grid of rests ends");
        TempoSettings t; t.layers = 8; t.lengthBars = 64; t.cycles = 1; t.grid.assign(32, 2);
        auto big = buildTempo(t, tr);
        expect(big.noteCount() <= (size_t)kMaxNotes, "very dense settings stay under the cap");
    }
}

static void testMidi() {
    std::printf("midi\n");
    Transport tr; tr.num = 3; tr.den = 4; tr.bpm = 97;
    TonalSettings ts; ts.output = outOctave; ts.cc11 = true; ts.lengthBars = 6; ts.cycles = 2;
    TempoSettings ps; ps.output = toPerLayer; ps.lengthBars = 6; ps.cycles = 2;
    for (int which = 0; which < 2; ++which) {
        auto sc = which == 0 ? buildTonal(ts, tr) : buildTempo(ps, tr);
        auto bytes = writeMidi(sc);
        ReadResult rr;
        expect(readMidi(bytes.data(), bytes.size(), rr), "file parses");
        expect(rr.ppq == 960 && rr.num == 3 && rr.den == 4 && std::fabs(rr.bpm - 97) < 0.01, "header: 960 ppq, meter and tempo");
        expect(rr.tracks.size() == sc.tracks.size() + 1, "conductor plus every track");
        bool ok = true;
        for (size_t i = 0; i < sc.tracks.size(); ++i) {
            auto& a = sc.tracks[i]; auto& b = rr.tracks[i + 1];
            ok = ok && a.notes.size() == b.notes.size() && a.ccs.size() == b.ccs.size() && a.name == b.name;
            for (size_t j = 0; ok && j < a.notes.size(); ++j) {
                // the writer sorts by tick, the score by tick too
                auto &x = a.notes[j], &y = b.notes[j];
                ok = x.tick == y.tick && x.note == y.note && x.velocity == y.velocity && x.length == y.length;
            }
        }
        expect(ok, std::string("round trip of notes, CC and names (") + (which ? "tempo" : "tonal") + ")");
    }
}

int main() {
    testTonal();
    testTempo();
    testMidi();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
