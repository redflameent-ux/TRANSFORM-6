#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

/** Runs the TRANSFORM-6 engine program in the background and keeps track of
    its progress. Everything here is safe to read from the UI thread through
    getSnapshot(). */
class EngineRunner : private juce::Thread
{
public:
    struct Options
    {
        juce::File input;
        juce::File outputFolder;
        juce::String mode       = "auto";     // auto | split | single
        juce::String singleType = "melodic";  // melodic | drums | bass | vocals
        float sensitivity       = 0.5f;
        juce::String quantize   = "off";      // off | 1/8 | 1/16 | 1/32
        double tempo            = 0.0;        // 0 = detect
        juce::String device     = "auto";
    };

    struct Part
    {
        juce::String name;
        juce::File audio, midi;
        int notes = 0;
        bool kept = true;
    };

    struct Snapshot
    {
        bool running = false;
        bool finished = false;
        bool ok = false;
        float progress = 0.0f;
        juce::String status;
        juce::String message;
        double tempo = 0.0;
        bool wasSplit = false;
        juce::Array<Part> parts;
        juce::File combinedMidi;
        juce::File outputFolder;
        juce::StringArray log;
        int generation = 0;   // bumps every time results change
    };

    EngineRunner();
    ~EngineRunner() override;

    /** Finds the engine. Returns an empty File if it can't be found. */
    juce::File findEngine() const;
    void setEnginePath (const juce::File& f);
    juce::File getSavedEnginePath() const;

    bool start (const Options& o);
    void cancel();
    bool isBusy() const { return isThreadRunning(); }

    Snapshot getSnapshot() const;

    /** Restore the last results (e.g. after the DAW project is reopened). */
    void restoreResults (const juce::String& json);
    juce::String getResultsJson() const;

private:
    void run() override;
    void handleLine (const juce::String& line);
    void applyResult (const juce::var& v);

    std::unique_ptr<juce::PropertiesFile> props;
    Options opts;
    mutable juce::CriticalSection lock;
    Snapshot snap;
    juce::String lastResultJson;
    std::unique_ptr<juce::ChildProcess> proc;
    juce::CriticalSection procLock;
};
