#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    // ---- géométrie (pixels de background.png, 1240 x 928) ----
    const juce::Point<float> kKnobC[6] = { { 176.4f, 431.3f }, { 349.1f, 431.3f }, { 521.8f, 431.3f },
                                           { 715.2f, 431.3f }, { 888.7f, 431.3f }, { 1061.4f, 431.3f } };
    constexpr float kKnobHit = 66.0f;
    constexpr float kPtrR0 = 13.0f, kPtrR1 = 45.0f, kDotR = 54.5f;

    const juce::Rectangle<int> kScreen  { 643, 83, 464, 162 };
    const juce::Rectangle<int> kStatus  { 429, 627, 379, 33 };
    const juce::Point<int>     kTitlePos { 49, 64 };
    const juce::Point<int>     kLedPos   { 573, 545 };
    const juce::Rectangle<int> kOctave  { 100, 755, 300, 85 };
    const juce::Point<float>   kHandlePos { 105.19f, 1.76f };   // dans kOctave, position « 0 »
    constexpr float kHandleScale = 1240.0f / 1680.0f;          // sprite en résolution d'origine du design
    constexpr float kHandleTravel = 66.0f;
    const juce::Point<float>   kFootC   { 620.0f, 804.0f };

    constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kEndAngle   = juce::MathConstants<float>::pi * 2.75f;

    const float kScales[3] = { ui::kDefaultScale, 0.75f, 1.0f };

    // ---- couleurs relevées sur le design ----
    const juce::Colour kMint     { 0xff7ef0d6 };
    const juce::Colour kMintCore { 0xffb0f6f2 };
    const juce::Colour kText     { 0xffe2e8ec };
    const juce::Colour kTeal     { 0xff78beb4 };
    const juce::Colour kViolet   { 0xffb48cff };
    const juce::Colour kGroove   { 0xff241e34 };

    juce::Typeface::Ptr typeface (bool semibold)
    {
        static auto sb = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        static auto rg = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf,  BinaryData::InterRegular_otfSize);
        return semibold ? sb : rg;
    }

    juce::Font uiFont (float size, bool semibold = true)
    {
        return juce::Font (juce::FontOptions (typeface (semibold)).withHeight (size));
    }

    juce::Point<float> polar (juce::Point<float> c, float r, float angle)
    {
        return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
    }

    juce::String noteName (float midi, int& centsOut)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = juce::roundToInt (midi);
        centsOut = juce::roundToInt ((midi - (float) n) * 100.0f);
        const int pc = ((n % 12) + 12) % 12;
        return juce::String (names[pc]) + juce::String (n / 12 - 1);
    }

    bool isBypassed (FantomineAudioProcessor& p) { return p.apvts.getRawParameterValue ("bypass")->load() > 0.5f; }
    bool isHold (FantomineAudioProcessor& p)     { return p.apvts.getRawParameterValue ("hold")->load() > 0.5f; }

    juce::Image loadImage (const void* data, int size) { return juce::ImageCache::getFromMemory (data, size); }
}

//==============================================================================
PedalKnob::PedalKnob (juce::String labelText, const Lights& l) : label (std::move (labelText)), lights (l)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kStartAngle, kEndAngle, true);
    setMouseDragSensitivity (260);
    setScrollWheelEnabled (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool PedalKnob::hitTest (int x, int y)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) <= kKnobHit;
}

void PedalKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { getParentComponent()->mouseDown (e.getEventRelativeTo (getParentComponent())); return; }
    juce::Slider::mouseDown (e);
}

void PedalKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float prop = (float) valueToProportionOfLength (getValue());
    const float angle = kStartAngle + prop * (kEndAngle - kStartAngle);
    const auto p0 = polar (c, kPtrR0, angle), p1 = polar (c, kPtrR1, angle);
    const float on = lights.power;

    // rainure éteinte (toujours présente sous le trait, visible en bypass)
    g.setColour (kGroove.withAlpha (0.85f));
    g.drawLine ({ p0, p1 }, 3.6f);
    g.setColour (juce::Colour (0xff9a8cc4).withAlpha (0.22f * (1.0f - on)));
    g.drawLine ({ p0.translated (0.9f, 1.2f), p1.translated (0.9f, 1.2f) }, 1.0f);

    // trait lumineux
    if (on > 0.01f)
    {
        g.setColour (kMint.withAlpha (0.16f * on)); g.drawLine ({ p0, p1 }, 10.0f);
        g.setColour (kMint.withAlpha (0.38f * on)); g.drawLine ({ p0, p1 }, 5.6f);
        g.setColour (kMintCore.withAlpha (on));     g.drawLine ({ p0, p1 }, 3.0f);
    }

    // repère blanc sur la jupe
    const auto dot = polar (c, kDotR, angle);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (dot.x - 2.6f + 0.6f, dot.y - 2.6f + 0.9f, 5.2f, 5.2f);
    g.setColour (juce::Colour (0xffeef3fa).interpolatedWith (juce::Colour (0xff8a889a), 1.0f - (0.5f + 0.5f * on)));
    g.fillEllipse (dot.x - 2.6f, dot.y - 2.6f, 5.2f, 5.2f);
}

