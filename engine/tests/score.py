"""Compare a MIDI file against the answer key: how many expected notes were
found (within 60 ms and same pitch) and how many extra notes appeared."""
import json, sys
import pretty_midi

midi, key_path, part = sys.argv[1], sys.argv[2], sys.argv[3]
key = json.load(open(key_path))["notes"][part]
pm = pretty_midi.PrettyMIDI(midi)
got = [(n.start, n.pitch) for i in pm.instruments for n in i.notes]
used = set()
hit = 0
for t, p in key:
    for j, (gt, gp) in enumerate(got):
        if j not in used and gp == p and abs(gt - t) < 0.06:
            used.add(j); hit += 1; break
extra = len(got) - len(used)
print(f"{part}: found {hit}/{len(key)} expected notes ({100*hit/len(key):.0f}%), {extra} extra; tempo {pm.estimate_tempo() if len(got)>2 else 0:.0f}, file tempo {pm.get_tempo_changes()[1][0]:.1f}")
