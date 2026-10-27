#pragma once
#include "PluginProcessor.h"
#include "AuroraLookAndFeel.h"

//==============================================================================
// Вертикальный индикатор: уровень (снизу вверх) или gain reduction (сверху вниз).
class LevelMeter : public juce::Component
{
public:
    LevelMeter (std::function<float()> source, float minValue, float maxValue,
                bool isReduction, juce::String caption);

    void tick();
    void paint (juce::Graphics&) override;

private:
    std::function<float()> getValue;
    float minV, maxV, shown = 0.0f;
    bool reduction;
    juce::String name;
};

//==============================================================================
// График передаточной характеристики с живой точкой.
class TransferCurve : public juce::Component
{
public:
    explicit TransferCurve (AuroraCompressorAudioProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    AuroraCompressorAudioProcessor& proc;
    static constexpr float minDb = -60.0f, maxDb = 0.0f;
};

//==============================================================================
// Весь интерфейс рисуется в фиксированном «базовом» размере 820x510,
// а редактор масштабирует эту панель трансформом.
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int baseW = 820, baseH = 510;

    explicit MainPanel (AuroraCompressorAudioProcessor&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawAurora (juce::Graphics&, juce::Rectangle<float>) const;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; // уничтожается первым
    };

    void setupKnob (Knob&, const juce::String& title, const juce::String& paramId,
                    const juce::String& suffix, int decimals);
    void setupCombo (juce::ComboBox&, const juce::StringArray& items, const juce::String& paramId,
                     std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void setupCaption (juce::Label&, const juce::String& text);
    static void place (Knob&, juce::Rectangle<int>);

    // пресеты
    void refreshPresets();
    void stepPreset (int direction);
    void promptSavePreset();

    AuroraCompressorAudioProcessor& proc;

    Knob kThresh, kRatio, kAttack, kRelease, kKnee, kMakeup, kMix, kHpf, kOutput;

    juce::ComboBox detector, lookahead, oversample, scSource;
    juce::TextButton autoBtn { "AUTO MAKEUP" };
    juce::Label capDetector, capLookahead, capOversample, capScSource;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detAtt, lookAtt, osAtt, scAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   autoAtt;

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" };
    std::vector<int> presetIds;

    LevelMeter inMeter, grMeter, outMeter;
    TransferCurve curve;

    juce::Rectangle<float> bottomPanel;
    float phase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainPanel)
};

//==============================================================================
class AuroraCompressorAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AuroraCompressorAudioProcessorEditor (AuroraCompressorAudioProcessor&);
    ~AuroraCompressorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AuroraCompressorAudioProcessor& proc;
    AuroraLookAndFeel laf;      // должен пережить panel
    MainPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraCompressorAudioProcessorEditor)
};
