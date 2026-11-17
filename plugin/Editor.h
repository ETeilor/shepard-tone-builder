#pragma once
#include "Look.h"
#include "Processor.h"

// Picture of the region: tonal = pitch over time, tempo = one row per layer. A playhead moves while auditioning.
class GraphView : public juce::Component {
public:
    explicit GraphView(StbProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
private:
    StbProcessor& proc;
};

// Up to 32 cells of one bar, a click cycles the level 0..3
class GridEdit : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit GridEdit(StbProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void changeLength(int delta);
private:
    int cellAt(juce::Point<int>) const;
    juce::Rectangle<float> cellRect(int i, int n) const;
    StbProcessor& proc;
};

// A card that can be dragged to a Logic track as a MIDI file.
class DragCard : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit DragCard(StbProcessor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
    juce::String title, subtitle;
private:
    StbProcessor& proc;
    bool dragging = false, hover = false;
};

class StbEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AsyncUpdater {
public:
    explicit StbEditor(StbProcessor&);
    ~StbEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // for the test runner
    int visibleRows() const;
private:
    struct Row {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::SliderParameterAttachment> sAtt;
        std::unique_ptr<juce::ComboBoxParameterAttachment> cAtt;
        std::unique_ptr<juce::ButtonParameterAttachment> bAtt;
        int mask = 3;                     // 1 tonal, 2 tempo
        int group = 0;                    // 0 settings, 1 shared
        juce::String id;
        juce::Component* control() { return slider ? (juce::Component*)slider.get() : combo ? (juce::Component*)combo.get() : (juce::Component*)toggle.get(); }
    };
    Row& addSlider(const char* text, const char* id, int mask, const juce::String& tip, int group = 0);
    Row& addCombo(const char* text, const char* id, std::initializer_list<const char*> items, int mask, const juce::String& tip);
    Row& addToggle(const char* text, const char* id, int mask, const juce::String& tip, int group = 0);
    void timerCallback() override;
    void handleAsyncUpdate() override { refreshMode(); }
    void refreshMode();
    void layoutRows(juce::Rectangle<int> area, int group);
    bool rowVisible(const Row&) const;

    StbProcessor& proc;
    StbLook look;
    juce::TooltipWindow tips{this, 500};
    GraphView graph;
    GridEdit gridEdit;
    juce::OwnedArray<Row> rows;
    juce::TextButton tabTonal, tabTempo, play, save, gridMinus, gridPlus;
    juce::Label infoLabel, gridLabel, settingsHead, sharedHead;
    DragCard card;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::Drawable> logo;
    int shownMode_ = -1;
    juce::Rectangle<int> graphArea, setArea, sharedArea, bottomArea, gridArea;
};
