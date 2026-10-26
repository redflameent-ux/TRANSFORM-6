"""Builds a 4-bar test loop at 100 BPM with known notes:
drums (kick/snare/hats), a bass line, chords and a lead melody.
Writes each part separately plus the full mix, and a JSON answer key."""
import json, os, sys
import numpy as np
import soundfile as sf

SR = 44100
BPM = 100.0
BEAT = 60.0 / BPM
BARS = 4
N = int(SR * BEAT * 4 * BARS) + SR
rng = np.random.default_rng(1)


def hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def tone(m, dur, kind="saw", amp=0.3):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = hz(m)
    if kind == "saw":
        y = sum(((-1) ** (k + 1)) / k * np.sin(2 * np.pi * f * k * t) for k in range(1, 12) if f * k < 16000)
    elif kind == "keys":
        y = np.sin(2 * np.pi * f * t) + 0.5 * np.sin(4 * np.pi * f * t) + 0.25 * np.sin(6 * np.pi * f * t)
    else:  # bass
        y = np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)
    env = np.minimum(1, t / 0.01) * np.exp(-t * (1.5 if kind == "keys" else 0.8))
    rel = np.minimum(1, (dur - t) / 0.02)
    return amp * y * env * np.clip(rel, 0, 1)


def add(buf, y, start):
    a = int(start * SR)
    b = min(len(buf), a + len(y))
    buf[a:b] += y[: b - a]


parts = {k: np.zeros(N) for k in ["drums", "bass", "keys", "lead"]}
key = {k: [] for k in parts}

# Drums
for bar in range(BARS):
    for beat in range(4):
        t0 = (bar * 4 + beat) * BEAT
        if beat in (0, 2):
            n = int(0.3 * SR); tt = np.arange(n) / SR
            f = 50 + 100 * np.exp(-tt * 30)
            add(parts["drums"], 0.9 * np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 9), t0)
            key["drums"].append([t0, 36])
        if beat in (1, 3):
            n = int(0.2 * SR); tt = np.arange(n) / SR
            y = 0.5 * rng.standard_normal(n) * np.exp(-tt * 18) + 0.4 * np.sin(2 * np.pi * 190 * tt) * np.exp(-tt * 25)
            add(parts["drums"], y, t0)
            key["drums"].append([t0, 38])
        for half in (0, 0.5):
            n = int(0.05 * SR); tt = np.arange(n) / SR
            y = rng.standard_normal(n)
            y = np.diff(np.concatenate([[0], y]))  # brighten
            add(parts["drums"], 0.25 * y * np.exp(-tt * 70), t0 + half * BEAT)
            key["drums"].append([t0 + half * BEAT, 42])

# Bass: one note per beat, root pattern
roots = [36, 36, 43, 41]  # C2 C2 G2 F2 per bar
for bar in range(BARS):
    for beat in range(4):
        t0 = (bar * 4 + beat) * BEAT
        m = roots[bar] + (12 if beat == 3 else 0)
        add(parts["bass"], tone(m, BEAT * 0.9, "bass", 0.5), t0)
        key["bass"].append([t0, m])

# Keys: whole-bar chords
chords = [[60, 64, 67], [57, 60, 64], [55, 59, 62], [53, 57, 60]]
for bar in range(BARS):
    t0 = bar * 4 * BEAT
    for m in chords[bar]:
        add(parts["keys"], tone(m, 4 * BEAT * 0.95, "keys", 0.15), t0)
        key["keys"].append([t0, m])

# Lead: 8th-note-ish melody
mel = [72, 74, 76, 79, 76, 74, 72, 71]
for bar in range(BARS):
    for i, m in enumerate(mel[::2] if bar % 2 else mel[1::2]):
        t0 = (bar * 4 + i) * BEAT
        add(parts["lead"], tone(m, BEAT * 0.8, "saw", 0.12), t0)
        key["lead"].append([t0, m])

out = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(out, exist_ok=True)
mix = sum(parts.values())
mix /= np.max(np.abs(mix)) * 1.1
sf.write(os.path.join(out, "test_mix.wav"), mix, SR)
for k, v in parts.items():
    sf.write(os.path.join(out, f"test_{k}.wav"), v / (np.max(np.abs(v)) * 1.1), SR)
json.dump({"bpm": BPM, "notes": key}, open(os.path.join(out, "answer_key.json"), "w"), indent=1)
print("written to", out)
