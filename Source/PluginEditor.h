#pragma once
#include <array>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Gráfico de tono: curva de entrada, curva de salida y nota objetivo de los últimos ~3,5 s.
class PitchView : public juce::Component, private juce::Timer
{
public:
    explicit PitchView (EntonadorProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    EntonadorProcessor& proc;
};

// Medidor: nota objetivo, desviación de entrada y de salida en cents.
class MeterView : public juce::Component, private juce::Timer
{
public:
    explicit MeterView (EntonadorProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    EntonadorProcessor& proc;
};

class EntonadorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EntonadorEditor (EntonadorProcessor&);
    ~EntonadorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        std::unique_ptr<SliderAtt> att;
        juce::String label;
    };

    void timerCallback() override;
    void toggleNote (int pc);
    void addKnob (const char* paramId, const juce::String& label, const juce::String& suffix);

    EntonadorProcessor& proc;
    PitchView pitch;
    MeterView meter;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ComboBox keyBox, scaleBox, shapeBox;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt, shapeAtt;
    std::array<juce::TextButton, 12> noteButtons;
    juce::Rectangle<int> shapeLabelArea, keyLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EntonadorEditor)
};
