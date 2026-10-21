#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr double kLongPressMs = 600.0;
    const juce::StringArray kParamIds { "glide", "range", "tone", "vibrato", "speed", "blend", "octave", "pedalMode" };
}

//==============================================================================
const std::vector<FactoryPreset>& FantomineAudioProcessor::getFactoryPresets()
{
    //                         name                 glide range tone  vib   speed blend oct mode(0 UP,1 DOWN,2 BIP)
    static const std::vector<FactoryPreset> presets {
        { "Classic Theremin",      0.35f, 12.f, 0.35f, 0.30f, 0.45f, 0.75f,  0, 0 },
        { "Clara (Pure Sine)",     0.45f, 12.f, 0.05f, 0.35f, 0.40f, 0.85f,  0, 0 },
        { "Sci-Fi Swoop",          0.80f, 24.f, 0.40f, 0.15f, 0.55f, 0.90f,  0, 2 },
        { "Ghost Choir +1",        0.30f,  7.f, 0.55f, 0.30f, 0.40f, 0.60f,  1, 0 },
        { "Spectre Bass -1",       0.25f, 12.f, 0.45f, 0.10f, 0.35f, 0.65f, -1, 1 },
        { "Saucer Wobble",         0.55f,  5.f, 0.30f, 0.85f, 0.85f, 0.85f,  0, 2 },
        { "Haunted Lead",          0.15f,  2.f, 0.70f, 0.40f, 0.50f, 0.70f,  0, 0 },
        { "Ondes Blend",           0.40f, 12.f, 0.60f, 0.25f, 0.45f, 0.45f,  0, 0 },
        { "Slow Phantom",          1.00f, 24.f, 0.20f, 0.25f, 0.30f, 1.00f,  0, 1 },
        { "Tight Tracker",         0.02f,  2.f, 0.25f, 0.00f, 0.50f, 0.80f,  0, 0 },
        { "Reed Wraith",           0.30f, 12.f, 1.00f, 0.35f, 0.50f, 0.80f,  0, 0 },
        { "Drone Ghost (Hold)",    0.60f, 12.f, 0.30f, 0.20f, 0.35f, 0.70f, -1, 2 },
    };
    return presets;
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FantomineAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 },   "Glide",   NormalisableRange<float> (0.f, 1.f), 0.35f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 },   "Range",   NormalisableRange<float> (1.f, 24.f, 1.f), 12.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String::fromUTF8 ("\xc2\xb1") + String (roundToInt (v)) + " st"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 },    "Tone",    NormalisableRange<float> (0.f, 1.f), 0.35f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vibrato", 1 }, "Vibrato", NormalisableRange<float> (0.f, 1.f), 0.30f, pct));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 },   "Speed",   NormalisableRange<float> (0.f, 1.f), 0.45f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (3.0 * std::pow (3.0, (double) v), 1) + " Hz"; })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "blend", 1 },   "Blend",   NormalisableRange<float> (0.f, 1.f), 0.75f, pct));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "octave", 1 }, "Octave", StringArray { "-1", "0", "+1" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "pedalMode", 1 }, "Pedal Mode", StringArray { "Up", "Down", "Bipolar" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "expression", 1 }, "Expression", NormalisableRange<float> (0.f, 1.f), 0.0f, pct));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 },   "Hold",   false));

    return { p.begin(), p.end() };
}

//==============================================================================
FantomineAudioProcessor::FantomineAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FantomineState", createLayout())
{
    pGlide = apvts.getRawParameterValue ("glide");
    pRange = apvts.getRawParameterValue ("range");
    pTone  = apvts.getRawParameterValue ("tone");
    pVib   = apvts.getRawParameterValue ("vibrato");
    pSpeed = apvts.getRawParameterValue ("speed");
    pBlend = apvts.getRawParameterValue ("blend");
    pOctave = apvts.getRawParameterValue ("octave");
    pPedalMode = apvts.getRawParameterValue ("pedalMode");
    pBypass = apvts.getRawParameterValue ("bypass");
    pHold   = apvts.getRawParameterValue ("hold");

    apvts.addParameterListener ("expression", this);
    for (auto& id : kParamIds) apvts.addParameterListener (id, this);

    loadUserPresetsFromDisk();
}

FantomineAudioProcessor::~FantomineAudioProcessor()
{
    apvts.removeParameterListener ("expression", this);
    for (auto& id : kParamIds) apvts.removeParameterListener (id, this);
}

void FantomineAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == "expression")
        expressionValue.store (newValue);
    else if (! applyingPreset.load())
        presetDirty.store (true);
}

//==============================================================================
bool FantomineAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void FantomineAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    wetGain.reset (sampleRate, 0.012);
    wetGain.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    monoIn.setSize (1, samplesPerBlock, false, false, true);
    monoOut.setSize (1, samplesPerBlock, false, false, true);
}

