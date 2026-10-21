#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Tout est exprimé en pixels du visuel background.png (1240 x 928, même format que Ring Think).
namespace ui
{
    constexpr int   kW = 1240, kH = 928;
    constexpr int   kRefW = 993;                              // tailles 67 / 75 / 100 % exprimées sur cette largeur (comme Ring Think)
    constexpr float kDefaultScale = 661.0f / 993.0f;          // fenêtre par défaut = 661 x 495 px, et taille minimale
    constexpr int   kMaxW = 1500;
}

//==============================================================================
// État lumineux partagé : 1 = pédale active, 0 = bypass (toutes les lumières éteintes)
struct Lights
{
    float power = 1.0f;       // fondu global
    float title = 1.0f;       // néon du titre (petit scintillement à l'allumage)
    float led   = 0.0f;       // LED au-dessus du footswitch
};

//==============================================================================
// Potard : le corps est dans le visuel, seul le trait lumineux (et le repère) tourne.
class PedalKnob : public juce::Slider
{
public:
    PedalKnob (juce::String labelText, const Lights& l);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    juce::String label;
private:
    const Lights& lights;
};

//==============================================================================
class OctaveSwitch : public juce::Component
{
public:
    OctaveSwitch (juce::RangedAudioParameter& p, const juce::Image& handle);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onChange;
    int position = 1;
    float anim = 1.0f;
private:
    void setFromX (float x);
    juce::ParameterAttachment attachment;
    juce::Image handle;
};

//==============================================================================
class Footswitch : public juce::Component
{
public:
    explicit Footswitch (FantomineAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;
    float pressAnim = 0.0f;
private:
    FantomineAudioProcessor& proc;
};

//==============================================================================
// Écran : nom du preset, sens de la pédale, voix fantôme animée, note jouée, barre EXP.
class GhostDisplay : public juce::Component
{
public:
    GhostDisplay (FantomineAudioProcessor& p, const Lights& l) : proc (p), lights (l) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (float dt);
    std::function<void (const juce::String&)> showStatus;

private:
    void showPresetMenu();
    void stepPreset (int delta);
    void setExpressionFromY (float y);
    juce::Rectangle<float> presetArea() const, prevArea() const, nextArea() const,
                           modeArea() const, expArea() const;

    FantomineAudioProcessor& proc;
    const Lights& lights;
    float phase = 0.0f, ampSm = 0.0f, midiSm = 57.0f, expSm = 0.0f, midiBlink = 0.0f;
    bool draggingExp = false;
};

//==============================================================================
class PedalView : public juce::Component, private juce::Slider::Listener
{
public:
    explicit PedalView (FantomineAudioProcessor&);
    ~PedalView() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick (float dt);
    std::function<void (int)> onSizeRequest;     // -1 = défaut (67 %), 1 = 75 %, 2 = 100 %
    std::function<int()> getSizeIndex;

private:
    void sliderValueChanged (juce::Slider*) override;
    void sliderDragStarted (juce::Slider*) override;
    void showContextMenu();
    void setStatus (const juce::String& s, double seconds = 2.5);

    FantomineAudioProcessor& proc;
    Lights lights;
    juce::Image background, titleOn, ledOn, octHandle;

    PedalKnob glide { "GLIDE", lights }, range { "RANGE", lights }, tone { "TONE", lights },
              vibrato { "VIBRATO", lights }, speed { "SPEED", lights }, blend { "BLEND", lights };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<OctaveSwitch> octave;
    Footswitch footswitch;
    GhostDisplay display;

    juce::String statusText;
    double statusUntil = 0.0;
    float ledPulse = 0.0f, flickerT = 1.0f, lastPower = -1.0f;
    bool wasOn = true;
};

//==============================================================================
class FantomineAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FantomineAudioProcessorEditor (FantomineAudioProcessor&);
    ~FantomineAudioProcessorEditor() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void resized() override;

    int  getScaleIndex() const;
    void setScaleIndex (int idx);

private:
    void timerCallback() override;

    FantomineAudioProcessor& proc;
    PedalView pedal;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FantomineAudioProcessorEditor)
};
