#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Cyrillic literals must reach JUCE as UTF-8
inline juce::String U(const char* s) { return juce::String::fromUTF8(s); }
inline juce::String U(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

namespace ui {
// the dark palette of the series with the amber accent of Shepard Tone Builder
const juce::Colour bg{0xff0c0c0e}, panel{0xff141417}, panelHi{0xff1c1c20}, line{0xff29292f}, text{0xfff1f0ec},
    textDim{0xff80808a}, accent{0xffffb36b}, onAccent{0xff241505}, danger{0xffff6b5e}, switchOff{0xff3a3a42};
inline juce::Colour lift(juce::Colour c, float amount) { return c.brighter(amount); }
inline juce::Font font(float size, bool bold = false) {
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}
// one colour per layer of the tempo mode
inline juce::Colour layerColour(int layer) {
    static const juce::uint32 c[8] = {0xffffb36b, 0xff7fd6c2, 0xff8ab4f8, 0xfff28b82, 0xffa8d98a, 0xffc58af9, 0xfff0c674, 0xff7cc4ff};
    return juce::Colour(c[layer < 0 ? 0 : layer > 7 ? 7 : layer]);
}
}  // namespace ui
