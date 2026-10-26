#include "PluginEditor.h"

using namespace s2m;

//==============================================================================
// Look and feel
//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::bg);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::panelHi);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::bg);
    setColour (juce::ComboBox::backgroundColourId, colours::panelHi);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::line);
    setColour (juce::ComboBox::arrowColourId, colours::dim);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::bg);
    setColour (juce::TextEditor::backgroundColourId, colours::panelHi);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, colours::line);
    setColour (juce::CaretComponent::caretColourId, colours::accent);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& base,
                                        bool isOver, bool isDown)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = base;
    if (! b.isEnabled())   c = c.withMultipliedAlpha (0.4f);
    else if (isDown)       c = c.brighter (0.15f);
    else if (isOver)       c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : colours::line);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path p;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (colours::dim);
    g.fillPath (p);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = (float) y + (float) h * 0.5f;
    juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) w, 4.0f);
    g.setColour (colours::line);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (colours::accent);
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    g.setColour (colours::text);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

//==============================================================================
// Drop zone
//==============================================================================
DropZone::DropZone (juce::AudioFormatManager& fm)
    : formats (fm), thumb (512, fm, cache)
{
    thumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

DropZone::~DropZone() { thumb.removeChangeListener (this); }

void DropZone::setFile (const juce::File& f)
{
    file = f;
    if (f.existsAsFile())
        thumb.setSource (new juce::FileInputSource (f));
    else
        thumb.clear();
    repaint();
}

bool DropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (formats.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void DropZone::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        juce::File file2 (f);
        if (formats.findFormatForFileExtension (file2.getFileExtension()) != nullptr)
        {
            if (onFile) onFile (file2);
            break;
        }
    }
    repaint();
}

void DropZone::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    chooser = std::make_unique<juce::FileChooser> ("Pick a sample",
                    file.existsAsFile() ? file.getParentDirectory()
                                        : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                    formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto r = fc.getResult();
                              if (r.existsAsFile() && onFile) onFile (r);
                          });
}

void DropZone::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (dragOver ? colours::panelHi : colours::panel);
    g.fillRoundedRectangle (r, 10.0f);

    juce::Path border;
    border.addRoundedRectangle (r, 10.0f);
    const float dashes[] = { 6.0f, 5.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (dragOver || isMouseOver() ? colours::accent : colours::line);
    g.fillPath (dashed);

    auto inner = getLocalBounds().reduced (18, 12);
    if (file.existsAsFile() && thumb.getTotalLength() > 0)
    {
        auto top = inner.removeFromTop (22);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (file.getFileName(), top, juce::Justification::centredLeft, true);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (juce::String (thumb.getTotalLength(), 1) + " sec  -  click or drop to change",
                    top, juce::Justification::centredRight, true);
        inner.removeFromTop (6);
        g.setColour (colours::accent.withAlpha (0.85f));
        thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Drop a sample here", inner.withTrimmedBottom (inner.getHeight() / 2 - 4),
                    juce::Justification::centredBottom);
        g.setColour (colours::dim);
        g.setFont (juce::FontOptions (13.5f));
        g.drawText ("or click to browse  -  WAV, MP3, FLAC, AIFF, OGG",
                    inner.withTrimmedTop (inner.getHeight() / 2 + 4), juce::Justification::centredTop);
    }
}

//==============================================================================
// Drag chips & result rows
//==============================================================================
DragChip::DragChip (const juce::String& l, juce::Colour c) : label (l), colour (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag into your DAW");
    setEnabled (false);
}

void DragChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = isEnabled();
    g.setColour (on ? colour.withAlpha (isMouseOver() ? 0.32f : 0.18f) : colours::panelHi.withAlpha (0.4f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? colour : colours::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    // grip dots
    auto grip = r.removeFromLeft (16.0f).reduced (5.0f, 0.0f);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (grip.getX() + (float) j * 4.0f, grip.getCentreY() - 5.0f + (float) i * 4.0f, 2.0f, 2.0f);

    g.setColour (on ? colours::text : colours::dim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (label, r.withTrimmedRight (6.0f), juce::Justification::centred);
}

void DragChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! file.existsAsFile() || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

PartRow::PartRow (Sample2MidiProcessor& p, const EngineRunner::Part& pt, bool isCombined)
    : proc (p), part (pt), combined (isCombined)
{
    midiChip.setFile (part.midi);
    audioChip.setFile (part.audio);
    addAndMakeVisible (midiChip);
    if (! combined)
    {
        addAndMakeVisible (audioChip);
        addAndMakeVisible (play);
        play.setEnabled (part.audio.existsAsFile());
        play.setTooltip ("Listen to this part");
        play.onClick = [this]
        {
            if (proc.isPreviewing (part.audio)) proc.stopPreview();
            else                                 proc.startPreview (part.audio);
            refreshPlayState();
        };
    }
    refreshPlayState();
}

void PartRow::refreshPlayState()
{
    const bool on = ! combined && proc.isPreviewing (part.audio);
    play.setButtonText (on ? "Stop" : "Play");
    play.setColour (juce::TextButton::buttonColourId, on ? colours::accent : colours::panelHi);
    play.setColour (juce::TextButton::textColourOffId, on ? colours::bg : colours::text);
}

void PartRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
    g.setColour (combined ? colours::accent2.withAlpha (0.08f) : colours::panel);
    g.fillRoundedRectangle (r, 7.0f);

    auto text = getLocalBounds().reduced (14, 0);
    text.setRight (combined ? midiChip.getX() - 10 : play.getX() - 10);

    juce::String title = combined ? "All parts (one multi-track MIDI)"
                                  : part.name.substring (0, 1).toUpperCase() + part.name.substring (1);
    juce::String sub;
    if (! part.kept)               sub = "too quiet - nothing here";
    else if (part.notes == 0)      sub = "no notes found";
    else if (part.name == "drums") sub = juce::String (part.notes) + " hits  (kick / snare / hats)";
    else                           sub = juce::String (part.notes) + " notes";

    g.setColour (part.kept ? colours::text : colours::dim);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, text.withTrimmedBottom (text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (colours::dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (sub, text.withTrimmedTop (text.getHeight() / 2 + 2), juce::Justification::topLeft, true);
}

void PartRow::resized()
{
    auto r = getLocalBounds().reduced (10, 10);
    if (combined)
    {
        midiChip.setBounds (r.removeFromRight (84));
        return;
    }
    audioChip.setBounds (r.removeFromRight (74));
    r.removeFromRight (8);
    midiChip.setBounds (r.removeFromRight (74));
    r.removeFromRight (8);
    play.setBounds (r.removeFromRight (60));
}

//==============================================================================
// Editor
//==============================================================================
Sample2MidiEditor::Sample2MidiEditor (Sample2MidiProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dropZone (p.formats)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (dropZone);
    dropZone.onFile = [this] (const juce::File& f)
    {
        proc.options.input = f;
        dropZone.setFile (f);
        convertButton.setEnabled (! proc.engine.isBusy());
    };

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, s2m::colours::dim);
        l.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (l);
    };
    setupLabel (modeLabel, "WHAT'S IN THE SAMPLE");
    setupLabel (typeLabel, "INSTRUMENT");
    setupLabel (sensLabel, "SENSITIVITY");
    setupLabel (quantLabel, "SNAP TO GRID");
    setupLabel (tempoLabel, "TEMPO (BPM)");

    modeBox.addItem ("Auto-detect", 1);
    modeBox.addItem ("Several instruments - split it", 2);
    modeBox.addItem ("One instrument - don't split", 3);
    modeBox.onChange = [this] { pushOptions(); };
    addAndMakeVisible (modeBox);

    typeBox.addItem ("Melodic (keys, synth, guitar...)", 1);
    typeBox.addItem ("Drums", 2);
    typeBox.addItem ("Bass", 3);
    typeBox.addItem ("Vocals / lead line", 4);
    typeBox.onChange = [this] { pushOptions(); };
    addAndMakeVisible (typeBox);

    sensSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sensSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sensSlider.setRange (0.0, 1.0, 0.01);
    sensSlider.setTooltip ("Left = only the clear notes. Right = catch quieter notes too.");
    sensSlider.onValueChange = [this] { pushOptions(); };
    addAndMakeVisible (sensSlider);

    quantBox.addItem ("Off (keep the feel)", 1);
    quantBox.addItem ("1/8", 2);
    quantBox.addItem ("1/16", 3);
    quantBox.addItem ("1/32", 4);
    quantBox.onChange = [this] { pushOptions(); };
    addAndMakeVisible (quantBox);

    tempoEdit.setTextToShowWhenEmpty ("Auto", s2m::colours::dim);
    tempoEdit.setInputRestrictions (6, "0123456789.");
    tempoEdit.setJustification (juce::Justification::centredLeft);
    tempoEdit.setIndents (10, 0);
    tempoEdit.onTextChange = [this] { pushOptions(); };
    addAndMakeVisible (tempoEdit);

    convertButton.setColour (juce::TextButton::buttonColourId, s2m::colours::accent);
    convertButton.setColour (juce::TextButton::textColourOffId, s2m::colours::bg);
    convertButton.onClick = [this] { startConversion(); };
    addAndMakeVisible (convertButton);

    cancelButton.onClick = [this] { proc.engine.cancel(); };
    addAndMakeVisible (cancelButton);

    outputButton.setTooltip ("Where the MIDI and audio files are saved");
    outputButton.onClick = [this] { chooseOutputFolder(); };
    addAndMakeVisible (outputButton);

    openFolderButton.onClick = [this]
    {
        auto snap = proc.engine.getSnapshot();
        auto f = snap.outputFolder.isDirectory() ? snap.outputFolder : proc.options.outputFolder;
        f.createDirectory();
        f.startAsProcess();
    };
    addAndMakeVisible (openFolderButton);

    detailsButton.setClickingTogglesState (true);
    detailsButton.onClick = [this] { showingLog = detailsButton.getToggleState(); resized(); };
    addAndMakeVisible (detailsButton);

    engineButton.onClick = [this] { findEngine(); };
    addChildComponent (engineButton);

    resultsView.setViewedComponent (&resultsHolder, false);
    resultsView.setScrollBarsShown (true, false);
    addAndMakeVisible (resultsView);

    logView.setMultiLine (true);
    logView.setReadOnly (true);
    logView.setScrollbarsShown (true);
    logView.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    addChildComponent (logView);

    setResizable (true, true);
    setResizeLimits (640, 640, 1400, 1400);
    setSize (780, 780);

    syncFromProcessor();
    startTimerHz (20);
}

Sample2MidiEditor::~Sample2MidiEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Sample2MidiEditor::syncFromProcessor()
{
    auto& o = proc.options;
    dropZone.setFile (o.input);
    modeBox.setSelectedId (o.mode == "split" ? 2 : o.mode == "single" ? 3 : 1, juce::dontSendNotification);
    typeBox.setSelectedId (o.singleType == "drums" ? 2 : o.singleType == "bass" ? 3
                           : o.singleType == "vocals" ? 4 : 1, juce::dontSendNotification);
    sensSlider.setValue (o.sensitivity, juce::dontSendNotification);
    quantBox.setSelectedId (o.quantize == "1/8" ? 2 : o.quantize == "1/16" ? 3
                            : o.quantize == "1/32" ? 4 : 1, juce::dontSendNotification);
    tempoEdit.setText (o.tempo > 0 ? juce::String (o.tempo, 1) : juce::String(), false);
    outputButton.setButtonText ("Save to: " + o.outputFolder.getFileName());
    typeBox.setEnabled (o.mode == "single");
    typeLabel.setAlpha (o.mode == "single" ? 1.0f : 0.45f);
    lastGeneration = -1;
    lastStateVersion = proc.stateVersion.load();
}

void Sample2MidiEditor::pushOptions()
{
    auto& o = proc.options;
    const int m = modeBox.getSelectedId();
    o.mode = m == 2 ? "split" : m == 3 ? "single" : "auto";
    const int t = typeBox.getSelectedId();
    o.singleType = t == 2 ? "drums" : t == 3 ? "bass" : t == 4 ? "vocals" : "melodic";
    o.sensitivity = (float) sensSlider.getValue();
    const int q = quantBox.getSelectedId();
    o.quantize = q == 2 ? "1/8" : q == 3 ? "1/16" : q == 4 ? "1/32" : "off";
    o.tempo = juce::jlimit (0.0, 400.0, tempoEdit.getText().getDoubleValue());
    typeBox.setEnabled (o.mode == "single");
    typeLabel.setAlpha (o.mode == "single" ? 1.0f : 0.45f);
}

void Sample2MidiEditor::chooseOutputFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Where should the files go?", proc.options.outputFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto r = fc.getResult();
                              if (r.isDirectory())
                              {
                                  proc.options.outputFolder = r;
                                  outputButton.setButtonText ("Save to: " + r.getFileName());
                              }
                          });
}

