#include "PluginEditor.h"

using namespace aurora::col;

//==============================================================================
// LevelMeter
//==============================================================================
LevelMeter::LevelMeter (std::function<float()> source, float minValue, float maxValue,
                        bool isReduction, juce::String caption)
    : getValue (std::move (source)), minV (minValue), maxV (maxValue),
      reduction (isReduction), name (std::move (caption))
{
}

void LevelMeter::tick()
{
    const float norm = juce::jlimit (0.0f, 1.0f, juce::jmap (getValue(), minV, maxV, 0.0f, 1.0f));
    shown = norm > shown ? norm : shown * 0.9f + norm * 0.1f;
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto b   = getLocalBounds().toFloat();
    auto bar = b.withTrimmedBottom (18.0f);

    g.setColour (panel.withAlpha (0.9f));
    g.fillRoundedRectangle (bar, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (bar.reduced (0.5f), 6.0f, 1.0f);

    auto inner = bar.reduced (3.0f);
    const float h = inner.getHeight() * shown;

    if (reduction)
    {
        g.setGradientFill (juce::ColourGradient (violet, 0.0f, inner.getY(), pink, 0.0f, inner.getBottom(), false));
        g.fillRoundedRectangle (inner.withHeight (h), 3.0f);
    }
    else
    {
        juce::ColourGradient grad (teal, 0.0f, inner.getBottom(), pink, 0.0f, inner.getY(), false);
        grad.addColour (0.65, violet);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (inner.withTop (inner.getBottom() - h), 3.0f);
    }

    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (name, b.removeFromBottom (16.0f).toNearestInt(), juce::Justification::centred);
}

//==============================================================================
// TransferCurve
//==============================================================================
void TransferCurve::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (panel.withAlpha (0.88f));
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);

    auto area = b.reduced (18.0f);
    auto toX = [&] (float db) { return area.getX() + (db - minDb) / (maxDb - minDb) * area.getWidth(); };
    auto toY = [&] (float db) { return area.getBottom() - (db - minDb) / (maxDb - minDb) * area.getHeight(); };

    // сетка
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (float db = -48.0f; db <= 0.0f; db += 12.0f)
    {
        g.drawLine (toX (db), area.getY(), toX (db), area.getBottom(), 1.0f);
        g.drawLine (area.getX(), toY (db), area.getRight(), toY (db), 1.0f);
    }

    // линия 1:1
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (toX (minDb), toY (minDb), toX (maxDb), toY (maxDb), 1.0f);

    const float thr   = proc.apvts.getRawParameterValue ("thresh")->load();
    const float ratio = proc.apvts.getRawParameterValue ("ratio")->load();
    const float knee  = proc.apvts.getRawParameterValue ("knee")->load();

    // порог
    g.setColour (teal.withAlpha (0.28f));
    g.drawLine (toX (thr), area.getY(), toX (thr), area.getBottom(), 1.0f);

    // кривая
    juce::Path curve;
    const int steps = 120;
    for (int i = 0; i <= steps; ++i)
    {
        const float in  = minDb + (maxDb - minDb) * (float) i / (float) steps;
        const float out = in - aurora::computeReductionDb (in, thr, ratio, knee);
        if (i == 0) curve.startNewSubPath (toX (in), toY (out));
        else        curve.lineTo (toX (in), toY (out));
    }

    for (int i = 3; i >= 1; --i)
    {
        g.setColour (violet.withAlpha (0.07f * (float) (4 - i)));
        g.strokePath (curve, juce::PathStrokeType (3.0f + 3.0f * (float) i, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::ColourGradient grad (teal, area.getX(), area.getBottom(), pink, area.getRight(), area.getY(), false);
    grad.addColour (0.5, violet);
    g.setGradientFill (grad);
    g.strokePath (curve, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // живая точка
    const float in  = juce::jlimit (minDb, maxDb, proc.inDb.load());
    const float out = in - aurora::computeReductionDb (in, thr, ratio, knee);
    const float px = toX (in), py = toY (out);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillEllipse (px - 9.0f, py - 9.0f, 18.0f, 18.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);

    // подписи осей
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("IN dB",  juce::Rectangle<float> (area.getRight() - 50.0f, area.getBottom() - 14.0f, 50.0f, 14.0f).toNearestInt(),
                juce::Justification::centredRight);
    g.drawText ("OUT dB", juce::Rectangle<float> (area.getX() + 2.0f, area.getY(), 60.0f, 14.0f).toNearestInt(),
                juce::Justification::centredLeft);
}

//==============================================================================
// MainPanel
//==============================================================================
MainPanel::MainPanel (AuroraCompressorAudioProcessor& p)
    : proc (p),
      inMeter  ([&p] { return p.inDb.load();  }, -60.0f, 0.0f,  false, "IN"),
      grMeter  ([&p] { return p.grDb.load();  },   0.0f, 24.0f, true,  "GR"),
      outMeter ([&p] { return p.outDb.load(); }, -60.0f, 0.0f,  false, "OUT"),
      curve (p)
{
    setupKnob (kThresh,  "THRESHOLD", "thresh",  " dB", 1);
    setupKnob (kRatio,   "RATIO",     "ratio",   "",    1);
    setupKnob (kAttack,  "ATTACK",    "attack",  " ms", 1);
    setupKnob (kRelease, "RELEASE",   "release", " ms", 0);
    setupKnob (kKnee,    "KNEE",      "knee",    " dB", 1);
    setupKnob (kMakeup,  "MAKEUP",    "makeup",  " dB", 1);
    setupKnob (kMix,     "MIX",       "mix",     " %",  0);
    setupKnob (kHpf,     "SC HPF",    "schpf",   " Hz", 0);
    setupKnob (kOutput,  "OUTPUT",    "output",  " dB", 1);

    // Ratio: «∞:1» на максимуме
    kRatio.slider.textFromValueFunction = [] (double v)
    {
        return v >= 19.95 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) + ":1"
                          : juce::String (v, 1) + ":1";
    };
    kRatio.slider.updateText();

    setupCombo (detector,   { "Peak", "RMS" },                          "detector",   detAtt);
    setupCombo (lookahead,  { "Off", "1 ms", "2 ms", "5 ms", "10 ms" }, "lookahead",  lookAtt);
    setupCombo (oversample, { "Off", "2x", "4x" },                      "oversample", osAtt);
    setupCombo (scSource,   { "Internal", "External" },                 "scsource",   scAtt);

    setupCaption (capDetector,   "DETECTOR");
    setupCaption (capLookahead,  "LOOKAHEAD");
    setupCaption (capOversample, "OVERSAMPLING");
    setupCaption (capScSource,   "SIDECHAIN");

    autoBtn.setClickingTogglesState (true);
    addAndMakeVisible (autoBtn);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "auto", autoBtn);

    // --- пресеты ---
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);
    addAndMakeVisible (saveBtn);
    presetBox.setTextWhenNothingSelected ("Custom");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id < 1000) proc.loadFactoryPreset (id - 1);
        else           proc.loadUserPreset (presetBox.getText());
    };
    prevBtn.onClick = [this] { stepPreset (-1); };
    nextBtn.onClick = [this] { stepPreset (+1); };
    saveBtn.onClick = [this] { promptSavePreset(); };
    refreshPresets();

    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (curve);

    setSize (baseW, baseH);
    startTimerHz (30);
}

