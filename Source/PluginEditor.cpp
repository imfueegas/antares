#include "PluginEditor.h"
#include <cmath>

namespace
{
const juce::Colour kBg (0xff0f1418), kPanel (0xff171d23), kPanel2 (0xff1e252c), kLine (0xff2b343c),
                   kInk (0xffe3e9e5), kMuted (0xff8f9e97), kOrig (0xffeaa352), kFix (0xff88a4ff);
const char* kNames[12] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };

juce::String noteName (int midi)
{
    return juce::String (kNames[((midi % 12) + 12) % 12]) + juce::String ((int) std::floor (midi / 12.0) - 1);
}
}

//==============================================================================
void PitchView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (kPanel2);
    g.fillRoundedRectangle (b, 8.f);

    constexpr int cap = 700;
    float in[cap], out[cap], tg[cap];
    const int n = proc.engine.readHistory (in, out, tg, cap);

    float mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < n; ++i)
        for (float v : { in[i], out[i] })
            if (! std::isnan (v)) { mn = std::min (mn, v); mx = std::max (mx, v); }
    if (mn > mx) { mn = 57.f; mx = 70.f; }
    int lo = (int) std::floor (mn) - 2, hi = (int) std::ceil (mx) + 2;
    while (hi - lo < 14) { --lo; ++hi; }

    auto plot = b.withTrimmedLeft (48.f).reduced (0.f, 8.f).withTrimmedRight (10.f);
    const float rowH = plot.getHeight() / (float) (hi - lo + 1);
    auto Y = [&] (float m) { return plot.getY() + ((float) hi + 0.5f - m) * rowH; };
    const auto mask = proc.getMask();

    g.setFont (juce::FontOptions (11.f));
    for (int k = lo; k <= hi; ++k)
    {
        const int pc = ((k % 12) + 12) % 12;
        const float y = Y ((float) k);
        const bool allowed = mask[(size_t) pc];
        g.setColour (allowed ? kFix.withAlpha (0.12f) : juce::Colours::white.withAlpha (0.03f));
        g.fillRect (plot.getX(), y - rowH * 0.5f, plot.getWidth(), rowH);
        g.setColour (kLine.withAlpha (0.8f));
        g.drawHorizontalLine ((int) (y - rowH * 0.5f), plot.getX(), plot.getRight());
        if (allowed || pc == 0)
        {
            g.setColour (allowed ? kInk : kMuted);
            g.drawText (noteName (k), juce::Rectangle<float> (0.f, y - 8.f, 42.f, 16.f), juce::Justification::centredRight);
        }
    }

    if (n < 2) return;
    const float dx = plot.getWidth() / (float) (cap - 1);
    auto X = [&] (int i) { return plot.getRight() - (float) (n - 1 - i) * dx; };

    g.setColour (kFix.withAlpha (0.25f));
    for (int i = 0; i < n; ++i)
        if (! std::isnan (tg[i]))
            g.fillRect (X (i) - dx * 0.5f, Y (tg[i]) - rowH * 0.34f, dx + 0.6f, rowH * 0.68f);

    auto curve = [&] (const float* a, juce::Colour c, float lw)
    {
        juce::Path p;
        bool open = false;
        for (int i = 0; i < n; ++i)
        {
            if (std::isnan (a[i])) { open = false; continue; }
            if (open) p.lineTo (X (i), Y (a[i])); else { p.startNewSubPath (X (i), Y (a[i])); open = true; }
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    curve (in, kOrig, 1.6f);
    curve (out, kFix, 2.4f);
}

//==============================================================================
void MeterView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (kPanel2);
    g.fillRoundedRectangle (b, 8.f);

    float in[24], out[24], tg[24];
    const int n = proc.engine.readHistory (in, out, tg, 24);
    int idx = -1;
    for (int i = n - 1; i >= 0; --i) if (! std::isnan (in[i])) { idx = i; break; }

    auto area = b.reduced (12.f, 8.f);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (34.f, juce::Font::bold));
    g.drawText (idx >= 0 ? noteName ((int) std::lround (tg[idx])) : juce::String ("-"), area.removeFromTop (40.f), juce::Justification::centredLeft);

    auto bar = area.removeFromTop (34.f).reduced (0.f, 2.f);
    g.setColour (kLine);
    for (int i = 0; i <= 10; ++i) g.drawVerticalLine ((int) (bar.getX() + bar.getWidth() * (float) i / 10.f), bar.getY(), bar.getBottom());
    g.setColour (kInk.withAlpha (0.6f));
    g.drawVerticalLine ((int) bar.getCentreX(), bar.getY(), bar.getBottom());

    juce::String txtIn = "-", txtOut = "-";
    if (idx >= 0)
    {
        const float ci = (in[idx] - tg[idx]) * 100.f, co = (out[idx] - tg[idx]) * 100.f;
        auto pos = [&] (float c) { return bar.getX() + bar.getWidth() * (juce::jlimit (-50.f, 50.f, c) + 50.f) / 100.f; };
        g.setColour (kOrig);
        g.fillEllipse (pos (ci) - 6.f, bar.getY() + 1.f, 12.f, 12.f);
        g.setColour (kFix);
        g.fillEllipse (pos (co) - 6.f, bar.getBottom() - 13.f, 12.f, 12.f);
        txtIn = juce::String (ci, 0) + " c";
        txtOut = juce::String (co, 0) + " c";
    }
    g.setFont (juce::FontOptions (12.f));
    g.setColour (kMuted);
    g.drawText ("Entrada " + txtIn + "    Salida " + txtOut, area.removeFromTop (20.f), juce::Justification::centredLeft);
}

