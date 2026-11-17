#include "Editor.h"
#include "Brand.h"
#include <cmath>

static juce::String noteName(int n) {
    static const char* nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(nm[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2);
}

// ---------------------------------------------------------------- graph
void GraphView::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    g.setColour(ui::panel);
    g.fillRoundedRectangle(b, 8.0f);
    auto vw = proc.view();
    if (!vw) return;
    auto in = b.reduced(14, 12);
    in.removeFromLeft(40);
    const double len = (double)vw->regionTicks;
    g.setColour(ui::line);
    g.drawRect(in, 1.0f);

    // cycle borders
    if (vw->cycles > 0)
        for (int c = 1; c < vw->cycles; ++c) {
            const float x = in.getX() + in.getWidth() * (float)c / (float)vw->cycles;
            g.setColour(ui::line.brighter(0.2f));
            g.drawVerticalLine((int)x, in.getY(), in.getBottom());
        }
    const int stride = vw->events.size() > 12000 ? (int)(vw->events.size() / 12000) + 1 : 1;
    if (vw->mode == 0) {
        const int lo = vw->tonal.low, hi = vw->voices > 0 ? std::min(127, vw->tonal.low + 12 * vw->voices) : vw->tonal.high;
        const float span = (float)std::max(1, hi - lo + 1);
        // octave rulers
        g.setFont(ui::font(10.5f));
        for (int n = ((lo + 11) / 12) * 12; n <= hi; n += 12) {
            const float y = in.getBottom() - in.getHeight() * ((float)(n - lo) + 0.5f) / span;
            g.setColour(ui::line);
            g.drawHorizontalLine((int)y, in.getX(), in.getRight());
            g.setColour(ui::textDim);
            g.drawText(noteName(n), (int)b.getX() + 4, (int)y - 8, 38, 16, juce::Justification::centredRight);
        }
        const float h = std::max(1.5f, in.getHeight() / span - 0.5f);
        for (size_t i = 0; i < vw->events.size(); i += (size_t)stride) {
            const auto& e = vw->events[i];
            if (e.kind != 0) continue;
            const float x = in.getX() + in.getWidth() * (float)((double)e.tick / len);
            const float w = std::max(1.5f, in.getWidth() * (float)((double)e.length / len));
            const float y = in.getBottom() - in.getHeight() * ((float)(e.note - lo) + 1.0f) / span;
            if (vw->voices > 0) {
                g.setColour(ui::layerColour(e.layer).withAlpha(0.12f + 0.88f * std::min(1.0f, e.level)));
                g.fillRect(x, y, std::min(w, in.getRight() - x), std::max(h, 3.0f));
            } else {
                g.setColour(ui::accent.withAlpha(0.15f + 0.85f * (float)e.velocity / 127.0f));
                g.fillRect(x, y, std::min(w, in.getRight() - x), h);
            }
        }
    } else {
        const int layers = std::max(1, (int)vw->score.tracks.size());
        const float rowH = in.getHeight() / (float)layers;
        g.setFont(ui::font(10.5f));
        for (int l = 0; l < layers; ++l) {
            const float cy = in.getBottom() - rowH * ((float)l + 0.5f);
            g.setColour(ui::line);
            g.drawHorizontalLine((int)cy, in.getX(), in.getRight());
            g.setColour(ui::layerColour(l));
            g.drawText("L" + juce::String(l + 1), (int)b.getX() + 4, (int)cy - 8, 38, 16, juce::Justification::centredRight);
        }
        for (size_t i = 0; i < vw->events.size(); i += (size_t)stride) {
            const auto& e = vw->events[i];
            if (e.kind != 0) continue;
            const float x = in.getX() + in.getWidth() * (float)((double)e.tick / len);
            const float cy = in.getBottom() - rowH * ((float)e.layer + 0.5f);
            const float v = (float)e.velocity / 127.0f;
            const float hh = 3.0f + (rowH - 8.0f) * v;
            g.setColour(ui::layerColour(e.layer).withAlpha(0.25f + 0.75f * v));
            g.fillRect(x, cy - hh / 2, 1.6f, hh);
        }
    }
    if (proc.isPreviewing()) {
        const float x = in.getX() + in.getWidth() * (float)proc.previewFraction();
        g.setColour(ui::text);
        g.drawVerticalLine((int)x, in.getY(), in.getBottom());
    }
}