//==============================================================================
OctaveSwitch::OctaveSwitch (FantomineAudioProcessor& pr, juce::RangedAudioParameter& p, const juce::Image& h)
    : proc (pr), attachment (p, [this] (float v)
      {
          position = juce::jlimit (0, 2, juce::roundToInt (v));
          if (onChange) onChange();
          repaint();
      }),
      handle (h)
{
    attachment.sendInitialUpdate();
    anim = (float) position;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void OctaveSwitch::paint (juce::Graphics& g)
{
    // dessin en sous-pixel, à partir du sprite pleine résolution : net à toutes les tailles de fenêtre
    const float dx = (anim - 1.0f) * kHandleTravel;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (handle, juce::AffineTransform::scale (kHandleScale)
                                        .translated (kHandlePos.x + dx, kHandlePos.y));
}

void OctaveSwitch::setFromX (float x)
{
    const float c = kHandlePos.x + (float) handle.getWidth() * kHandleScale * 0.5f;
    const int pos = x < c - kHandleTravel * 0.5f ? 0 : (x > c + kHandleTravel * 0.5f ? 2 : 1);
    if (proc.whammyActive.load())
    {
        proc.whammyActive.store (false);          // le levier reprend la main sur la pédale
        if (pos == position && onChange) onChange();
    }
    if (pos != position) attachment.setValueAsCompleteGesture ((float) pos);
}

void OctaveSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { getParentComponent()->mouseDown (e.getEventRelativeTo (getParentComponent())); return; }
    setFromX (e.position.x);
}
void OctaveSwitch::mouseDrag (const juce::MouseEvent& e) { if (! e.mods.isPopupMenu()) setFromX (e.position.x); }

//==============================================================================
// coordonnées locales : 160 x 160, centre (80, 80)
bool Footswitch::hitTest (int x, int y) { return juce::Point<float> (80.0f, 80.0f).getDistanceFrom ({ (float) x, (float) y }) <= 72.0f; }

void Footswitch::paint (juce::Graphics& g)
{
    if (pressAnim < 0.01f) return;
    // le chapeau s'enfonce : il s'assombrit et son ombre interne s'élargit
    const juce::Point<float> c (80.0f, 80.0f);
    const float r = 50.0f;
    g.setColour (juce::Colours::black.withAlpha (0.28f * pressAnim));
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f), c.x, c.y,
                             juce::Colours::black.withAlpha (0.45f * pressAnim), c.x + r, c.y, true);
    g.setGradientFill (sh);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
}

void Footswitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { getParentComponent()->mouseDown (e.getEventRelativeTo (getParentComponent())); return; }
    proc.footswitchDown();
}

void Footswitch::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    proc.footswitchUp();
}

//==============================================================================
// coordonnées locales de l'écran : 464 x 162 (positions relevées sur le design)
juce::Rectangle<float> GhostDisplay::prevArea() const   { return { 6.0f, 8.0f, 22.0f, 24.0f }; }
juce::Rectangle<float> GhostDisplay::presetArea() const { return { 30.0f, 8.0f, 218.0f, 24.0f }; }
juce::Rectangle<float> GhostDisplay::nextArea() const   { return { 248.0f, 8.0f, 22.0f, 24.0f }; }
juce::Rectangle<float> GhostDisplay::modeArea() const   { return { 298.2f, 9.6f, 80.4f, 21.4f }; }
juce::Rectangle<float> GhostDisplay::expArea() const    { return { 425.1f, 27.3f, 29.5f, 125.5f }; }

void GhostDisplay::tick (float dt)
{
    ampSm  += (proc.uiAmp.load() - ampSm) * juce::jmin (1.0f, dt * 12.0f);
    midiSm += (proc.uiMidi.load() - midiSm) * juce::jmin (1.0f, dt * 20.0f);
    expSm  += (proc.uiExpression.load() - expSm) * juce::jmin (1.0f, dt * 25.0f);
    phase  += dt * 1.6f;
    if (proc.midiActivity.exchange (0.0f) > 0.0f) midiBlink = 1.0f;
    midiBlink = juce::jmax (0.0f, midiBlink - dt * 4.0f);

    // la voix fantôme ondule en continu quand la pédale est allumée : on redessine à chaque tic.
    // Éteint (bypass), paint() ne dessine rien : un dernier redessin pour effacer, puis plus rien.
    const float on = lights.power;
    if (on >= 0.01f || drawnPower >= 0.01f)
    {
        if (auto* pv = dynamic_cast<PedalView*> (getParentComponent())) pv->askRepaint (getBounds());   // suivi du chien de garde
        else repaint();
    }
    drawnPower = on;
}

