#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "TunerCore.h"

class EntonadorProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kCustomScale = 12;   // índice de "Personalizada" en el parámetro scale

    EntonadorProcessor();
    ~EntonadorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Entonador Vocal"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Notas permitidas (0 = Do ... 11 = Si) según tonalidad, escala y teclas personalizadas.
    std::array<bool, 12> getMask() const;
    static std::array<bool, 12> maskFor (int key, int scale);
    static const juce::StringArray& scaleNames();

    juce::AudioProcessorValueTreeState apvts;
    entonador::Engine engine;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float raw (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    entonador::Params readParams() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EntonadorProcessor)
};
