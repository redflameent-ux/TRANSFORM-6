"""
TRANSFORM-6 engine
==================
Turns an audio sample into MIDI. If the sample has several instruments in it,
it first splits it into parts (vocals, drums, bass, guitar, piano, other) and
makes one MIDI file per part, plus one combined multi-track MIDI file.

The plugin / app runs this program in the background and reads its output.
Every line it prints starts with one of:
    PROGRESS <0.0-1.0> <message>
    LOG <message>
    ERROR <message>
    RESULT <json>          (last line, describes every file it made)

Usage:
    transform6-engine --input song.wav --out OUTPUT_FOLDER
                       [--mode auto|split|single]
                       [--single-type melodic|drums|bass|vocals]
                       [--sensitivity 0.5] [--quantize off|1/8|1/16|1/32]
                       [--tempo 0 (0 = detect)] [--device auto|cpu|cuda]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import traceback
import warnings
from dataclasses import dataclass, field, asdict

warnings.filterwarnings("ignore")
import logging
logging.getLogger().setLevel(logging.ERROR)
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "3")

import numpy as np

SR = 44100
ENGINE_VERSION = "1.0.0"

# General MIDI programs used for each part so it sounds about right on any
# GM synth before you swap in your own instruments.
GM_PROGRAM = {
    "vocals": 52,   # Choir Aahs
    "bass": 33,     # Electric Bass (finger)
    "guitar": 27,   # Electric Guitar (clean)
    "piano": 0,     # Acoustic Grand
    "other": 89,    # Pad 2 (warm)
    "melodic": 0,
}

# General MIDI drum notes
KICK, SNARE, CLOSED_HAT, OPEN_HAT, CLAP = 36, 38, 42, 46, 39

# Note range limits (MIDI numbers) per part — stops the bass part getting
# stray high notes, vocals getting rumble, and so on.
NOTE_RANGE = {
    "vocals": (40, 88),
    "bass": (24, 64),
    "guitar": (40, 96),
    "piano": (21, 108),
    "other": (24, 108),
    "melodic": (21, 108),
}

MONOPHONIC = {"vocals", "bass"}


# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------
def emit(kind: str, *parts):
    sys.stdout.write(kind + " " + " ".join(str(p) for p in parts) + "\n")
    sys.stdout.flush()


def progress(frac: float, msg: str):
    emit("PROGRESS", f"{max(0.0, min(1.0, frac)):.3f}", msg)


def log(msg: str):
    emit("LOG", msg)


@dataclass
class PartResult:
    name: str
    audio: str | None
    midi: str | None
    notes: int
    loudness_db: float
    kept: bool = True


@dataclass
class EngineResult:
    ok: bool
    input: str
    tempo: float
    split: bool
    parts: list = field(default_factory=list)
    combined_midi: str | None = None
    output_folder: str = ""
    message: str = ""


# --------------------------------------------------------------------------
# Audio loading
# --------------------------------------------------------------------------
def load_audio(path: str) -> np.ndarray:
    """Load any audio file as stereo float32 at 44.1 kHz, shape (2, n)."""
    import librosa

    y, _ = librosa.load(path, sr=SR, mono=False)
    if y.ndim == 1:
        y = np.stack([y, y])
    elif y.shape[0] > 2:
        y = y[:2]
    return y.astype(np.float32)


def rms_db(y: np.ndarray) -> float:
    r = float(np.sqrt(np.mean(np.square(y)) + 1e-12))
    return 20.0 * np.log10(r + 1e-12)


def safe_name(s: str) -> str:
    keep = "".join(c if c.isalnum() or c in " -_()" else "_" for c in s)
    return keep.strip() or "sample"


# --------------------------------------------------------------------------
# Tempo
# --------------------------------------------------------------------------
def detect_tempo(mono: np.ndarray) -> float:
    import librosa

    try:
        tempo, _ = librosa.beat.beat_track(y=mono, sr=SR)
        tempo = float(np.atleast_1d(tempo)[0])
    except Exception:
        tempo = 0.0
    if not np.isfinite(tempo) or tempo <= 0:
        return 120.0
    # Pull into a normal 70-180 range
    while tempo < 70:
        tempo *= 2
    while tempo > 180:
        tempo /= 2
    # Most music sits on a whole-number tempo; snap when we're close.
    if abs(tempo - round(tempo)) < 0.75:
        return float(round(tempo))
    return round(tempo, 2)


# --------------------------------------------------------------------------
# Stem splitting (Demucs, 6 parts)
# --------------------------------------------------------------------------
def models_dir() -> str:
    """Where the splitting model is stored. Bundled builds ship it next to
    the engine; otherwise it downloads once into the user's app data."""
    here = os.path.dirname(os.path.abspath(sys.argv[0]))
    bundled = os.path.join(here, "models")
    if os.path.isdir(bundled) and os.listdir(bundled):
        return bundled
    base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~/.local/share")
    d = os.path.join(base, "TRANSFORM-6", "models")
    os.makedirs(d, exist_ok=True)
    return d


