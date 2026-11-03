// Rendert den Plugin-Editor direkt (ohne VST3-Wrapper) als PNG.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
int main (int, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    PWKlonProcessor proc;
    proc.setCurrentProgram (4);
    proc.setPlayConfigDetails (2, 2, 48000, 512);
    proc.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> buf (4, 512); juce::MidiBuffer midi;
    for (int b = 0; b < 100; ++b) {
        for (int i = 0; i < 512; ++i) { float v = 0.7f * std::sin (0.13f * (b * 512 + i)); buf.setSample (0, i, v); buf.setSample (1, i, v * 0.8f); }
        proc.processBlock (buf, midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditorIfNeeded());
    for (int i = 0; i < 10; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    juce::File f (argv[1]); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
    proc.editorBeingDeleted (ed.get());
    return 0;
}