MainPanel::~MainPanel()
{
    stopTimer();
}

void MainPanel::setupKnob (Knob& k, const juce::String& title, const juce::String& paramId,
                           const juce::String& suffix, int decimals)
{
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);

    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);

    juce::Font f (juce::FontOptions (12.0f, juce::Font::bold));
    f.setExtraKerningFactor (0.14f);
    k.label.setFont (f);
    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, text.withAlpha (0.85f));

    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);

    // Атачмент подменяет функции текста, поэтому наши задаём после него.
    k.slider.textFromValueFunction = [suffix, decimals] (double v) { return juce::String (v, decimals) + suffix; };
    k.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    k.slider.updateText();
}

void MainPanel::setupCombo (juce::ComboBox& box, const juce::StringArray& items, const juce::String& paramId,
                            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
{
    box.addItemList (items, 1);
    addAndMakeVisible (box);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, box);
}

void MainPanel::setupCaption (juce::Label& l, const juce::String& caption)
{
    juce::Font f (juce::FontOptions (10.0f, juce::Font::bold));
    f.setExtraKerningFactor (0.14f);
    l.setFont (f);
    l.setText (caption, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centredLeft);
    l.setColour (juce::Label::textColourId, text.withAlpha (0.55f));
    addAndMakeVisible (l);
}

