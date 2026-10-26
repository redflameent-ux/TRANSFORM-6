#include "PluginProcessor.h"
#include "PluginEditor.h"

Sample2MidiProcessor::Sample2MidiProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    readAheadThread.startThread();
    recordThread.startThread();
    options.outputFolder = defaultOutputFolder();
}

Sample2MidiProcessor::~Sample2MidiProcessor()
{
    stopPreview();
    stopRecording();
    transport.setSource (nullptr);
    readAheadThread.stopThread (2000);
    recordThread.stopThread (2000);
}

juce::File Sample2MidiProcessor::defaultOutputFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("TRANSFORM-6");
}

bool Sample2MidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

void Sample2MidiProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    currentBlock = samplesPerBlock;
    previewBuffer.setSize (2, juce::jmax (1, samplesPerBlock));
    const juce::ScopedLock sl (previewLock);
    transport.prepareToPlay (samplesPerBlock, sampleRate);
}

void Sample2MidiProcessor::releaseResources()
{
    const juce::ScopedLock sl (previewLock);
    transport.releaseResources();
}

void Sample2MidiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Capture the live input to disk while recording (standalone: the audio interface).
    // Done before anything else so we store clean input, not the monitored passthrough.
    // The lock pairs with stopRecording() so the writer can't be deleted mid-write.
    {
        const juce::ScopedLock sl (writerLock);
        if (auto* writer = activeWriter.load())
        {
            const int nin = getTotalNumInputChannels();
            if (nin > 0 && numSamples > 0)
            {
                const float* chans[2];
                chans[0] = buffer.getReadPointer (0);
                chans[1] = nin > 1 ? buffer.getReadPointer (1) : chans[0];
                writer->write (chans, numSamples);
                samplesRecorded.fetch_add ((juce::int64) numSamples);

                const float pk = juce::jmax (buffer.getMagnitude (0, 0, numSamples),
                                             nin > 1 ? buffer.getMagnitude (1, 0, numSamples) : 0.0f);
                recordLevel.store (pk);
                if (pk > recordPeakMax.load())
                    recordPeakMax.store (pk);
            }
        }
    }

    // Clear any output channels that have no matching input
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Audio passes straight through; the stem preview is mixed on top.
    const juce::ScopedTryLock sl (previewLock);
    if (! sl.isLocked() || ! transport.isPlaying())
        return;

    if (previewBuffer.getNumSamples() < numSamples)
        previewBuffer.setSize (2, numSamples, false, false, true);

    previewBuffer.clear();
    juce::AudioSourceChannelInfo info (&previewBuffer, 0, numSamples);
    transport.getNextAudioBlock (info);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.addFrom (ch, 0, previewBuffer, juce::jmin (ch, 1), 0, numSamples);
}

void Sample2MidiProcessor::startPreview (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
        return;

    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    const double fileRate = newSource->getAudioFormatReader()->sampleRate;

    const juce::ScopedLock sl (previewLock);
    transport.stop();
    transport.setSource (nullptr);
    readerSource = std::move (newSource);
    transport.setSource (readerSource.get(), 32768, &readAheadThread, fileRate, 2);
    transport.prepareToPlay (currentBlock, currentRate);
    transport.setPosition (0.0);
    transport.start();
    previewFile = f;
}

void Sample2MidiProcessor::stopPreview()
{
    const juce::ScopedLock sl (previewLock);
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    previewFile = juce::File();
}

bool Sample2MidiProcessor::isPreviewing (const juce::File& f) const
{
    const juce::ScopedLock sl (previewLock);
    return previewFile == f && transport.isPlaying();
}

juce::File Sample2MidiProcessor::getPreviewFile() const
{
    const juce::ScopedLock sl (previewLock);
    return transport.isPlaying() ? previewFile : juce::File();
}

bool Sample2MidiProcessor::startRecording (const juce::File& destWav)
{
    stopRecording();

    const int nin = getTotalNumInputChannels();
    if (nin <= 0 || currentRate <= 0.0)
        return false;

    destWav.getParentDirectory().createDirectory();
    destWav.deleteFile();

    std::unique_ptr<juce::FileOutputStream> fos (destWav.createOutputStream());
    if (fos == nullptr)
        return false;

    juce::WavAudioFormat wav;
    recordChans = juce::jlimit (1, 2, nin);
    if (auto* writer = wav.createWriterFor (fos.get(), currentRate,
                                            (unsigned int) recordChans, 24, {}, 0))
    {
        fos.release(); // the writer owns the stream now
        samplesRecorded.store (0);
        recordLevel.store (0.0f);
        recordPeakMax.store (0.0f);
        recordedFile = destWav;
        threadedWriter.reset (new juce::AudioFormatWriter::ThreadedWriter (writer, recordThread, 1 << 15));

        const juce::ScopedLock sl (writerLock);
        activeWriter.store (threadedWriter.get());
        recording.store (true);
        return true;
    }

    return false;
}

void Sample2MidiProcessor::stopRecording()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter.store (nullptr);
        recording.store (false);
    }
    threadedWriter.reset(); // flushes the FIFO and closes the file
    recordLevel.store (0.0f);
}

void Sample2MidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("TRANSFORM6");
    state.setProperty ("input", options.input.getFullPathName(), nullptr);
    state.setProperty ("output", options.outputFolder.getFullPathName(), nullptr);
    state.setProperty ("mode", options.mode, nullptr);
    state.setProperty ("singleType", options.singleType, nullptr);
    state.setProperty ("sensitivity", options.sensitivity, nullptr);
    state.setProperty ("quantize", options.quantize, nullptr);
    state.setProperty ("tempo", options.tempo, nullptr);
    state.setProperty ("results", engine.getResultsJson(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void Sample2MidiProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("TRANSFORM6"))
        return;

    options.input        = juce::File (state.getProperty ("input").toString());
    auto out             = state.getProperty ("output").toString();
    options.outputFolder = out.isNotEmpty() ? juce::File (out) : defaultOutputFolder();
    options.mode         = state.getProperty ("mode", "auto").toString();
    options.singleType   = state.getProperty ("singleType", "melodic").toString();
    options.sensitivity  = (float) state.getProperty ("sensitivity", 0.5f);
    options.quantize     = state.getProperty ("quantize", "off").toString();
    options.tempo        = (double) state.getProperty ("tempo", 0.0);
    engine.restoreResults (state.getProperty ("results").toString());
    ++stateVersion;   // the editor notices this and refreshes its controls
}

juce::AudioProcessorEditor* Sample2MidiProcessor::createEditor()
{
    return new Sample2MidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Sample2MidiProcessor();
}
