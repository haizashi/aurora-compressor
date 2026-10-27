#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

// Фабричные пресеты (в коде) + пользовательские (XML-файлы в папке приложения).
// Папка пользовательских пресетов:
//   Windows: %APPDATA%\Aurora Audio\AURORA Compressor\Presets
//   macOS:   ~/Library/Application Support/Aurora Audio/AURORA Compressor/Presets
//   Linux:   ~/.config/Aurora Audio/AURORA Compressor/Presets
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;   // id параметра -> значение
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        // Индексы choice-параметров: detector 0=Peak 1=RMS; lookahead 0..4 = Off,1,2,5,10 ms;
        // oversample 0..2 = Off,2x,4x; scsource 0=Internal 1=External.
        presets.push_back ({ "Init", {} });

        presets.push_back ({ "Glue Bus", {
            { "thresh", -20.0f }, { "ratio", 2.0f }, { "attack", 30.0f }, { "release", 200.0f },
            { "knee", 12.0f }, { "auto", 1.0f }, { "detector", 1.0f }, { "schpf", 80.0f } } });

        presets.push_back ({ "Vocal Smooth", {
            { "thresh", -24.0f }, { "ratio", 3.5f }, { "attack", 8.0f }, { "release", 140.0f },
            { "knee", 9.0f }, { "makeup", 3.0f }, { "detector", 1.0f }, { "schpf", 100.0f },
            { "lookahead", 1.0f } } });

        presets.push_back ({ "Drum Punch", {
            { "thresh", -16.0f }, { "ratio", 5.0f }, { "attack", 25.0f }, { "release", 80.0f },
            { "knee", 3.0f }, { "makeup", 2.0f }, { "schpf", 60.0f } } });

        presets.push_back ({ "Bass Tamer", {
            { "thresh", -22.0f }, { "ratio", 4.0f }, { "attack", 15.0f }, { "release", 160.0f },
            { "knee", 6.0f }, { "makeup", 3.0f }, { "detector", 1.0f }, { "schpf", 40.0f } } });

        presets.push_back ({ "Parallel Smash", {
            { "thresh", -35.0f }, { "ratio", 12.0f }, { "attack", 2.0f }, { "release", 90.0f },
            { "knee", 2.0f }, { "makeup", 8.0f }, { "mix", 40.0f }, { "schpf", 120.0f } } });

        presets.push_back ({ "Mastering Gentle", {
            { "thresh", -14.0f }, { "ratio", 1.8f }, { "attack", 40.0f }, { "release", 300.0f },
            { "knee", 18.0f }, { "makeup", 1.0f }, { "detector", 1.0f }, { "schpf", 60.0f },
            { "lookahead", 2.0f }, { "oversample", 1.0f } } });

        presets.push_back ({ "Peak Catcher", {
            { "thresh", -6.0f }, { "ratio", 20.0f }, { "attack", 0.5f }, { "release", 50.0f },
            { "knee", 0.0f }, { "lookahead", 3.0f }, { "oversample", 2.0f } } });
    }

    const std::vector<Preset>& factory() const { return presets; }

    void applyFactory (int index)
    {
        if (index < 0 || index >= (int) presets.size()) return;

        // сначала всё по умолчанию, затем значения пресета
        for (auto* p : apvts.processor.getParameters())
            p->setValueNotifyingHost (p->getDefaultValue());

        for (const auto& [id, value] : presets[(size_t) index].values)
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    //==========================================================================
    static juce::File userDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("Aurora Audio").getChildFile ("AURORA Compressor").getChildFile ("Presets");
        d.createDirectory();
        return d;
    }

    static juce::StringArray userNames()
    {
        juce::StringArray names;
        for (const auto& f : userDir().findChildFiles (juce::File::findFiles, false, "*.xml"))
            names.add (f.getFileNameWithoutExtension());
        names.sort (true);
        return names;
    }

    // Возвращает фактическое имя сохранённого пресета или пустую строку при ошибке.
    juce::String saveUser (const juce::String& name)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty()) return {};

        if (auto xml = apvts.copyState().createXml())
            if (xml->writeTo (userDir().getChildFile (clean + ".xml")))
                return clean;

        return {};
    }

    bool loadUser (const juce::String& name)
    {
        if (auto xml = juce::parseXML (userDir().getChildFile (name + ".xml")))
            if (xml->hasTagName (apvts.state.getType()))
            {
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
                return true;
            }
        return false;
    }

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> presets;
};