def load_split_model():
    import torch

    if os.environ.get("TRANSFORM6_TEST_UNTRAINED") == "1":
        # Only for testing the plumbing when the real model can't be fetched.
        from demucs.htdemucs import HTDemucs

        log("TEST MODE: using an untrained splitter (output will be junk)")
        m = HTDemucs(sources=["drums", "bass", "other", "vocals", "guitar", "piano"])
        m.eval()
        return m

    from demucs.pretrained import get_model

    mdir = models_dir()
    os.environ["TORCH_HOME"] = mdir
    torch.hub.set_dir(mdir)
    local_yaml = os.path.join(mdir, "htdemucs_6s.yaml")
    if os.path.exists(local_yaml):
        from pathlib import Path
        return get_model("htdemucs_6s", repo=Path(mdir))
    log("Getting the splitting model (one-time download, about 55 MB)...")
    return get_model("htdemucs_6s")


def split_stems(audio: np.ndarray, device: str, on_progress) -> dict[str, np.ndarray]:
    import torch
    from demucs.apply import apply_model

    model = load_split_model()
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    log(f"Splitting on {'graphics card' if device == 'cuda' else 'processor'}")

    wav = torch.from_numpy(audio)
    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    wav = (wav - mean) / std

    # Demucs prints its own progress bar; we report coarse progress instead.
    on_progress(0.0)
    with torch.no_grad():
        out = apply_model(
            model, wav[None], device=device, shifts=1, split=True,
            overlap=0.25, progress=False, num_workers=0,
        )[0]
    on_progress(1.0)
    out = out * std + mean
    return {name: out[i].cpu().numpy() for i, name in enumerate(model.sources)}


# --------------------------------------------------------------------------
# Melodic audio -> notes (Basic Pitch, Spotify's open model)
# --------------------------------------------------------------------------
_BP_MODEL = None


def basic_pitch_model():
    global _BP_MODEL
    if _BP_MODEL is None:
        from basic_pitch.inference import Model
        from basic_pitch import ICASSP_2022_MODEL_PATH

        path = str(ICASSP_2022_MODEL_PATH)
        onnx = os.path.splitext(path)[0] + ".onnx" if not path.endswith(".onnx") else path
        _BP_MODEL = Model(onnx if os.path.exists(onnx) else path)
    return _BP_MODEL


