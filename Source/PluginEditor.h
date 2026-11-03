#pragma once

#include "PluginProcessor.h"

class PWKlonEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PWKlonEditor (PWKlonProcessor&);
    ~PWKlonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeter (juce::Graphics&, juce::Rectangle<int>, const juce::String&, float fraction,
                    const juce::String& value, juce::Colour);

    struct Control
    {
        juce::String paramId;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> toggle;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
        juce::Component* component() const
        {
            if (slider) return slider.get();
            if (combo) return combo.get();
            return toggle.get();
        }
    };

    struct Section
    {
        juce::String title;
        std::vector<juce::String> ids;
        juce::Rectangle<int> bounds;
    };

    PWKlonProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<Section> sections;
    juce::ComboBox presetBox;
    juce::Rectangle<int> meterArea;

    float gr1 = 0, gr2 = 0, inDb = -100, outDb = -100, ffShare = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PWKlonEditor)
};
