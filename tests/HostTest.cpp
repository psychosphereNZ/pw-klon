// Laedt die gebaute VST3, prueft Audio, Presets und rendert die GUI als PNG.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

static double rmsDb (const juce::AudioBuffer<float>& b, int ch, int start)
{
    double s = 0; int n = b.getNumSamples() - start;
    for (int i = start; i < b.getNumSamples(); ++i) s += b.getSample (ch, i) * b.getSample (ch, i);
    return 10 * std::log10 (s / n + 1e-20);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm; fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> descs;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (descs, argv[1]);
    if (descs.isEmpty()) { std::cout << "FAIL: plugin not found\n"; return 1; }
    std::cout << "Plugin: " << descs[0]->name << " by " << descs[0]->manufacturerName << "\n";
    juce::String err;
    const double sr = 48000; const int bs = 512;
    auto inst = fm.createPluginInstance (*descs[0], sr, bs, err);
    if (! inst) { std::cout << "FAIL: " << err << "\n"; return 1; }
    inst->enableAllBuses();
    inst->prepareToPlay (sr, bs);
    std::cout << "Latency samples: " << inst->getLatencySamples() << "  Params: " << inst->getParameters().size()
              << "  Programs: " << inst->getNumPrograms() << "  In ch: " << inst->getTotalNumInputChannels() << "\n";

    auto setP = [&] (const juce::String& name, float v) {
        for (auto* p : inst->getParameters())
            if (p->getName (64) == name) {
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) { rp->setValueNotifyingHost (rp->convertTo0to1 (v)); return; }
                if (p->getNumSteps() <= 10) { p->setValueNotifyingHost (v / (float) (p->getNumSteps() - 1)); return; }
                float best = 0, bd = 1e9;
                for (int k = 0; k <= 4000; ++k) { float nv = k / 4000.f; float t = p->getText (nv, 32).getFloatValue(); if (std::abs (t - v) < bd) { bd = std::abs (t - v); best = nv; } }
                p->setValueNotifyingHost (best); return;
            }
        std::cout << "  (param not found: " << name << ")\n";
    };

    auto runSine = [&] (float levelDb, int seconds) {
        const int total = (int) sr * seconds; juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> buf (inst->getTotalNumInputChannels() > 2 ? 4 : 2, bs); juce::MidiBuffer midi;
        const float a = std::sqrt (2.f) * juce::Decibels::decibelsToGain (levelDb);
        for (int pos = 0; pos < total; pos += bs) {
            buf.clear();
            for (int i = 0; i < bs; ++i) { float v = a * std::sin (2 * juce::MathConstants<float>::pi * 1000 * (pos + i) / (float) sr); buf.setSample (0, i, v); buf.setSample (1, i, v); }
            inst->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, std::min (bs, total - pos));
        }
        return out;
    };

    bool ok = true;
    for (auto [mode, name] : { std::pair<int, const char*> { 0, "FF" }, { 1, "FB" }, { 2, "INT" } }) {
        inst->setCurrentProgram (0);
        setP ("Detection", (float) mode); setP ("Soft Knee", 0); setP ("Ratio", 4); setP ("Soft Clip", 0);
        setP ("RMS Window", 5); setP ("Attack", 5); setP ("Release", 100);
        auto lo = runSine (-12, 1); auto hi = runSine (0, 1);
        double r1 = rmsDb (lo, 0, 33600), r2 = rmsDb (hi, 0, 33600);
        double ratio = 12.0 / (r2 - r1);
        std::cout << name << " ratio 4: out@-12 " << r1 << " dB, out@0 " << r2 << " dB -> eff. " << ratio << ":1\n";
        if (! std::isfinite (r1) || ! std::isfinite (r2)) ok = false;
    }
    for (int p = 0; p < inst->getNumPrograms(); ++p) {
        inst->setCurrentProgram (p);
        auto o = runSine (-6, 1);
        float pk = o.getMagnitude (0, 0, o.getNumSamples());
        bool fin = true; for (int i = 0; i < o.getNumSamples(); ++i) if (! std::isfinite (o.getSample (0, i))) fin = false;
        std::cout << "Preset " << p << " '" << inst->getProgramName (p) << "': peak " << juce::Decibels::gainToDecibels (pk) << " dBFS" << (fin ? "" : " NaN!") << "\n";
        if (! fin) ok = false;
    }
    // state roundtrip
    inst->setCurrentProgram (4); juce::MemoryBlock st; inst->getStateInformation (st);
    inst->setCurrentProgram (0); inst->setStateInformation (st.getData(), (int) st.getSize());
    std::cout << "State roundtrip program: " << inst->getCurrentProgram() << " (expected 4)\n";

    if (argc > 2) {
        inst->setCurrentProgram (4); runSine (-3, 1);
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        if (ed) {
            juce::Component holder; holder.setSize (ed->getWidth(), ed->getHeight()); holder.addAndMakeVisible (*ed);
            for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto img = holder.createComponentSnapshot (holder.getLocalBounds(), true, 2.0f);
            juce::File f (argv[2]); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            std::cout << "Editor " << ed->getWidth() << "x" << ed->getHeight() << " -> " << argv[2] << "\n";
            holder.removeChildComponent (ed.get());
            inst->editorBeingDeleted (ed.get());
        }
    }
    inst->releaseResources();
    std::cout << (ok ? "ALL OK\n" : "FAILURES\n");
    return ok ? 0 : 1;
}