def melodic_to_notes(mono: np.ndarray, part: str, sensitivity: float, tmpdir: str):
    """Returns list of (start_sec, end_sec, pitch, velocity)."""
    import soundfile as sf
    from basic_pitch.inference import predict

    lo, hi = NOTE_RANGE.get(part, NOTE_RANGE["melodic"])
    # Higher sensitivity = lower thresholds = more notes.
    s = float(np.clip(sensitivity, 0.0, 1.0))
    onset_t = 0.75 - 0.5 * s          # 0.75 .. 0.25
    frame_t = 0.45 - 0.3 * s          # 0.45 .. 0.15
    min_len_ms = 120 - 80 * s         # 120 .. 40 ms

    # Normalise so quiet stems still get picked up.
    peak = np.max(np.abs(mono)) + 1e-9
    y = (mono / peak * 0.9).astype(np.float32)
    tmp = os.path.join(tmpdir, f"_bp_{part}.wav")
    sf.write(tmp, y, SR)

    import librosa
    import contextlib
    with contextlib.redirect_stdout(sys.stderr):
      _, _, note_events = predict(
        tmp,
        basic_pitch_model(),
        onset_threshold=onset_t,
        frame_threshold=frame_t,
        minimum_note_length=min_len_ms,
        minimum_frequency=float(librosa.midi_to_hz(lo)),
        maximum_frequency=float(librosa.midi_to_hz(hi)),
        multiple_pitch_bends=False,
        melodia_trick=True,
    )
    try:
        os.remove(tmp)
    except OSError:
        pass

    notes = []
    for ev in note_events:
        start, end, pitch, amp = float(ev[0]), float(ev[1]), int(ev[2]), float(ev[3])
        if pitch < lo or pitch > hi or end <= start:
            continue
        vel = int(np.clip(40 + amp * 100, 30, 127))
        notes.append((start, end, pitch, vel))
    notes.sort()

    notes = split_repeated_notes(notes, y, s)
    if part in MONOPHONIC:
        notes = make_monophonic(notes)
    else:
        notes = remove_overtone_ghosts(notes)
    if part == "bass":
        notes = fix_octave_jumps(notes)
    return notes


def split_repeated_notes(notes, y, sensitivity):
    """The note detector often merges the same note played twice in a row
    into one long note. Use attack detection on the audio to cut them apart."""
    import librosa

    if not notes:
        return notes
    hop = 256
    env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=hop)
    if env.max() <= 0:
        return notes
    env = env / env.max()
    onsets = librosa.onset.onset_detect(
        onset_envelope=env, sr=SR, hop_length=hop, units="time",
        backtrack=False, delta=0.12 - 0.07 * sensitivity, wait=int(0.08 * SR / hop),
    )
    onsets = np.asarray(onsets)
    # Energy at each semitone over time, to check that the note itself
    # re-attacks (so a melody note on top doesn't chop up a held chord).
    fmin = librosa.midi_to_hz(21)
    C = np.abs(librosa.cqt(y, sr=SR, hop_length=hop, fmin=fmin,
                           n_bins=88, bins_per_octave=12))
    fps = SR / hop

    def pitch_energy(p, t0, t1):
        b = int(np.clip(p - 21, 0, 87))
        a, z = int(t0 * fps), max(int(t0 * fps) + 1, int(t1 * fps))
        seg = C[b, a:z]
        return float(seg.mean()) if seg.size else 0.0

    out = []
    for s, e, p, v in notes:
        inside = onsets[(onsets > s + 0.08) & (onsets < e - 0.08)]
        inside = [float(t) for t in inside
                  if pitch_energy(p, t, t + 0.06) > 1.4 * pitch_energy(p, t - 0.08, t - 0.01)]
        if not inside:
            out.append((s, e, p, v))
            continue
        cuts = [s] + inside + [e]
        for a, b in zip(cuts[:-1], cuts[1:]):
            if b - a > 0.04:
                out.append((a, b - 0.005, p, v))
    out.sort()
    return out


def remove_overtone_ghosts(notes):
    """Drop quieter, shorter notes that start together with a lower note an
    octave / octave+fifth / two octaves below — usually just overtones."""
    if len(notes) < 2:
        return notes
    keep = []
    for i, (s, e, p, v) in enumerate(notes):
        ghost = False
        for j, (s2, e2, p2, v2) in enumerate(notes):
            if i == j:
                continue
            if p - p2 in (12, 19, 24, 28, 31) and abs(s - s2) < 0.06 \
                    and (e - s) < 0.8 * (e2 - s2) and v <= v2 + 5:
                ghost = True
                break
        if not ghost:
            keep.append((s, e, p, v))
    return keep