//==============================================================================
void FantomineAudioProcessor::setParam (const char* id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

void FantomineAudioProcessor::footswitchDown()
{
    if (footswitchPressed.exchange (true)) return;
    footDownTimeMs.store (juce::Time::getMillisecondCounterHiRes());
    longPressFired.store (false);
}

void FantomineAudioProcessor::footswitchUp()
{
    if (! footswitchPressed.exchange (false)) return;
    footDownTimeMs.store (-1.0);

    if (longPressFired.load()) return;   // l'appui long a déjà agi

    // appui court : sort du HOLD s'il est actif, sinon bascule le bypass
    if (pHold->load() > 0.5f)
        setParam ("hold", 0.0f);
    else
        setParam ("bypass", pBypass->load() > 0.5f ? 0.0f : 1.0f);
}

void FantomineAudioProcessor::checkLongPress()
{
    const double t0 = footDownTimeMs.load();
    if (t0 < 0.0 || longPressFired.load()) return;
    if (juce::Time::getMillisecondCounterHiRes() - t0 >= kLongPressMs)
    {
        longPressFired.store (true);
        if (pBypass->load() > 0.5f) setParam ("bypass", 0.0f);   // HOLD rallume la pédale
        setParam ("hold", pHold->load() > 0.5f ? 0.0f : 1.0f);
    }
}

//==============================================================================
void FantomineAudioProcessor::handleMidi (const juce::MidiBuffer& midi, int)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;

        const int cc = m.getControllerNumber();
        const int val = m.getControllerValue();
        lastCCSeen.store (cc);
        midiActivity.store (1.0f);

        // MIDI learn depuis le menu clic droit
        const int learn = learnTarget.load();
        if (learn == 1 && (cc < 32 || cc > 63)) { expressionCC.store (cc); learnTarget.store (0); }
        else if (learn == 2)                     { footswitchCC.store (cc); learnTarget.store (0); }

        const int expCC = expressionCC.load();
        if (cc == expCC)
        {
            ccMSB = val;
            expressionValue.store ((float) val / 127.0f);
        }
        else if (expCC < 32 && cc == expCC + 32)
        {
            // LSB d'un CC 14 bits : résolution de 16384 pas
            expressionValue.store ((float) ((ccMSB << 7) | val) / 16383.0f);
        }
        else if (cc == footswitchCC.load())
        {
            if (val >= 64) footswitchDown(); else footswitchUp();
        }
    }
}

void FantomineAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    handleMidi (midi, n);
    checkLongPress();

    if (monoIn.getNumSamples() < n)
    {
        monoIn.setSize (1, n, false, false, true);
        monoOut.setSize (1, n, false, false, true);
    }

    // entrée : somme mono
    auto* mi = monoIn.getWritePointer (0);
    if (numIn > 0)
    {
        juce::FloatVectorOperations::copy (mi, buffer.getReadPointer (0), n);
        for (int c = 1; c < numIn; ++c)
            juce::FloatVectorOperations::add (mi, buffer.getReadPointer (c), n);
        if (numIn > 1) juce::FloatVectorOperations::multiply (mi, 1.0f / (float) numIn, n);
    }
    else juce::FloatVectorOperations::clear (mi, n);

    params.glide      = pGlide->load();
    params.rangeSemis = pRange->load();
    params.tone       = pTone->load();
    params.vibrato    = pVib->load();
    params.speed      = pSpeed->load();
    params.blend      = pBlend->load();
    params.octave     = juce::roundToInt (pOctave->load()) - 1;
    params.pedalMode  = juce::roundToInt (pPedalMode->load());
    params.expression = expressionValue.load();
    params.hold       = pHold->load() > 0.5f;

    auto* mo = monoOut.getWritePointer (0);
    engine.process (mi, mo, n, params);

    // bypass avec fondu de 12 ms (pas de clic)
    wetGain.setTargetValue (pBypass->load() > 0.5f ? 0.0f : 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const float w = wetGain.getNextValue();
        mo[i] = mi[i] + w * (mo[i] - mi[i]);
    }

    for (int c = 0; c < numOut; ++c)
        buffer.copyFrom (c, 0, monoOut, 0, 0, n);

    // infos interface
    uiMidi.store ((float) engine.displayMidi);
    uiAmp.store ((float) engine.displayAmp);
    uiNote.store (engine.noteDetected);
    uiExpression.store (params.expression);
    {
        const juce::SpinLock::ScopedTryLockType lock (harmLock);
        if (lock.isLocked()) harmonics = engine.getHarmonics();
    }
}

std::array<double, 12> FantomineAudioProcessor::getHarmonicsSnapshot() const
{
    const juce::SpinLock::ScopedLockType lock (harmLock);
    return harmonics;
}

