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

// Une pédale = une fonction : un CC appris pour une cible est retiré des autres cibles
// (sinon la pédale de whammy, souvent sur le CC 11, courbait aussi la hauteur et faussait les notes).
void FantomineAudioProcessor::assignCC (int target, int cc)
{
    for (int t = 0; t < kNumTargets; ++t)
        if (t != target && targetCC[(size_t) t].load() == cc) targetCC[(size_t) t].store (-1);
    if (target != kLearnFootswitch && footswitchCC.load() == cc) footswitchCC.store (-1);

    if (target == kLearnFootswitch) footswitchCC.store (cc);
    else                            targetCC[(size_t) target].store (cc);

    // la hauteur repart à zéro si sa pédale vient d'être retirée
    if (targetCC[kPitch].load() < 0) expressionValue.store (0.0f);
}

void FantomineAudioProcessor::resetMidiMap()
{
    learnTarget.store (-1);
    for (int t = 0; t < kNumTargets; ++t) targetCC[(size_t) t].store (t == kPitch ? 11 : -1);
    footswitchCC.store (-1);
}

// Courbe de pédale : zone morte au talon (et au centre en BIPOLAR) et fin de course atteinte avant la butée.
// Une pédale d'expression envoie rarement 0 pile au talon : sans cette zone, la voix restait désaccordée.
float FantomineAudioProcessor::shapePedal (float e, bool bipolar)
{
    constexpr float dead = 0.04f, top = 0.97f;
    e = juce::jlimit (0.0f, 1.0f, e);
    if (! bipolar) return juce::jlimit (0.0f, 1.0f, (e - dead) / (top - dead));
    const float d = e - 0.5f;
    const float a = juce::jlimit (0.0f, 1.0f, (std::abs (d) - dead * 0.5f) / (0.5f - dead * 0.5f - (1.0f - top)));
    return 0.5f + (d < 0 ? -0.5f : 0.5f) * a;
}

const char* FantomineAudioProcessor::getTargetName (int t)
{
    static const char* n[] = { "PITCH", "OCTAVE", "VIBRATO", "SPEED", "BLEND", "GLIDE", "RANGE", "TONE", "PEDAL MODE" };
    if (t == kLearnFootswitch) return "FOOTSWITCH";
    return juce::isPositiveAndBelow (t, (int) kNumTargets) ? n[t] : "";
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
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "whammy", 1 }, "Octave Whammy", NormalisableRange<float> (0.f, 1.f), 0.5f, pct));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "whammyRange", 1 }, "Whammy Range",
                                                         StringArray { "-1 > +1 oct", "0 > +1 oct", "0 > -1 oct" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "pedalMode", 1 }, "Pedal Mode", StringArray { "Up", "Down", "Bipolar" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "expression", 1 }, "Expression", NormalisableRange<float> (0.f, 1.f), 0.0f, pct));
    // v1.3.1 : sens des pédales d'expression MIDI inversé par défaut (comme Crycat et Wharry)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "invert", 1 }, "MIDI Pedal Invert", true));
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
    pWhammy = apvts.getRawParameterValue ("whammy");
    pWhammyRange = apvts.getRawParameterValue ("whammyRange");
    pHold   = apvts.getRawParameterValue ("hold");
    pInvert = apvts.getRawParameterValue ("invert");

    apvts.addParameterListener ("expression", this);
    apvts.addParameterListener ("whammy", this);
    for (auto& id : kParamIds) apvts.addParameterListener (id, this);

    for (int i = 0; i < kNumQueued; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; queuedParam[i] = apvts.getParameter (queuedId (i)); }
    for (auto& h : harmonics) h.store (0.0f);

    loadUserPresetsFromDisk();
    startTimerHz (60);
}

FantomineAudioProcessor::~FantomineAudioProcessor()
{
    stopTimer();
    apvts.removeParameterListener ("expression", this);
    apvts.removeParameterListener ("whammy", this);
    for (auto& id : kParamIds) apvts.removeParameterListener (id, this);
}

void FantomineAudioProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == "expression")
        expressionValue.store (newValue);
    else if (id == "whammy")
        whammyActive.store (true);                 // la pédale prend la main sur le levier d'octave
    else
    {
        if (id == "octave") whammyActive.store (false);   // levier touché (ou preset) : retour aux 3 positions
        if (! applyingPreset.load()) presetDirty.store (true);
    }
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

// Anti-gel v2 (Studio Wiga) : jamais d'appel à l'hôte (setValueNotifyingHost) ni d'allocation depuis le
// thread audio. Le MIDI et le footswitch (qui peut être piloté en MIDI) mettent la valeur en file ;
// le timer du processeur (thread message) la transmet au DAW et à la fenêtre.
const char* FantomineAudioProcessor::queuedId (int i)
{
    static const char* ids[kNumQueued] = { "hold", "bypass", "whammy", "vibrato", "speed", "blend", "glide", "range", "tone", "pedalMode" };
    return ids[juce::jlimit (0, kNumQueued - 1, i)];
}

void FantomineAudioProcessor::setParamQueued (const char* id, float plainValue)
{
    for (int i = 0; i < kNumQueued; ++i)
        if (std::strcmp (id, queuedId (i)) == 0)
        {
            pendingValue[i].store (plainValue, std::memory_order_relaxed);
            pendingSet[i].store (true, std::memory_order_release);
            return;
        }
}

void FantomineAudioProcessor::setQueuedIndex (int qi, float plainValue)
{
    if (qi < 0 || qi >= kNumQueued) return;
    pendingValue[qi].store (plainValue, std::memory_order_relaxed);
    pendingSet[qi].store (true, std::memory_order_release);
}

// bascule : part de la valeur en file si elle existe, sinon de la valeur courante
float FantomineAudioProcessor::queuedOrCurrent (const char* id, const std::atomic<float>* current) const
{
    for (int i = 0; i < kNumQueued; ++i)
        if (std::strcmp (id, queuedId (i)) == 0 && pendingSet[i].load (std::memory_order_acquire))
            return pendingValue[i].load (std::memory_order_relaxed);
    return current->load (std::memory_order_relaxed);
}

void FantomineAudioProcessor::timerCallback()
{
    for (int i = 0; i < kNumQueued; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acquire))
            if (auto* p = apvts.getParameter (queuedId (i)))
                p->setValueNotifyingHost (p->convertTo0to1 (pendingValue[i].load (std::memory_order_relaxed)));
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
    if (queuedOrCurrent ("hold", pHold) > 0.5f)
        setParamQueued ("hold", 0.0f);
    else
        setParamQueued ("bypass", queuedOrCurrent ("bypass", pBypass) > 0.5f ? 0.0f : 1.0f);
}

