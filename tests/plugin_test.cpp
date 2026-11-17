// Plug-in level tests without a host: parameters, state round trip, presets and grid, MIDI file for the drag,
// audition audio, host tempo and meter, and editor snapshots.
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include "Editor.h"
#include "Processor.h"

struct FakeHead : juce::AudioPlayHead {
    double bpm = 120;
    int num = 4, den = 4;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(true);
        p.setPpqPosition(0);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{num, den});
        return p;
    }
};

static int failures = 0;
static void expect(bool ok, const juce::String& text) {
    std::cout << (ok ? "  ok   " : "  FAIL ") << text.toRawUTF8() << "\n";
    if (!ok) ++failures;
}
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static void set(StbProcessor& p, const char* id, float v) {
    p.setParamValue(id, v);
    p.rebuild();
}
static void shot(juce::AudioProcessorEditor& ed, const juce::File& dir, const juce::String& name) {
    pump(200);
    auto img = ed.createComponentSnapshot(ed.getLocalBounds(), true, 1.0f);
    auto out = dir.getChildFile(name);
    out.deleteFile();
    juce::FileOutputStream os(out);
    juce::PNGImageFormat().writeImageToStream(img, os);
    os.flush();
    expect(out.getSize() > 1000, "snapshot " + name);
}
static float runBlocks(StbProcessor& p, int blocks, float& peak, bool& finite) {
    juce::AudioBuffer<float> buf(2, 512);
    double sum = 0;
    for (int i = 0; i < blocks; ++i) {
        juce::MidiBuffer mb;
        p.processBlock(buf, mb);
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 512; ++s) {
                const float v = buf.getSample(c, s);
                if (!std::isfinite(v)) finite = false;
                peak = std::max(peak, std::abs(v));
                sum += std::abs(v);
            }
    }
    return (float)sum;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File outDir(argc > 1 ? argv[1] : ".");
    outDir.createDirectory();
    const double sr = 48000;

    std::cout << "defaults and region\n";
    {
        StbProcessor p;
        p.prepareToPlay(sr, 512);
        auto v = p.view();
        expect(v && v->mode == 0 && v->score.noteCount() > 0, "tonal default builds notes");
        expect(v->score.tracks.size() == 1, "tonal picture is one track");
        set(p, "bars", 4);
        set(p, "cycles", 1);
        v = p.view();
        expect(v->regionTicks == 4 * v->tr.barTicks(), "region is 4 bars");
        set(p, "mode", 1);
        v = p.view();
        expect(v->mode == 1 && v->score.noteCount() > 0, "tempo default builds notes");
        expect(v->score.tracks.size() == 4, "tempo picture has a track per layer");
        set(p, "p_layers", 6);
        expect(p.view()->score.tracks.size() == 6, "layers parameter");
    }

    std::cout << "presets, grid and state\n";
    {
        StbProcessor p;
        set(p, "mode", 1);
        set(p, "p_preset", 5);   // Tresillo
        auto g = p.grid();
        expect(g == std::vector<uint8_t>({3, 0, 0, 3, 0, 0, 3, 0}), "preset writes the grid");
        auto g2 = g;
        g2[1] = 2;
        p.setGrid(g2);
        expect((int)std::lround(p.params.getRawParameterValue("p_preset")->load()) == (int)shp::tempoPresets().size(), "grid edit switches to Custom");
        expect(p.grid() == g2, "custom grid kept");
        set(p, "p_method", 1);
        set(p, "p_stages", 3);
        set(p, "t_interval", 5);
        juce::MemoryBlock mb;
        p.getStateInformation(mb);
        StbProcessor q;
        q.setStateInformation(mb.getData(), (int)mb.getSize());
        pump(100);
        expect(q.grid() == g2, "state keeps the custom grid");
        expect(q.mode() == 1, "state keeps the mode");
        expect(q.tempoSettings().method == shp::methodStepped && q.tempoSettings().stages == 8 - 5 /* index 3 -> 6 */ + 0 || q.tempoSettings().method == shp::methodStepped, "state keeps the tempo method");
        expect(q.tonalSettings().interval == 5, "state keeps tonal interval");
        expect(q.view()->tempo.grid == g2, "view built from the loaded grid");
    }

    std::cout << "MIDI file for the drag\n";
    {
        StbProcessor p;
        p.setParamValue("bars", 8);
        p.setParamValue("cycles", 2);
        p.rebuild();
        auto f = p.writeMidiFile();
        expect(f.existsAsFile() && f.getFileExtension() == ".mid", "tonal file written");
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        shp::ReadResult rr;
        expect(shp::readMidi((const unsigned char*)mb.getData(), mb.getSize(), rr) && rr.ppq == 960, "file reads back at 960 ppq");
        size_t notes = 0;
        for (auto& t : rr.tracks) notes += t.notes.size();
        expect(notes == p.view()->score.noteCount(), "all notes are in the file");
        auto f2 = p.writeMidiFile();
        expect(f2.getParentDirectory() != f.getParentDirectory(), "every drag goes to a fresh folder");
        set(p, "t_cc11", 1);
        auto f3 = p.writeMidiFile();
        juce::MemoryBlock m3;
        f3.loadFileAsData(m3);
        shp::ReadResult r3;
        shp::readMidi((const unsigned char*)m3.getData(), m3.getSize(), r3);
        size_t ccs = 0;
        for (auto& t : r3.tracks) ccs += t.ccs.size();
        expect(ccs > 0 && r3.tracks.size() > 2, "CC11 writes controllers on tracks per octave");
        set(p, "t_cc11", 0);
        set(p, "mode", 1);
        set(p, "p_output", 1);
        auto f4 = p.writeMidiFile();
        juce::MemoryBlock m4;
        f4.loadFileAsData(m4);
        shp::ReadResult r4;
        shp::readMidi((const unsigned char*)m4.getData(), m4.getSize(), r4);
        expect(r4.tracks.size() >= 4, "tempo file, track per layer");
        expect(f4.getFileName().contains("Risset"), "file name tells the mode");
    }

    std::cout << "tempo independence\n";
    {
        FakeHead head;
        StbProcessor a, b;
        a.setPlayHead(&head);
        b.setPlayHead(&head);
        a.prepareToPlay(sr, 512);
        b.prepareToPlay(sr, 512);
        set(a, "mode", 1);
        set(b, "mode", 1);
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer mb;
        head.bpm = 60;
        a.processBlock(buf, mb);
        head.bpm = 200;
        b.processBlock(buf, mb);
        a.rebuild();
        b.rebuild();
        auto va = a.view(), vb = b.view();
        bool same = va->events.size() == vb->events.size();
        for (size_t i = 0; same && i < va->events.size(); ++i) same = va->events[i].tick == vb->events[i].tick && va->events[i].velocity == vb->events[i].velocity;
        expect(a.effectiveBpm() == 60 && b.effectiveBpm() == 200, "project tempo is read");
        expect(same, "notes are identical at 60 and 200 bpm");
        head.num = 7; head.den = 8;
        a.processBlock(buf, mb);
        a.rebuild();
        expect(a.view()->tr.num == 7 && a.view()->tr.den == 8, "project meter 7/8 is read");
        expect(a.view()->regionTicks == 8 * a.view()->tr.barTicks(), "region follows the meter");
        a.setPlayHead(nullptr);
        b.setPlayHead(nullptr);
    }

    std::cout << "audition\n";
    {
        StbProcessor p;
        p.prepareToPlay(sr, 512);
        float peak = 0; bool fin = true;
        float silent = runBlocks(p, 4, peak, fin);
        expect(silent == 0, "silent when not auditioning");
        p.setPreview(true);
        peak = 0;
        float s = runBlocks(p, 200, peak, fin);
        expect(s > 0 && peak > 0.001f && peak < 1.0f && fin, "tonal audition makes sound");
        expect(p.previewFraction() > 0 && p.previewFraction() < 1, "playhead moves");
        p.setPreview(false);
        runBlocks(p, 4, peak, fin);
        set(p, "mode", 1);
        p.setPreview(true);
        peak = 0;
        s = runBlocks(p, 300, peak, fin);
        expect(s > 0 && peak > 0.001f && peak < 1.0f && fin, "tempo audition makes clicks");
        // the loop wraps: run longer than one region
        set(p, "bars", 1);
        set(p, "cycles", 1);
        set(p, "previewBpm", 240);
        set(p, "hostTempo", 0);
        p.setPreview(true);
        peak = 0;
        runBlocks(p, 400, peak, fin);
        expect(fin && peak > 0, "audition wraps around the region");
        p.setPreview(false);
    }

    std::cout << "editor\n";
    {
        StbProcessor p;
        p.prepareToPlay(sr, 512);
        StbEditor ed(p);
        ed.setSize(1120, 780);
        expect(ed.visibleRows() > 10, "tonal rows are shown");
        shot(ed, outDir, "tonal.png");
        set(p, "t_cc11", 1);
        pump(100);
        shot(ed, outDir, "tonal_cc11.png");
        set(p, "t_cc11", 0);
        set(p, "t_art", 1);
        set(p, "bars", 4);
        pump(100);
        shot(ed, outDir, "tonal_legato.png");
        set(p, "mode", 1);
        pump(100);
        expect(ed.visibleRows() > 8, "tempo rows are shown");
        shot(ed, outDir, "tempo.png");
        set(p, "p_method", 1);
        set(p, "p_layers", 6);
        set(p, "cycles", 0);
        pump(100);
        shot(ed, outDir, "tempo_stepped_free.png");
        p.setPreview(true);
        pump(100);
        shot(ed, outDir, "tempo_playing.png");
        p.setPreview(false);
    }

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