void GhostDisplay::paint (juce::Graphics& g)
{
    const float on = lights.power;
    if (on < 0.01f) return;                       // bypass : écran éteint, seule la vitre du visuel reste
    const auto b = getLocalBounds().toFloat();
    g.setOpacity (1.0f);

    juce::Path clip; clip.addRoundedRectangle (b, 10.0f);
    g.reduceClipRegion (clip);

    // trame de l'écran
    g.setColour (kMint.withAlpha (0.045f * on));
    for (float y = 6.0f; y < b.getHeight() - 4.0f; y += 7.5f)
        g.drawHorizontalLine ((int) y, 6.0f, 416.0f);
    g.setColour (kMint.withAlpha (0.025f * on));
    for (float x = 6.0f; x < 416.0f; x += 7.5f)
        g.drawVerticalLine ((int) x, 6.0f, b.getHeight() - 6.0f);

    // voix fantôme : forme d'onde réelle (harmoniques du moteur), trois passes lumineuses
    const auto h = proc.getHarmonicsSnapshot();
    const float x0 = 12.5f, x1 = 403.7f, cy = 86.3f;
    const float cycles = juce::jlimit (1.5f, 5.0f, 1.6f + (midiSm - 36.0f) / 14.0f);
    const float amp = 33.0f * juce::jlimit (0.04f, 1.0f, ampSm * 1.3f);
    double norm = 0.0; for (auto a : h) norm += a; norm = juce::jmax (1e-6, norm);

    juce::Path p;
    bool first = true;
    for (float x = x0; x <= x1; x += 1.5f)
    {
        const float t = (x - x0) / (x1 - x0);
        const float th = juce::MathConstants<float>::twoPi * (t * cycles - phase);
        double s = 0.0;
        for (size_t k = 0; k < h.size(); ++k) s += h[k] * std::sin ((double) (k + 1) * th);
        const float edge = std::sin (juce::MathConstants<float>::pi * t);
        const float y = cy - amp * edge * (float) (s / norm) * 1.6f;
        if (first) { p.startNewSubPath (x, y); first = false; } else p.lineTo (x, y);
    }
    const juce::PathStrokeType st1 (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType st2 (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType st3 (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (kMint.withAlpha (0.10f * on)); g.strokePath (p, st1);
    g.setColour (kMint.withAlpha (0.25f * on)); g.strokePath (p, st2);
    g.setColour (kMint.withAlpha (0.95f * on)); g.strokePath (p, st3);

    // preset
    auto arrow = [&] (juce::Rectangle<float> r, bool left)
    {
        juce::Path t; const auto c = r.getCentre();
        if (left) t.addTriangle (c.x + 4.5f, c.y - 6.5f, c.x + 4.5f, c.y + 6.5f, c.x - 5.0f, c.y);
        else      t.addTriangle (c.x - 4.5f, c.y - 6.5f, c.x - 4.5f, c.y + 6.5f, c.x + 5.0f, c.y);
        g.setColour (kTeal.withAlpha (on)); g.fillPath (t);
    };
    arrow (prevArea(), true);
    arrow (nextArea(), false);

    auto name = proc.getCurrentPresetName();
    if (proc.presetDirty.load()) name << " *";
    g.setFont (uiFont (21.0f));
    g.setColour (kText.withAlpha (on));
    g.drawFittedText (name, presetArea().withTrimmedLeft (2.5f).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);

    // sens de la pédale
    const auto ma = modeArea();
    const int mode = juce::roundToInt (proc.apvts.getRawParameterValue ("pedalMode")->load());
    const char* modes[] = { "UP", "DOWN", "BIPOLAR" };
    g.setColour (kTeal.withAlpha (0.85f * on));
    g.drawRoundedRectangle (ma, ma.getHeight() * 0.5f, 1.3f);
    g.setFont (uiFont (15.5f));
    g.setColour (kText.withAlpha (on));
    g.drawText (modes[juce::jlimit (0, 2, mode)], ma, juce::Justification::centred);

    // activité MIDI
    const float md = 0.35f + 0.65f * midiBlink;
    g.setColour (kMint.withAlpha (md * on));
    g.fillEllipse (398.6f - 4.4f, 19.9f - 4.4f, 8.8f, 8.8f);

    // note jouée
    if (proc.uiNote.load())
    {
        int cents = 0;
        const auto nn = noteName (midiSm, cents);
        g.setFont (uiFont (32.0f));
        g.setColour (kText.withAlpha (on));
        g.drawText (nn, juce::Rectangle<float> (12.5f, 125.0f, 70.0f, 38.0f), juce::Justification::centredLeft);
        const float nx = 12.5f + juce::GlyphArrangement::getStringWidth (uiFont (32.0f), nn) + 14.0f;
        g.setFont (uiFont (16.0f, false));
        g.setColour (kTeal.withAlpha (on));
        g.drawText ((cents >= 0 ? "+" : "") + juce::String (cents) + juce::String::fromUTF8 (" \xc2\xa2"),
                    juce::Rectangle<float> (juce::jmax (65.7f, nx), 133.0f, 80.0f, 24.0f), juce::Justification::centredLeft);
    }

    // étiquettes d'état
    g.setFont (uiFont (12.0f));
    if (isHold (proc))
    {
        g.setColour (kViolet.withAlpha (on));
        g.drawText ("HOLD", juce::Rectangle<float> (300.0f, 136.0f, 100.0f, 18.0f), juce::Justification::centredRight);
    }
    if (proc.learnTarget.load() >= 0)
    {
        g.setColour (kViolet.withAlpha (on));
        g.drawText ("LEARN", juce::Rectangle<float> (300.0f, 136.0f, 100.0f, 18.0f), juce::Justification::centredRight);
    }

    // barre pédale d'expression
    const auto ea = expArea();
    g.setFont (uiFont (13.5f));
    g.setColour (kTeal.withAlpha (on));
    g.drawText ("EXP", juce::Rectangle<float> (ea.getX() - 6.0f, 5.0f, ea.getWidth() + 12.0f, 16.0f), juce::Justification::centred);
    g.setColour (kTeal.withAlpha (0.75f * on));
    g.drawRoundedRectangle (ea, 6.0f, 1.3f);
    auto fill = ea.reduced (3.5f);
    fill = fill.withTop (fill.getBottom() - fill.getHeight() * juce::jlimit (0.0f, 1.0f, expSm));
    if (fill.getHeight() > 0.5f)
    {
        juce::ColourGradient eg (kMint.withAlpha (0.95f * on), fill.getX(), ea.getY(), kMint.withAlpha (0.55f * on), fill.getX(), ea.getBottom(), false);
        g.setGradientFill (eg);
        g.fillRoundedRectangle (fill, 3.5f);
    }
}

void GhostDisplay::stepPreset (int delta)
{
    const int n = proc.getNumPrograms();
    proc.setCurrentProgram ((proc.getCurrentProgram() + delta + n) % n);
    if (showStatus) showStatus (proc.getCurrentPresetName());
    repaint();
}

void GhostDisplay::showPresetMenu()
{
    juce::PopupMenu m, userLoad, userSave;
    const auto& fp = FantomineAudioProcessor::getFactoryPresets();
    m.addSectionHeader ("Factory presets");
    for (int i = 0; i < (int) fp.size(); ++i)
        m.addItem (100 + i, juce::String (i + 1).paddedLeft ('0', 2) + "  " + fp[(size_t) i].name, true,
                   proc.getCurrentProgram() == i && proc.getCurrentPresetName() == fp[(size_t) i].name);

    for (int s = 1; s <= 9; ++s)
    {
        const bool used = proc.isUserSlotUsed (s);
        userLoad.addItem (200 + s, juce::String (s) + (used ? "  " + proc.getUserSlotName (s) : juce::String ("  (empty)")), used);
        userSave.addItem (300 + s, juce::String (s) + (used ? "  (replace)" : juce::String()));
    }
    m.addSeparator();
    m.addSubMenu ("Load user preset", userLoad);
    m.addSubMenu ("Save to slot", userSave);

    juce::Component::SafePointer<GhostDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        if (r >= 100 && r < 200)      p.setCurrentProgram (r - 100);
        else if (r > 200 && r < 300)  p.loadUserPreset (r - 200);
        else if (r > 300 && r < 400)  p.saveUserPreset (r - 300);
        if (safe->showStatus) safe->showStatus ((r > 300 ? juce::String ("Saved: ") : juce::String()) + p.getCurrentPresetName());
        safe->repaint();
    });
}

void GhostDisplay::setExpressionFromY (float y)
{
    const auto ea = expArea().reduced (3.5f);
    const float v = juce::jlimit (0.0f, 1.0f, (ea.getBottom() - y) / ea.getHeight());
    if (auto* p = proc.apvts.getParameter ("expression")) p->setValueNotifyingHost (v);
    if (showStatus) showStatus ("EXP  " + juce::String (juce::roundToInt (v * 100.0f)) + " %");
}

void GhostDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { getParentComponent()->mouseDown (e.getEventRelativeTo (getParentComponent())); return; }
    const auto pos = e.position;
    if (expArea().expanded (6.0f).contains (pos))
    {
        draggingExp = true;
        if (auto* p = proc.apvts.getParameter ("expression")) p->beginChangeGesture();
        setExpressionFromY (pos.y);
    }
    else if (prevArea().contains (pos))   stepPreset (-1);
    else if (nextArea().contains (pos))   stepPreset (+1);
    else if (presetArea().contains (pos)) showPresetMenu();
    else if (modeArea().expanded (4.0f).contains (pos))
    {
        if (auto* p = proc.apvts.getParameter ("pedalMode"))
        {
            const int mode = (juce::roundToInt (proc.apvts.getRawParameterValue ("pedalMode")->load()) + 1) % 3;
            p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
            const char* names[] = { "Pedal UP: heel = played note", "Pedal DOWN: toe = lower", "Pedal BIPOLAR: centre = played note" };
            if (showStatus) showStatus (names[mode]);
        }
    }
}

void GhostDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingExp) setExpressionFromY (e.position.y);
}

void GhostDisplay::mouseUp (const juce::MouseEvent&)
{
    if (draggingExp)
        if (auto* p = proc.apvts.getParameter ("expression")) p->endChangeGesture();
    draggingExp = false;
}

//==============================================================================
PedalView::PedalView (FantomineAudioProcessor& p)
    : proc (p), footswitch (p), display (p, lights)
{
    background = loadImage (BinaryData::background_png, BinaryData::background_pngSize);
    titleOn    = loadImage (BinaryData::title_on_png,   BinaryData::title_on_pngSize);
    ledOn      = loadImage (BinaryData::led_on_png,     BinaryData::led_on_pngSize);
    octHandle  = loadImage (BinaryData::oct_handle_png, BinaryData::oct_handle_pngSize);

    const bool on = ! isBypassed (p);
    lights.power = lights.title = on ? 1.0f : 0.0f;
    wasOn = on;

    PedalKnob* knobs[] = { &glide, &range, &tone, &vibrato, &speed, &blend };
    const char* ids[]  = { "glide", "range", "tone", "vibrato", "speed", "blend" };
    for (int i = 0; i < 6; ++i)
    {
        auto* k = knobs[i];
        addAndMakeVisible (*k);
        k->setBounds (juce::Rectangle<int> (140, 140).withCentre (kKnobC[i].roundToInt()));
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ids[i], *k));
        k->addListener (this);
    }

    octave = std::make_unique<OctaveSwitch> (p, *p.apvts.getParameter ("octave"), octHandle);
    addAndMakeVisible (*octave);
    octave->setBounds (kOctave);
    octave->onChange = [this]
    {
        const char* txt[] = { "OCTAVE  -1", "OCTAVE  0", "OCTAVE  +1" };
        setStatus (txt[octave->position]);
    };

    addAndMakeVisible (footswitch);
    footswitch.setBounds (juce::Rectangle<int> (160, 160).withCentre (kFootC.roundToInt()));
    footswitch.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    addAndMakeVisible (display);
    display.setBounds (kScreen);
    display.showStatus = [this] (const juce::String& s) { setStatus (s); };
}

