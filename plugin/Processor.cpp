#include "Processor.h"
#include "Editor.h"
#include <algorithm>
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

namespace {
const int kStepTicks[] = {960, 480, 320, 240, 160, 120, 80, 60};
const int kStages[] = {2, 3, 4, 6, 8};
}  // namespace

int stbStepTicks(int index) { return kStepTicks[std::clamp(index, 0, 7)]; }
int stbStages(int index) { return kStages[std::clamp(index, 0, 4)]; }

APVTS::ParameterLayout StbProcessor::makeLayout() {
    using namespace juce;
    APVTS::ParameterLayout l;
    auto choice = [&](const char* id, const char* name, StringArray items, int def) {
        l.add(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def));
    };
    auto integer = [&](const char* id, const char* name, int lo, int hi, int def) {
        l.add(std::make_unique<AudioParameterInt>(ParameterID{id, 1}, name, lo, hi, def));
    };
    auto real = [&](const char* id, const char* name, float lo, float hi, float step, float def) {
        l.add(std::make_unique<AudioParameterFloat>(ParameterID{id, 1}, name, NormalisableRange<float>(lo, hi, step), def));
    };
    auto flag = [&](const char* id, const char* name, bool def) {
        l.add(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, name, def));
    };
    // ids never change once released, choices are only appended
    choice("mode", "Mode", {"Tonal", "Tempo"}, 0);
    integer("bars", "Region bars", 1, shp::kMaxBars, 8);
    integer("cycles", "Cycles", 0, 64, 2);
    integer("cycleBars", "Cycle bars", 1, 64, 4);
    choice("stepLen", "Step length", {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"}, 1);
    integer("previewBpm", "Preview tempo", 40, 240, 100);
    flag("hostTempo", "Project tempo", true);
    flag("hostMeter", "Project meter", true);
    integer("meterNum", "Meter numerator", 1, 32, 4);
    choice("meterDen", "Meter denominator", {"2", "4", "8", "16"}, 1);

    // tonal
    choice("t_dir", "Direction", {"Up", "Down"}, 0);
    {
        StringArray sets;
        for (int i = 0; i < shp::psNumSets; ++i) sets.add(shp::pitchSetName(i));
        choice("t_set", "Pitch set", sets, 0);
    }
    integer("t_tonic", "Tonic", 0, 11, 0);
    integer("t_interval", "Interval", 1, 7, 1);
    integer("t_low", "Low note", 12, 120, 48);
    integer("t_high", "High note", 12, 120, 96);
    integer("t_center", "Bell centre", 0, 120, 0);
    real("t_width", "Bell width", 0.0f, 6.0f, 0.5f, 0.0f);
    choice("t_shape", "Bell shape", {"Cosine", "Gauss"}, 0);
    choice("t_art", "Articulation", {"Staccato", "Legato"}, 1);
    integer("t_gate", "Gate", 0, 200, 0);
    choice("t_spread", "Spread", {"Together", "Up", "Down", "Random"}, 0);
    integer("t_spreadPct", "Spread share", 0, 100, 50);
    integer("t_vmin", "Velocity min", 1, 127, 20);
    integer("t_vmax", "Velocity max", 1, 127, 115);
    real("t_gamma", "Velocity curve", 0.3f, 3.0f, 0.05f, 1.0f);
    integer("t_thr", "Velocity threshold", 0, 60, 0);
    integer("t_jitter", "Velocity jitter", 0, 30, 0);
    integer("t_seed", "Seed", 1, 99999, 1);
    choice("t_output", "Output", {"One track", "By register", "By octave"}, 0);
    integer("t_regTracks", "Register tracks", 2, 4, 3);
    flag("t_cc11", "CC11", false);
    integer("t_ccVel", "CC11 velocity", 1, 127, 96);
    // added in 0.2
    choice("t_profile", "Profile", {"Synth", "Strings legato CC1", "Strings legato CC11", "Custom"}, 1);
    choice("t_layout", "Layout", {"Chord per step", "Voice lines"}, 1);
    choice("t_ctl", "Dynamics by", {"Velocity", "CC1", "CC11"}, 1);
    flag("t_edge", "Silent edges (CC11)", true);
    flag("t_whole", "Whole loops", true);

    // tempo
    choice("p_method", "Method", {"Smooth", "Stepped"}, 0);
    choice("p_stages", "Stages per doubling", {"2", "3", "4", "6", "8"}, 2);
    choice("p_dir", "Direction", {"Accelerando", "Ritardando"}, 0);
    integer("p_layers", "Layers", 2, 8, 4);
    {
        StringArray pr;
        for (auto& p : shp::tempoPresets()) pr.add(p.name);
        pr.add("Custom");
        choice("p_preset", "Pattern", pr, 0);
    }
    integer("p_note", "Note", 0, 127, 60);
    choice("p_start", "Start", {"Auto", "Exact", "Aligned"}, 0);
    integer("p_vmin", "Velocity min", 1, 127, 8);
    integer("p_vmax", "Velocity max", 1, 127, 122);
    real("p_gamma", "Velocity curve", 0.3f, 3.0f, 0.05f, 1.0f);
    real("p_floor", "Level floor", 0.0f, 0.2f, 0.005f, 0.03f);
    choice("p_lenMode", "Note length", {"Proportional", "Fixed"}, 1);
    integer("p_gate", "Gate", 10, 100, 50);
    integer("p_ticks", "Fixed ticks", 5, 480, 40);
    integer("p_maxPerSec", "Max hits per second", 0, 60, 0);
    choice("p_output", "Output", {"One track", "Track per layer"}, 0);
    return l;
}

StbProcessor::StbProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "STATE", makeLayout()) {
    for (auto* p : getParameters())
        if (auto* pp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.addParameterListener(pp->paramID, this);
    params.state.setProperty("grid", "3222", nullptr);
    lastPreset_ = 0;
    lastProfile_ = 1;
    for (int i = 0; i < kTracks; ++i) gain1_[i] = gain11_[i] = cur_[i] = 1.0f;
    rebuild();
}

StbProcessor::~StbProcessor() {
    cancelPendingUpdate();
    for (auto* p : getParameters())
        if (auto* pp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.removeParameterListener(pp->paramID, this);
}

static int iv(const APVTS& a, const char* id) { return (int)std::lround(a.getRawParameterValue(id)->load()); }
static double fv(const APVTS& a, const char* id) { return a.getRawParameterValue(id)->load(); }

std::vector<uint8_t> StbProcessor::grid() const {
    std::vector<uint8_t> g;
    for (auto c : params.state.getProperty("grid").toString())
        if (c >= '0' && c <= '3') g.push_back((uint8_t)(c - '0'));
    if (g.empty()) g = {3};
    if (g.size() > 32) g.resize(32);
    return g;
}

void StbProcessor::setGrid(const std::vector<uint8_t>& g, bool fromPreset) {
    juce::String s;
    for (auto v : g) s << (int)std::min<uint8_t>(v, 3);
    params.state.setProperty("grid", s.isEmpty() ? juce::String("3") : s, nullptr);
    if (!fromPreset) {
        lastPreset_ = (int)shp::tempoPresets().size();
        setParamValue("p_preset", (float)lastPreset_);
    }
    rebuild();
}

shp::TonalSettings StbProcessor::tonalSettings() const {
    shp::TonalSettings s;
    s.direction = iv(params, "t_dir");
    s.pitchSet = iv(params, "t_set");
    s.tonic = iv(params, "t_tonic");
    s.interval = iv(params, "t_interval");
    s.low = iv(params, "t_low");
    s.high = iv(params, "t_high");
    if (s.high < s.low) std::swap(s.low, s.high);
    s.center = iv(params, "t_center") == 0 ? -1 : iv(params, "t_center");
    s.widthOct = fv(params, "t_width");
    s.shape = iv(params, "t_shape");
    s.articulation = iv(params, "t_art");
    s.gatePct = iv(params, "t_gate");
    s.spread = iv(params, "t_spread");
    s.spreadPct = iv(params, "t_spreadPct");
    s.vmin = iv(params, "t_vmin");
    s.vmax = iv(params, "t_vmax");
    s.gamma = fv(params, "t_gamma");
    s.threshold = iv(params, "t_thr");
    s.jitter = iv(params, "t_jitter");
    s.seed = iv(params, "t_seed");
    s.output = iv(params, "t_output");
    s.registerTracks = iv(params, "t_regTracks");
    s.cc11 = iv(params, "t_cc11") != 0;
    s.ccVelocity = iv(params, "t_ccVel");
    s.cycles = 0;                                  // the step length decides, loops are counted from it
    s.stepTicks = stbStepTicks(iv(params, "stepLen"));
    s.layout = iv(params, "t_layout");
    s.control = iv(params, "t_ctl");
    s.edgeFade = iv(params, "t_edge") != 0;
    s.lengthBars = iv(params, "bars");
    if (iv(params, "t_whole") != 0) s.lengthBars = shp::tonalWholeCycleBars(s, transport(), s.lengthBars);
    return s;
}

shp::TempoSettings StbProcessor::tempoSettings() const {
    shp::TempoSettings s;
    s.method = iv(params, "p_method");
    s.stages = stbStages(iv(params, "p_stages"));
    s.direction = iv(params, "p_dir");
    s.layers = iv(params, "p_layers");
    s.grid = grid();
    s.note = iv(params, "p_note");
    s.startMode = iv(params, "p_start");
    s.lengthBars = iv(params, "bars");
    s.cycles = iv(params, "cycles");
    s.cycleBars = iv(params, "cycleBars");
    s.vmin = iv(params, "p_vmin");
    s.vmax = iv(params, "p_vmax");
    s.gamma = fv(params, "p_gamma");
    s.ampFloor = fv(params, "p_floor");
    s.lengthMode = iv(params, "p_lenMode");
    s.gatePct = iv(params, "p_gate");
    s.fixedTicks = iv(params, "p_ticks");
    s.maxPerSec = iv(params, "p_maxPerSec");
    s.output = iv(params, "p_output");
    return s;
}

double StbProcessor::effectiveBpm() const {
    if (iv(params, "hostTempo") != 0) {
        const double b = hostBpm_.load();
        if (b > 20) return b;
    }
    return fv(params, "previewBpm");
}

shp::Transport StbProcessor::transport() const {
    shp::Transport t;
    t.bpm = effectiveBpm();
    int n = iv(params, "meterNum"), d = 2 << iv(params, "meterDen");
    if (iv(params, "hostMeter") != 0 && hostNum_.load() > 0 && hostDen_.load() > 0) { n = hostNum_.load(); d = hostDen_.load(); }
    t.num = n;
    t.den = d;
    return t;
}

void StbProcessor::setParamValue(const char* id, float value) {
    if (auto* p = params.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(value));
}

void StbProcessor::handleAsyncUpdate() { rebuild(); }

namespace {
struct Profile { int layout, ctl, art, gate, vmin, vmax; double gamma; int thr, edge; };
const Profile kProfiles[3] = {
    {0, 0, 0, 0, 12, 120, 1.0, 6, 1},     // Synth: a chord of octaves per step, velocity
    {1, 1, 1, 0, 20, 115, 1.0, 0, 1},     // Strings legato, mod wheel (CC1), CC11 closes the edges
    {1, 2, 1, 0, 1, 115, 1.0, 0, 0},      // Strings legato, expression (CC11) carries the bell
};
}  // namespace

bool StbProcessor::matchesProfile(int profile) const {
    if (profile < 0 || profile > 2) return true;
    const auto& p = kProfiles[profile];
    return iv(params, "t_layout") == p.layout && iv(params, "t_ctl") == p.ctl && iv(params, "t_art") == p.art &&
           iv(params, "t_gate") == p.gate && iv(params, "t_vmin") == p.vmin && iv(params, "t_vmax") == p.vmax &&
           std::fabs(fv(params, "t_gamma") - p.gamma) < 0.026 && iv(params, "t_thr") == p.thr && iv(params, "t_edge") == p.edge;
}

void StbProcessor::applyProfile(int profile) {
    if (profile < 0 || profile > 2) return;
    const auto& p = kProfiles[profile];
    applying_ = true;
    setParamValue("t_layout", (float)p.layout);
    setParamValue("t_ctl", (float)p.ctl);
    setParamValue("t_art", (float)p.art);
    setParamValue("t_gate", (float)p.gate);
    setParamValue("t_vmin", (float)p.vmin);
    setParamValue("t_vmax", (float)p.vmax);
    setParamValue("t_gamma", (float)p.gamma);
    setParamValue("t_thr", (float)p.thr);
    setParamValue("t_edge", (float)p.edge);
    setParamValue("t_cc11", 0.0f);
    setParamValue("t_output", 0.0f);
    setParamValue("t_spread", 0.0f);
    setParamValue("t_jitter", 0.0f);
    applying_ = false;
}

void StbProcessor::rebuild() {
    // a pattern preset overwrites the grid only when the choice itself was changed
    const int preset = iv(params, "p_preset");
    const auto& presets = shp::tempoPresets();
    if (preset != lastPreset_) {
        lastPreset_ = preset;
        if (preset < (int)presets.size()) {
            juce::String s;
            for (auto v : presets[(size_t)preset].grid) s << (int)v;
            params.state.setProperty("grid", s, nullptr);
        }
    }
    // the same for the tonal profile; editing a value behind it turns the profile into "Custom"
    if (!applying_) {
        const int profile = iv(params, "t_profile");
        if (profile != lastProfile_) {
            lastProfile_ = profile;
            applyProfile(profile);
        } else if (profile < 3 && !matchesProfile(profile)) {
            lastProfile_ = 3;
            setParamValue("t_profile", 3.0f);
        }
    }
    auto v = std::make_shared<View>();
    v->mode = mode();
    v->tr = transport();
    v->tonal = tonalSettings();
    v->tempo = tempoSettings();
    if (v->mode == 0) {
        auto ts = v->tonal;
        if (ts.layout == shp::layoutChord) { ts.output = shp::outSingle; ts.cc11 = false; }
        v->score = shp::buildTonal(ts, v->tr);
        v->cycles = 0;
        v->voices = ts.layout == shp::layoutVoices ? shp::tonalVoiceCount(ts) : 0;
        v->cycleTicks = shp::tonalCycleTicks(ts);
        v->bars = ts.lengthBars;
        v->closed = v->cycleTicks > 0 && ((long long)ts.lengthBars * v->tr.barTicks()) % v->cycleTicks == 0;
    } else {
        auto ps = v->tempo;
        ps.output = shp::toPerLayer;
        v->score = shp::buildTempo(ps, v->tr);
        v->cycles = ps.cycles;
    }
    v->regionTicks = std::max<long long>(1, v->score.lengthTicks);
    int layer = 0;
    for (auto& t : v->score.tracks) {
        // level of the voice at a tick: the controllers that are in force, or the velocity
        std::vector<std::pair<long long, int>> c1, c11;
        for (auto& c : t.ccs) (c.cc == 1 ? c1 : c11).push_back({c.tick, c.value});
        auto at = [](const std::vector<std::pair<long long, int>>& v, long long tick, int dflt) {
            auto it = std::upper_bound(v.begin(), v.end(), std::make_pair(tick, 1000));
            return it == v.begin() ? dflt : std::prev(it)->second;
        };
        const bool has1 = !c1.empty(), has11 = !c11.empty();
        for (auto& n : t.notes) {
            PreviewEvent e{n.tick, n.length, n.note, n.velocity, layer, 0, 1.0f};
            if (has1 || has11) {
                const long long mid = n.tick + n.length / 2;
                const float a = has1 ? std::pow(at(c1, mid, 127) / 127.0f, 1.5f) : 1.0f;
                const float b = has11 ? (float)at(c11, mid, 127) / 127.0f : 1.0f;
                e.level = a * b;
            } else {
                e.level = (float)n.velocity / 127.0f;
            }
            v->events.push_back(e);
        }
        for (auto& c : t.ccs) v->events.push_back({c.tick, 0, c.cc, c.value, layer, 1, 1.0f});
        ++layer;
    }
    std::stable_sort(v->events.begin(), v->events.end(), [](const PreviewEvent& a, const PreviewEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.kind > b.kind;           // controllers first, so a note starts at the level that belongs to it
    });
    std::atomic_store(&view_, std::shared_ptr<const View>(v));
    if (onChanged) onChanged();
}

juce::String StbProcessor::fileBaseName() const {
    juce::String s;
    if (mode() == 0) {
        auto t = tonalSettings();
        s = juce::String("Shepard tonal ") + (t.direction == shp::dirUp ? "up " : "down ") + juce::String(t.lengthBars) + " bars";
        if (t.layout == shp::layoutVoices) s += t.control == shp::ctlCc11 ? " voices CC11" : t.control == shp::ctlCc1 ? " voices CC1" : " voices";
        else if (t.cc11) s += " CC11";
    } else {
        auto t = tempoSettings();
        s = juce::String("Risset ") + (t.method == shp::methodSmooth ? "smooth " : "stepped ") +
            (t.direction == shp::tempoAccel ? "accel " : "rit ") + juce::String(t.lengthBars) + " bars";
    }
    return s;
}

static std::vector<unsigned char> buildBytes(const StbProcessor& p) {
    const auto tr = p.transport();
    if (p.mode() == 0) return shp::writeMidi(shp::buildTonal(p.tonalSettings(), tr));
    return shp::writeMidi(shp::buildTempo(p.tempoSettings(), tr));
}

juce::File StbProcessor::writeMidiFile() const {
    const auto bytes = buildBytes(*this);
    // every drag goes to a fresh folder, so the host never sees a file name twice
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ShepardToneBuilder")
                   .getChildFile("drag" + juce::String(juce::Time::currentTimeMillis() % 100000000) + "_" + juce::String(++dragCounter_));
    dir.createDirectory();
    auto f = dir.getChildFile(fileBaseName() + ".mid");
    f.replaceWithData(bytes.data(), bytes.size());
    return f;
}

bool StbProcessor::saveMidiTo(const juce::File& f) const {
    const auto bytes = buildBytes(*this);
    return f.replaceWithData(bytes.data(), bytes.size());
}

void StbProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto xml = std::make_unique<juce::XmlElement>("ShepardToneBuilder");
    xml->addChildElement(params.copyState().createXml().release());
    copyXmlToBinary(*xml, dest);
}

void StbProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("ShepardToneBuilder")) return;
    if (auto* st = xml->getChildByName(params.state.getType())) {
        params.replaceState(juce::ValueTree::fromXml(*st));
        if (!params.state.hasProperty("grid")) params.state.setProperty("grid", "3222", nullptr);
        lastPreset_ = iv(params, "p_preset");
        lastProfile_ = iv(params, "t_profile");
    }
    rebuild();
}