def make_monophonic(notes):
    """Keep one note at a time — the loudest when they overlap."""
    if not notes:
        return notes
    notes = sorted(notes, key=lambda n: (n[0], -n[3]))
    out = []
    for n in notes:
        if out and n[0] < out[-1][1] - 0.02:
            prev = out[-1]
            if n[3] > prev[3] and n[0] - prev[0] > 0.05:
                out[-1] = (prev[0], n[0], prev[2], prev[3])
                out.append(n)
            elif n[3] > prev[3]:
                out[-1] = n
            # else: quieter overlapping note dropped
        else:
            out.append(n)
    return [n for n in out if n[1] - n[0] > 0.03]


def fix_octave_jumps(notes):
    """Bass detectors sometimes jump an octave up on a single note."""
    if len(notes) < 3:
        return notes
    out = list(notes)
    for i in range(1, len(out) - 1):
        p0, p1, p2 = out[i - 1][2], out[i][2], out[i + 1][2]
        if p1 - p0 == 12 and p1 - p2 == 12:
            s, e, _, v = out[i]
            out[i] = (s, e, p1 - 12, v)
    return out


# --------------------------------------------------------------------------
# Drum audio -> kick / snare / hat hits
# --------------------------------------------------------------------------
def drums_to_notes(mono: np.ndarray, sensitivity: float):
    """Find every drum hit, then label it kick / snare / hat (or several at
    once) by where its energy sits and how long it rings."""
    import librosa
    from scipy.signal import butter, sosfiltfilt

    s = float(np.clip(sensitivity, 0.0, 1.0))
    y = (mono / (np.max(np.abs(mono)) + 1e-9)).astype(np.float32)

    def filt(kind, f):
        return sosfiltfilt(butter(4, f, btype=kind, fs=SR, output="sos"), y)

    low = filt("lowpass", 140)
    mid = filt("bandpass", [200, 3500])
    high = filt("highpass", 7000)
    hop = 128

    # Hits are found per band and then merged, so a quiet hat between two
    # loud kicks still gets found.
    times = []
    for sig in (low, mid, high):
        env = librosa.onset.onset_strength(y=sig.astype(np.float32), sr=SR, hop_length=hop,
                                           lag=1, max_size=1)
        if env.max() <= 0:
            continue
        env = env / env.max()
        t = librosa.onset.onset_detect(
            onset_envelope=env, sr=SR, hop_length=hop, units="time", backtrack=True,
            delta=0.20 - 0.14 * s, wait=int(0.04 * SR / hop),
            pre_max=2, post_max=2, pre_avg=8, post_avg=3,
        )
        times.extend(float(x) for x in t)
    times.sort()
    merged = []
    for t in times:
        if merged and t - merged[-1] < 0.03:
            continue
        merged.append(t)

    def rms(sig, a, b):
        a, b = int(a * SR), int(b * SR)
        seg = sig[max(0, a):max(0, b)]
        return float(np.sqrt(np.mean(seg ** 2) + 1e-12)) if seg.size else 0.0

    feats = []
    for t in merged:
        # Compare the hit with the moment just before it, so a ringing
        # kick doesn't count as a new kick under every hat.
        f = {}
        for name, sig in (("low", low), ("mid", mid), ("high", high)):
            now = rms(sig, t, t + 0.04)
            before = rms(sig, t - 0.03, t - 0.003)
            f[name] = max(0.0, now - 1.1 * before)
        f["high_tail"] = rms(high, t + 0.10, t + 0.22)
        f["high_head"] = rms(high, t, t + 0.04) + 1e-9
        f["mid_tail"] = rms(mid, t + 0.06, t + 0.14)
        f["t"] = t
        feats.append(f)
    if not feats:
        return []

    # Each band's loudest hit sets the scale for that band.
    mx = {k: max(1e-9, max(f[k] for f in feats)) for k in ("low", "mid", "high")}
    floor = 0.22 - 0.14 * s   # how loud (vs. the loudest hit) a hit must be

    notes = []
    for f in feats:
        t = f["t"]
        lo, md, hi = f["low"] / mx["low"], f["mid"] / mx["mid"], f["high"] / mx["high"]
        hits = []
        if lo > floor and f["low"] > 0.6 * f["mid"]:
            hits.append((KICK, lo))
        # Snare: strong mid with body that keeps going a little, and not
        # just the click on top of a kick.
        snare_like = md > max(floor, 0.3) and f["mid_tail"] > 0.12 * (f["mid"] + 1e-9)
        if snare_like and not (hits and md < 0.6 and f["mid"] < 1.5 * f["low"]):
            hits.append((SNARE, md))
        # Hat: highs that aren't just the top end of the snare.
        if hi > floor and (not snare_like or f["high"] > 1.6 * f["mid"]):
            open_ = f["high_tail"] > 0.35 * f["high_head"]
            hits.append((OPEN_HAT if open_ else CLOSED_HAT, hi))
        for note, lvl in hits:
            vel = int(np.clip(45 + lvl * 82, 35, 127))
            notes.append((t, t + 0.1, note, vel))
    notes.sort()
    return notes