// ---------------------------------------------------------------- grid
juce::Rectangle<float> GridEdit::cellRect(int i, int n) const {
    const float w = (float)getWidth() / (float)std::max(n, 1);
    return {i * w + 1.5f, 0.0f, w - 3.0f, (float)getHeight()};
}

void GridEdit::paint(juce::Graphics& g) {
    const auto gr = proc.grid();
    const int n = (int)gr.size();
    for (int i = 0; i < n; ++i) {
        auto r = cellRect(i, n);
        g.setColour(ui::panelHi);
        g.fillRoundedRectangle(r, 3.0f);
        const int lv = gr[(size_t)i];
        if (lv > 0) {
            const float h = r.getHeight() * (0.25f + 0.25f * (float)lv);
            g.setColour(ui::accent.withAlpha(lv == 1 ? 0.45f : lv == 2 ? 0.75f : 1.0f));
            g.fillRoundedRectangle(r.withTop(r.getBottom() - h), 3.0f);
        }
        if (i % 4 == 0 && n > 4) { g.setColour(ui::textDim); g.fillRect(r.getX(), r.getY(), 2.0f, 2.0f); }
    }
}

int GridEdit::cellAt(juce::Point<int> p) const {
    const int n = (int)proc.grid().size();
    return std::clamp(p.x * n / std::max(1, getWidth()), 0, n - 1);
}

void GridEdit::mouseDown(const juce::MouseEvent& e) {
    auto g = proc.grid();
    const int i = cellAt(e.getPosition());
    const int d = e.mods.isRightButtonDown() ? 3 : 1;
    g[(size_t)i] = (uint8_t)((g[(size_t)i] + d) % 4);
    proc.setGrid(g);
    repaint();
}

void GridEdit::changeLength(int delta) {
    auto g = proc.grid();
    const int n = std::clamp((int)g.size() + delta, 1, 32);
    g.resize((size_t)n, 0);
    proc.setGrid(g);
    repaint();
}

// ---------------------------------------------------------------- card
void DragCard::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(hover ? ui::panelHi : ui::panel);
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(hover ? ui::accent : ui::line);
    g.drawRoundedRectangle(r, 10.0f, 1.2f);
    StbLook::drawIcon(g, "export", {r.getX() + 16, r.getCentreY() - 11, 22, 22}, ui::accent);
    g.setColour(ui::text);
    g.setFont(ui::font(14.5f, true));
    g.drawText(title, (int)r.getX() + 52, (int)r.getCentreY() - 20, (int)r.getWidth() - 60, 22, juce::Justification::centredLeft);
    g.setColour(ui::textDim);
    g.setFont(ui::font(12.0f));
    g.drawText(subtitle, (int)r.getX() + 52, (int)r.getCentreY() + 2, (int)r.getWidth() - 60, 18, juce::Justification::centredLeft);
}

void DragCard::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    auto f = proc.writeMidiFile();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
}

// ---------------------------------------------------------------- editor
StbEditor::Row& StbEditor::addSlider(const char* text, const char* id, int mask, const juce::String& tip, int group) {
    auto* r = rows.add(new Row());
    r->mask = mask; r->group = group; r->id = id;
    r->label.setText(U(text), juce::dontSendNotification);
    r->label.setFont(ui::font(12.5f));
    r->label.setColour(juce::Label::textColourId, ui::textDim);
    r->label.setTooltip(tip);
    r->slider = std::make_unique<juce::Slider>();
    r->slider->setSliderStyle(juce::Slider::LinearHorizontal);
    r->slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 22);
    r->slider->setTooltip(tip);
    r->sAtt = std::make_unique<juce::SliderParameterAttachment>(*proc.params.getParameter(id), *r->slider);
    addChildComponent(r->label);
    addChildComponent(*r->slider);
    return *r;
}

StbEditor::Row& StbEditor::addCombo(const char* text, const char* id, std::initializer_list<const char*> items, int mask, const juce::String& tip) {
    auto* r = rows.add(new Row());
    r->mask = mask; r->id = id;
    r->label.setText(U(text), juce::dontSendNotification);
    r->label.setFont(ui::font(12.5f));
    r->label.setColour(juce::Label::textColourId, ui::textDim);
    r->label.setTooltip(tip);
    r->combo = std::make_unique<juce::ComboBox>();
    int i = 1;
    for (auto* it : items) r->combo->addItem(U(it), i++);
    r->combo->setTooltip(tip);
    r->cAtt = std::make_unique<juce::ComboBoxParameterAttachment>(*static_cast<juce::RangedAudioParameter*>(proc.params.getParameter(id)), *r->combo);
    addChildComponent(r->label);
    addChildComponent(*r->combo);
    return *r;
}

