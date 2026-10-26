#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "EngineRunner.h"

class Sample2MidiProcessor : public juce::AudioProcessor
{
public:
    Sample2MidiProcessor();
    ~Sample2MidiProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TRANSFORM-6"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor ----
    EngineRunner engine;
    EngineRunner::Options options;
    juce::File defaultOutputFolder() const;

    /** Play a file through the plugin's output (for previewing stems). */
    void startPreview (const juce::File& f);
    void stopPreview();
    bool isPreviewing (const juce::File& f) const;
    juce::File getPreviewFile() const;

    juce::AudioFormatManager formats;
    std::atomic<int> stateVersion { 0 };

private:
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TimeSliceThread readAheadThread { "TRANSFORM-6 preview" };
    juce::File previewFile;
    juce::CriticalSection previewLock;
    double currentRate = 44100.0;
    int currentBlock = 512;
    juce::AudioBuffer<float> previewBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sample2MidiProcessor)
};
