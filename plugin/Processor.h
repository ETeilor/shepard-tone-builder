#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include "Shepard.h"

// One event of the audition: the note list of the score, flattened and sorted by tick.
// kind 0: a note; kind 1: a controller (note = controller number, velocity = value)
struct PreviewEvent { long long tick; int length, note, velocity, layer; int kind = 0; float level = 1.0f; };

// What the editor draws and the audition plays. Built on the message thread, read from the audio thread.
struct View {
    int mode = 0;                       // 0 tonal, 1 tempo
    shp::Score score;                   // tonal: one track; tempo: one track per layer (for the picture)
    shp::TonalSettings tonal;
    shp::TempoSettings tempo;
    shp::Transport tr;
    long long regionTicks = 0;
    int cycles = 0;
    std::vector<PreviewEvent> events;
    int voices = 0;                     // tonal, voices layout
    long long cycleTicks = 0;           // tonal: one loop at the chosen step
    int bars = 0;                       // tonal: region length after rounding to whole loops
    double bendRange = 0.5;             // semitones of a full pitch wheel throw, for the preview
    bool closed = true;                 // tonal: the region holds whole loops
};

class StbProcessor : public juce::AudioProcessor,
                     private juce::AudioProcessorValueTreeState::Listener,
                     private juce::AsyncUpdater {
public:
    StbProcessor();
    ~StbProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    juce::AudioProcessorValueTreeState params;

    // message thread
    shp::TonalSettings tonalSettings() const;
    shp::TempoSettings tempoSettings() const;
    shp::Transport transport() const;
    std::vector<uint8_t> grid() const;
    void setGrid(const std::vector<uint8_t>& g, bool fromPreset = false);
    int mode() const { return (int)std::lround(params.getRawParameterValue("mode")->load()); }
    void rebuild();
    std::function<void()> onChanged;
    juce::File writeMidiFile() const;                 // current mode, chosen output, a fresh folder every time
    bool saveMidiTo(const juce::File& f) const;
    void setParamValue(const char* id, float value);
    juce::String fileBaseName() const;
    std::shared_ptr<const View> view() const { return std::atomic_load(&view_); }

    // audition
    void setPreview(bool on);
    bool isPreviewing() const { return previewOn_.load(); }
    double previewFraction() const { return previewPos_.load(); }   // 0..1 inside the region
    double effectiveBpm() const;
    // note length steps, stage counts: tables shared with the editor

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Shepard Tone Builder"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

private:
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;

    std::shared_ptr<const View> view_;
    std::atomic<bool> previewOn_{false}, restart_{false};
    std::atomic<double> previewPos_{0};
    std::atomic<int> hostNum_{0}, hostDen_{0};
    std::atomic<double> hostBpm_{0};
    int lastPreset_ = -1, lastProfile_ = -1;
    bool applying_ = false;
    void applyProfile(int profile);
    bool matchesProfile(int profile) const;
    mutable int dragCounter_ = 0;

    // audition voices: a soft sine for the tonal mode, a short click for the tempo mode
    struct Voice { bool on = false, click = false, held = false; double phase = 0, inc = 0; float env = 0, amp = 0, decay = 0; long long left = 0; int age = 0; int track = 0; };
    Voice voices_[64];
    static constexpr int kTracks = 16;
    float gain1_[kTracks], gain11_[kTracks], cur_[kTracks];
    double bendMul_[kTracks];
    double sampleRate_ = 44100;
    double tick_ = 0;
    int voiceAge_ = 0;
    void startVoice(const PreviewEvent& e, bool click, double samplesPerTick);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StbProcessor)
};

int stbStepTicks(int index);
int stbStages(int index);