PedalView::~PedalView()
{
    for (auto* k : { &glide, &range, &tone, &vibrato, &speed, &blend }) k->removeListener (this);
}

void PedalView::setStatus (const juce::String& s, double seconds)
{
    statusText = s;
    statusUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    repaint (kStatus);
}

void PedalView::sliderValueChanged (juce::Slider* s)
{
    if (auto* k = dynamic_cast<PedalKnob*> (s))
        setStatus (k->label + "   " + k->getTextFromValue (k->getValue()));
}

void PedalView::sliderDragStarted (juce::Slider* s) { sliderValueChanged (s); }

void PedalView::askRepaint (juce::Rectangle<int> r)
{
    if (repaintAskedMs <= 0.0) repaintAskedMs = juce::Time::getMillisecondCounterHiRes();
    repaint (r);
}

// fond mis à l'échelle exacte de l'écran : appelé par le timer de l'éditeur, jamais par paint()
void PedalView::rebuildCache (float px)
{
    const int tw = juce::jlimit (64, ui::kW * 3, (int) std::lround (ui::kW * px));
    const int th = juce::jlimit (48, ui::kH * 3, (int) std::lround (ui::kH * px));
    if (bgCache.isValid() && bgCache.getWidth() == tw && bgCache.getHeight() == th) return;
    bgCache = (tw >= background.getWidth()) ? background : background.rescaled (tw, th, juce::Graphics::highResamplingQuality);
    repaint();
}

void PedalView::paint (juce::Graphics& g)
{
    lastPaintMs = juce::Time::getMillisecondCounterHiRes();
    repaintAskedMs = 0.0;
    g.fillAll (juce::Colours::black);
    // anti-gel : aucun rescale ici, on dessine le cache existant (ou l'original en attendant le timer)
    const auto& bg = bgCache.isValid() ? bgCache : background;
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed (bg, juce::AffineTransform::scale ((float) ui::kW / (float) bg.getWidth(),
                                                              (float) ui::kH / (float) bg.getHeight()));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // néon du titre
    if (lights.title > 0.005f)
    {
        g.setOpacity (juce::jlimit (0.0f, 1.0f, lights.title));
        g.drawImageAt (titleOn, kTitlePos.x, kTitlePos.y);
        g.setOpacity (1.0f);
    }
}

void PedalView::paintOverChildren (juce::Graphics& g)
{
    // LED au-dessus du footswitch
    if (lights.led > 0.005f)
    {
        g.setOpacity (juce::jlimit (0.0f, 1.0f, lights.led));
        g.drawImageAt (ledOn, kLedPos.x, kLedPos.y);
        g.setOpacity (1.0f);
    }

    // ligne d'état
    const juce::String txt = currentStatusText();
    g.setColour (juce::Colour (0xffd8dae4).withAlpha (isBypassed (proc) ? 0.55f : 0.95f));
    g.setFont (uiFont (18.5f));
    g.drawFittedText (txt, kStatus.reduced (12, 0), juce::Justification::centred, 1, 0.9f);
}

juce::String PedalView::currentStatusText() const
{
    juce::String txt = statusText;
    if (juce::Time::getMillisecondCounterHiRes() > statusUntil)
    {
        txt = isBypassed (proc) ? juce::String ("BYPASS")
                                : (isHold (proc) ? juce::String ("HOLD  -  tap to release") : proc.getCurrentPresetName());
    }
    // MIDI Learn en attente : prioritaire sur tout autre message
    const int lt = proc.learnTarget.load();
    if (lt >= 0) txt = "MIDI LEARN: move a control for " + juce::String (FantomineAudioProcessor::getTargetName (lt));
    return txt;
}

