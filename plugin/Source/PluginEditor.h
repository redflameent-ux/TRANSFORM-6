#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace s2m
{
    namespace colours
    {
        const juce::Colour bg       { 0xff101215 };
        const juce::Colour panel    { 0xff191c21 };
        const juce::Colour panelHi  { 0xff22262d };
        const juce::Colour line     { 0xff2c313a };
        const juce::Colour text     { 0xffe8e6e1 };
        const juce::Colour dim      { 0xff8b9099 };
        const juce::Colour accent   { 0xffe0a245 };   // amber
        const juce::Colour accent2  { 0xff56c2b1 };   // teal (MIDI)
        const juce::Colour bad      { 0xffe0665a };
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool isOver, bool isDown) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int,
                           juce::ComboBox&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float,
                               float, juce::Slider::SliderStyle, juce::Slider&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (14.0f)); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (14.0f, juce::Font::bold)); }
    };

    /** Shows the loaded sample as a waveform; drop files on it or click to browse. */
    class DropZone : public juce::Component,
                     public juce::FileDragAndDropTarget,
                     private juce::ChangeListener
    {
    public:
        DropZone (juce::AudioFormatManager&);
        ~DropZone() override;
        std::function<void (const juce::File&)> onFile;
        void setFile (const juce::File&);
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;
    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        juce::AudioFormatManager& formats;
        juce::AudioThumbnailCache cache { 4 };
        juce::AudioThumbnail thumb;
        juce::File file;
        bool dragOver = false;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    /** A little tag you can drag straight out into the DAW. */
    class DragChip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        DragChip (const juce::String& label, juce::Colour c);
        void setFile (const juce::File& f) { file = f; setEnabled (f.existsAsFile()); repaint(); }
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override { dragging = false; }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    private:
        juce::String label;
        juce::Colour colour;
        juce::File file;
        bool dragging = false;
    };

    class PartRow : public juce::Component
    {
    public:
        PartRow (Sample2MidiProcessor&, const EngineRunner::Part&, bool isCombined = false);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refreshPlayState();
    private:
        Sample2MidiProcessor& proc;
        EngineRunner::Part part;
        bool combined;
        juce::TextButton play;
        DragChip midiChip { "MIDI", colours::accent2 };
        DragChip audioChip { "WAV", colours::accent };
    };
}

class Sample2MidiEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit Sample2MidiEditor (Sample2MidiProcessor&);
    ~Sample2MidiEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void syncFromProcessor();

private:
    void timerCallback() override;
    void startConversion();
    void rebuildResults (const EngineRunner::Snapshot&);
    void pushOptions();
    void chooseOutputFolder();
    void findEngine();
    void toggleRecord();

    Sample2MidiProcessor& proc;
    s2m::LookAndFeel lnf;

    s2m::DropZone dropZone;
    juce::Label modeLabel, typeLabel, sensLabel, quantLabel, tempoLabel;
    juce::ComboBox modeBox, typeBox, quantBox;
    juce::Slider sensSlider;
    juce::TextEditor tempoEdit;
    juce::TextButton convertButton { "Convert to MIDI" }, cancelButton { "Cancel" };
    juce::TextButton recordButton { "Record" };
    juce::TextButton outputButton, openFolderButton { "Open folder" },
                     detailsButton { "Details" }, engineButton { "Find engine" };

    juce::Rectangle<int> progressArea;
    float shownProgress = 0.0f;
    juce::String statusText, messageText;
    bool lastOk = true, running = false;

    juce::Viewport resultsView;
    juce::Component resultsHolder;
    juce::OwnedArray<s2m::PartRow> rows;
    juce::TextEditor logView;
    bool showingLog = false;
    int lastGeneration = -1, lastStateVersion = -1;
    juce::String headerInfo;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sample2MidiEditor)
};
