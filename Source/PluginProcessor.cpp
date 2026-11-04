#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const int kIntervals[12][12] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },  // cromática
    { 0, 2, 4, 5, 7, 9, 11, -1 },              // mayor
    { 0, 2, 3, 5, 7, 8, 10, -1 },              // menor natural
    { 0, 2, 3, 5, 7, 8, 11, -1 },              // menor armónica
    { 0, 2, 4, 7, 9, -1 },                     // pentatónica mayor
    { 0, 3, 5, 7, 10, -1 },                    // pentatónica menor
    { 0, 3, 5, 6, 7, 10, -1 },                 // blues
    { 0, 2, 3, 5, 7, 9, 10, -1 },              // dórica
    { 0, 2, 4, 5, 7, 9, 10, -1 },              // mixolidia
    { 0, 1, 3, 5, 7, 8, 10, -1 },              // frigia
    { 0, 2, 4, 6, 7, 9, 11, -1 },              // lidia
    { 0, 2, 4, 6, 8, 10, -1 },                 // tonos enteros
};
const char* kNoteIds[12] = { "note0", "note1", "note2", "note3", "note4", "note5", "note6", "note7", "note8", "note9", "note10", "note11" };
}

const juce::StringArray& EntonadorProcessor::scaleNames()
{
    static const juce::StringArray names { "Cromatica", "Mayor", "Menor natural", "Menor armonica", "Pentatonica mayor",
                                           "Pentatonica menor", "Blues", "Dorica", "Mixolidia", "Frigia", "Lidia",
                                           "Tonos enteros", "Personalizada" };
    return names;
}

std::array<bool, 12> EntonadorProcessor::maskFor (int key, int scale)
{
    std::array<bool, 12> m {};
    if (scale < 0 || scale >= kCustomScale) return m;
    for (int i = 0; i < 12 && kIntervals[scale][i] >= 0; ++i)
        m[(size_t) ((key + kIntervals[scale][i]) % 12)] = true;
    return m;
}

juce::AudioProcessorValueTreeState::ParameterLayout EntonadorProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<F> (juce::ParameterID { "retune", 1 }, "Velocidad de correccion", R (0.f, 400.f, 1.f, 0.5f), 25.f, A().withLabel ("ms")));
    l.add (std::make_unique<F> (juce::ParameterID { "human", 1 }, "Humanizar", R (0.f, 100.f, 1.f), 30.f, A().withLabel ("%")));
    l.add (std::make_unique<F> (juce::ParameterID { "vib", 1 }, "Vibrato natural", R (0.f, 200.f, 1.f), 100.f, A().withLabel ("%")));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "cvShape", 1 }, "Forma del vibrato",
                                                         juce::StringArray { "Senoidal", "Triangular", "Cuadrada" }, 0));
    l.add (std::make_unique<F> (juce::ParameterID { "cvRate", 1 }, "Vibrato velocidad", R (1.f, 10.f, 0.1f), 5.5f, A().withLabel ("Hz")));
    l.add (std::make_unique<F> (juce::ParameterID { "cvDepth", 1 }, "Vibrato profundidad", R (0.f, 100.f, 1.f), 0.f, A().withLabel ("cents")));
    l.add (std::make_unique<F> (juce::ParameterID { "cvDelay", 1 }, "Vibrato retardo", R (0.f, 1.5f, 0.05f), 0.3f, A().withLabel ("s")));
    l.add (std::make_unique<F> (juce::ParameterID { "transpose", 1 }, "Transposicion", R (-12.f, 12.f, 1.f), 0.f, A().withLabel ("st")));
    l.add (std::make_unique<F> (juce::ParameterID { "detune", 1 }, "Afinacion fina", R (-100.f, 100.f, 1.f), 0.f, A().withLabel ("cents")));
    l.add (std::make_unique<F> (juce::ParameterID { "formant", 1 }, "Formantes", R (-6.f, 6.f, 0.5f), 0.f, A().withLabel ("st")));
    l.add (std::make_unique<F> (juce::ParameterID { "refA", 1 }, "Referencia La", R (430.f, 450.f, 1.f), 440.f, A().withLabel ("Hz")));
    l.add (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mezcla", R (0.f, 100.f, 1.f), 100.f, A().withLabel ("%")));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "key", 1 }, "Tonica",
                                                         juce::StringArray { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scale", 1 }, "Escala", scaleNames(), 1));
    const auto def = maskFor (0, 1);
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { kNoteIds[i], 1 }, juce::String ("Nota ") + juce::String (i + 1), def[(size_t) i]));
    return l;
}

EntonadorProcessor::EntonadorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ESTADO", createLayout())
{
}

bool EntonadorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void EntonadorProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.latency());
}

std::array<bool, 12> EntonadorProcessor::getMask() const
{
    const int scale = (int) std::lround (raw ("scale"));
    if (scale < kCustomScale) return maskFor ((int) std::lround (raw ("key")), scale);
    std::array<bool, 12> m {};
    for (int i = 0; i < 12; ++i) m[(size_t) i] = raw (kNoteIds[i]) > 0.5f;
    return m;
}

entonador::Params EntonadorProcessor::readParams() const
{
    entonador::Params p;
    p.refA = raw ("refA");
    const auto m = getMask();
    for (int i = 0; i < 12; ++i) p.mask[i] = m[(size_t) i];
    p.retuneMs = raw ("retune");
    p.humanize = raw ("human");
    p.vibAmt = raw ("vib") / 100.f;
    p.cvShape = (int) std::lround (raw ("cvShape"));
    p.cvRate = raw ("cvRate");
    p.cvDepth = raw ("cvDepth");
    p.cvDelay = raw ("cvDelay");
    p.transpose = raw ("transpose");
    p.detune = raw ("detune");
    p.formant = raw ("formant");
    p.mix = raw ("mix") / 100.f;
    return p;
}

void EntonadorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (numCh < 1) return;

    engine.setParams (readParams());
    float* left = buffer.getWritePointer (0);
    if (numCh >= 2)
    {
        float* right = buffer.getWritePointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float y = engine.process (0.5f * (left[i] + right[i]));
            left[i] = y;
            right[i] = y;
        }
        for (int ch = 2; ch < numCh; ++ch) buffer.clear (ch, 0, numSamples);
    }
    else
    {
        for (int i = 0; i < numSamples; ++i) left[i] = engine.process (left[i]);
    }
}

juce::AudioProcessorEditor* EntonadorProcessor::createEditor() { return new EntonadorEditor (*this); }

void EntonadorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void EntonadorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EntonadorProcessor(); }
