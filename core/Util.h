#pragma once
// internal helpers shared by the generators
#include <algorithm>
#include <cmath>
#include <map>
#include "Shepard.h"

namespace shp {

inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int gcdInt(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a < 0 ? -a : a; }
inline long long divRound(long long a, long long b) { return (a + b / 2) / b; }

// same pitch twice at one tick: the louder wins. Same pitch overlapping: the earlier one ends where the next begins.
// Every note ends inside the region.
inline void tidyTrack(Track& t, long long regionEnd) {
    std::sort(t.notes.begin(), t.notes.end(), [](const Note& a, const Note& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.note != b.note) return a.note < b.note;
        return a.velocity > b.velocity;
    });
    std::vector<Note> out;
    out.reserve(t.notes.size());
    std::map<int, size_t> last;   // pitch -> index in out
    for (const auto& n : t.notes) {
        auto it = last.find(n.note);
        if (it != last.end()) {
            Note& p = out[it->second];
            if (p.tick == n.tick) continue;                       // duplicate, the louder came first
            if (p.tick + p.length > n.tick) p.length = (int)(n.tick - p.tick);
        }
        Note c = n;
        if (c.tick + c.length > regionEnd) c.length = (int)std::max(1LL, regionEnd - c.tick);
        if (c.length < 1) c.length = 1;
        out.push_back(c);
        last[c.note] = out.size() - 1;
    }
    t.notes.swap(out);
    std::stable_sort(t.ccs.begin(), t.ccs.end(), [](const Cc& a, const Cc& b) { return a.tick < b.tick; });
}

}  // namespace shp