void Sample2MidiEditor::findEngine()
{
   #if JUCE_WINDOWS
    const juce::String pattern = "transform6-engine.exe;*.py";
   #else
    const juce::String pattern = "transform6-engine;*.py";
   #endif
    chooser = std::make_unique<juce::FileChooser> ("Find the TRANSFORM-6 engine",
                    juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory), pattern);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto r = fc.getResult();
                              if (r.existsAsFile())
                              {
                                  proc.engine.setEnginePath (r);
                                  messageText = "Engine found. You're good to go.";
                                  lastOk = true;
                                  engineButton.setVisible (false);
                                  repaint();
                              }
                          });
}

void Sample2MidiEditor::startConversion()
{
    pushOptions();
    if (! proc.options.input.existsAsFile())
    {
        messageText = "Drop a sample in first.";
        lastOk = false;
        repaint();
        return;
    }
    if (! proc.engine.findEngine().existsAsFile())
    {
        messageText = "Couldn't find the engine. Click \"Find engine\" and pick transform6-engine.";
        lastOk = false;
        engineButton.setVisible (true);
        resized();
        repaint();
        return;
    }
    proc.stopPreview();
    rows.clear();
    resultsHolder.setSize (resultsView.getMaximumVisibleWidth(), 0);
    logView.clear();
    proc.engine.start (proc.options);
}

