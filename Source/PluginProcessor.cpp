#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
std::unique_ptr<juce::AudioParameterFloat> fparam (const char* id, const char* name, float lo, float hi,
                                                   float def, float skew, const char* unit, int decimals)
{
    juce::NormalisableRange<float> range (lo, hi, 0.0f, skew);
    const juce::String u (unit);
    auto attrs = juce::AudioParameterFloatAttributes()
                     .withLabel (u)
                     .withStringFromValueFunction ([u, decimals] (float v, int)
                                                   { return juce::String (v, decimals) + (u.isEmpty() ? "" : " " + u); })
                     .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs);
}

std::unique_ptr<juce::AudioParameterChoice> cparam (const char* id, const char* name, juce::StringArray items, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
}

std::unique_ptr<juce::AudioParameterBool> bparam (const char* id, const char* name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

// Werkspresets = Special Views (nur Abweichungen vom Standard)
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Init", {} },
        { "Easy Compressor", { { "threshold", -20 }, { "ratio", 3 }, { "knee", 8 }, { "adapt", 50 },
                               { "rms", 1 }, { "detection", 2 } } },
        { "Vocal Compressor", { { "input", 9 }, { "ratio", 3.5f }, { "knee", 10 }, { "attack", 5 },
                                { "release", 120 }, { "adapt", 60 }, { "rms", 5 }, { "detection", 2 },
                                { "sclow", 100 }, { "satamt", 3 }, { "satdyn", 50 }, { "warmth", 4 },
                                { "mix", 85 }, { "dryhp", 4000 } } },
        { "De-Esser", { { "threshold", -30 }, { "ratio", 6 }, { "knee", 3 }, { "attack", 1 },
                        { "release", 60 }, { "adapt", 60 }, { "rms", 1 }, { "sclow", 5000 },
                        { "schigh", 12000 }, { "scslope", 1 } } },
        { "Drum Compressor", { { "input", 6 }, { "ratio", 8 }, { "knee", 3 }, { "attack", 3 },
                               { "release", 80 }, { "adapt", 30 }, { "rms", 1 }, { "dpr", 1 },
                               { "sclow", 120 }, { "satamt", 8 }, { "satdyn", 60 }, { "warmth", 6 },
                               { "mix", 70 } } },
        { "Bus Compressor", { { "detection", 2 }, { "threshold", -20 }, { "ratio", 4 }, { "knee", 8 },
                              { "attack", 10 }, { "release", 200 }, { "adapt", 50 }, { "rms", 5 },
                              { "sclow", 60 }, { "automakeup", 1 }, { "satamt", 2 } } },
        { "M/S Program Compressor", { { "stereo", 1 }, { "link", 0 }, { "detection", 2 }, { "ratio", 3 },
                                      { "knee", 10 }, { "attack", 15 }, { "release", 250 }, { "adapt", 50 },
                                      { "rms", 5 }, { "sclow", 80 } } },
        { "Limiter Smooth", { { "ratio", 20 }, { "knee", 0 }, { "rms", 0 }, { "threshold", -6 },
                              { "wetlvl", 6 }, { "output", -0.3f }, { "scdelay", -1.25f }, { "attack", 1 },
                              { "release", 150 }, { "adapt", 50 } } },
        { "Limiter Balanced", { { "ratio", 20 }, { "knee", 0 }, { "rms", 0 }, { "threshold", -6 },
                                { "wetlvl", 6 }, { "output", -0.3f }, { "scdelay", -0.8f }, { "attack", 0.5f },
                                { "release", 80 }, { "adapt", 30 } } },
        { "Limiter Punchy", { { "ratio", 20 }, { "knee", 0 }, { "rms", 0 }, { "threshold", -6 },
                              { "wetlvl", 6 }, { "output", -0.3f }, { "scdelay", -0.35f }, { "attack", 0.2f },
                              { "release", 40 }, { "dpr", 1 } } },
    };
    return list;
}
} // namespace

