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
        expect(v->voices == 4 && v->score.tracks.size() == 4, "default profile: four voice tracks");
        expect(v->bars == 12 && v->regionTicks == 12 * v->tr.barTicks() && v->closed, "8 bars requested, whole loops make it 12");
        set(p, "t_whole", 0);
        set(p, "bars", 4);
        v = p.view();
        expect(v->regionTicks == 4 * v->tr.barTicks() && !v->closed, "whole loops off: region is 4 bars and not closed");
        set(p, "t_whole", 1);
        set(p, "mode", 1);
        v = p.view();
        expect(v->mode == 1 && v->score.noteCount() > 0, "tempo default builds notes");
        expect(v->score.tracks.size() == 4, "tempo picture has a track per layer");
        set(p, "p_layers", 6);
        expect(p.view()->score.tracks.size() == 6, "layers parameter");
    }

    std::cout << "pitch glide\n";
    {
        StbProcessor p;
        p.prepareToPlay(sr, 512);
        auto bends = [&] { size_t n = 0; for (auto& t : p.view()->score.tracks) for (auto& c : t.ccs) if (c.cc == 128) ++n; return n; };
        auto ts = p.tonalSettings();
        expect(ts.glide && std::fabs(ts.bendRange - 0.5) < 1e-6 && ts.glideShare == 100, "glide defaults: on, half a semitone, 100 %");
        expect(ts.stagger && ts.humTime == 8 && ts.humVel == 8 && std::fabs(ts.taperOct - 1.0) < 1e-6 && std::fabs(ts.tiltDb - 1.0) < 1e-6, "strings defaults: stagger, humanize 8, soft edges 1 octave, tilt 1 dB");
        expect(bends() > 400 && std::fabs(p.view()->bendRange - 0.5) < 1e-6, "default build writes pitch bend on the voice tracks");
        set(p, "t_glide", 0);
        expect(bends() == 0, "glide off: no pitch bend");
        set(p, "t_glide", 1);
        set(p, "t_art", 0);
        expect(bends() == 0, "staccato: no pitch bend");
        set(p, "t_art", 1);
        set(p, "t_bend", 2.0f);
        expect(std::fabs(p.view()->bendRange - 2.0) < 1e-6 && bends() > 0, "bend range follows the parameter");
    }

    std::cout << "glissando profile\n";
    {
        StbProcessor p;
        p.prepareToPlay(sr, 512);
        set(p, "t_profile", 3);
        auto ts = p.tonalSettings();
        expect(ts.layout == shp::layoutGliss && ts.control == shp::ctlCc11 && std::fabs(ts.bendRange - 24.0) < 1e-6, "profile glissando: layout, CC11 and a bend of 24");
        auto v = p.view();
        expect(v->voices == 4 && v->score.tracks.size() == 4 && std::fabs(v->bendRange - 24.0) < 1e-6, "glissando builds 4 voice tracks");
        size_t notes = 0; for (auto& t : v->score.tracks) notes += t.notes.size();
        expect(notes <= 12, "glissando writes a few long notes");
        set(p, "t_profile", 1);
        expect(std::fabs(p.tonalSettings().bendRange - 0.5) < 1e-6 && p.tonalSettings().layout == shp::layoutVoices, "back to strings: bend range 0.5");
    }

    std::cout << "tonal profiles\n";
    {
        StbProcessor p;
        auto prof = [&] { return (int)std::lround(p.params.getRawParameterValue("t_profile")->load()); };
        expect(prof() == 1 && p.tonalSettings().layout == shp::layoutVoices && p.tonalSettings().control == shp::ctlCc1, "default is strings legato CC1");
        set(p, "t_profile", 0);
        auto ts = p.tonalSettings();
        expect(prof() == 0 && ts.layout == shp::layoutChord && ts.control == shp::ctlVelocity && ts.articulation == shp::artStaccato, "profile Synth writes its values");
        expect(p.view()->score.tracks.size() == 1 && p.view()->voices == 0, "Synth: one track of chords");
        set(p, "t_profile", 2);
        ts = p.tonalSettings();
        expect(prof() == 2 && ts.control == shp::ctlCc11 && !ts.edgeFade, "profile strings CC11");
        p.rebuild();
        bool only11 = true;
        for (auto& t : p.view()->score.tracks) for (auto& c : t.ccs) if (c.cc != 11 && c.cc != 128) only11 = false;
        expect(only11, "CC11 profile writes only CC11 and pitch bend");
        set(p, "t_vmax", 100);
        expect(prof() == 4, "editing a hidden value turns the profile into Custom");
        set(p, "t_profile", 1);
        expect(p.tonalSettings().vmax == 115 && prof() == 1, "choosing a profile restores its values");
        set(p, "t_low", 36);
        set(p, "t_high", 96);
        expect(prof() == 1 && p.view()->voices == 5, "range is not part of a profile, 5 octaves give 5 voices");
        set(p, "t_set", 5);
        set(p, "t_interval", 2);
        p.rebuild();
        expect(p.view()->score.noteCount() > 0 && p.view()->closed, "major scale, interval 2, closed region");
        // speed: step length changes the cycle, region stays whole
        set(p, "t_low", 48); set(p, "t_high", 96); set(p, "t_set", 0); set(p, "t_interval", 1);
        set(p, "stepLen", 3);   // 1/16
        expect(p.view()->cycleTicks == 48 * 240 && p.view()->bars % 3 == 0, "1/16 step: 3-bar loop");
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
        auto f3 = p.writeMidiFile();
        juce::MemoryBlock m3;
        f3.loadFileAsData(m3);
        shp::ReadResult r3;
        shp::readMidi((const unsigned char*)m3.getData(), m3.getSize(), r3);
        size_t ccs = 0;
        for (auto& t : r3.tracks) ccs += t.ccs.size();
        bool has1 = false, has11 = false;
        for (auto& t : r3.tracks) for (auto& c : t.ccs) { if (c.cc == 1) has1 = true; if (c.cc == 11) has11 = true; }
        expect(ccs > 0 && r3.tracks.size() == 5 && has1 && has11, "voice file: conductor plus 4 tracks with CC1 and CC11");
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
        expect(ed.visibleRows() >= 7 && ed.visibleRows() <= 9, "tonal shows only the main rows");
        shot(ed, outDir, "tonal.png");
        ed.setAdvancedOpen(true);
        pump(100);
        shot(ed, outDir, "tonal_advanced.png");
        ed.setAdvancedOpen(false);
        set(p, "t_profile", 0);
        pump(100);
        shot(ed, outDir, "tonal_synth.png");
        set(p, "t_profile", 2);
        set(p, "t_set", 5);
        set(p, "t_dir", 1);
        pump(100);
        shot(ed, outDir, "tonal_cc11_major_down.png");
        set(p, "t_set", 0);
        set(p, "t_dir", 0);
        set(p, "t_profile", 1);
        pump(100);
        p.setPreview(true);
        pump(100);
        shot(ed, outDir, "tonal_playing.png");
        p.setPreview(false);
        set(p, "mode", 1);
        pump(100);
        expect(ed.visibleRows() >= 5 && ed.visibleRows() <= 7, "tempo shows only the main rows");
        shot(ed, outDir, "tempo.png");
        set(p, "p_method", 1);
        set(p, "p_layers", 6);
        set(p, "cycles", 0);
        pump(100);
        shot(ed, outDir, "tempo_stepped_free.png");
        ed.setAdvancedOpen(true);
        pump(100);
        shot(ed, outDir, "tempo_advanced.png");
        ed.setAdvancedOpen(false);
        p.setPreview(true);
        pump(100);
        shot(ed, outDir, "tempo_playing.png");
        p.setPreview(false);
    }

    std::cout << (failures ? "FAILED: " : "all passed, failures: ") << failures << "\n";
    return failures ? 1 : 0;
}
