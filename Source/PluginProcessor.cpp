#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kLookMs[5] = { 0.0f, 1.0f, 2.0f, 5.0f, 10.0f };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AuroraCompressorAudioProcessor::createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "thresh", 1 }, "Threshold",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f, Attr().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "ratio", 1 }, "Ratio",
        NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack",
        NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.4f), 10.0f, Attr().withLabel ("ms")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (5.0f, 1500.0f, 0.1f, 0.4f), 120.0f, Attr().withLabel ("ms")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "knee", 1 }, "Knee",
        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, Attr().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "makeup", 1 }, "Makeup",
        NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, Attr().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "schpf", 1 }, "SC HPF",
        NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.5f), 20.0f, Attr().withLabel ("Hz")));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, Attr().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "detector", 1 }, "Detector",
        StringArray { "Peak", "RMS" }, 0));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "auto", 1 }, "Auto Makeup", false));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "lookahead", 1 }, "Lookahead",
        StringArray { "Off", "1 ms", "2 ms", "5 ms", "10 ms" }, 0));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "oversample", 1 }, "Oversampling",
        StringArray { "Off", "2x", "4x" }, 0));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "scsource", 1 }, "Sidechain",
        StringArray { "Internal", "External" }, 0));

    return { p.begin(), p.end() };
}

//==============================================================================
AuroraCompressorAudioProcessor::AuroraCompressorAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout()),
      presets (apvts)
{
    pThresh     = apvts.getRawParameterValue ("thresh");
    pRatio      = apvts.getRawParameterValue ("ratio");
    pAttack     = apvts.getRawParameterValue ("attack");
    pRelease    = apvts.getRawParameterValue ("release");
    pKnee       = apvts.getRawParameterValue ("knee");
    pMakeup     = apvts.getRawParameterValue ("makeup");
    pMix        = apvts.getRawParameterValue ("mix");
    pScHpf      = apvts.getRawParameterValue ("schpf");
    pOutput     = apvts.getRawParameterValue ("output");
    pDetector   = apvts.getRawParameterValue ("detector");
    pAuto       = apvts.getRawParameterValue ("auto");
    pLookahead  = apvts.getRawParameterValue ("lookahead");
    pOversample = apvts.getRawParameterValue ("oversample");
    pScSource   = apvts.getRawParameterValue ("scsource");
}

//==============================================================================
int AuroraCompressorAudioProcessor::lookaheadHostSamples (int lookIdx) const
{
    return (int) std::lround ((double) kLookMs[juce::jlimit (0, 4, lookIdx)] * 0.001 * hostSr);
}

void AuroraCompressorAudioProcessor::applyConfig (int osIdx, int lookIdx)
{
    if (osIdx != currentOsIdx)
    {
        currentOsIdx = osIdx;
        dsp.setSampleRate (hostSr * (double) (1 << osIdx));
        dsp.reset();
        for (auto& o : osMain) if (o) o->reset();
        for (auto& o : osSide) if (o) o->reset();
    }
    currentLookIdx = lookIdx;

    // Латентность хоста = lookahead + задержка FIR-фильтров оверсэмплера (в сэмплах хоста)
    int osLatency = 0;
    if (osIdx > 0 && osMain[(size_t) osIdx - 1] != nullptr)
        osLatency = (int) std::lround (osMain[(size_t) osIdx - 1]->getLatencyInSamples());

    setLatencySamples (lookaheadHostSamples (lookIdx) + osLatency);
}

void AuroraCompressorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSr = sampleRate;
    dsp.prepare (sampleRate);

    using OS = juce::dsp::Oversampling<float>;
    for (size_t i = 0; i < 2; ++i)
    {
        osMain[i] = std::make_unique<OS> (2, i + 1, OS::filterHalfBandFIREquiripple, true, true);
        osSide[i] = std::make_unique<OS> (2, i + 1, OS::filterHalfBandFIREquiripple, true, true);
        osMain[i]->initProcessing ((size_t) samplesPerBlock);
        osSide[i]->initProcessing ((size_t) samplesPerBlock);
    }

    lastOutGain    = juce::Decibels::decibelsToGain (pOutput->load());
    currentOsIdx   = -1;   // принудительно применить конфигурацию
    applyConfig (juce::jlimit (0, 2, (int) std::lround (pOversample->load())),
                 juce::jlimit (0, 4, (int) std::lround (pLookahead->load())));
}

bool AuroraCompressorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (out != layouts.getMainInputChannelSet())
        return false;

    const auto sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