StbEditor::Row& StbEditor::addToggle(const char* text, const char* id, int mask, const juce::String& tip, int group) {
    auto* r = rows.add(new Row());
    r->mask = mask; r->group = group; r->id = id;
    r->toggle = std::make_unique<juce::ToggleButton>(U(text));
    r->toggle->setTooltip(tip);
    r->bAtt = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter(id), *r->toggle);
    addChildComponent(*r->toggle);
    return *r;
}

// one slider with two handles for the lowest and the highest note
void StbEditor::addRange() {
    auto* r = rows.add(new Row());
    r->mask = 1; r->group = 0; r->id = "range";
    r->label.setText(U("Диапазон"), juce::dontSendNotification);
    r->label.setFont(ui::font(12.5f));
    r->label.setColour(juce::Label::textColourId, ui::textDim);
    const auto tip = U("Нижняя и верхняя нота. Голосов столько, сколько октав в диапазоне (от 2 до 8). Подберите под диапазон инструмента.");
    r->label.setTooltip(tip);
    r->slider = std::make_unique<juce::Slider>(juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox);
    r->slider->setRange(12, 120, 1);
    r->slider->setMinAndMaxValues(proc.params.getRawParameterValue("t_low")->load(), proc.params.getRawParameterValue("t_high")->load(), juce::dontSendNotification);
    r->slider->setTooltip(tip);
    r->valueText = std::make_unique<juce::Label>();
    r->valueText->setFont(ui::font(12.5f));
    r->valueText->setColour(juce::Label::textColourId, ui::text);
    r->valueText->setJustificationType(juce::Justification::centredRight);
    auto* sl = r->slider.get();
    auto* vt = r->valueText.get();
    sl->onValueChange = [this, sl, vt]() {
        if (syncingRange_) return;
        int lo = (int)std::lround(sl->getMinValue()), hi = (int)std::lround(sl->getMaxValue());
        if (hi - lo < 12) {
            // keep at least one octave: the handle that is not being dragged stays
            if (sl->getThumbBeingDragged() == 1) lo = hi - 12; else hi = lo + 12;
            lo = std::max(12, lo); hi = std::min(120, std::max(hi, lo + 12));
            syncingRange_ = true;
            sl->setMinAndMaxValues(lo, hi, juce::dontSendNotification);
            syncingRange_ = false;
        }
        vt->setText(noteName(lo) + " - " + noteName(hi), juce::dontSendNotification);
        proc.setParamValue("t_low", (float)lo);
        proc.setParamValue("t_high", (float)hi);
    };
    addChildComponent(r->label);
    addChildComponent(*r->slider);
    addChildComponent(*r->valueText);
}

static void noteText(juce::Slider& s) {
    s.textFromValueFunction = [](double v) { return noteName((int)v); };
}

