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

double tonalBellF(const TonalSettings& s, double p) {
    if (p < s.low || p > s.high) return 0.0;
    const double c = s.center < 0 ? (s.low + s.high) / 2.0 : (double)s.center;
    const double half = std::max(0.5, (s.widthOct > 0 ? s.widthOct * 12.0 : (double)(s.high - s.low)) / 2.0);
    const double d = (p - c) / half;
    if (std::fabs(d) >= 1.0) return 0.0;
    if (s.shape == bellGauss) return std::exp(-4.5 * d * d);
    const double v = std::cos(3.14159265358979323846 * d / 2.0);
    return v * v;
}

double tonalBell(const TonalSettings& s, int p) { return tonalBellF(s, (double)p); }

int tonalVoiceCount(const TonalSettings& s) {
    return clampInt((int)std::lround((double)(s.high - s.low) / 12.0), 2, 8);
}

long long tonalCycleTicks(const TonalSettings& s) {
    const long long st = std::max(1, s.stepTicks);
    if (s.layout == layoutVoices) {
        const int nn = pitchSetSize(s.pitchSet) * tonalVoiceCount(s);
        return (long long)(nn / gcdInt(nn, std::max(1, s.interval))) * st;
    }
    return (long long)tonalStepsPerCycle(s) * st;
}

int tonalWholeCycleBars(const TonalSettings& s, const Transport& tr, int requested) {
    requested = clampInt(requested, 1, kMaxBars);
    const long long c = tonalCycleTicks(s), bar = tr.barTicks();
    long long a = c, b = bar;
    while (b) { const long long t = a % b; a = b; b = t; }
    const long long unit = c / a;            // bars in the shortest region made of whole loops
    if (unit < 1 || unit > kMaxBars) return requested;
    long long n = (requested + unit - 1) / unit * unit;
    if (n > kMaxBars) n = n - unit >= unit ? n - unit : requested;
    return (int)n;
}

namespace {
long long floorDiv(long long a, long long b) { long long q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
double smooth01(double x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

// absolute pitch of the loop at step g (before folding into the range)
long long loopPitch(const TonalSettings& s, long long g) {
    const auto& si = setOf(s.pitchSet);
    const long long idx = (s.direction == dirDown ? -1LL : 1LL) * g * std::max(1, s.interval);
    const long long oct = floorDiv(idx, si.n);
    return clampInt(s.tonic, 0, 11) + 12 * oct + si.d[idx - oct * si.n];
}

Score buildVoices(TonalSettings s, const Transport& tr) {
    const int N = tonalVoiceCount(s);
    s.high = s.low + 12 * N;
    if (s.high > 127) { s.low -= s.high - 127; s.high = 127; if (s.low < 0) s.low = 0; }
    const int span = 12 * N;
    Score sc;
    sc.tr = tr;
    const long long regionTicks = (long long)s.lengthBars * tr.barTicks();
    sc.lengthTicks = regionTicks;
    const long long st = std::max(10, s.stepTicks);
    const long long G = (regionTicks + st - 1) / st;
    const bool cc = s.control != ctlVelocity;
    const int ccNum = s.control == ctlCc11 ? 11 : 1;
    const int gate = s.gatePct > 0 ? clampInt(s.gatePct, 10, 200) : 0;
    const long long overlap = std::min<long long>(st / 4, 30);

    auto fold = [&](long long P, int k) { return s.low + (int)(((P + 12LL * k - s.low) % span + span) % span); };
    auto level = [&](double a) {   // controller value for a bell amplitude
        const double x = std::pow(std::max(0.0, std::min(1.0, a)), s.gamma);
        if (s.control == ctlCc11) return clampInt((int)std::lround(s.vmax * x), 0, 127);
        return clampInt((int)std::lround(s.vmin + (s.vmax - s.vmin) * x), 0, 127);
    };

    const std::string what = std::string("Shepard voice ") + (s.direction == dirDown ? "down " : "up ") + pitchSetName(s.pitchSet);
    for (int k = 0; k < N && sc.noteCount() < (size_t)kMaxNotes; ++k) {
        Track t;
        t.name = what + " " + std::to_string(k + 1) + (s.control == ctlCc1 ? " CC1" : s.control == ctlCc11 ? " CC11" : " vel");
        std::vector<int> pos((size_t)G + 1);
        for (long long g = 0; g <= G; ++g) pos[(size_t)g] = fold(loopPitch(s, g), k);
        for (long long g = 0; g < G; ++g) {
            const long long t0 = g * st;
            const int p = pos[(size_t)g], pn = pos[(size_t)g + 1];
            const bool wraps = s.direction == dirDown ? pn > p : pn < p;
            long long len = st;
            if (gate > 0) len = std::max(1LL, st * gate / 100);
            else if (s.articulation == artLegato && !wraps) len = st + overlap;
            else if (s.articulation == artStaccato) len = std::max(1LL, st * 60 / 100);
            if (wraps && len > st) len = st;
            int vel = clampInt(s.ccVelocity, 1, 127);
            if (!cc) {
                const double a = tonalBellF(s, p);
                vel = clampInt((int)std::lround(s.vmin + (s.vmax - s.vmin) * std::pow(a, s.gamma)), 1, 127);
                if (vel < s.threshold) continue;
            }
            t.notes.push_back({t0, (int)len, p, vel});
        }
        if (cc) {
            // the bell is sampled at the middle of every step and joined by straight lines
            auto amp = [&](long long g) { g = g < 0 ? 0 : g >= G ? G - 1 : g; return tonalBellF(s, pos[(size_t)g]); };
            const long long ci = std::max<long long>(6, std::min<long long>(60, st / 8));
            int last1 = -1, last11 = -1;
            for (long long tick = 0; tick <= regionTicks; tick += ci) {
                const double f = ((double)tick - (double)st / 2.0) / (double)st;
                const long long g0 = (long long)std::floor(f);
                const double fr = f - (double)g0;
                const double a = amp(g0) * (1.0 - fr) + amp(g0 + 1) * fr;
                const int v = level(a);
                if (v != last1) { t.ccs.push_back({tick, ccNum, v}); last1 = v; }
                if (s.control == ctlCc1 && s.edgeFade) {
                    const int e = clampInt((int)std::lround(127.0 * smooth01(a / 0.35)), 0, 127);
                    if (e != last11) { t.ccs.push_back({tick, 11, e}); last11 = e; }
                }
            }
        }
        tidyTrack(t, regionTicks);
        sc.tracks.push_back(std::move(t));
    }
    sc.name = what;
    sc.info = what + ", " + std::to_string(N) + " voices, step " + std::to_string((long long)st) + " ticks, " +
              std::to_string(s.lengthBars) + " bars" + (s.control == ctlCc1 ? ", CC1 + CC11 edges" : s.control == ctlCc11 ? ", CC11" : "");
    return sc;
}
}  // namespace

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
    if (s.layout == layoutVoices) {
        s.vmax = clampInt(in.vmax, 1, 127);
        s.vmin = clampInt(in.vmin, 0, s.vmax);
        s.gamma = in.gamma <= 0 ? 1.0 : in.gamma;
        return buildVoices(s, tr);
    }

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