//==============================================================================
void AuroraCompressorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    auto mainBuf = getBusBuffer (buffer, true, 0);
    const int nch = std::min (mainBuf.getNumChannels(), 2);

    const int osIdx   = juce::jlimit (0, 2, (int) std::lround (pOversample->load()));
    const int lookIdx = juce::jlimit (0, 4, (int) std::lround (pLookahead->load()));
    if (osIdx != currentOsIdx || lookIdx != currentLookIdx)
        applyConfig (osIdx, lookIdx);

    // внешний сайдчейн — только если выбран и шина включена в хосте
    const auto* scBus = getBus (true, 1);
    const bool useExternal = pScSource->load() > 0.5f && scBus != nullptr && scBus->isEnabled();
    juce::AudioBuffer<float> sideBus;
    if (useExternal) sideBus = getBusBuffer (buffer, true, 1);

    const float inPeak = mainBuf.getMagnitude (0, n);

    aurora::CompressorDSP::Params prm;
    prm.thresholdDb = pThresh->load();
    prm.ratio       = pRatio->load();
    prm.attackMs    = pAttack->load();
    prm.releaseMs   = pRelease->load();
    prm.kneeDb      = pKnee->load();
    prm.makeupDb    = pMakeup->load();
    prm.mix         = pMix->load() * 0.01f;
    prm.scHpfHz     = pScHpf->load();
    prm.rms         = pDetector->load() > 0.5f;
    prm.autoMakeup  = pAuto->load() > 0.5f;
    prm.lookaheadSamples = lookaheadHostSamples (lookIdx) << osIdx;

    float gr = 0.0f;

    if (osIdx == 0)
    {
        gr = dsp.process (mainBuf, useExternal ? &sideBus : nullptr, prm);
    }
    else
    {
        auto& om = *osMain[(size_t) osIdx - 1];
        juce::dsp::AudioBlock<float> mainBlock (mainBuf);

        auto upMain = om.processSamplesUp (mainBlock);
        float* mp[2] = { nullptr, nullptr };
        for (int c = 0; c < nch; ++c) mp[c] = upMain.getChannelPointer ((size_t) c);
        juce::AudioBuffer<float> up (mp, nch, (int) upMain.getNumSamples());

        juce::AudioBuffer<float> upSide;
        if (useExternal)
        {
            juce::dsp::AudioBlock<float> sideBlock (sideBus);
            auto upS = osSide[(size_t) osIdx - 1]->processSamplesUp (sideBlock);
            float* sp[2] = { nullptr, nullptr };
            const int sch = std::min ((int) upS.getNumChannels(), 2);
            for (int c = 0; c < sch; ++c) sp[c] = upS.getChannelPointer ((size_t) c);
            upSide = juce::AudioBuffer<float> (sp, sch, (int) upS.getNumSamples());
        }

        gr = dsp.process (up, useExternal ? &upSide : nullptr, prm);
        om.processSamplesDown (mainBlock);
    }

    const float outGain = juce::Decibels::decibelsToGain (pOutput->load());
    mainBuf.applyGainRamp (0, n, lastOutGain, outGain);
    lastOutGain = outGain;

    const float outPeak = mainBuf.getMagnitude (0, n);

    inDb.store  (juce::Decibels::gainToDecibels (inPeak,  -100.0f));
    outDb.store (juce::Decibels::gainToDecibels (outPeak, -100.0f));
    grDb.store  (gr);
}

//==============================================================================
const juce::String AuroraCompressorAudioProcessor::getProgramName (int index)
{
    const auto& f = presets.factory();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? f[(size_t) index].name : juce::String();
}

void AuroraCompressorAudioProcessor::loadFactoryPreset (int index)
{
    const auto& f = presets.factory();
    if (! juce::isPositiveAndBelow (index, (int) f.size())) return;

    currentProgram = index;
    currentPresetName = f[(size_t) index].name;
    presets.applyFactory (index);
}

void AuroraCompressorAudioProcessor::loadUserPreset (const juce::String& name)
{
    if (presets.loadUser (name))
        currentPresetName = name;
}

//==============================================================================
juce::AudioProcessorEditor* AuroraCompressorAudioProcessor::createEditor()
{
    return new AuroraCompressorAudioProcessorEditor (*this);
}

void AuroraCompressorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        xml->setAttribute ("editorWidth", editorWidth.load());
        xml->setAttribute ("preset", currentPresetName);
        copyXmlToBinary (*xml, destData);
    }
}

void AuroraCompressorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (apvts.state.getType()))
            return;

        editorWidth = xml->getIntAttribute ("editorWidth", 820);
        currentPresetName = xml->getStringAttribute ("preset", "Init");
        xml->removeAttribute ("editorWidth");
        xml->removeAttribute ("preset");

        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraCompressorAudioProcessor();
}