# --------------------------------------------------------------------------
# MIDI writing
# --------------------------------------------------------------------------
def quantize_notes(notes, tempo: float, grid: str):
    if grid == "off" or not notes:
        return notes
    div = {"1/8": 2, "1/16": 4, "1/32": 8}.get(grid)
    if not div:
        return notes
    step = 60.0 / tempo / div
    out = []
    for s, e, p, v in notes:
        qs = round(s / step) * step
        qe = max(qs + step, round(e / step) * step)
        out.append((qs, qe, p, v))
    return out


def build_instrument(part: str, notes, is_drum: bool):
    import pretty_midi

    inst = pretty_midi.Instrument(
        program=0 if is_drum else GM_PROGRAM.get(part, 0),
        is_drum=is_drum,
        name=part.capitalize(),
    )
    for s, e, p, v in notes:
        inst.notes.append(pretty_midi.Note(velocity=int(v), pitch=int(p),
                                           start=max(0.0, s), end=max(s + 0.01, e)))
    return inst


def write_midi(path: str, instruments, tempo: float):
    import pretty_midi

    pm = pretty_midi.PrettyMIDI(initial_tempo=tempo)
    for inst in instruments:
        pm.instruments.append(inst)
    pm.write(path)


# --------------------------------------------------------------------------
# Main job
# --------------------------------------------------------------------------
def run(args) -> EngineResult:
    import soundfile as sf

    src = os.path.abspath(args.input)
    if not os.path.isfile(src):
        raise FileNotFoundError(f"Can't find the file: {src}")
    base = safe_name(os.path.splitext(os.path.basename(src))[0])
    out_dir = os.path.abspath(os.path.join(args.out, base))
    os.makedirs(out_dir, exist_ok=True)

    progress(0.02, "Loading sample")
    audio = load_audio(src)
    dur = audio.shape[1] / SR
    log(f"Loaded {dur:.1f} seconds")
    mono = audio.mean(0)
    if rms_db(mono) < -70:
        raise ValueError("The sample is silent — nothing to convert.")

    tempo = float(args.tempo) if args.tempo and args.tempo > 0 else detect_tempo(mono)
    log(f"Tempo: {tempo:g} BPM")

    result = EngineResult(ok=True, input=src, tempo=tempo, split=False, output_folder=out_dir)

    do_split = args.mode in ("auto", "split")
    stems: dict[str, np.ndarray]
    if do_split:
        progress(0.06, "Splitting into parts (this is the slow bit)")
        stems = split_stems(audio, args.device,
                            lambda f: progress(0.06 + 0.54 * f, "Splitting into parts"))
        result.split = True
    else:
        stems = {args.single_type: audio}

    # Decide which parts are really there. Relative to the loudest part, a
    # part more than ~30 dB down is just leftover noise from the splitter.
    levels = {k: rms_db(v.mean(0)) for k, v in stems.items()}
    loudest = max(levels.values())
    min_rel = -24 - 16 * float(args.sensitivity)   # -24 .. -40 dB
    present = {k for k, lv in levels.items() if lv > loudest + min_rel and lv > -60}

    if args.mode == "auto" and do_split and len(present) == 1:
        only = next(iter(present))
        log(f"Sample sounds like a single part ({only}); using the original audio for best detail")
        stems = {only: audio}
        levels = {only: rms_db(mono)}
        present = {only}
        result.split = False

    order = ["vocals", "drums", "bass", "guitar", "piano", "other", "melodic"]
    names = sorted(stems.keys(), key=lambda n: order.index(n) if n in order else 99)
    tmpdir = out_dir
    instruments = []
    n = len(names)
    for i, name in enumerate(names):
        frac0 = 0.62 + 0.34 * i / max(1, n)
        y = stems[name]
        kept = name in present
        audio_path = None
        if result.split:
            audio_path = os.path.join(out_dir, f"{base} - {name}.wav")
            sf.write(audio_path, y.T, SR, subtype="PCM_24")
        if not kept:
            result.parts.append(asdict(PartResult(name, audio_path, None, 0, round(levels[name], 1), False)))
            log(f"{name}: too quiet, skipped")
            continue

        progress(frac0, f"Finding notes: {name}")
        m = y.mean(0)
        is_drum = name == "drums"
        if is_drum:
            notes = drums_to_notes(m, args.sensitivity)
        else:
            notes = melodic_to_notes(m, name, args.sensitivity, tmpdir)
        notes = quantize_notes(notes, tempo, args.quantize)

        midi_path = None
        if notes:
            inst = build_instrument(name, notes, is_drum)
            midi_path = os.path.join(out_dir, f"{base} - {name}.mid")
            write_midi(midi_path, [inst], tempo)
            instruments.append(build_instrument(name, notes, is_drum))
        log(f"{name}: {len(notes)} notes")
        result.parts.append(asdict(PartResult(name, audio_path, midi_path, len(notes),
                                              round(levels[name], 1), True)))

    if len(instruments) > 1:
        progress(0.97, "Writing combined MIDI")
        combined = os.path.join(out_dir, f"{base} - all parts.mid")
        write_midi(combined, instruments, tempo)
        result.combined_midi = combined
    elif len(instruments) == 1:
        result.combined_midi = next((p["midi"] for p in result.parts if p["midi"]), None)

    total = sum(p["notes"] for p in result.parts)
    if total == 0:
        result.message = "No notes found. Try turning sensitivity up."
    else:
        result.message = f"Done: {total} notes across {len(instruments)} part(s)"
    progress(1.0, result.message)
    return result