//==============================================================================
EntonadorEditor::EntonadorEditor (EntonadorProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pitch (p), meter (p)
{
    setSize (920, 600);
    addAndMakeVisible (pitch);
    addAndMakeVisible (meter);

    addKnob ("retune", "Velocidad", " ms");
    addKnob ("human", "Humanizar", " %");
    addKnob ("vib", "Vibrato natural", " %");
    addKnob ("cvDepth", "Vib. profund.", " c");
    addKnob ("cvRate", "Vib. velocidad", " Hz");
    addKnob ("cvDelay", "Vib. retardo", " s");
    addKnob ("transpose", "Transposicion", " st");
    addKnob ("detune", "Afinacion fina", " c");
    addKnob ("formant", "Formantes", " st");
    addKnob ("refA", "Referencia La", " Hz");
    addKnob ("mix", "Mezcla", " %");

    for (int i = 0; i < 12; ++i) keyBox.addItem (kNames[i], i + 1);
    scaleBox.addItemList (EntonadorProcessor::scaleNames(), 1);
    shapeBox.addItemList (juce::StringArray { "Senoidal", "Triangular", "Cuadrada" }, 1);
    for (auto* c : { &keyBox, &scaleBox, &shapeBox }) addAndMakeVisible (*c);
    keyAtt = std::make_unique<ComboAtt> (proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "scale", scaleBox);
    shapeAtt = std::make_unique<ComboAtt> (proc.apvts, "cvShape", shapeBox);

    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteButtons[(size_t) i];
        b.setButtonText (kNames[i]);
        b.onClick = [this, i] { toggleNote (i); };
        addAndMakeVisible (b);
    }
    startTimerHz (10);
    timerCallback();
}

EntonadorEditor::~EntonadorEditor() { stopTimer(); }

void EntonadorEditor::addKnob (const char* paramId, const juce::String& label, const juce::String& suffix)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    k->slider.setTextValueSuffix (suffix);
    k->slider.setColour (juce::Slider::rotarySliderFillColourId, kFix);
    k->slider.setColour (juce::Slider::thumbColourId, kInk);
    k->slider.setColour (juce::Slider::rotarySliderOutlineColourId, kLine);
    k->slider.setColour (juce::Slider::textBoxTextColourId, kInk);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->att = std::make_unique<SliderAtt> (proc.apvts, paramId, k->slider);
    addAndMakeVisible (k->slider);
    knobs.push_back (std::move (k));
}

