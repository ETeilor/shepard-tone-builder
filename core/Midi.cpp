#include "Shepard.h"
#include "Util.h"

namespace shp {

namespace {
void putVlq(std::vector<unsigned char>& o, unsigned v) {
    unsigned char b[5];
    int n = 0;
    b[n++] = v & 0x7F;
    while (v >>= 7) b[n++] = (unsigned char)((v & 0x7F) | 0x80);
    while (n) o.push_back(b[--n]);
}
void put32(std::vector<unsigned char>& o, unsigned v) { for (int s = 24; s >= 0; s -= 8) o.push_back((unsigned char)(v >> s)); }
void put16(std::vector<unsigned char>& o, unsigned v) { o.push_back((unsigned char)(v >> 8)); o.push_back((unsigned char)v); }
void putTrackChunk(std::vector<unsigned char>& f, const std::vector<unsigned char>& body) {
    f.insert(f.end(), {'M', 'T', 'r', 'k'});
    put32(f, (unsigned)body.size());
    f.insert(f.end(), body.begin(), body.end());
}
void putText(std::vector<unsigned char>& t, int type, const std::string& s) {
    t.push_back(0x00); t.push_back(0xFF); t.push_back((unsigned char)type);
    putVlq(t, (unsigned)s.size());
    t.insert(t.end(), s.begin(), s.end());
}
struct Raw { long long tick; int order; int a, b; };   // order 0 note off, 1 cc, 2 note on
}  // namespace

std::vector<unsigned char> writeMidi(const Score& sc) {
    std::vector<unsigned char> f;
    f.insert(f.end(), {'M', 'T', 'h', 'd'});
    put32(f, 6);
    put16(f, 1);
    put16(f, (unsigned)(1 + sc.tracks.size()));
    put16(f, (unsigned)kTpb);

    std::vector<unsigned char> c;
    putText(c, 0x03, sc.name.empty() ? "Shepard Tone Builder" : sc.name);
    if (!sc.info.empty()) putText(c, 0x01, "Shepard Tone Builder: " + sc.info);
    const unsigned mpq = (unsigned)std::lround(60000000.0 / std::max(20.0, std::min(sc.tr.bpm, 400.0)));
    c.insert(c.end(), {0x00, 0xFF, 0x51, 0x03, (unsigned char)(mpq >> 16), (unsigned char)(mpq >> 8), (unsigned char)mpq});
    int dd = 0;
    for (int d = std::max(1, sc.tr.den); d > 1; d >>= 1) ++dd;
    c.insert(c.end(), {0x00, 0xFF, 0x58, 0x04, (unsigned char)std::max(1, sc.tr.num), (unsigned char)dd, 24, 8});
    c.insert(c.end(), {0x00, 0xFF, 0x2F, 0x00});
    putTrackChunk(f, c);

    for (const auto& tr : sc.tracks) {
        std::vector<Raw> ev;
        for (const auto& n : tr.notes) {
            ev.push_back({n.tick + n.length, 0, n.note, 0});
            ev.push_back({n.tick, 2, n.note, n.velocity});
        }
        for (const auto& cc : tr.ccs) ev.push_back({cc.tick, 1, cc.cc, cc.value});
        std::stable_sort(ev.begin(), ev.end(), [](const Raw& a, const Raw& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.order != b.order) return a.order < b.order;
            return a.a < b.a;
        });
        std::vector<unsigned char> t;
        putText(t, 0x03, tr.name);
        long long last = 0;
        for (const auto& e : ev) {
            putVlq(t, (unsigned)(e.tick - last));
            last = e.tick;
            if (e.order == 1) { t.push_back(0xB0); t.push_back((unsigned char)e.a); t.push_back((unsigned char)e.b); }
            else { t.push_back(e.order ? 0x90 : 0x80); t.push_back((unsigned char)e.a); t.push_back((unsigned char)e.b); }
        }
        putVlq(t, (unsigned)std::max(0LL, sc.lengthTicks - last));
        t.insert(t.end(), {0xFF, 0x2F, 0x00});
        putTrackChunk(f, t);
    }
    return f;
}

bool readMidi(const unsigned char* d, size_t size, ReadResult& out) {
    out = ReadResult();
    auto u16 = [&](size_t p) { return (unsigned)(d[p] << 8 | d[p + 1]); };
    auto u32 = [&](size_t p) { return (unsigned)(d[p] << 24 | d[p + 1] << 16 | d[p + 2] << 8 | d[p + 3]); };
    if (size < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd') return false;
    const unsigned tracks = u16(10), div = u16(12);
    if (div == 0 || (div & 0x8000)) return false;
    out.ppq = (int)div;
    size_t p = 8 + u32(4);
    bool gotTempo = false, gotMeter = false;
    for (unsigned t = 0; t < tracks && p + 8 <= size; ++t) {
        if (d[p] != 'M' || d[p + 1] != 'T' || d[p + 2] != 'r' || d[p + 3] != 'k') return false;
        const size_t len = u32(p + 4);
        size_t q = p + 8;
        const size_t end = std::min(size, q + len);
        long long tick = 0;
        int running = 0;
        ReadTrack rt;
        struct Open { long long tick; int vel; };
        std::map<int, std::vector<Open>> open;
        while (q < end) {
            unsigned delta = 0;
            for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; delta = (delta << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
            tick += delta;
            if (q >= end) break;
            int st = d[q];
            if (st & 0x80) { ++q; if (st < 0xF0) running = st; } else st = running;
            if (st == 0xFF) {
                if (q >= end) break;
                const int type = d[q++];
                unsigned l = 0;
                for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; l = (l << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                if (q + l > end) break;
                if (type == 0x03 && rt.name.empty()) rt.name.assign((const char*)d + q, l);
                else if (type == 0x51 && l == 3 && !gotTempo) {
                    const unsigned mpq = (unsigned)(d[q] << 16 | d[q + 1] << 8 | d[q + 2]);
                    if (mpq) { out.bpm = 60000000.0 / mpq; gotTempo = true; }
                } else if (type == 0x58 && l >= 2 && !gotMeter) {
                    out.num = std::max(1, (int)d[q]); out.den = 1 << std::min(6, (int)d[q + 1]); gotMeter = true;
                }
                q += l;
            } else if (st == 0xF0 || st == 0xF7) {
                unsigned l = 0;
                for (int i = 0; i < 4 && q < end; ++i) { const unsigned char b = d[q++]; l = (l << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                q += l;
            } else {
                const int kind = st & 0xF0;
                const int need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
                if (q + (size_t)need > end) break;
                if (kind == 0x90 && d[q + 1] > 0) open[d[q]].push_back({tick, d[q + 1]});
                else if (kind == 0x80 || (kind == 0x90 && d[q + 1] == 0)) {
                    auto& v = open[d[q]];
                    if (!v.empty()) { rt.notes.push_back({v.front().tick, (int)(tick - v.front().tick), d[q], v.front().vel}); v.erase(v.begin()); }
                } else if (kind == 0xB0) rt.ccs.push_back({tick, d[q], d[q + 1]});
                q += (size_t)need;
            }
        }
        std::sort(rt.notes.begin(), rt.notes.end(), [](const Note& a, const Note& b) { return a.tick != b.tick ? a.tick < b.tick : a.note < b.note; });
        out.tracks.push_back(std::move(rt));
        p += 8 + len;
    }
    return true;
}

}  // namespace shp