void FantomineAudioProcessor::checkLongPress()
{
    const double t0 = footDownTimeMs.load();
    if (t0 < 0.0 || longPressFired.load()) return;
    if (juce::Time::getMillisecondCounterHiRes() - t0 >= kLongPressMs)
    {
        longPressFired.store (true);
        if (queuedOrCurrent ("bypass", pBypass) > 0.5f) setParamQueued ("bypass", 0.0f);   // HOLD rallume la pédale
        setParamQueued ("hold", queuedOrCurrent ("hold", pHold) > 0.5f ? 0.0f : 1.0f);
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

        // MIDI learn depuis le menu clic droit (le CC appris ne fait rien d'autre dans ce message)
        const int learn = learnTarget.load();
        if (cc < 120 && (learn == kLearnFootswitch || (learn >= 0 && learn < kNumTargets && (learn != kPitch || cc < 32 || cc > 63))))
        {
            assignCC (learn, cc);
            lastLearned.store (learn);
            learnTarget.store (-1);
            continue;
        }

        const float v = (float) val / 127.0f;
        // inversion (menu « Invert MIDI pedal direction », active par défaut) : pédales PITCH et OCTAVE seulement
        const bool inv = pInvert->load (std::memory_order_relaxed) > 0.5f;
        const int pitchCC = targetCC[kPitch].load();
        if (cc == pitchCC)
        {
            ccMSB = val;
            expressionValue.store (inv ? 1.0f - v : v);
        }
        else if (pitchCC >= 0 && pitchCC < 32 && cc == pitchCC + 32)
        {
            // LSB d'un CC 14 bits : résolution de 16384 pas
            const float v14 = (float) ((ccMSB << 7) | val) / 16383.0f;
            expressionValue.store (inv ? 1.0f - v14 : v14);
        }

        // chaque CC pilote une seule cible (voir assignCC) ; cible t -> file t + 1
        for (int t = kOctave; t < kNumTargets; ++t)
            if (cc == targetCC[(size_t) t].load())
            {
                if (t == kPedalMode) setQueuedIndex (t + 1, (float) juce::jmin (2, val * 3 / 128));   // 3 positions UP / DOWN / BIPOLAR
                else                 setQueuedIndex (t + 1, queuedParam[t + 1]->convertFrom0to1 (t == kOctave && inv ? 1.0f - v : v));  // le potard (ou le levier) suit la pédale
            }

        if (cc == footswitchCC.load())
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
    if (whammyActive.load())
    {
        const int wr = juce::roundToInt (pWhammyRange->load());
        const float w = shapePedal (pWhammy->load(), wr == 0);
        switch (wr)
        {
            case 1:  params.octaveSemis =  12.0f * w; break;            // talon 0, pointe +1 octave
            case 2:  params.octaveSemis = -12.0f * w; break;            // talon 0, pointe -1 octave
            default: params.octaveSemis = -12.0f + 24.0f * w; break;    // talon -1, milieu 0, pointe +1
        }
    }
    else
        params.octaveSemis = 12.0f * (float) (juce::roundToInt (pOctave->load()) - 1);
    uiOctave.store (params.octaveSemis / 12.0f);
    params.pedalMode  = juce::roundToInt (pPedalMode->load());
    params.expression = shapePedal (expressionValue.load(), params.pedalMode == 2);
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
    const auto& h = engine.getHarmonics();
    for (size_t i = 0; i < harmonics.size(); ++i) harmonics[i].store ((float) h[i], std::memory_order_relaxed);
}

std::array<double, 12> FantomineAudioProcessor::getHarmonicsSnapshot() const
{
    std::array<double, 12> out {};
    for (size_t i = 0; i < out.size(); ++i) out[i] = (double) harmonics[i].load (std::memory_order_relaxed);
    return out;
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
    for (int t = 0; t < kNumTargets; ++t)
        state.setProperty ("cc" + juce::String (t), targetCC[(size_t) t].load(), nullptr);
    state.setProperty ("whammyActive", whammyActive.load(), nullptr);
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
        learnTarget.store (-1);
        for (int t = 0; t < kNumTargets; ++t)      // cc5..cc8 absents des projets < 1.3 : non assignés
            targetCC[(size_t) t].store (juce::jlimit (-1, 127, (int) state.getProperty ("cc" + juce::String (t), t == kPitch ? 11 : -1)));
        if (state.hasProperty ("expressionCC"))                       // projets de la première version
            targetCC[kPitch].store ((int) state.getProperty ("expressionCC"));
        footswitchCC.store ((int) state.getProperty ("footswitchCC", -1));
        uiScaleIdx.store ((int) state.getProperty ("uiScaleIdx", -1));
        currentPresetName = state.getProperty ("presetName", "Classic Theremin").toString();
        currentProgram = (int) state.getProperty ("program", 0);
        const bool wa = (bool) state.getProperty ("whammyActive", false);
        apvts.replaceState (state);
        whammyActive.store (wa);
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