void Sample2MidiEditor::rebuildResults (const EngineRunner::Snapshot& s)
{
    rows.clear();
    if (s.finished && s.ok)
    {
        const bool multiple = s.parts.size() > 1 && s.combinedMidi.existsAsFile();
        if (multiple)
        {
            EngineRunner::Part all;
            all.name = "all";
            all.midi = s.combinedMidi;
            for (auto& p : s.parts) all.notes += p.notes;
            rows.add (new s2m::PartRow (proc, all, true));
        }
        for (auto& p : s.parts)
            rows.add (new s2m::PartRow (proc, p));
        for (auto* r : rows)
            resultsHolder.addAndMakeVisible (r);
    }
    headerInfo = s.finished && s.ok
        ? (juce::String (s.wasSplit ? "Split into parts" : "Single part") + "   -   "
           + juce::String (s.tempo, s.tempo == std::floor (s.tempo) ? 0 : 1) + " BPM   -   drag a tag into your DAW")
        : juce::String();
    resized();
}

void Sample2MidiEditor::timerCallback()
{
    if (proc.stateVersion.load() != lastStateVersion)
        syncFromProcessor();

    auto s = proc.engine.getSnapshot();
    running = s.running;

    shownProgress += (s.progress - shownProgress) * 0.25f;
    if (std::abs (s.progress - shownProgress) < 0.002f) shownProgress = s.progress;

    statusText = s.running ? s.status : juce::String();
    if (s.finished || s.running)
    {
        if (s.message.isNotEmpty() || ! s.running)
        {
            messageText = s.running ? juce::String() : s.message;
            lastOk = s.ok;
        }
    }

    if (s.generation != lastGeneration)
    {
        lastGeneration = s.generation;
        rebuildResults (s);
        if (s.finished && ! s.ok && s.status == "Engine not found")
        {
            engineButton.setVisible (true);
            resized();
        }
    }

    if (showingLog)
    {
        auto joined = s.log.joinIntoString ("\n");
        if (joined != logView.getText())
        {
            logView.setText (joined, false);
            logView.moveCaretToEnd();
        }
    }

    for (auto* r : rows) r->refreshPlayState();

    convertButton.setEnabled (! s.running && proc.options.input.existsAsFile());
    convertButton.setButtonText (s.running ? "Working..." : "Convert to MIDI");
    cancelButton.setVisible (s.running);
    modeBox.setEnabled (! s.running);
    repaint (progressArea.expanded (2, 24));
}

