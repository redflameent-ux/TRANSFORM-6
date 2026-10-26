#include "EngineRunner.h"

namespace
{
   #if JUCE_WINDOWS
    const char* engineExeName = "transform6-engine.exe";
   #else
    const char* engineExeName = "transform6-engine";
   #endif

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "TRANSFORM-6";
        o.filenameSuffix      = ".settings";
        o.folderName          = "TRANSFORM-6";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }
}

EngineRunner::EngineRunner() : juce::Thread ("TRANSFORM-6 engine")
{
    props = std::make_unique<juce::PropertiesFile> (settingsOptions());
}

EngineRunner::~EngineRunner()
{
    cancel();
}

juce::File EngineRunner::getSavedEnginePath() const
{
    return juce::File (props->getValue ("enginePath"));
}

void EngineRunner::setEnginePath (const juce::File& f)
{
    props->setValue ("enginePath", f.getFullPathName());
    props->saveIfNeeded();
}

juce::File EngineRunner::findEngine() const
{
    juce::Array<juce::File> candidates;

    // 1. Something the user picked with "Find engine..."
    candidates.add (getSavedEnginePath());

    // 2. Environment override (handy while developing)
    auto env = juce::SystemStats::getEnvironmentVariable ("TRANSFORM6_ENGINE", {});
    if (env.isNotEmpty())
        candidates.add (juce::File (env));

    // 3. Where the installer puts it
    auto apps = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    candidates.add (apps.getChildFile ("TRANSFORM-6").getChildFile ("engine").getChildFile (engineExeName));
   #if JUCE_WINDOWS
    auto local = juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}));
    if (local.getFullPathName().isNotEmpty())
        candidates.add (local.getChildFile ("Programs").getChildFile ("TRANSFORM-6")
                             .getChildFile ("engine").getChildFile (engineExeName));
   #endif
   #if JUCE_MAC
    // The macOS package ships one engine per CPU type next to the app in
    // /Applications/TRANSFORM-6. Pick the slice matching how we're running.
    #if defined (__aarch64__)
     const juce::String macArch = "engine-arm64";
    #else
     const juce::String macArch = "engine-x86_64";
    #endif
    const juce::File macBase ("/Applications/TRANSFORM-6");
    candidates.add (macBase.getChildFile (macArch).getChildFile (engineExeName));
    candidates.add (macBase.getChildFile ("engine").getChildFile (engineExeName));
    auto appFolder = juce::File::getSpecialLocation (juce::File::currentApplicationFile).getParentDirectory();
    candidates.add (appFolder.getChildFile (macArch).getChildFile (engineExeName));
    candidates.add (appFolder.getChildFile ("engine").getChildFile (engineExeName));
   #endif

    // 4. Next to the app or plugin itself
    auto here = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    candidates.add (here.getChildFile ("engine").getChildFile (engineExeName));
    candidates.add (here.getParentDirectory().getChildFile ("engine").getChildFile (engineExeName));

    // 5. Home folder (Mac / Linux builds)
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    candidates.add (home.getChildFile ("TRANSFORM-6").getChildFile ("engine").getChildFile (engineExeName));

    for (auto& f : candidates)
        if (f.getFullPathName().isNotEmpty() && f.existsAsFile())
            return f;

    return {};
}

bool EngineRunner::start (const Options& o)
{
    if (isThreadRunning())
        return false;

    opts = o;
    {
        const juce::ScopedLock sl (lock);
        snap = {};
        snap.running = true;
        snap.status = "Starting...";
        snap.generation = juce::Random::getSystemRandom().nextInt();
    }
    startThread (juce::Thread::Priority::normal);
    return true;
}

void EngineRunner::cancel()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (procLock);
        if (proc != nullptr)
            proc->kill();
    }
    stopThread (8000);
}

EngineRunner::Snapshot EngineRunner::getSnapshot() const
{
    const juce::ScopedLock sl (lock);
    return snap;
}