juce::AudioProcessorEditor* StbProcessor::createEditor() { return new StbEditor(*this); }

// ---------------------------------------------------------------- audition
void StbProcessor::setPreview(bool on) {
    if (on) restart_ = true;
    previewOn_ = on;
}

void StbProcessor::prepareToPlay(double sr, int) {
    sampleRate_ = sr;
    for (auto& v : voices_) v.on = false;
    for (int i = 0; i < kTracks; ++i) gain1_[i] = gain11_[i] = cur_[i] = 1.0f;
}

void StbProcessor::startVoice(const PreviewEvent& e, bool click, double samplesPerTick) {
    Voice* v = &voices_[0];
    for (auto& c : voices_) {
        if (!c.on) { v = &c; break; }
        if (c.age < v->age) v = &c;
    }
    v->on = true;
    v->click = click;
    v->age = ++voiceAge_;
    v->phase = 0;
    v->env = 0;
    v->held = true;
    v->track = std::clamp(e.layer, 0, kTracks - 1);
    const float vel = (float)e.velocity / 127.0f;
    if (click) {
        static const double freq[8] = {1100, 700, 1700, 2500, 900, 1300, 2000, 3000};
        v->inc = 2.0 * juce::MathConstants<double>::pi * freq[e.layer & 7] / sampleRate_;
        v->env = 1.0f;
        v->amp = 0.5f * std::pow(vel, 1.5f);
        v->decay = (float)std::exp(-1.0 / (0.018 * sampleRate_));
        v->left = 0;
    } else {
        const double hz = 440.0 * std::pow(2.0, (e.note - 69) / 12.0);
        v->inc = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate_;
        v->amp = 0.07f * std::pow(vel, 1.5f);
        v->left = std::max<long long>(32, (long long)(e.length * samplesPerTick));
    }
}

void StbProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    midi.clear();
    buf.clear();
    const int n = buf.getNumSamples();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) hostBpm_ = *b;
            if (auto ts = pos->getTimeSignature()) { hostNum_ = ts->numerator; hostDen_ = ts->denominator; }
        }

    auto vw = view();
    struct Hit { int at; PreviewEvent e; };
    Hit hits[512];
    int nh = 0;
    double spt = 0;
    const bool click = vw && vw->mode == 1;
    if (previewOn_.load() && vw && !vw->events.empty()) {
        if (restart_.exchange(false)) {
            tick_ = 0;
            for (int i = 0; i < kTracks; ++i) { gain1_[i] = gain11_[i] = 1.0f; cur_[i] = 0.0f; }
        }
        const double tps = effectiveBpm() * shp::kTpb / (60.0 * sampleRate_);   // ticks per sample
        spt = 1.0 / tps;
        const double t0 = tick_, t1 = tick_ + tps * n, len = (double)vw->regionTicks;
        auto collect = [&](double a, double b, double offset) {
            auto it = std::lower_bound(vw->events.begin(), vw->events.end(), (long long)std::ceil(a),
                                       [](const PreviewEvent& e, long long t) { return e.tick < t; });
            for (; it != vw->events.end() && (double)it->tick < b && nh < 512; ++it)
                hits[nh++] = {(int)(((double)it->tick - a + offset) / tps), *it};
        };
        if (t1 < len) collect(t0, t1, 0);
        else { collect(t0, len, 0); collect(0, t1 - len, len - t0); }
        tick_ = std::fmod(t1, len);
        previewPos_ = tick_ / len;
    } else if (!previewOn_.load()) {
        tick_ = 0;
        previewPos_ = 0;
    }

    auto* l = buf.getWritePointer(0);
    auto* r = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
    int hi = 0;
    for (int i = 0; i < n; ++i) {
        while (hi < nh && hits[hi].at <= i) {
            const auto& e = hits[hi].e;
            if (e.kind == 1) {
                const int tr = std::clamp(e.layer, 0, kTracks - 1);
                if (e.note == 1) gain1_[tr] = std::pow((float)e.velocity / 127.0f, 1.5f);
                else if (e.note == 11) gain11_[tr] = (float)e.velocity / 127.0f;
            } else {
                startVoice(e, click, spt);
            }
            ++hi;
        }
        for (int t = 0; t < kTracks; ++t) cur_[t] += (gain1_[t] * gain11_[t] - cur_[t]) * 0.004f;
        float out = 0;
        for (auto& v : voices_) {
            if (!v.on) continue;
            if (v.click) {
                out += std::sin((float)v.phase) * v.env * v.amp;
                v.env *= v.decay;
                if (v.env < 0.0005f) v.on = false;
            } else {
                if (v.left > 0) { v.env = std::min(1.0f, v.env + 1.0f / 220.0f); --v.left; }
                else { v.env -= 1.0f / 1800.0f; if (v.env <= 0) { v.on = false; continue; } }
                out += std::sin((float)v.phase) * v.env * v.amp * cur_[v.track];
            }
            v.phase += v.inc;
            if (v.phase > 6.283185307179586) v.phase -= 6.283185307179586;
        }
        l[i] = out;
        if (r) r[i] = out;
    }
    if (!previewOn_.load()) for (auto& v : voices_) if (v.left > 0) v.left = 0;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
#ifndef STB_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StbProcessor(); }
#endif