void MainPanel::place (Knob& k, juce::Rectangle<int> r)
{
    k.label.setBounds (r.removeFromTop (18));
    k.slider.setBounds (r);
}

//==============================================================================
// Пресеты
//==============================================================================
void MainPanel::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    presetIds.clear();

    presetBox.addSectionHeading ("Factory");
    const auto& f = proc.presets.factory();
    for (size_t i = 0; i < f.size(); ++i)
    {
        presetBox.addItem (f[i].name, (int) i + 1);
        presetIds.push_back ((int) i + 1);
    }

    const auto users = PresetManager::userNames();
    if (users.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("User");
        for (int i = 0; i < users.size(); ++i)
        {
            presetBox.addItem (users[i], 1000 + i);
            presetIds.push_back (1000 + i);
        }
    }

    int selectId = 0;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        if (presetBox.getItemText (i) == proc.currentPresetName)
            selectId = presetBox.getItemId (i);

    presetBox.setSelectedId (selectId, juce::dontSendNotification);
}

void MainPanel::stepPreset (int direction)
{
    if (presetIds.empty()) return;

    const int size = (int) presetIds.size();
    const auto it  = std::find (presetIds.begin(), presetIds.end(), presetBox.getSelectedId());
    const int idx  = it == presetIds.end() ? (direction > 0 ? 0 : size - 1)
                                           : ((int) (it - presetIds.begin()) + direction + size) % size;

    presetBox.setSelectedId (presetIds[(size_t) idx], juce::sendNotificationSync);
}

void MainPanel::promptSavePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.currentPresetName);
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainPanel> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;

        const auto saved = safe->proc.presets.saveUser (w->getTextEditorContents ("name"));
        if (saved.isNotEmpty())
        {
            safe->proc.currentPresetName = saved;
            safe->refreshPresets();
        }
    }), true);
}

//==============================================================================
void MainPanel::resized()
{
    // заголовок + панель пресетов
    prevBtn.setBounds   (430, 18, 30, 28);
    presetBox.setBounds (464, 18, 200, 28);
    nextBtn.setBounds   (668, 18, 30, 28);
    saveBtn.setBounds   (706, 18, 70, 28);

    // индикаторы и график
    inMeter.setBounds  (24,  76, 22, 220);
    curve.setBounds    (56,  76, 300, 220);
    grMeter.setBounds  (368, 76, 22, 220);
    outMeter.setBounds (396, 76, 22, 220);

    place (kThresh, { 436, 72, 180, 230 });
    place (kRatio,  { 624, 72, 180, 230 });

    // полоса переключателей
    juce::Label* caps[] = { &capDetector, &capLookahead, &capOversample, &capScSource };
    juce::Component* combos[] = { &detector, &lookahead, &oversample, &scSource };
    for (int i = 0; i < 4; ++i)
    {
        const int x = 24 + i * 158;
        caps[i]->setBounds (x, 306, 148, 13);
        combos[i]->setBounds (x, 320, 148, 26);
    }
    autoBtn.setBounds (24 + 4 * 158, 320, 148, 26);

    // нижняя панель с ручками
    bottomPanel = juce::Rectangle<float> (16.0f, 356.0f, 788.0f, 140.0f);

    Knob* row[] = { &kAttack, &kRelease, &kKnee, &kMakeup, &kMix, &kHpf, &kOutput };
    int x = 24;
    for (auto* k : row)
    {
        place (*k, { x, 362, 110, 128 });
        x += 112;
    }
}