void PedalView::tick (float dt)
{
    const bool on = ! isBypassed (proc);
    const bool hold = isHold (proc);

    // fondu global : extinction rapide, allumage un peu plus doux
    const float target = on ? 1.0f : 0.0f;
    const float fadeSpeed = on ? 9.0f : 14.0f;
    lights.power += (target - lights.power) * juce::jmin (1.0f, dt * fadeSpeed);
    if (std::abs (target - lights.power) < 0.002f) lights.power = target;

    display.tick (dt);   // après le fondu : l'écran sait tout de suite s'il doit se rallumer

    // néon : petit scintillement à l'allumage
    if (on && ! wasOn) flickerT = 0.0f;
    wasOn = on;
    flickerT += dt;
    float title = lights.power;
    if (on && flickerT < 0.35f)
    {
        const float f = flickerT;
        const bool dark = (f > 0.04f && f < 0.08f) || (f > 0.15f && f < 0.19f);
        title *= dark ? 0.25f : 1.0f;
    }
    lights.title = title;

    // LED : suit le niveau de la voix, pulse en HOLD, éteinte en bypass
    ledPulse += dt;
    float led = 0.0f;
    if (on)
        led = hold ? 0.55f + 0.45f * (0.5f + 0.5f * std::sin (ledPulse * juce::MathConstants<float>::twoPi * 1.2f))
                   : 0.72f + 0.28f * juce::jlimit (0.0f, 1.0f, proc.uiAmp.load() * 1.4f);
    lights.led += (led * lights.power - lights.led) * juce::jmin (1.0f, dt * 20.0f);

    const float press = proc.footswitchPressed.load() ? 1.0f : 0.0f;
    if (std::abs (press - footswitch.pressAnim) > 0.001f)
    {
        footswitch.pressAnim += (press - footswitch.pressAnim) * juce::jmin (1.0f, dt * 30.0f);
        askRepaint (footswitch.getBounds());
    }
    // levier : 3 positions, ou suivi continu quand une pédale le pilote (whammy)
    const bool whammy = proc.whammyActive.load();
    const float oct = proc.uiOctave.load();
    const float op = whammy ? juce::jlimit (0.0f, 2.0f, oct + 1.0f) : (float) octave->position;
    if (std::abs (op - octave->anim) > 0.001f)
    {
        octave->anim += (op - octave->anim) * juce::jmin (1.0f, dt * (whammy ? 30.0f : 22.0f));
        askRepaint (octave->getBounds());
    }
    if (whammy && std::abs (oct - lastOct) > 0.004f)
        setStatus ("OCTAVE  " + juce::String (oct >= 0 ? "+" : "") + juce::String (oct * 12.0f, 1) + " st");
    lastOct = oct;

    // les potards ne se redessinent que pendant un fondu
    if (std::abs (lastPower - lights.power) > 0.0005f)
    {
        for (auto* k : { &glide, &range, &tone, &vibrato, &speed, &blend }) askRepaint (k->getBounds());
        lastPower = lights.power;
    }

    // on ne redessine une zone que si ce qu'elle affiche a vraiment changé (zéro travail au repos)
    auto changed = [] (float& drawn, float now) { if (std::abs (drawn - now) < 0.002f) return false; drawn = now; return true; };

    if (changed (drawnTitle, lights.title))
        askRepaint ({ kTitlePos.x, kTitlePos.y, titleOn.getWidth(), titleOn.getHeight() });
    if (changed (drawnLed, lights.led))
        askRepaint ({ kLedPos.x, kLedPos.y, ledOn.getWidth(), ledOn.getHeight() });

    // MIDI Learn : confirmation « <Commande> = CC n » pendant 2 s
    const int learned = proc.lastLearned.exchange (-1);
    if (learned >= 0)
    {
        const int cc = learned == FantomineAudioProcessor::kLearnFootswitch ? proc.footswitchCC.load()
                                                                            : proc.targetCC[(size_t) learned].load();
        setStatus (juce::String (FantomineAudioProcessor::getTargetName (learned)) + " = CC " + juce::String (cc), 2.0);
    }

    const juce::String st = currentStatusText();
    const bool bypassed = isBypassed (proc);
    if (! statusDrawnOnce || st != drawnStatus || bypassed != drawnStatusBypass)
    {
        statusDrawnOnce = true;
        drawnStatus = st;
        drawnStatusBypass = bypassed;
        askRepaint (kStatus);
    }
}

void PedalView::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    // clic droit sur un contrôle pilotable : son MIDI Learn apparaît en tête du menu
    int focus = -1;
    const auto* src = e.originalComponent;
    if (src == &vibrato) focus = FantomineAudioProcessor::kVibrato;
    else if (src == &speed) focus = FantomineAudioProcessor::kSpeed;
    else if (src == &blend) focus = FantomineAudioProcessor::kBlend;
    else if (src == &glide) focus = FantomineAudioProcessor::kGlide;
    else if (src == &range) focus = FantomineAudioProcessor::kRange;
    else if (src == &tone)  focus = FantomineAudioProcessor::kTone;
    else if (src == octave.get()) focus = FantomineAudioProcessor::kOctave;
    else if (src == &footswitch)  focus = FantomineAudioProcessor::kLearnFootswitch;
    else if (src == &display)
        focus = display.isOverModeArea (e.getEventRelativeTo (&display).position) ? FantomineAudioProcessor::kPedalMode
                                                                                  : FantomineAudioProcessor::kPitch;
    showContextMenu (focus);
}