APVTS::ParameterLayout PWKlonProcessor::createLayout()
{
    APVTS::ParameterLayout l;
    l.add (fparam ("input",     "Input",          -24, 24,    0,    1.0f,  "dB", 1));
    l.add (fparam ("output",    "Output",         -24, 24,    0,    1.0f,  "dB", 1));
    l.add (fparam ("threshold", "Threshold",      -60, 0,     -18,  1.0f,  "dB", 1));
    l.add (fparam ("ratio",     "Ratio",          1,   20,    4,    0.4f,  ":1", 2));
    l.add (fparam ("knee",      "Soft Knee",      0,   24,    6,    1.0f,  "dB", 1));
    l.add (fparam ("nonlin",    "Non Lin",        -1,  1,     0,    1.0f,  "",   2));
    l.add (bparam ("automakeup","Auto Makeup",    false));
    l.add (fparam ("attack",    "Attack",         0.05f, 200, 10,   0.3f,  "ms", 2));
    l.add (fparam ("release",   "Release",        5,   2000,  150,  0.35f, "ms", 0));
    l.add (fparam ("adapt",     "Adapt",          0,   100,   0,    1.0f,  "%",  0));
    l.add (fparam ("rms",       "RMS Window",     0,   50,    2,    0.4f,  "ms", 1));
    l.add (cparam ("detection", "Detection",      { "FF", "FB", "INT" }, 0));
    l.add (fparam ("link",      "Channel Link",   0,   100,   100,  1.0f,  "%",  0));
    l.add (cparam ("stereo",    "Stereo Mode",    { "L/R", "M/S" }, 0));
    l.add (bparam ("dpr",       "DPR",            false));
    l.add (fparam ("scmix",     "SC Int:Ext",     -100, 100,  -100, 1.0f,  "",   0));
    l.add (fparam ("scdelay",   "SC Delay",       -5,  5,     0,    1.0f,  "ms", 2));
    l.add (fparam ("sclow",     "SC Low Cut",     20,  2000,  20,   0.3f,  "Hz", 0));
    l.add (fparam ("schigh",    "SC High Cut",    500, 20000, 20000,0.3f,  "Hz", 0));
    l.add (cparam ("scslope",   "SC Slope",       { "6 dB", "12 dB" }, 0));
    l.add (cparam ("scmon",     "SC Monitor",     { "Off", "CH1", "CH2", "Both" }, 0));
    l.add (fparam ("scmonlvl",  "SC Mon Level",   -24, 24,    0,    1.0f,  "dB", 1));
    l.add (cparam ("satpos",    "Sat Position",   { "Post", "Pre" }, 0));
    l.add (fparam ("satamt",    "Sat Amount",     0,   24,    0,    1.0f,  "dB", 1));
    l.add (fparam ("satdyn",    "Sat Dynamics",   0,   100,   0,    1.0f,  "%",  0));
    l.add (fparam ("warmth",    "Warmth",         0,   12,    0,    1.0f,  "dB", 1));
    l.add (fparam ("mix",       "Dry:Wet",        0,   100,   100,  1.0f,  "%",  0));
    l.add (fparam ("wetlvl",    "Wet Level",      -24, 24,    0,    1.0f,  "dB", 1));
    l.add (fparam ("dryhp",     "Dry HP",         20,  10000, 20,   0.3f,  "Hz", 0));
    l.add (bparam ("expand",    "Expand",         false));
    l.add (bparam ("softclip",  "Soft Clip",      true));
    return l;
}

PWKlonProcessor::PWKlonProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (p))
            raw[wp->getParameterID()] = apvts.getRawParameterValue (wp->getParameterID());
}

bool PWKlonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (in != out) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

pw::Params PWKlonProcessor::readParams() const
{
    pw::Params p;
    p.inputDb = get ("input");       p.outputDb = get ("output");
    p.threshold = get ("threshold"); p.ratio = get ("ratio");
    p.knee = get ("knee");           p.nonlin = get ("nonlin");
    p.autoMakeup = get ("automakeup") > 0.5f;
    p.attackMs = get ("attack");     p.releaseMs = get ("release");
    p.adapt = get ("adapt");         p.rmsMs = get ("rms");
    p.detection = (int) get ("detection");
    p.link = get ("link");
    p.midSide = (int) get ("stereo") == 1;
    p.dpr = get ("dpr") > 0.5f;
    p.scMix = get ("scmix");         p.scDelayMs = get ("scdelay");
    p.scLow = get ("sclow");         p.scHigh = get ("schigh");
    p.sc12dB = (int) get ("scslope") == 1;
    p.scMonitor = (int) get ("scmon");
    p.scMonDb = get ("scmonlvl");
    p.satPre = (int) get ("satpos") == 1;
    p.satAmount = get ("satamt");    p.satDynamics = get ("satdyn");
    p.warmth = get ("warmth");
    p.mix = get ("mix");             p.wetDb = get ("wetlvl");
    p.dryHp = get ("dryhp");
    p.expand = get ("expand") > 0.5f;
    p.softClip = get ("softclip") > 0.5f;
    return p;
}

void PWKlonProcessor::prepareToPlay (double sampleRate, int)
{
    dsp.setParams (readParams());
    dsp.prepare (sampleRate);
    setLatencySamples (dsp.latency());
}

void PWKlonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    auto mainIn = getBusBuffer (buffer, true, 0);
    float* L = mainIn.getWritePointer (0);
    float* R = mainIn.getNumChannels() > 1 ? mainIn.getWritePointer (1) : nullptr;

    // Mono: rechten Kanal temporaer duplizieren
    juce::HeapBlock<float> monoTmp;
    if (R == nullptr) { monoTmp.allocate ((size_t) n, false); std::copy (L, L + n, monoTmp.get()); R = monoTmp.get(); }

    const float* eL = nullptr;
    const float* eR = nullptr;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            eL = sc.getReadPointer (0);
            eR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : eL;
        }
    }

    dsp.setParams (readParams());
    dsp.process (L, R, eL, eR, n);

    if (monoTmp != nullptr)
        for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    meterGR1 = dsp.meterGR1; meterGR2 = dsp.meterGR2;
    meterIn = dsp.meterIn;   meterOut = dsp.meterOut;
    meterFF = dsp.meterFF;
}

int PWKlonProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String PWKlonProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size()) ? juce::String (presets()[(size_t) index].name) : juce::String();
}

void PWKlonProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : presets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

void PWKlonProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PWKlonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = vt.getProperty ("program", 0);
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* PWKlonProcessor::createEditor() { return new PWKlonEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PWKlonProcessor(); }