//==============================================================================
void FantomineAudioProcessor::applyValues (float glide, float range, float tone, float vib, float speed,
                                           float blend, int octave, int pedalMode)
{
    applyingPreset.store (true);
    setParam ("glide", glide);   setParam ("range", range);
    setParam ("tone", tone);     setParam ("vibrato", vib);
    setParam ("speed", speed);   setParam ("blend", blend);
    setParam ("octave", (float) (octave + 1));
    setParam ("pedalMode", (float) pedalMode);
    applyingPreset.store (false);
    presetDirty.store (false);
}

int FantomineAudioProcessor::getNumPrograms() { return (int) getFactoryPresets().size(); }

void FantomineAudioProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        loadFactoryPreset (index);
}

const juce::String FantomineAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        return getFactoryPresets()[(size_t) index].name;
    return {};
}

void FantomineAudioProcessor::loadFactoryPreset (int index)
{
    const auto& p = getFactoryPresets()[(size_t) index];
    applyValues (p.glide, p.range, p.tone, p.vibrato, p.speed, p.blend, p.octave, p.pedalMode);
    currentProgram = index;
    currentPresetName = p.name;
}

//==============================================================================
juce::File FantomineAudioProcessor::getUserPresetFile() const
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    base = base.getChildFile ("Application Support");   // sur macOS, userApplicationDataDirectory = ~/Library
   #endif
    return base.getChildFile ("Studio Wiga").getChildFile ("Fantomine").getChildFile ("UserPresets.xml");
}

void FantomineAudioProcessor::loadUserPresetsFromDisk()
{
    const auto f = getUserPresetFile();
    if (f.existsAsFile())
        if (auto xml = juce::XmlDocument::parse (f))
            userPresets = juce::ValueTree::fromXml (*xml);
    if (! userPresets.isValid() || ! userPresets.hasType ("UserPresets"))
        userPresets = juce::ValueTree ("UserPresets");
}

void FantomineAudioProcessor::saveUserPresetsToDisk()
{
    const auto f = getUserPresetFile();
    f.getParentDirectory().createDirectory();
    if (auto xml = userPresets.createXml())
        xml->writeTo (f);
}

bool FantomineAudioProcessor::isUserSlotUsed (int slot) const
{
    return userPresets.getChildWithProperty ("slot", slot).isValid();
}

juce::String FantomineAudioProcessor::getUserSlotName (int slot) const
{
    auto c = userPresets.getChildWithProperty ("slot", slot);
    return c.isValid() ? c.getProperty ("name").toString() : juce::String();
}

void FantomineAudioProcessor::saveUserPreset (int slot)
{
    auto existing = userPresets.getChildWithProperty ("slot", slot);
    if (existing.isValid()) userPresets.removeChild (existing, nullptr);

    juce::ValueTree t ("Preset");
    t.setProperty ("slot", slot, nullptr);
    t.setProperty ("name", "User " + juce::String (slot), nullptr);
    for (auto& id : kParamIds)
        t.setProperty (id, apvts.getRawParameterValue (id)->load(), nullptr);
    userPresets.appendChild (t, nullptr);
    saveUserPresetsToDisk();
    currentPresetName = "User " + juce::String (slot);
    presetDirty.store (false);
}

bool FantomineAudioProcessor::loadUserPreset (int slot)
{
    auto t = userPresets.getChildWithProperty ("slot", slot);
    if (! t.isValid()) return false;
    applyingPreset.store (true);
    for (auto& id : kParamIds)
        if (t.hasProperty (id))
            setParam (id.toRawUTF8(), (float) t.getProperty (id));
    applyingPreset.store (false);
    presetDirty.store (false);
    currentPresetName = t.getProperty ("name").toString();
    return true;
}

//==============================================================================
void FantomineAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", kStateVersion, nullptr);
    state.setProperty ("expressionCC", expressionCC.load(), nullptr);
    state.setProperty ("footswitchCC", footswitchCC.load(), nullptr);
    state.setProperty ("uiScaleIdx", uiScaleIdx.load(), nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FantomineAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (apvts.state.getType())) return;
        expressionCC.store ((int) state.getProperty ("expressionCC", 11));
        footswitchCC.store ((int) state.getProperty ("footswitchCC", -1));
        uiScaleIdx.store ((int) state.getProperty ("uiScaleIdx", -1));
        currentPresetName = state.getProperty ("presetName", "Classic Theremin").toString();
        currentProgram = (int) state.getProperty ("program", 0);
        apvts.replaceState (state);
        expressionValue.store (apvts.getRawParameterValue ("expression")->load());
    }
}

//==============================================================================
juce::AudioProcessorEditor* FantomineAudioProcessor::createEditor()
{
    return new FantomineAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FantomineAudioProcessor();
}