void PedalView::showContextMenu (int focus)
{
    using P = FantomineAudioProcessor;
    juce::PopupMenu m, size, pedals, foot, mode, wrange;

    auto ccText = [] (int cc) { return cc < 0 ? juce::String ("not assigned") : "CC " + juce::String (cc); };

    // clic droit sur une commande : MIDI Learn de cette commande en tête du menu
    const int lt = proc.learnTarget.load();
    if (focus >= 0)
    {
        const bool fs = focus == P::kLearnFootswitch;
        const int cc = fs ? proc.footswitchCC.load() : proc.targetCC[(size_t) focus].load();
        const juce::String name = P::getTargetName (focus);
        m.addSectionHeader (name);
        m.addItem (fs ? 30 : 1000 + focus * 10, "MIDI Learn: " + name + " (" + (cc >= 0 ? "CC " + juce::String (cc) : juce::String ("none")) + ")",
                   true, lt == focus);
        m.addItem (fs ? 31 : 1001 + focus * 10, "Clear MIDI: " + name, cc >= 0);
        if (lt >= 0) m.addItem (1200, "Cancel MIDI learn");
        m.addSeparator();
    }

    // une entrée par cible : Learn / Clear (+ CC courants pour la hauteur)
    static const int ccs[] = { 11, 4, 7, 1, 2 };
    const char* ccNames[] = { "CC 11 - Expression", "CC 4 - Foot Controller", "CC 7 - Volume", "CC 1 - Modulation", "CC 2 - Breath" };
    for (int t = 0; t < P::kNumTargets; ++t)
    {
        juce::PopupMenu sub;
        const int cc = proc.targetCC[(size_t) t].load();
        sub.addItem (1000 + t * 10, "MIDI Learn");
        sub.addItem (1001 + t * 10, "Clear", cc >= 0);
        if (t == P::kPitch)
        {
            sub.addSeparator();
            for (int i = 0; i < 5; ++i) sub.addItem (1002 + i, ccNames[i], true, cc == ccs[i]);
        }
        const juce::String label = t == P::kPitch  ? "PITCH (bend)"
                                 : t == P::kOctave ? "OCTAVE (whammy)" : juce::String (P::getTargetName (t));
        pedals.addSubMenu ("Learn: " + label + "   " + ccText (cc), sub, true, nullptr, lt == t);
    }
    pedals.addSeparator();
    if (lt >= 0) pedals.addItem (1200, "Cancel MIDI learn");
    pedals.addItem (1201, "Reset MIDI to defaults (PITCH CC 11, others none)");
    pedals.addSeparator();
    const bool inv = proc.apvts.getRawParameterValue ("invert")->load() > 0.5f;
    pedals.addItem (50, "Invert pedal direction", true, inv);
    m.addSubMenu ("Expression pedals (MIDI)", pedals);

    const int wr = juce::roundToInt (proc.apvts.getRawParameterValue ("whammyRange")->load());
    wrange.addItem (41, "Heel -1 oct  >  centre 0  >  toe +1 oct", true, wr == 0);
    wrange.addItem (42, "Heel 0  >  toe +1 oct", true, wr == 1);
    wrange.addItem (43, "Heel 0  >  toe -1 oct", true, wr == 2);
    m.addSubMenu ("Octave whammy range", wrange);

    const int pm = juce::roundToInt (proc.apvts.getRawParameterValue ("pedalMode")->load());
    mode.addItem (21, "UP: heel = played note, toe = + Range", true, pm == 0);
    mode.addItem (22, "DOWN: heel = played note, toe = - Range", true, pm == 1);
    mode.addItem (23, "BIPOLAR: centre = played note", true, pm == 2);
    m.addSubMenu ("Pitch pedal direction", mode);
    m.addItem (50, "Invert MIDI pedal direction (heel / toe)", true, inv);

    const int fcc = proc.footswitchCC.load();
    foot.addItem (30, "MIDI Learn (press the footswitch)");
    foot.addItem (31, "None (use the DAW's MIDI learn)", true, fcc < 0);
    foot.addItem (32, "CC 64", true, fcc == 64);
    foot.addItem (33, "CC 80", true, fcc == 80);
    if (fcc >= 0 && fcc != 64 && fcc != 80) foot.addItem (34, "CC " + juce::String (fcc) + " (learned)", true, true);
    m.addSubMenu ("Footswitch (MIDI)", foot);

    const int cur = getSizeIndex ? getSizeIndex() : -1;
    size.addItem (104, "60 % (default)", true, cur < 0);
    size.addItem (102, "75 %",  true, cur == 1);
    size.addItem (103, "100 %", true, cur == 2);
    m.addSeparator();
    m.addSubMenu ("Plugin size", size);

    juce::Component::SafePointer<PedalView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& p = safe->proc;
        auto setChoice = [&p] (const char* id, int v)
        {
            if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
        };
        if (r >= 102 && r <= 104)        { if (safe->onSizeRequest) safe->onSizeRequest (r == 104 ? -1 : r - 101); }
        else if (r == 1200) p.learnTarget.store (-1);
        else if (r == 1201) { p.resetMidiMap(); safe->setStatus ("MIDI reset to defaults"); }
        else if (r >= 1000 && r < 1100)
        {
            const int t = (r - 1000) / 10, k = (r - 1000) % 10;
            if (k == 0)      p.learnTarget.store (p.learnTarget.load() == t ? -1 : t);   // re-cliquer annule
            else if (k == 1) { p.targetCC[(size_t) t].store (-1); if (t == P::kPitch) p.apvts.getParameter ("expression")->setValueNotifyingHost (0.0f);
                               safe->setStatus (juce::String (P::getTargetName (t)) + ": MIDI cleared"); }
            else             { p.assignCC (P::kPitch, ccs[k - 2]); safe->setStatus ("PITCH: CC " + juce::String (ccs[k - 2])); }
        }
        else if (r >= 21 && r <= 23)     setChoice ("pedalMode", r - 21);
        else if (r >= 41 && r <= 43)     setChoice ("whammyRange", r - 41);
        else if (r == 30) p.learnTarget.store (p.learnTarget.load() == P::kLearnFootswitch ? -1 : P::kLearnFootswitch);
        else if (r == 50)
        {
            if (auto* prm = p.apvts.getParameter ("invert"))
            {
                const bool on = prm->getValue() < 0.5f;
                prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
                safe->setStatus (on ? "MIDI pedal inverted" : "MIDI pedal normal");
            }
        }
        else if (r == 31) p.footswitchCC.store (-1);
        else if (r == 32) p.assignCC (P::kLearnFootswitch, 64);
        else if (r == 33) p.assignCC (P::kLearnFootswitch, 80);
    });
}