void EngineRunner::run()
{
    auto engine = findEngine();
    if (! engine.existsAsFile())
    {
        const juce::ScopedLock sl (lock);
        snap.running = false;
        snap.finished = true;
        snap.ok = false;
        snap.status = "Engine not found";
        snap.message = "Couldn't find the TRANSFORM-6 engine. Click \"Find engine\" and pick transform6-engine (or reinstall).";
        ++snap.generation;
        return;
    }

    juce::StringArray cmd;
    if (engine.hasFileExtension (".py"))
    {
       #if JUCE_WINDOWS
        cmd.add ("python");
       #else
        cmd.add ("python3");
       #endif
    }
    cmd.add (engine.getFullPathName());
    cmd.addArray ({ "--input", opts.input.getFullPathName(),
                    "--out", opts.outputFolder.getFullPathName(),
                    "--mode", opts.mode,
                    "--single-type", opts.singleType,
                    "--sensitivity", juce::String (opts.sensitivity, 2),
                    "--quantize", opts.quantize,
                    "--tempo", juce::String (opts.tempo, 2),
                    "--device", opts.device });

    opts.outputFolder.createDirectory();

    {
        const juce::ScopedLock sl (procLock);
        proc = std::make_unique<juce::ChildProcess>();
        if (! proc->start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            proc.reset();
            const juce::ScopedLock sl2 (lock);
            snap.running = false;
            snap.finished = true;
            snap.status = "Couldn't start the engine";
            snap.message = "The engine program wouldn't start: " + engine.getFullPathName();
            ++snap.generation;
            return;
        }
    }

    juce::String pending;
    char buffer[4096];

    while (! threadShouldExit())
    {
        int n = 0;
        {
            // readProcessOutput blocks until there's output or the program ends
            n = proc->readProcessOutput (buffer, (int) sizeof (buffer));
        }
        if (n <= 0)
        {
            if (! proc->isRunning())
                break;
            juce::Thread::sleep (20);
            continue;
        }

        pending += juce::String::fromUTF8 (buffer, n);
        int nl;
        while ((nl = pending.indexOfChar ('\n')) >= 0)
        {
            handleLine (pending.substring (0, nl).trimEnd());
            pending = pending.substring (nl + 1);
        }
    }
    if (pending.isNotEmpty())
        handleLine (pending.trim());

    const bool cancelled = threadShouldExit();
    {
        const juce::ScopedLock sl (procLock);
        if (proc != nullptr && proc->isRunning())
            proc->kill();
        proc.reset();
    }

    const juce::ScopedLock sl (lock);
    snap.running = false;
    if (cancelled)
    {
        snap.finished = true;
        snap.ok = false;
        snap.status = "Cancelled";
        snap.message = "Stopped.";
    }
    else if (! snap.finished)
    {
        snap.finished = true;
        snap.ok = false;
        snap.status = "Engine stopped unexpectedly";
        if (snap.message.isEmpty())
            snap.message = "The engine closed without finishing. Check the details for clues.";
    }
    ++snap.generation;
}

void EngineRunner::handleLine (const juce::String& line)
{
    if (line.isEmpty())
        return;

    const juce::ScopedLock sl (lock);

    if (line.startsWith ("PROGRESS "))
    {
        auto rest = line.fromFirstOccurrenceOf ("PROGRESS ", false, false);
        snap.progress = rest.upToFirstOccurrenceOf (" ", false, false).getFloatValue();
        snap.status = rest.fromFirstOccurrenceOf (" ", false, false);
    }
    else if (line.startsWith ("RESULT "))
    {
        auto json = line.fromFirstOccurrenceOf ("RESULT ", false, false);
        auto v = juce::JSON::parse (json);
        lastResultJson = json;
        applyResult (v);
    }
    else if (line.startsWith ("ERROR "))
    {
        snap.message = line.fromFirstOccurrenceOf ("ERROR ", false, false);
        snap.log.add (line);
    }
    else
    {
        snap.log.add (line.startsWith ("LOG ") ? line.substring (4) : line);
        if (snap.log.size() > 400)
            snap.log.removeRange (0, 100);
    }
}

void EngineRunner::applyResult (const juce::var& v)
{
    // lock already held by caller (or taken in restoreResults)
    snap.finished = true;
    snap.ok = (bool) v.getProperty ("ok", false);
    snap.message = v.getProperty ("message", snap.message).toString();
    snap.tempo = (double) v.getProperty ("tempo", 0.0);
    snap.wasSplit = (bool) v.getProperty ("split", false);
    snap.outputFolder = juce::File (v.getProperty ("output_folder", "").toString());
    auto combined = v.getProperty ("combined_midi", juce::var());
    snap.combinedMidi = combined.isString() ? juce::File (combined.toString()) : juce::File();
    snap.parts.clear();

    if (auto* arr = v.getProperty ("parts", juce::var()).getArray())
    {
        for (auto& p : *arr)
        {
            Part part;
            part.name = p.getProperty ("name", "").toString();
            auto a = p.getProperty ("audio", juce::var());
            auto m = p.getProperty ("midi", juce::var());
            part.audio = a.isString() ? juce::File (a.toString()) : juce::File();
            part.midi  = m.isString() ? juce::File (m.toString()) : juce::File();
            part.notes = (int) p.getProperty ("notes", 0);
            part.kept  = (bool) p.getProperty ("kept", true);
            snap.parts.add (part);
        }
    }
    snap.status = snap.ok ? "Done" : "Something went wrong";
    if (snap.ok)
        snap.progress = 1.0f;
    ++snap.generation;
}

void EngineRunner::restoreResults (const juce::String& json)
{
    if (json.isEmpty() || isThreadRunning())
        return;
    auto v = juce::JSON::parse (json);
    if (! v.isObject())
        return;
    const juce::ScopedLock sl (lock);
    lastResultJson = json;
    applyResult (v);
}

juce::String EngineRunner::getResultsJson() const
{
    const juce::ScopedLock sl (lock);
    return lastResultJson;
}
