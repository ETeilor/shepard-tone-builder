#include "Shepard.h"
#include "Util.h"

namespace shp {

namespace {
const int kChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const int kWhole[] = {0, 2, 4, 6, 8, 10};
const int kOcta[] = {0, 2, 3, 5, 6, 8, 9, 11};
const int kMajPent[] = {0, 2, 4, 7, 9};
const int kMinPent[] = {0, 3, 5, 7, 10};
const int kMajor[] = {0, 2, 4, 5, 7, 9, 11};
const int kMinor[] = {0, 2, 3, 5, 7, 8, 10};
const int kDorian[] = {0, 2, 3, 5, 7, 9, 10};
const int kMixo[] = {0, 2, 4, 5, 7, 9, 10};
struct SetInfo { const int* d; int n; const char* name; };
const SetInfo kSets[psNumSets] = {
    {kChromatic, 12, "chromatic"}, {kWhole, 6, "whole tone"}, {kOcta, 8, "octatonic"},
    {kMajPent, 5, "major pentatonic"}, {kMinPent, 5, "minor pentatonic"}, {kMajor, 7, "major"},
    {kMinor, 7, "natural minor"}, {kDorian, 7, "dorian"}, {kMixo, 7, "mixolydian"}};
const SetInfo& setOf(int id) { return kSets[clampInt(id, 0, psNumSets - 1)]; }
}  // namespace

int pitchSetSize(int set) { return setOf(set).n; }
const int* pitchSetDegrees(int set) { return setOf(set).d; }
const char* pitchSetName(int set) { return setOf(set).name; }

int tonalStepsPerCycle(const TonalSettings& s) {
    const int n = pitchSetSize(s.pitchSet);
    const int d = std::max(1, s.interval);
    return n / gcdInt(n, d);
}

int tonalPitchClass(const TonalSettings& s, int step) {
    const auto& si = setOf(s.pitchSet);
    const long long d = std::max(1, s.interval);
    long long idx = ((s.direction == dirDown ? -1LL : 1LL) * step * d) % si.n;
    if (idx < 0) idx += si.n;
    return (clampInt(s.tonic, 0, 11) + si.d[idx]) % 12;
}

double tonalBell(const TonalSettings& s, int p) {
    if (p < s.low || p > s.high) return 0.0;
    const double c = s.center < 0 ? (s.low + s.high) / 2.0 : (double)s.center;
    const double half = std::max(0.5, (s.widthOct > 0 ? s.widthOct * 12.0 : (double)(s.high - s.low)) / 2.0);
    const double d = ((double)p - c) / half;
    if (std::fabs(d) >= 1.0) return 0.0;
    if (s.shape == bellGauss) return std::exp(-4.5 * d * d);
    const double v = std::cos(3.14159265358979323846 * d / 2.0);
    return v * v;
}

Score buildTonal(const TonalSettings& in, const Transport& tr) {
    TonalSettings s = in;
    s.low = clampInt(s.low, 0, 126);
    s.high = clampInt(s.high, s.low + 1, 127);
    s.lengthBars = clampInt(s.lengthBars, 1, kMaxBars);
    s.cycles = clampInt(s.cycles, 0, 64);
    s.vmin = clampInt(s.vmin, 1, 127);
    s.vmax = clampInt(s.vmax, s.vmin, 127);
    s.registerTracks = clampInt(s.registerTracks, 2, 4);
    if (s.cc11) s.output = outOctave;

    Score sc;
    sc.tr = tr;
    const long long regionTicks = (long long)s.lengthBars * tr.barTicks();
    sc.lengthTicks = regionTicks;
    const int S = tonalStepsPerCycle(s);
    const int gate = s.gatePct > 0 ? clampInt(s.gatePct, 10, 200) : (s.articulation == artLegato ? 140 : 60);

    // step starts
    std::vector<long long> start;
    if (s.cycles > 0) {
        const long long total = (long long)s.cycles * S;
        for (long long i = 0; i <= total; ++i) start.push_back(divRound(i * regionTicks, total));
    } else {
        const long long st = std::max(1, s.stepTicks);
        for (long long t = 0; t < regionTicks + st; t += st) start.push_back(std::min(t, regionTicks));
    }

    std::string what = std::string("Shepard tonal ") + (s.direction == dirDown ? "down " : "up ") + pitchSetName(s.pitchSet) +
                       (s.articulation == artLegato ? " legato" : " staccato");
    std::map<int, Track> tracks;   // key: track index
    auto trackFor = [&](int key, const std::string& suffix) -> Track& {
        auto it = tracks.find(key);
        if (it == tracks.end()) { Track t; t.name = what + suffix; it = tracks.emplace(key, std::move(t)).first; }
        return it->second;
    };

    size_t made = 0;
    for (size_t i = 0; i + 1 < start.size() && made < (size_t)kMaxNotes; ++i) {
        const long long t0 = start[i];
        if (t0 >= regionTicks) break;
        const long long stepLen = std::max(1LL, start[i + 1] - t0);
        const int stepIn = (int)(i % (size_t)S);
        const int pc = tonalPitchClass(s, stepIn);
        std::vector<int> pitches;
        for (int p = s.low; p <= s.high; ++p)
            if (p % 12 == pc && tonalBell(s, p) > 1e-3) pitches.push_back(p);
        const int m = (int)pitches.size();
        std::vector<int> order((size_t)m);
        for (int j = 0; j < m; ++j) order[(size_t)j] = j;
        if (s.spread == spreadDown) std::reverse(order.begin(), order.end());
        if (s.spread == spreadRandom) {
            Rng r((uint64_t)s.seed * 7919 + (uint64_t)stepIn);
            for (int j = m - 1; j > 0; --j) std::swap(order[(size_t)j], order[(size_t)r.below(j + 1)]);
        }
        for (int j = 0; j < m; ++j) {
            const int p = pitches[(size_t)j];
            const double a = tonalBell(s, p);
            int vel = (int)std::lround(s.vmin + (s.vmax - s.vmin) * std::pow(a, s.gamma));
            if (vel < s.threshold) continue;
            int ccv = clampInt(vel, 1, 127);
            if (s.jitter > 0) {
                Rng r((uint64_t)s.seed * 1000003ull + (uint64_t)stepIn * 131ull + (uint64_t)p);
                vel += r.below(2 * s.jitter + 1) - s.jitter;
            }
            vel = clampInt(vel, 1, 127);
            long long off = 0;
            if (s.spread != spreadTogether && m > 1) {
                int rank = j;
                for (int q = 0; q < m; ++q) if (order[(size_t)q] == j) { rank = q; break; }
                off = stepLen * clampInt(s.spreadPct, 0, 100) / 100 * rank / m;
            }
            const long long tick = t0 + off;
            if (tick >= regionTicks) continue;
            Note n{tick, (int)std::max(1LL, stepLen * gate / 100), p, vel};
            int key = 0;
            std::string suffix;
            if (s.output == outRegister) {
                key = clampInt((p - s.low) * s.registerTracks / (s.high - s.low + 1), 0, s.registerTracks - 1);
                suffix = " reg" + std::to_string(key + 1);
            } else if (s.output == outOctave) {
                key = p / 12;
                suffix = " oct C" + std::to_string(p / 12 - 2);
            }
            Track& t = trackFor(key, suffix);
            if (s.cc11) {
                n.velocity = clampInt(s.ccVelocity, 1, 127);
                t.ccs.push_back({tick, 11, ccv});
            }
            t.notes.push_back(n);
            ++made;
        }
    }

    for (auto& kv : tracks) {
        tidyTrack(kv.second, regionTicks);
        sc.tracks.push_back(std::move(kv.second));
    }
    sc.name = what;
    sc.info = what + ", step " + std::to_string(std::max(1, s.interval)) + ", " + std::to_string(S) + " steps per loop, " +
              std::to_string(s.lengthBars) + " bars" + (s.cycles > 0 ? ", " + std::to_string(s.cycles) + " loops" : ", free run") +
              (s.cc11 ? ", CC11" : "");
    return sc;
}

}  // namespace shp