StbEditor::StbEditor(StbProcessor& p)
    : AudioProcessorEditor(&p), proc(p), graph(p), gridEdit(p), card(p) {
    setLookAndFeel(&look);
    logo = brand::make(false, ui::text);
    addAndMakeVisible(graph);
    addChildComponent(gridEdit);
    addAndMakeVisible(infoLabel);
    infoLabel.setFont(ui::font(12.0f));
    infoLabel.setColour(juce::Label::textColourId, ui::textDim);

    auto tab = [&](juce::TextButton& b, const char* text, int mode, const char* tip) {
        b.setButtonText(U(text));
        b.setTooltip(U(tip));
        b.onClick = [this, mode]() { proc.setParamValue("mode", (float)mode); };
        addAndMakeVisible(b);
    };
    tab(tabTonal, "Тональный", 0, "Бесконечно растущая или падающая нота: октавные голоса с колоколом громкости");
    tab(tabTempo, "Темповой", 1, "Бесконечное ускорение или замедление: слои одного ритма и огибающая громкости, темп проекта не меняется");

    // ================= tonal, main
    addCombo("Профиль", "t_profile", {"Синтезатор (velocity)", "Струнные легато, CC1", "Струнные легато, CC11", "Свой"}, 1,
             "Готовые настройки. Струнные: по одному треку на голос, колокол громкости идёт контроллером (CC1 колесо модуляции, CC11 экспрессия). Синтезатор: аккорд октав на шаг, громкость velocity. Правка скрытых параметров переключает на «Свой».");
    addCombo("Направление", "t_dir", {"Вверх", "Вниз"}, 1, "Куда уходит высота");
    addCombo("Звукоряд", "t_set", {"Хроматика", "Целотонный", "Уменьшённый (тон-полутон)", "Мажорная пентатоника", "Минорная пентатоника", "Мажор", "Минор", "Дорийский", "Миксолидийский"}, 1, "Из каких ступеней строится лестница");
    addSlider("Тоника", "t_tonic", 1, U("Ступень, с которой начинается цикл. Для хроматики ничего не меняет"));
    rows.getLast()->slider->textFromValueFunction = [](double v) { return noteName((int)v + 24).dropLastCharacters(1); };
    addRange();
    addSlider("Ширина колокола", "t_width", 1, U("В октавах. «весь» это колокол на весь диапазон: голоса в сумме дают ровную громкость"));
    rows.getLast()->slider->textFromValueFunction = [](double v) { return v < 0.25 ? U("весь") : juce::String(v, 1) + U(" окт"); };
    addCombo("Скорость, шаг", "stepLen", {"1/4", "1/8", "1/8 триоль", "1/16", "1/16 триоль", "1/32", "1/32 триоль", "1/64"}, 1,
             "Длина шага лестницы в долях такта. Темп проекта делает подъём быстрее или медленнее, ноты в файле от него не зависят. Для легато струнных удобны 1/8 и 1/16");
    addSlider("Длина, тактов", "bars", 1, U("Длина региона в тактах. При включённом «Целое число циклов» длина округляется вверх до целых циклов"));
    addToggle("Целое число циклов", "t_whole", 1, U("Длина региона подгоняется так, чтобы в ней уместились целые циклы и петля замыкалась без стыка"));
    // ================= tonal, advanced
    addSlider("Шаг лестницы", "t_interval", 1, U("На сколько ступеней звукоряда сдвигается каждый шаг. Для тритонового парадокса безопаснее до 5, 7 это круг квинт"), 1);
    addSlider("Центр колокола", "t_center", 1, U("Где громче всего. 0 = середина диапазона"), 1);
    rows.getLast()->slider->textFromValueFunction = [](double v) { return v < 1 ? U("авто") : noteName((int)v); };
    addCombo("Форма колокола", "t_shape", {"Косинус", "Гаусс"}, 1, "Как громкость падает к краям диапазона");
    rows.getLast()->group = 1;
    addCombo("Раскладка", "t_layout", {"Аккорд на шаг", "Линии по голосам"}, 1, "Линии по голосам: трек на голос, плавная громкость контроллером. Аккорд на шаг: все октавы одним треком");
    rows.getLast()->group = 1;
    addCombo("Громкость через", "t_ctl", {"Velocity", "CC1", "CC11"}, 1, "Чем задаётся колокол. Для длинных нот и легато нужен контроллер: CC1 (колесо модуляции, динамика BBCSO) или CC11");
    rows.getLast()->group = 1;
    addCombo("Артикуляция", "t_art", {"Стаккато", "Легато"}, 1, "Легато: ноты чуть перекрываются, чтобы сработал легато-скрипт");
    rows.getLast()->group = 1;
    addSlider("Длина ноты, %", "t_gate", 1, U("Процент шага. 0 = по артикуляции"), 1);
    rows.getLast()->slider->textFromValueFunction = [](double v) { return v < 1 ? U("авто") : juce::String((int)v); };
    addSlider("Уровень мин", "t_vmin", 1, U("Значение контроллера (или velocity) на краях колокола"), 1);
    addSlider("Уровень макс", "t_vmax", 1, U("Значение контроллера (или velocity) в центре колокола"), 1);
    addSlider("Кривая", "t_gamma", 1, U("Меньше 1 громче середина, больше 1 уже колокол. Подбирайте на слух под библиотеку"), 1);
    rows.getLast()->slider->setNumDecimalPlacesToDisplay(2);
    addSlider("Порог velocity", "t_thr", 1, U("Для velocity: ноты тише этого не пишутся"), 1);
    addToggle("Тихие края через CC11", "t_edge", 1, U("CC1: контроллер 11 закрывает голос на краях диапазона, чтобы скачок вниз и вверх был беззвучным"), 1);
    addSlider("Velocity нот", "t_ccVel", 1, U("Velocity нот, когда громкость идёт контроллером"), 1);

    // ================= tempo, main
    addCombo("Метод", "p_method", {"Плавный", "Ступенчатый"}, 2, "Плавный: логарифмическая шкала времени. Ступенчатый: ускорения на целых долях, удары попадают на сетку");
    addCombo("Направление", "p_dir", {"Ускорение", "Замедление"}, 2, "Замедление это то же самое, перевёрнутое во времени");
    addSlider("Слоёв", "p_layers", 2, U("Сколько копий ритма звучит одновременно в разных скоростях"));
    addCombo("Рисунок", "p_preset", {"Часы, четыре доли", "Один удар в такт", "Восьмые", "Шестнадцатые", "Триоли", "Тресильо", "Клаве сон 3-2", "Свой"}, 2, "Готовые рисунки одного такта. Правка сетки переключает на «Свой»");
    addSlider("Длина, тактов", "bars", 2, U("Длина региона в тактах"));
    addSlider("Циклов", "cycles", 2, U("Сколько раз бесконечный круг умещается в регион. 0 = один свободный прогон"));
    rows.getLast()->slider->textFromValueFunction = [](double v) { return v < 1 ? U("свободно") : juce::String((int)v); };
    // ================= tempo, advanced
    addCombo("Ступеней на удвоение", "p_stages", {"2", "3", "4", "6", "8"}, 2, "Только для ступенчатого метода");
    rows.getLast()->group = 1;
    addSlider("Цикл, тактов", "cycleBars", 2, U("Длина цикла, когда циклов 0"), 1);
    addSlider("Нота", "p_note", 2, U("Нота, которой пишутся удары"), 1); noteText(*rows.getLast()->slider);
    addCombo("Старт", "p_start", {"Авто", "Точный", "Выровненный"}, 2, "Выровненный: все слои совпадают на начале цикла. Точный: стартовая скорость ровно 1");
    rows.getLast()->group = 1;
    addSlider("Velocity мин", "p_vmin", 2, U("Самая тихая нота"), 1);
    addSlider("Velocity макс", "p_vmax", 2, U("Самая громкая нота"), 1);
    addSlider("Кривая", "p_gamma", 2, U("Подбирайте на слух под библиотеку"), 1);
    rows.getLast()->slider->setNumDecimalPlacesToDisplay(2);
    addSlider("Нижний порог", "p_floor", 2, U("Удары тише этого уровня не пишутся"), 1);
    rows.getLast()->slider->setNumDecimalPlacesToDisplay(3);
    addCombo("Длина ноты", "p_lenMode", {"Пропорциональная", "Фиксированная"}, 2, "Фиксированная лучше для сэмплов вроде щипков и часов");
    rows.getLast()->group = 1;
    addSlider("Доля интервала, %", "p_gate", 2, U("Для пропорциональной длины"), 1);
    addSlider("Тиков", "p_ticks", 2, U("Для фиксированной длины, 960 тиков = четверть"), 1);
    addSlider("Макс. ударов в сек", "p_maxPerSec", 2, U("Защита сэмплера от слишком частых ударов, по темпу хоста. 0 = выкл"), 1);
    rows.getLast()->slider->textFromValueFunction = [](double v) { return v < 1 ? U("выкл") : juce::String((int)v); };
    addCombo("Вывод", "p_output", {"Один трек", "Трек на слой"}, 2, "Трек на слой удобен для отдельной громкости и разных звуков");
    rows.getLast()->group = 1;

    // ================= both, advanced
    addSlider("Темп прослушивания", "previewBpm", 3, U("Темп для кнопки «Слушать», когда выключен «Темп проекта» или хост его не сообщает. На ноты в файле не влияет"), 1);
    addToggle("Темп проекта", "hostTempo", 3, U("Включено: темп берётся из проекта"), 1);
    addToggle("Размер проекта", "hostMeter", 3, U("Включено: размер берётся из проекта"), 1);
    addSlider("Размер, доли", "meterNum", 3, U("Числитель размера, когда не из проекта"), 1);
    addCombo("Размер, длительность", "meterDen", {"2", "4", "8", "16"}, 3, "Знаменатель размера, когда не из проекта");
    rows.getLast()->group = 1;

    gridLabel.setText(U("Рисунок такта"), juce::dontSendNotification);
    gridLabel.setFont(ui::font(12.5f));
    gridLabel.setColour(juce::Label::textColourId, ui::textDim);
    addChildComponent(gridLabel);
    gridMinus.setButtonText("-"); gridPlus.setButtonText("+");
    gridMinus.setTooltip(U("Короче на одну клетку")); gridPlus.setTooltip(U("Длиннее на одну клетку, до 32"));
    gridMinus.onClick = [this]() { gridEdit.changeLength(-1); };
    gridPlus.onClick = [this]() { gridEdit.changeLength(1); };
    addChildComponent(gridMinus); addChildComponent(gridPlus);
    gridEdit.setTooltip(U("Клик по клетке: уровень 0, 1, 2, 3. Правый клик в обратную сторону"));

    advBtn.setTooltip(U("Остальные параметры. Профиль и значения по умолчанию подходят для большинства случаев."));
    advBtn.onClick = [this]() { advOpen_ = !advOpen_; refreshMode(); };
    addAndMakeVisible(advBtn);

    play.setButtonText(U("Слушать"));
    play.getProperties().set("icon", "play");
    play.setTooltip(U("Прослушать регион: синусом для тонального режима (с той же огибающей, что уйдёт в файл), кликом для темпового"));
    play.onClick = [this]() {
        const bool on = !proc.isPreviewing();
        proc.setPreview(on);
        play.setButtonText(on ? U("Стоп") : U("Слушать"));
        play.getProperties().set("icon", on ? "stop" : "play");
        play.repaint();
    };
    addAndMakeVisible(play);
    save.setButtonText(U("Сохранить .mid"));
    save.getProperties().set("icon", "export");
    save.setTooltip(U("Записать MIDI-файл в выбранное место"));
    save.onClick = [this]() {
        chooser = std::make_unique<juce::FileChooser>(U("Сохранить MIDI"), juce::File::getSpecialLocation(juce::File::userDesktopDirectory).getChildFile(proc.fileBaseName() + ".mid"), "*.mid");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) proc.saveMidiTo(f.withFileExtension("mid")); });
    };
    addAndMakeVisible(save);
    card.title = U("MIDI в трек");
    card.setTooltip(U("Потяните карточку на пустое место в Logic: создадутся треки (по одному на голос). Назначьте на них один и тот же инструмент. Ноты лежат в долях такта и не зависят от темпа."));
    addAndMakeVisible(card);

    StbLook::sectionLabel(settingsHead, U("Параметры"));
    addAndMakeVisible(settingsHead);

    for (auto* r : rows) if (r->slider && r->id != "range") r->slider->updateText();
    // the bars slider shows the length that is really written when whole loops are on
    for (auto* r : rows)
        if (r->id == "bars" && r->slider) {
            auto* sl = r->slider.get();
            sl->textFromValueFunction = [this](double v) {
                if (proc.mode() == 0) {
                    auto vw = proc.view();
                    if (vw && vw->bars > 0 && proc.params.getRawParameterValue("t_whole")->load() > 0.5f) return juce::String(vw->bars);
                }
                return juce::String((int)v);
            };
        }
    proc.onChanged = [this]() { triggerAsyncUpdate(); };
    startTimerHz(15);
    refreshMode();
}

