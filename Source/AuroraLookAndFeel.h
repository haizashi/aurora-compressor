#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace aurora::col
{
    inline const juce::Colour bg1    { 0xff060a18 };
    inline const juce::Colour bg2    { 0xff101638 };
    inline const juce::Colour panel  { 0xff0a0f24 };
    inline const juce::Colour teal   { 0xff2ef2c0 };
    inline const juce::Colour violet { 0xff8a5cff };
    inline const juce::Colour pink   { 0xffff5ca8 };
    inline const juce::Colour text   { 0xffcfd8ff };
}

class AuroraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AuroraLookAndFeel()
    {
        using namespace aurora::col;
        setColour (juce::Slider::textBoxTextColourId,       text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,  violet.withAlpha (0.5f));
        setColour (juce::Label::textColourId,               text);

        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff141a38));
        setColour (juce::ComboBox::outlineColourId,    teal.withAlpha (0.45f));
        setColour (juce::ComboBox::textColourId,       text);
        setColour (juce::ComboBox::arrowColourId,      teal);
        setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff141a38));
        setColour (juce::PopupMenu::textColourId,                  text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, violet.withAlpha (0.6f));
        setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

        setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff141a38));
        setColour (juce::TextButton::buttonOnColourId, teal.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId,  text);
        setColour (juce::TextButton::textColourOnId,   bg1);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        using namespace aurora::col;
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float r      = std::min (b.getWidth(), b.getHeight()) * 0.5f;
        const auto  c      = b.getCentre();
        const float trackR = r - 4.0f;
        const float bodyR  = r - 13.0f;
        const float angle  = startAngle + pos * (endAngle - startAngle);

        // дорожка
        juce::Path track;
        track.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // светящаяся дуга значения
        juce::Path val;
        val.addCentredArc (c.x, c.y, trackR, trackR, 0.0f, startAngle, angle, true);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (teal.withAlpha (0.05f * (float) (4 - i)));
            g.strokePath (val, juce::PathStrokeType (4.0f + 3.0f * (float) i, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::ColourGradient arc (teal, c.x - r, c.y, pink, c.x + r, c.y, false);
        arc.addColour (0.5, violet);
        g.setGradientFill (arc);
        g.strokePath (val, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // корпус ручки
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222b58), c.x, c.y - bodyR,
                                                 juce::Colour (0xff0a0f24), c.x, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // указатель
        const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
        const auto p1 = c + dir * (bodyR * 0.38f);
        const auto p2 = c + dir * (bodyR * 0.86f);
        g.setColour (teal.withAlpha (0.25f));
        g.drawLine (juce::Line<float> (p1, p2), 6.0f);
        g.setColour (juce::Colours::white);
        g.drawLine (juce::Line<float> (p1, p2), 2.5f);
    }
};