//==============================================================================
FantomineAudioProcessorEditor::FantomineAudioProcessorEditor (FantomineAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pedal (p)
{
    const int savedIdx = juce::jlimit (-1, 2, proc.uiScaleIdx.load());   // lu avant que les limites ne redimensionnent

    addAndMakeVisible (pedal);
    pedal.onSizeRequest = [this] (int idx) { setScaleIndex (idx); };
    pedal.getSizeIndex  = [this] { return getScaleIndex(); };

    // comme Ring Think : jamais plus petit que la taille par défaut, ratio verrouillé
    setResizable (true, true);
    const int minW = (int) std::lround (ui::kRefW * ui::kDefaultScale);
    setResizeLimits (minW, (int) std::lround ((double) minW * ui::kH / ui::kW),
                     ui::kMaxW, (int) std::lround ((double) ui::kMaxW * ui::kH / ui::kW));
    getConstrainer()->setFixedAspectRatio ((double) ui::kW / (double) ui::kH);
    setOpaque (true);                      // anti-gel : l'hôte ne redessine jamais ce qui est derrière la fenêtre
    setScaleIndex (savedIdx);
    rebuildCaches();

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

FantomineAudioProcessorEditor::~FantomineAudioProcessorEditor() { stopTimer(); }

//==============================================================================
// Anti-gel de la fenêtre (Windows) : JUCE 8+ dessine par défaut avec Direct2D (carte graphique).
// Avec certains pilotes (Intel notamment) la fenêtre du plugin cesse de se rafraîchir alors que
// le DAW continue de tourner. Le moteur logiciel est stable partout et largement assez rapide ici.
//
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel forcé sur Windows, appliqué à chaque nouvelle fenêtre hôte (peer)
//     et revérifié toutes les secondes, jamais en boucle ;
//  2. aucun calcul lourd dans paint() : le fond mis à l'échelle est refait par le timer seulement,
//     après stabilisation de la taille ou quand l'échelle d'affichage change (au plus toutes les 250 ms) ;
//  3. chien de garde : si un redessin demandé n'est pas arrivé au bout de 1,5 s alors que la fenêtre
//     est visible, le moteur de rendu est réappliqué et toute la fenêtre est redessinée ;
//  4. côté audio : aucun appel à l'hôte ni verrou partagé depuis le thread audio (voir PluginProcessor).
void FantomineAudioProcessorEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer != lastPeer) { lastPeer = peer; repaint(); }
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void FantomineAudioProcessorEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void FantomineAudioProcessorEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

float FantomineAudioProcessorEditor::physicalScale() const
{
    return juce::jlimit (1.0f, 4.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
}

void FantomineAudioProcessorEditor::rebuildCaches (float sc)
{
    if (sc <= 0.0f) sc = cacheScale > 0.0f ? cacheScale : physicalScale();
    if (getWidth() <= 0) return;
    cacheW = getWidth(); cacheScale = sc;
    pedal.rebuildCache ((float) getWidth() / (float) ui::kW * sc);
}

void FantomineAudioProcessorEditor::resized()
{
    // anti-gel : jamais de setSize ici, les limites du constrainer suffisent
    const float s = (float) getWidth() / (float) ui::kW;
    pedal.setTransform (juce::AffineTransform::scale (s));
    pedal.setBounds (0, 0, ui::kW, ui::kH);
    resizeSettle = 4;              // le fond sera refait par le timer quand la taille ne bouge plus
}

int FantomineAudioProcessorEditor::getScaleIndex() const
{
    const float s = (float) getWidth() / (float) ui::kRefW;
    for (int i = 1; i < 3; ++i) if (std::abs (s - kScales[i]) < 0.02f) return i;
    return -1;
}

void FantomineAudioProcessorEditor::setScaleIndex (int idx)
{
    idx = juce::jlimit (-1, 2, idx);
    if (idx == 0) idx = -1;
    proc.uiScaleIdx = idx;
    const float s = idx < 0 ? ui::kDefaultScale : kScales[idx];
    const int w = (int) std::lround (ui::kRefW * s);
    setSize (w, (int) std::lround ((double) w * ui::kH / ui::kW));
}

void FantomineAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) / 1000.0);
    lastTick = now;

    // moteur logiciel : seulement si la fenêtre hôte a changé, ou au plus une fois par seconde
    if (getPeer() != lastPeer || now - lastRendererCheckMs > 1000.0) { lastRendererCheckMs = now; useSoftwareRenderer(); }

    // caches : reconstruits ici seulement (jamais dans paint)
    if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
    else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
    {
        lastScaleCheckMs = now;
        const float sc = physicalScale();
        if (std::abs (sc - cacheScale) > 0.01f) rebuildCaches (sc);
    }

    // chien de garde : un redessin demandé n'arrive pas -> on relance le moteur de rendu et on redessine tout
    if (pedal.repaintAskedMs > 0.0 && isShowing() && now - pedal.repaintAskedMs > 1500.0 && pedal.lastPaintMs < pedal.repaintAskedMs)
    {
        pedal.repaintAskedMs = 0.0;
        lastPeer = nullptr;
        useSoftwareRenderer();
        repaint();
        pedal.repaint();
    }

    pedal.tick (dt);
}