StbEditor::~StbEditor() {
    proc.onChanged = nullptr;
    cancelPendingUpdate();
    proc.setPreview(false);
    setLookAndFeel(nullptr);
}

bool StbEditor::rowApplies(const Row& r) const {
    const int m = shownMode_ < 0 ? 0 : shownMode_;
    if (!(r.mask & (m == 0 ? 1 : 2))) return false;
    auto pv = [&](const char* id) { return (int)std::lround(proc.params.getRawParameterValue(id)->load()); };
    if (m == 0) {
        const bool voices = pv("t_layout") == 1;
        const int ctl = pv("t_ctl");
        if (r.id == "t_tonic") return pv("t_set") != 0;
        if (r.id == "t_thr") return !voices && ctl == 0;
        if (r.id == "t_edge") return voices && ctl == 1;
        if (r.id == "t_ccVel") return voices && ctl != 0;
        if (r.id == "t_vmin") return ctl != 2;
    } else {
        const bool st = pv("p_method") == 1;
        const bool fixedLen = pv("p_lenMode") == 1;
        if (r.id == "p_stages") return st;
        if (r.id == "p_gate") return !fixedLen;
        if (r.id == "p_ticks") return fixedLen;
        if (r.id == "cycleBars") return pv("cycles") == 0;
    }
    if (r.id == "meterNum" || r.id == "meterDen") return proc.params.getRawParameterValue("hostMeter")->load() < 0.5f;
    return true;
}