void Sample2MidiEditor::paint (juce::Graphics& g)
{
    g.fillAll (s2m::colours::bg);

    // Title
    auto top = getLocalBounds().reduced (20, 0).removeFromTop (54);
    g.setColour (s2m::colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("TRANSFORM-6", top, juce::Justification::centredLeft);
    g.setColour (s2m::colours::dim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("split it  -  find the notes  -  drag it in", top, juce::Justification::centredRight);

    // Progress bar + status
    auto bar = progressArea.toFloat();
    g.setColour (s2m::colours::panel);
    g.fillRoundedRectangle (bar, 4.0f);
    if (shownProgress > 0.0f && (running || ! lastOk || shownProgress >= 1.0f))
    {
        g.setColour (lastOk || running ? s2m::colours::accent : s2m::colours::bad);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * shownProgress)), 4.0f);
    }
    auto line = progressArea.translated (0, progressArea.getHeight() + 4).withHeight (18);
    g.setFont (juce::FontOptions (13.0f));
    if (running)
    {
        g.setColour (s2m::colours::text);
        g.drawText (statusText, line, juce::Justification::centredLeft, true);
        g.setColour (s2m::colours::dim);
        g.drawText (juce::String (juce::roundToInt (shownProgress * 100.0f)) + "%", line, juce::Justification::centredRight);
    }
    else if (messageText.isNotEmpty())
    {
        g.setColour (lastOk ? s2m::colours::accent2 : s2m::colours::bad);
        g.drawText (messageText, line, juce::Justification::centredLeft, true);
    }

    if (headerInfo.isNotEmpty() && ! running && ! showingLog)
    {
        g.setColour (s2m::colours::dim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (headerInfo, resultsView.getBounds().translated (0, -20).withHeight (18),
                    juce::Justification::centredLeft, true);
    }

    if (rows.isEmpty() && ! running && ! showingLog)
    {
        g.setColour (s2m::colours::dim.withAlpha (0.7f));
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText ("Your MIDI parts will show up here. Drag the MIDI or WAV tags\n"
                          "straight onto a track in FL Studio or Ableton.",
                          resultsView.getBounds(), juce::Justification::centred, 3);
    }
}

void Sample2MidiEditor::resized()
{
    auto r = getLocalBounds().reduced (20, 0);
    r.removeFromTop (54);

    dropZone.setBounds (r.removeFromTop (juce::jlimit (90, 130, getHeight() / 6)));
    r.removeFromTop (14);

    // Options grid: two rows of controls with small labels above them
    auto rowA = r.removeFromTop (52);
    const int gap = 12;
    auto place = [&] (juce::Rectangle<int>& row, int width, juce::Label& l, juce::Component& c)
    {
        auto cell = row.removeFromLeft (width);
        row.removeFromLeft (gap);
        l.setBounds (cell.removeFromTop (18));
        c.setBounds (cell.removeFromTop (30));
    };
    const int w = rowA.getWidth();
    place (rowA, (w - gap) / 2, modeLabel, modeBox);
    place (rowA, rowA.getWidth(), typeLabel, typeBox);
    r.removeFromTop (8);

    auto rowB = r.removeFromTop (52);
    const int w3 = (rowB.getWidth() - 2 * gap);
    place (rowB, w3 * 45 / 100, sensLabel, sensSlider);
    place (rowB, w3 * 30 / 100, quantLabel, quantBox);
    place (rowB, rowB.getWidth(), tempoLabel, tempoEdit);
    r.removeFromTop (14);

    auto actions = r.removeFromTop (40);
    convertButton.setBounds (actions.removeFromLeft (190));
    actions.removeFromLeft (10);
    cancelButton.setBounds (actions.removeFromLeft (90));
    outputButton.setBounds (actions.removeFromRight (juce::jmin (230, actions.getWidth() - 10)));
    r.removeFromTop (12);

    progressArea = r.removeFromTop (8);
    r.removeFromTop (26 + 22);

    // Footer
    auto footer = r.removeFromBottom (58).withTrimmedTop (10).withTrimmedBottom (12);
    openFolderButton.setBounds (footer.removeFromLeft (120));
    footer.removeFromLeft (10);
    detailsButton.setBounds (footer.removeFromLeft (90));
    engineButton.setBounds (footer.removeFromRight (120));

    resultsView.setBounds (r);
    logView.setBounds (r);
    resultsView.setVisible (! showingLog);
    logView.setVisible (showingLog);

    const int rowH = 58;
    const int width = r.getWidth() - (rows.size() * rowH > r.getHeight() ? resultsView.getScrollBarThickness() + 4 : 0);
    resultsHolder.setSize (width, rows.size() * rowH);
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, width, rowH);
    repaint();
}
