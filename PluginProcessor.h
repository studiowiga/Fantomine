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
                                private juce::AudioProcessorValueTreeState::Listener
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

    // MIDI : pédale d'expression et footswitch
    std::atomic<int>  expressionCC { 11 };     // CC 11 = Expression (standard)
    std::atomic<int>  footswitchCC { -1 };     // -1 = non assigné (utiliser le MIDI learn du DAW)
    std::atomic<int>  learnTarget  { 0 };      // 0 = rien, 1 = pédale, 2 = footswitch
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

    // la pédale peut venir du paramètre (automation / MIDI learn du DAW) ou du CC direct
    std::atomic<float> expressionValue { 0.0f };
    int ccMSB = 0;  // pour les CC 14 bits (CC 0..31 + LSB 32..63)

    // footswitch : appui long (> 600 ms) = HOLD
    void checkLongPress();
    std::atomic<double> footDownTimeMs { -1.0 };
    std::atomic<bool> longPressFired { false };
    void setParam (const char* id, float plainValue);

    // bypass en fondu
    juce::SmoothedValue<float> wetGain;
    juce::AudioBuffer<float> monoIn, monoOut;

    std::array<double, 12> harmonics {};
    juce::SpinLock harmLock;

    std::atomic<bool> applyingPreset { false };
    int currentProgram = 0;
    juce::String currentPresetName { "Classic Theremin" };
    juce::ValueTree userPresets { "UserPresets" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomineAudioProcessor)
};