bool StbEditor::rowVisible(const Row& r) const { return rowApplies(r) && (r.group == 0 || advOpen_); }

int StbEditor::visibleRows() const {
    int n = 0;
    for (auto* r : rows) if (rowVisible(*r)) ++n;
    return n;
}

int StbEditor::wantedHeight() const {
    int main = 0, adv = 0;
    for (auto* r : rows) if (rowApplies(*r)) (r->group == 0 ? main : adv)++;
    int h = 44 + 220 + 24 + 22 + ((main + 1) / 2) * 28 + 10 + 30;
    if (shownMode_ == 1) h += 48;
    if (advOpen_) h += 8 + ((adv + 1) / 2) * 28;
    return h + 14 + 76 + 14;
}

void StbEditor::refreshMode() {
    shownMode_ = proc.mode();
    tabTonal.setToggleState(shownMode_ == 0, juce::dontSendNotification);
    tabTempo.setToggleState(shownMode_ == 1, juce::dontSendNotification);
    tabTonal.getProperties().set("active", shownMode_ == 0);
    tabTempo.getProperties().set("active", shownMode_ == 1);
    for (auto* r : rows) {
        const bool v = rowVisible(*r);
        r->label.setVisible(v && !r->toggle);
        r->control()->setVisible(v);
        if (r->valueText) r->valueText->setVisible(v);
    }
    const bool tempo = shownMode_ == 1;
    gridEdit.setVisible(tempo);
    gridLabel.setVisible(tempo);
    gridMinus.setVisible(tempo);
    gridPlus.setVisible(tempo);
    advBtn.setButtonText(U(advOpen_ ? "Дополнительно  \xE2\x96\xBE" : "Дополнительно  \xE2\x96\xB8"));
    // keep the range slider and the displayed bars in step with the parameters
    for (auto* r : rows) {
        if (r->id == "range") {
            const int lo = (int)std::lround(proc.params.getRawParameterValue("t_low")->load());
            const int hi = (int)std::lround(proc.params.getRawParameterValue("t_high")->load());
            syncingRange_ = true;
            r->slider->setMinAndMaxValues(lo, hi, juce::dontSendNotification);
            syncingRange_ = false;
            r->valueText->setText(noteName(lo) + " - " + noteName(hi), juce::dontSendNotification);
        }
        if (r->id == "bars" && r->slider) r->slider->updateText();
    }
    auto vw = proc.view();
    if (vw) {
        juce::String s;
        const double bpm = vw->tr.bpm;
        if (vw->mode == 0) {
            const double ms = (double)vw->tonal.stepTicks * 60000.0 / ((double)shp::kTpb * bpm);
            const double cycBars = (double)vw->cycleTicks / (double)vw->tr.barTicks();
            if (vw->voices > 0) s = U("Голосов: ") + juce::String(vw->voices) + U(", ");
            s += U("цикл ") + juce::String(cycBars, 2) + U(" такта, регион ") + juce::String(vw->bars) + U(" тактов, шаг около ") +
                 juce::String((int)std::lround(ms)) + U(" мс, нот: ") + juce::String((int)vw->score.noteCount());
            if (!vw->closed) s += U(". Петля не замкнута: включите «Целое число циклов» или подберите длину");
        } else {
            const auto c = shp::tempoCycle(vw->tempo);
            s = U("Нот в файле: ") + juce::String((int)vw->score.noteCount()) + U(", цикл ") + juce::String(c.cycleBars, 2) +
                U(" такта, стартовая скорость ") + juce::String(c.rho, 3);
        }
        infoLabel.setText(s, juce::dontSendNotification);
        card.subtitle = proc.fileBaseName();
    }
    const int h = wantedHeight();
    if (getHeight() != h || getWidth() != 1120) setSize(1120, h);
    else resized();
    repaint();
}

