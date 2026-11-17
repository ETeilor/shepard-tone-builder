#pragma once
// Shepard Tone Builder core: plain C++17, no JUCE. Two generators write a Score (notes and CC11 per track):
//   tonal  - discrete Shepard scale, bell-shaped velocity over pitch
//   tempo  - Risset eternal accelerando / ritardando, smooth or stepped
// Everything is laid out in fractions of a bar, so the project tempo never enters the notes:
// the same settings give the same ticks at 60 and at 200 bpm.
#include <cstdint>
#include <string>
#include <vector>

namespace shp {

constexpr int kTpb = 960;           // MIDI ticks per quarter (Logic's own resolution)
constexpr int kMaxBars = 256;
constexpr int kMaxNotes = 400000;   // safety cap on one score

struct Transport {
    int num = 4, den = 4;
    double bpm = 120;
    long long barTicks() const { return (long long)num * 4 * kTpb / den; }
};

struct Note { long long tick; int length; int note; int velocity; };
struct Cc { long long tick; int cc; int value; };   // cc 128 = pitch bend, value 0..16383 (8192 = centre)
struct Track {
    std::string name;
    std::vector<Note> notes;
    std::vector<Cc> ccs;
};
struct Score {
    std::string name, info;
    Transport tr;
    long long lengthTicks = 0;
    std::vector<Track> tracks;
    size_t noteCount() const { size_t n = 0; for (auto& t : tracks) n += t.notes.size(); return n; }
};

// ------------------------------------------------------------------ tonal
enum Direction { dirUp, dirDown };
enum Articulation { artStaccato, artLegato };
enum PitchSetId { psChromatic, psWholeTone, psOctatonic, psMajorPent, psMinorPent, psMajor, psMinor, psDorian,
                  psMixolydian, psNumSets };
enum Spread { spreadTogether, spreadUp, spreadDown, spreadRandom, spreadNumModes };
enum Output { outSingle, outRegister, outOctave, outNumModes };
enum BellShape { bellCosine, bellGauss, bellNumShapes };
// layoutChord: at every step all octaves of the pitch sound together (the notes are placed on tracks by `output`).
// layoutVoices: one track per octave voice, each a single rising line; the bell is a continuous controller curve.
enum Layout { layoutChord, layoutVoices, layoutNumModes };
enum Control { ctlVelocity, ctlCc1, ctlCc11, ctlNumModes };

struct TonalSettings {
    int direction = dirUp;
    int pitchSet = psChromatic;
    int tonic = 0;                  // 0..11, pitch class of the first step
    int interval = 1;               // steps of the set per step of the loop (1..5, chromatic also 7)
    int low = 24, high = 108;       // range, MIDI notes (C1..C8)
    int center = -1;                // centre of the bell, -1 = middle of the range
    double widthOct = 0;            // width of the bell in octaves, 0 = the whole range
    int shape = bellCosine;
    int articulation = artStaccato;
    int gatePct = 0;                // 0 = default for the articulation (60 / 140), else 10..200
    int spread = spreadTogether;
    int spreadPct = 50;             // share of the step the voices are spread over
    int vmin = 12, vmax = 120;
    double gamma = 1.0;             // velocity curve: v = vmin + (vmax - vmin) * a^gamma
    int threshold = 6;              // notes with a lower velocity are dropped
    int jitter = 0;                 // +- velocity, seeded
    int seed = 1;
    int output = outSingle;
    int registerTracks = 3;         // 2..4 tracks for outRegister, split evenly over the range
    bool cc11 = false;              // volume by CC11 on one track per octave, velocity constant
    int ccVelocity = 96;
    int lengthBars = 8;             // length of the region
    int cycles = 2;                 // loops inside the region; 0 = one free run with a fixed step
    int stepTicks = 240;            // step length when cycles == 0
    int layout = layoutChord;
    int control = ctlVelocity;      // voices layout: the bell goes to CC1 or CC11 (velocity = notes keep their own velocity)
    bool glide = true;              // legato voices: pitch bend hides the step changes (needs a bend range on the instrument)
    double bendRange = 0.5;         // semitones the instrument bends for a full pitch wheel throw
    int glideShare = 100;           // glide width in percent of one step
    bool edgeFade = true;           // CC1 control: CC11 closes the voice at the edges of the range so the wrap is silent
};

int pitchSetSize(int set);
const int* pitchSetDegrees(int set);
const char* pitchSetName(int set);
// steps of the loop until the set of pitches repeats
int tonalStepsPerCycle(const TonalSettings& s);
// pitch class that the step plays (0..11)
int tonalPitchClass(const TonalSettings& s, int step);
// amplitude of a pitch under the bell, 0..1
double tonalBell(const TonalSettings& s, int pitch);
double tonalBellF(const TonalSettings& s, double pitch);
// voices layout: one voice per octave of the range, 2..8
int tonalVoiceCount(const TonalSettings& s);
// ticks of one full loop at the step length (voices: until every voice is back on its own line)
long long tonalCycleTicks(const TonalSettings& s);
// region length in bars rounded up so that whole loops fit exactly; the request itself when that is impossible
int tonalWholeCycleBars(const TonalSettings& s, const Transport& tr, int requestedBars);
Score buildTonal(const TonalSettings& s, const Transport& tr);

// ------------------------------------------------------------------ tempo
enum Method { methodSmooth, methodStepped };
enum Dir2 { tempoAccel, tempoRit };
enum StartMode { startAuto, startExact, startAligned };
enum LengthMode { lenProportional, lenFixed };
enum TempoOutput { toSingle, toPerLayer };

struct TempoSettings {
    int method = methodSmooth;
    int stages = 4;                 // stepped: stages per doubling (2, 3, 4, 6, 8)
    int direction = tempoAccel;
    int layers = 4;                 // N, 2..8 volume levels
    std::vector<uint8_t> grid = {3, 2, 2, 2};   // one bar, levels 0 rest, 1 soft, 2 normal, 3 accent
    int note = 60;
    int startMode = startAuto;
    int lengthBars = 8;
    int cycles = 2;                 // loops inside the region; 0 = a fixed cycle length from cycleBars
    int cycleBars = 4;
    int vmin = 8, vmax = 122;
    double gamma = 1.0;
    double ampFloor = 0.03;         // events under this level are dropped
    int lengthMode = lenFixed;
    int gatePct = 50;               // proportional: share of the interval to the next hit
    int fixedTicks = 40;            // fixed: note length in ticks
    int maxPerSec = 0;              // 0 = off; hits closer than 1/maxPerSec seconds (at the host tempo) are dropped
    int output = toSingle;
};

struct TempoPreset { const char* name; std::vector<uint8_t> grid; };
const std::vector<TempoPreset>& tempoPresets();
Score buildTempo(const TempoSettings& s, const Transport& tr);
// cycle constants, for tests and the editor: C (pattern repeats of layer 0 per cycle) and rho (start speed)
struct TempoCycle { double cycleBars, C, rho; int stages; };
TempoCycle tempoCycle(const TempoSettings& s);

// ------------------------------------------------------------------ MIDI
std::vector<unsigned char> writeMidi(const Score& sc);
struct ReadTrack { std::string name; std::vector<Note> notes; std::vector<Cc> ccs; };
struct ReadResult { int ppq = 0, num = 4, den = 4; double bpm = 120; std::vector<ReadTrack> tracks; };
bool readMidi(const unsigned char* data, size_t size, ReadResult& out);

// deterministic random for seeded details
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return n <= 1 ? 0 : (int)(next() % (uint64_t)n); }
};

}  // namespace shp
