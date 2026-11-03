#include "PluginEditor.h"

namespace
{
const juce::Colour bg     { 0xff1c1f24 };
const juce::Colour panel  { 0xff262a31 };
const juce::Colour accent { 0xffe8902a };
const juce::Colour text   { 0xffd8dde3 };
const juce::Colour green  { 0xff4cc76b };

constexpr int cellW = 86, cellH = 98, titleH = 22, gap = 10, headerH = 44, meterH = 108;
} // namespace

PWKlonEditor::PWKlonEditor (PWKlonProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    lnf.setColourScheme ({ bg, panel, panel, juce::Colour (0xff5a6270), text, panel, accent, accent, text });
    lnf.setColour (juce::Slider::rotarySliderFillColourId, accent);
    lnf.setColour (juce::Slider::thumbColourId, accent);
    lnf.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a404a));
    lnf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour (juce::ToggleButton::tickColourId, accent);
    setLookAndFeel (&lnf);

    sections = {
        { "LEVEL",       { "input", "output", "softclip" }, {} },
        { "COMPRESSION", { "threshold", "ratio", "knee", "nonlin", "automakeup" }, {} },
        { "ENVELOPE",    { "attack", "release", "adapt", "rms" }, {} },
        { "DETECTION",   { "detection", "link", "stereo", "dpr" }, {} },
        { "SIDECHAIN",   { "scmix", "scdelay", "sclow", "schigh", "scslope", "scmon", "scmonlvl" }, {} },
        { "SATURATION",  { "satpos", "satamt", "satdyn", "warmth" }, {} },
        { "MIX",         { "mix", "wetlvl", "dryhp", "expand" }, {} },
    };

    for (auto& s : sections)
        for (auto& id : s.ids)
        {
            auto* param = proc.apvts.getParameter (id);
            jassert (param != nullptr);
            auto c = std::make_unique<Control>();
            c->paramId = id;

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
            {
                c->combo = std::make_unique<juce::ComboBox>();
                c->combo->addItemList (choice->choices, 1);
                c->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, *c->combo);
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            {
                c->toggle = std::make_unique<juce::ToggleButton> ("On");
                c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, *c->toggle);
            }
            else
            {
                c->slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                c->slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, cellW - 6, 18);
                c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, *c->slider);
            }

            c->label = std::make_unique<juce::Label> ("", param->getName (32));
            c->label->setJustificationType (juce::Justification::centred);
            c->label->setFont (juce::FontOptions (12.0f));
            c->label->setColour (juce::Label::textColourId, text.withAlpha (0.8f));

            addAndMakeVisible (*c->component());
            addAndMakeVisible (*c->label);
            controls.push_back (std::move (c));
        }

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) proc.setCurrentProgram (idx);
    };
    addAndMakeVisible (presetBox);

    // Layout: Zeile 1: Level, Compression, Envelope | Zeile 2: Detection, Sidechain | Zeile 3: Saturation, Mix
    const int row1 = 3 + 5 + 4, row2 = 4 + 7;
    const int width = gap + std::max (row1 * cellW + 3 * gap, row2 * cellW + 2 * gap) + gap;
    const int height = headerH + meterH + 3 * (titleH + cellH + gap) + gap;
    setSize (width, height);
    startTimerHz (30);
}

PWKlonEditor::~PWKlonEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void PWKlonEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerH).reduced (gap, 8);
    presetBox.setBounds (header.removeFromRight (260));
    meterArea = area.removeFromTop (meterH).reduced (gap, 4);

    const std::vector<std::vector<int>> rows = { { 0, 1, 2 }, { 3, 4 }, { 5, 6 } };
    int y = meterArea.getBottom() + gap;
    for (auto& row : rows)
    {
        int x = gap;
        for (int si : row)
        {
            auto& s = sections[(size_t) si];
            s.bounds = { x, y, (int) s.ids.size() * cellW, titleH + cellH };
            int cx = x;
            for (auto& id : s.ids)
            {
                for (auto& c : controls)
                    if (c->paramId == id)
                    {
                        juce::Rectangle<int> cell (cx, y + titleH, cellW, cellH);
                        c->label->setBounds (cell.removeFromTop (16));
                        if (c->slider) c->component()->setBounds (cell.reduced (2, 0));
                        else c->component()->setBounds (cell.withSizeKeepingCentre (cellW - 10, 26));
                    }
                cx += cellW;
            }
            x += s.bounds.getWidth() + gap;
        }
        y += titleH + cellH + gap;
    }
}

void PWKlonEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("PW-KLON", gap, 0, 300, headerH, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Compressor  |  FF / FB / INT", gap + 110, 0, 300, headerH, juce::Justification::centredLeft);

    for (auto& s : sections)
    {
        g.setColour (panel);
        g.fillRoundedRectangle (s.bounds.toFloat(), 6.0f);
        g.setColour (accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (s.title, s.bounds.withHeight (titleH).reduced (8, 0), juce::Justification::centredLeft);
    }

    // Meter
    g.setColour (panel);
    g.fillRoundedRectangle (meterArea.toFloat(), 6.0f);
    auto m = meterArea.reduced (10, 8);
    const int h = (m.getHeight() - 3 * 4) / 4;
    const bool ms = (int) proc.apvts.getRawParameterValue ("stereo")->load() == 1;
    auto dbFrac = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f); };

    drawMeter (g, m.removeFromTop (h), ms ? "GR Mid" : "GR CH1", juce::jlimit (0.0f, 1.0f, gr1 / 24.0f),
               juce::String (gr1, 1) + " dB", accent);
    m.removeFromTop (4);
    drawMeter (g, m.removeFromTop (h), ms ? "GR Side" : "GR CH2", juce::jlimit (0.0f, 1.0f, gr2 / 24.0f),
               juce::String (gr2, 1) + " dB", accent);
    m.removeFromTop (4);
    drawMeter (g, m.removeFromTop (h), "IN", dbFrac (inDb), juce::String (inDb, 1) + " dB", green);
    m.removeFromTop (4);
    drawMeter (g, m.removeFromTop (h), "OUT", dbFrac (outDb), juce::String (outDb, 1) + " dB",
               outDb > -0.1f ? juce::Colours::red : green);

    const int det = (int) proc.apvts.getRawParameterValue ("detection")->load();
    if (det == 2)
    {
        g.setColour (text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("INT: FF share " + juce::String (juce::roundToInt (ffShare * 100)) + " %",
                    meterArea.reduced (12, 0).removeFromTop (14).translated (0, -14), juce::Justification::right);
    }
}

void PWKlonEditor::drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& name, float frac,
                              const juce::String& value, juce::Colour col)
{
    g.setColour (text.withAlpha (0.8f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (name, r.removeFromLeft (70), juce::Justification::centredLeft);
    g.drawText (value, r.removeFromRight (80), juce::Justification::centredRight);
    auto bar = r.reduced (6, 3);
    g.setColour (bg);
    g.fillRoundedRectangle (bar.toFloat(), 3.0f);
    g.setColour (col);
    g.fillRoundedRectangle (bar.withWidth (juce::roundToInt (bar.getWidth() * frac)).toFloat(), 3.0f);
}

void PWKlonEditor::timerCallback()
{
    auto smoothDb = [] (float cur, float target) { return target > cur ? target : cur + (target - cur) * 0.3f; };
    gr1 = smoothDb (gr1, proc.meterGR1.load());
    gr2 = smoothDb (gr2, proc.meterGR2.load());
    inDb = juce::Decibels::gainToDecibels (proc.meterIn.load(), -100.0f);
    outDb = juce::Decibels::gainToDecibels (proc.meterOut.load(), -100.0f);
    ffShare = proc.meterFF.load();

    const int prog = proc.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != prog) presetBox.setSelectedId (prog, juce::dontSendNotification);
    repaint (meterArea.expanded (0, 16));
}