void StbEditor::timerCallback() {
    if (proc.isPreviewing()) graph.repaint();
    bool changed = false;
    for (auto* r : rows) if (rowVisible(*r) != r->control()->isVisible()) { changed = true; break; }
    if (changed) refreshMode();
    const bool on = proc.isPreviewing();
    if ((play.getButtonText() == U("Стоп")) != on) {
        play.setButtonText(on ? U("Стоп") : U("Слушать"));
        play.getProperties().set("icon", on ? "stop" : "play");
        play.repaint();
    }
    for (auto* r : rows) if (r->id == "previewBpm") r->slider->setEnabled(proc.params.getRawParameterValue("hostTempo")->load() < 0.5f);
}

void StbEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::text);
    g.setFont(ui::font(13.0f, true));
    g.drawText("SHEPARD TONE BUILDER", 24, 12, 300, 22, juce::Justification::centredLeft);
    if (logo) logo->drawWithin(g, juce::Rectangle<float>((float)getWidth() - 24.0f - 150.0f, 15.0f, 150.0f, 15.0f),
                               juce::RectanglePlacement::xRight | juce::RectanglePlacement::yMid, 0.85f);
    g.setColour(ui::line);
    g.drawHorizontalLine(bottomArea.getY() - 8, 24, (float)getWidth() - 24);
}

