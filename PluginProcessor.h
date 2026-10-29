#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "FantomineDSP.h"

//==============================================================================
struct FactoryPreset
{
    const char* name;
    float glide, range, tone, vibrato, speed, blend;
    int octave, pedalMode;
};

//==============================================================================
class FantomineAudioProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
public:
    FantomineAudioProcessor();
    ~FantomineAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Fantomine"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // presets
    static const std::vector<FactoryPreset>& getFactoryPresets();
    void loadFactoryPreset (int index);
    void saveUserPreset (int slot);           // slot 1..9
    bool loadUserPreset (int slot);
    bool isUserSlotUsed (int slot) const;
    juce::String getUserSlotName (int slot) const;
    juce::String getCurrentPresetName() const { return currentPresetName; }
    std::atomic<bool> presetDirty { false };   // un potard a bougé depuis le chargement

    // MIDI : pédales d'expression (une par cible) et footswitch
    // v1.3 : MIDI Learn sur toutes les commandes (GLIDE, RANGE, TONE et le sens de la pédale en plus)
    enum MidiTarget { kPitch = 0, kOctave, kVibrato, kSpeed, kBlend, kGlide, kRange, kTone, kPedalMode, kNumTargets };
    static const char* getTargetName (int t);
    void assignCC (int target, int cc);          // target 0..4 ou kLearnFootswitch ; retire ce CC des autres cibles
    static float shapePedal (float e, bool bipolar);
    std::array<std::atomic<int>, kNumTargets> targetCC { { { 11 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 }, { -1 } } };  // -1 = non assigné
    std::atomic<int>  footswitchCC { -1 };     // -1 = non assigné (utiliser le MIDI learn du DAW)
    static constexpr int kLearnFootswitch = 100;
    std::atomic<int>  learnTarget  { -1 };     // -1 = rien, 0..8 = cible, kLearnFootswitch = footswitch
    std::atomic<int>  lastLearned  { -1 };     // dernière cible apprise (lue et remise à -1 par la fenêtre)
    void resetMidiMap();                       // PITCH = CC 11, le reste non assigné

    // octave pilotée en continu par une pédale (façon Whammy)
    std::atomic<bool>  whammyActive { false };
    std::atomic<float> uiOctave { 0.0f };      // décalage réel en octaves (-1..+1) pour le levier
    std::atomic<float> midiActivity { 0.0f };  // clignotement "MIDI" sur l'afficheur
    std::atomic<int>   lastCCSeen { -1 };

    // footswitch (appelé par l'interface ou par le MIDI)
    void footswitchDown();
    void footswitchUp();
    std::atomic<bool> footswitchPressed { false };

    // infos pour l'interface
    std::atomic<float> uiMidi { 57.0f }, uiAmp { 0.0f }, uiExpression { 0.0f };
    std::atomic<bool>  uiNote { false };
    std::array<double, 12> getHarmonicsSnapshot() const;

    // taille de fenêtre mémorisée avec le projet (comme Ring Think) : -1 = défaut 67 %, 1 = 75 %, 2 = 100 %
    std::atomic<int> uiScaleIdx { -1 };

    static constexpr int kStateVersion = 1;

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleMidi (const juce::MidiBuffer& midi, int numSamples);
    void applyValues (float glide, float range, float tone, float vib, float speed,
                      float blend, int octave, int pedalMode);
    juce::File getUserPresetFile() const;
    void loadUserPresetsFromDisk();
    void saveUserPresetsToDisk();

    fantomine::TheremEngine engine;
    fantomine::Params params;

    std::atomic<float>* pGlide = nullptr; std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pTone = nullptr;  std::atomic<float>* pVib = nullptr;
    std::atomic<float>* pSpeed = nullptr; std::atomic<float>* pBlend = nullptr;
    std::atomic<float>* pOctave = nullptr; std::atomic<float>* pPedalMode = nullptr;
    std::atomic<float>* pBypass = nullptr; std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pWhammy = nullptr; std::atomic<float>* pWhammyRange = nullptr;

    // la pédale peut venir du paramètre (automation / MIDI learn du DAW) ou du CC direct
    std::atomic<float> expressionValue { 0.0f };
    int ccMSB = 0;  // pour les CC 14 bits (CC 0..31 + LSB 32..63)

    // footswitch : appui long (> 600 ms) = HOLD
    void checkLongPress();
    std::atomic<double> footDownTimeMs { -1.0 };
    std::atomic<bool> longPressFired { false };
    void setParam (const char* id, float plainValue);

    // Anti-gel v2 (Studio Wiga) : valeurs demandées depuis le thread audio (MIDI, footswitch MIDI),
    // transmises à l'hôte par le timer du processeur (60 Hz, thread message)
    void timerCallback() override;
    static constexpr int kNumQueued = 10;     // cible t (1..8) -> file t + 1
    juce::RangedAudioParameter* queuedParam[kNumQueued] {};
    void setQueuedIndex (int qi, float plainValue);
    static const char* queuedId (int i);
    void setParamQueued (const char* id, float plainValue);
    float queuedOrCurrent (const char* id, const std::atomic<float>* current) const;
    std::atomic<float> pendingValue[kNumQueued];
    std::atomic<bool>  pendingSet[kNumQueued];

    // bypass en fondu
    juce::SmoothedValue<float> wetGain;
    juce::AudioBuffer<float> monoIn, monoOut;

    std::array<std::atomic<float>, 12> harmonics {};   // pas de verrou partagé audio / interface (anti-gel v2)

    std::atomic<bool> applyingPreset { false };
    int currentProgram = 0;
    juce::String currentPresetName { "Classic Theremin" };
    juce::ValueTree userPresets { "UserPresets" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomineAudioProcessor)
};