void MainPanel::timerCallback()
{
    phase += 0.03f;
    inMeter.tick();
    grMeter.tick();
    outMeter.tick();
    repaint();
}

void MainPanel::drawAurora (juce::Graphics& g, juce::Rectangle<float> b) const
{
    struct Ribbon { float y, amp, freq, speed, thick; juce::Colour c; };
    const Ribbon ribbons[] = {
        { 0.18f, 26.0f, 0.011f,  1.0f, 110.0f, teal   },
        { 0.27f, 34.0f, 0.008f, -0.7f, 130.0f, violet },
        { 0.12f, 22.0f, 0.014f,  0.6f,  90.0f, pink   },
    };

    for (const auto& r : ribbons)
    {
        const float base = b.getHeight() * r.y;
        auto yAt = [&] (float x)
        {
            return base + r.amp * std::sin (x * r.freq + phase * r.speed)
                        + 0.5f * r.amp * std::sin (x * r.freq * 2.3f - phase * r.speed * 1.3f);
        };

        const float step = 10.0f;
        juce::Path p;
        p.startNewSubPath (0.0f, yAt (0.0f));
        float x = step;
        for (; x <= b.getWidth() + step; x += step) p.lineTo (x, yAt (x));
        for (x -= step; x >= 0.0f; x -= step)       p.lineTo (x, yAt (x) + r.thick);
        p.closeSubPath();

        juce::ColourGradient grad (r.c.withAlpha (0.0f), 0.0f, base - r.amp * 1.5f,
                                   r.c.withAlpha (0.0f), 0.0f, base + r.thick + r.amp * 1.5f, false);
        grad.addColour (0.35, r.c.withAlpha (0.22f));
        grad.addColour (0.65, r.c.withAlpha (0.09f));
        g.setGradientFill (grad);
        g.fillPath (p);
    }
}

void MainPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (bg1, 0.0f, 0.0f, bg2, 0.0f, b.getHeight(), false));
    g.fillAll();
    drawAurora (g, b);

    // заголовок
    juce::Font title (juce::FontOptions (36.0f, juce::Font::bold));
    title.setExtraKerningFactor (0.18f);
    g.setFont (title);
    g.setGradientFill (juce::ColourGradient (teal, 24.0f, 0.0f, pink, 250.0f, 0.0f, false));
    g.drawText ("AURORA", 24, 8, 300, 40, juce::Justification::centredLeft);

    juce::Font sub (juce::FontOptions (12.0f, juce::Font::bold));
    sub.setExtraKerningFactor (0.55f);
    g.setFont (sub);
    g.setColour (text.withAlpha (0.65f));
    g.drawText ("COMPRESSOR", 27, 44, 300, 16, juce::Justification::centredLeft);

    // нижняя панель
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (bottomPanel, 14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (bottomPanel.reduced (0.5f), 14.0f, 1.0f);
}

//==============================================================================
// Editor: масштабируемая обёртка
//==============================================================================
AuroraCompressorAudioProcessorEditor::AuroraCompressorAudioProcessorEditor (AuroraCompressorAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (panel);

    constexpr int minW = 615, maxW = 1640;
    setResizable (true, true);
    setResizeLimits (minW, (int) std::round (minW * (double) MainPanel::baseH / MainPanel::baseW),
                     maxW, (int) std::round (maxW * (double) MainPanel::baseH / MainPanel::baseW));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) MainPanel::baseW / (double) MainPanel::baseH);

    const int w = juce::jlimit (minW, maxW, proc.editorWidth.load());
    setSize (w, (int) std::round (w * (double) MainPanel::baseH / MainPanel::baseW));
}

AuroraCompressorAudioProcessorEditor::~AuroraCompressorAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AuroraCompressorAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (aurora::col::bg1);
}

void AuroraCompressorAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) MainPanel::baseW;
    panel.setBounds (0, 0, MainPanel::baseW, MainPanel::baseH);
    panel.setTransform (juce::AffineTransform::scale (s));
    proc.editorWidth = getWidth();
}