void EntonadorEditor::toggleNote (int pc)
{
    auto mask = proc.getMask();
    auto setBool = [this] (const juce::String& id, bool v)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (v ? 1.f : 0.f);
            prm->endChangeGesture();
        }
    };
    if (auto* sc = proc.apvts.getParameter ("scale"))
    {
        const int cur = (int) std::lround (sc->convertFrom0to1 (sc->getValue()));
        if (cur != EntonadorProcessor::kCustomScale)
        {
            for (int i = 0; i < 12; ++i) setBool ("note" + juce::String (i), mask[(size_t) i]);
            sc->beginChangeGesture();
            sc->setValueNotifyingHost (sc->convertTo0to1 ((float) EntonadorProcessor::kCustomScale));
            sc->endChangeGesture();
        }
    }
    setBool ("note" + juce::String (pc), ! mask[(size_t) pc]);
}

void EntonadorEditor::timerCallback()
{
    const auto mask = proc.getMask();
    for (int i = 0; i < 12; ++i)
    {
        auto& b = noteButtons[(size_t) i];
        const bool on = mask[(size_t) i];
        b.setColour (juce::TextButton::buttonColourId, on ? kFix.withAlpha (0.85f) : kPanel2);
        b.setColour (juce::TextButton::textColourOffId, on ? juce::Colour (0xff0c1224) : kMuted);
        b.repaint();
    }
}

void EntonadorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (26.f, juce::Font::bold));
    g.drawText ("Entonador Vocal", 18, 10, 400, 34, juce::Justification::centredLeft);
    g.setColour (kMuted);
    g.setFont (juce::FontOptions (12.f));
    g.drawText ("Correccion de afinacion para voz solista", 20, 42, 400, 18, juce::Justification::centredLeft);

    g.setColour (kMuted);
    g.setFont (juce::FontOptions (11.f));
    g.drawText ("TONALIDAD Y ESCALA", keyLabelArea, juce::Justification::centredLeft);
    g.drawText ("FORMA DEL VIBRATO", shapeLabelArea, juce::Justification::centredLeft);
    for (auto& k : knobs)
    {
        auto r = k->slider.getBounds();
        g.setColour (kInk);
        g.setFont (juce::FontOptions (12.f));
        g.drawText (k->label, r.getX() - 6, r.getY() - 16, r.getWidth() + 12, 16, juce::Justification::centred);
    }
}

void EntonadorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (48);
    pitch.setBounds (area.removeFromTop (230));
    area.removeFromTop (12);

    auto left = area.removeFromLeft (330);
    area.removeFromLeft (16);

    keyLabelArea = left.removeFromTop (16);
    auto combos = left.removeFromTop (30);
    keyBox.setBounds (combos.removeFromLeft (100));
    combos.removeFromLeft (8);
    scaleBox.setBounds (combos);
    left.removeFromTop (8);
    auto keys = left.removeFromTop (30);
    const int kw = keys.getWidth() / 12;
    for (int i = 0; i < 12; ++i) noteButtons[(size_t) i].setBounds (keys.removeFromLeft (kw).reduced (1));
    left.removeFromTop (12);
    meter.setBounds (left.removeFromTop (130));

    // rejilla de mandos: 6 columnas x 2 filas
    const int cols = 6, rows = 2;
    const int cw = area.getWidth() / cols, ch = area.getHeight() / rows;
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int c = (int) i % cols, r = (int) i / cols;
        knobs[i]->slider.setBounds (area.getX() + c * cw + 4, area.getY() + r * ch + 18, cw - 8, ch - 22);
    }
    const int idx = (int) knobs.size();
    const int c = idx % cols, r = idx / cols;
    shapeLabelArea = { area.getX() + c * cw + 4, area.getY() + r * ch + 2, cw - 8, 16 };
    shapeBox.setBounds (area.getX() + c * cw + 4, area.getY() + r * ch + 24, cw - 8, 28);
}