void StbEditor::layoutRows(juce::Rectangle<int> area, int group) {
    std::vector<Row*> vis;
    for (auto* r : rows) if (r->group == group && rowVisible(*r)) vis.push_back(r);
    const int rowH = 28;
    const int colW = (area.getWidth() - 24) / 2;
    const int perCol = (int)std::ceil((double)vis.size() / 2.0);
    int idx = 0;
    for (auto* r : vis) {
        const int col = idx / std::max(1, perCol), row = idx % std::max(1, perCol);
        auto cell = juce::Rectangle<int>(area.getX() + col * (colW + 24), area.getY() + row * rowH, colW, rowH - 2);
        if (r->toggle) r->toggle->setBounds(cell);
        else {
            r->label.setBounds(cell.removeFromLeft(130));
            if (r->valueText) r->valueText->setBounds(cell.removeFromRight(96));
            r->control()->setBounds(cell);
        }
        ++idx;
    }
}

void StbEditor::resized() {
    auto b = getLocalBounds().reduced(24, 0);
    auto top = b.removeFromTop(44);
    tabTonal.setBounds(top.getX() + 300, 8, 110, 28);
    tabTempo.setBounds(top.getX() + 416, 8, 110, 28);
    graphArea = b.removeFromTop(220);
    graph.setBounds(graphArea);
    infoLabel.setBounds(b.removeFromTop(24));
    bottomArea = b.removeFromBottom(76 + 14);
    bottomArea.removeFromBottom(14);
    auto s = b;
    settingsHead.setBounds(s.removeFromTop(18));
    s.removeFromTop(4);
    if (shownMode_ == 1) {
        auto gr = s.removeFromTop(48);
        gridLabel.setBounds(gr.removeFromLeft(130).withHeight(28));
        auto btn = gr.removeFromRight(72);
        gridMinus.setBounds(btn.removeFromLeft(34).withHeight(28));
        btn.removeFromLeft(4);
        gridPlus.setBounds(btn.withHeight(28));
        gr.removeFromRight(8);
        gridEdit.setBounds(gr.withHeight(36));
    }
    int mainCount = 0, advCount = 0;
    for (auto* r : rows) if (rowApplies(*r)) (r->group == 0 ? mainCount : advCount)++;
    layoutRows(s.removeFromTop(((mainCount + 1) / 2) * 28), 0);
    s.removeFromTop(10);
    advBtn.setBounds(s.removeFromTop(30).removeFromLeft(170));
    if (advOpen_) {
        s.removeFromTop(8);
        layoutRows(s.removeFromTop(((advCount + 1) / 2) * 28), 1);
    }
    // bottom strip: audition, save, the card to drag
    auto bt = bottomArea.withTrimmedTop(8);
    play.setBounds(bt.removeFromLeft(140).withHeight(40).translated(0, 12));
    bt.removeFromLeft(10);
    save.setBounds(bt.removeFromLeft(160).withHeight(40).translated(0, 12));
    card.setBounds(bt.removeFromRight(360).withHeight(64));
}
