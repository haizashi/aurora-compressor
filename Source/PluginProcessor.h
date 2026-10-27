#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "CompressorDSP.h"
#include "PresetManager.h"

class AuroraCompressorAudioProcessor : public juce::AudioProcessor
{
public:
    AuroraCompressorAudioProcessor();
    ~AuroraCompressorAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AURORA Compressor"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Фабричные пресеты видны хосту как «программы»
    int getNumPrograms() override { return (int) presets.factory().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadFactoryPreset (int index);
    void loadUserPreset (const juce::String& name);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // Значения для индикаторов (пишет аудиопоток, читает GUI)
    std::atomic<float> inDb  { -100.0f };
    std::atomic<float> outDb { -100.0f };
    std::atomic<float> grDb  { 0.0f };    // положительное число = величина компрессии

    // Состояние интерфейса (сохраняется вместе с проектом)
    std::atomic<int> editorWidth { 820 };
    juce::String currentPresetName { "Init" };

private:
    void applyConfig (int osIdx, int lookIdx);
    int  lookaheadHostSamples (int lookIdx) const;

    aurora::CompressorDSP dsp;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> osMain, osSide;   // [0]=2x, [1]=4x

    double hostSr = 44100.0;
    int currentOsIdx = -1, currentLookIdx = -1, currentProgram = 0;
    float lastOutGain = 1.0f;

    std::atomic<float> *pThresh = nullptr, *pRatio = nullptr, *pAttack = nullptr, *pRelease = nullptr,
                       *pKnee = nullptr, *pMakeup = nullptr, *pMix = nullptr, *pScHpf = nullptr,
                       *pOutput = nullptr, *pDetector = nullptr, *pAuto = nullptr,
                       *pLookahead = nullptr, *pOversample = nullptr, *pScSource = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraCompressorAudioProcessor)
};