def main(argv=None):
    p = argparse.ArgumentParser(description="TRANSFORM-6 engine")
    p.add_argument("--input", "-i")
    p.add_argument("--out", "-o", default=None)
    p.add_argument("--mode", choices=["auto", "split", "single"], default="auto")
    p.add_argument("--single-type", choices=["melodic", "drums", "bass", "vocals"], default="melodic")
    p.add_argument("--sensitivity", type=float, default=0.5)
    p.add_argument("--quantize", choices=["off", "1/8", "1/16", "1/32"], default="off")
    p.add_argument("--tempo", type=float, default=0.0)
    p.add_argument("--device", choices=["auto", "cpu", "cuda"], default="auto")
    p.add_argument("--version", action="store_true")
    p.add_argument("--warmup", action="store_true",
                   help="Download/load models now so the first real run is fast")
    args = p.parse_args(argv)
    # Windows: make sure names like "brØKen" come through intact.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass

    if args.version:
        emit("RESULT", json.dumps({"ok": True, "version": ENGINE_VERSION}))
        return 0
    if args.warmup:
        try:
            progress(0.1, "Preparing note detector")
            basic_pitch_model()
            progress(0.4, "Preparing splitter")
            load_split_model()
            progress(1.0, "Ready")
            emit("RESULT", json.dumps({"ok": True, "message": "Ready"}))
            return 0
        except Exception as e:
            emit("ERROR", str(e))
            emit("RESULT", json.dumps({"ok": False, "message": str(e)}))
            return 1
    if not args.input:
        p.error("--input is required")
    if args.out is None:
        args.out = os.path.dirname(os.path.abspath(args.input))

    try:
        res = run(args)
        emit("RESULT", json.dumps(asdict(res)))
        return 0
    except Exception as e:
        emit("ERROR", str(e))
        for line in traceback.format_exc().splitlines():
            log(line)
        emit("RESULT", json.dumps({"ok": False, "message": str(e)}))
        return 1


if __name__ == "__main__":
    sys.exit(main())
