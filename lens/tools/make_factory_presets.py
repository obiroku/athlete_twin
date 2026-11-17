#!/usr/bin/env python3
"""Writes the factory bank (SPEC §11) to Presets/Factory as .lenspreset JSON.
Values are the SPEC's starting points. Dynamic thresholds ship at -24 dB.
Run from lens/:  python3 tools/make_factory_presets.py
"""
import json
import os
import re

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Presets", "Factory")
LEARN = " Play the track and press Learn."  # LESSONS.md Controls: Learn


def band(shape, freq, gain=0.0, q=1.0, slope="12", dyn=None, det=None):
    b = {"shape": shape, "freq": float(freq), "gain": float(gain), "q": float(q), "slope": slope, "stereo": "Stereo"}
    if dyn:
        mode, rng, att, rel = dyn
        b["dyn"] = {"on": True, "mode": mode, "range": float(rng), "thresh": -24.0, "ratio": 3.0,
                    "attack": float(att), "release": float(rel)}
    else:
        b["dyn"] = {"on": False}
    b["det"] = det or {"source": "Internal", "custom": False}
    return b


def cut(shape, freq, slope):
    return band(shape, freq, slope=str(slope))


def dyn(shape, freq, q, mode, rng, att, rel, det=None):
    return band(shape, freq, 0.0, q, dyn=(mode, rng, att, rel), det=det)


custom = lambda hp, lp, src="Internal": {"source": src, "custom": True, "hp": float(hp), "lp": float(lp)}

BANK = [
    ("Init", "Init", "Bus", "A clean start.", []),
    ("Upright Bass Body", "Bass", "Bass", "Evens out boomy notes and brings out the wood.", [
        cut("LowCut", 35, 12), band("Bell", 90, 2, 1), dyn("Bell", 180, 2.5, "Above", -5, 8, 120),
        band("Bell", 700, 1.5, 1.2), band("HighShelf", 6000, -2, 0.7)]),
    ("Electric Bass Tame", "Bass", "Bass", "Holds back notes that jump out and adds definition.", [
        cut("LowCut", 30, 24), dyn("Bell", 110, 3, "Above", -6, 8, 150), band("Bell", 250, -2, 1.5),
        band("Bell", 900, 2, 1), cut("HighCut", 9000, 12)]),
    ("Gospel Keys Warmth", "Keys", "Keys", "Full, warm keys that don't turn brittle when played hard.", [
        cut("LowCut", 60, 12), band("Bell", 200, 1.5, 0.8), dyn("Bell", 2800, 2, "Above", -4, 3, 80),
        band("HighShelf", 10000, 1, 0.7)]),
    ("Neo-Soul Vocal Air", "Vocal", "Vocal", "Air on top without sharp S's or chest boom.", [
        cut("LowCut", 90, 18), dyn("Bell", 250, 1.5, "Above", -3, 10, 120),
        dyn("Bell", 7000, 4, "Above", -3, 1, 50, custom(5000, 10000)), band("HighShelf", 12000, 3, 0.7)]),
    ("Folk-Gaze Guitar Wash", "Guitar", "Guitar", "Clears room for the vocal and softens harsh strums.", [
        cut("LowCut", 110, 12), band("Bell", 400, -2, 1), dyn("Bell", 3000, 1.5, "Above", -4, 5, 100),
        band("HighShelf", 8000, 2, 0.7)]),
    ("Harshness Tamer", "Bus", "Bus", "Pulls down 2–5 kHz only when it flares up.", [
        dyn("Bell", 3200, 2, "Above", -6, 2, 60)]),
    ("Boomy Room Fix", "Bus", "Bus", "Tames room boom and low-mid buildup.", [
        cut("LowCut", 40, 12), dyn("Bell", 150, 2, "Above", -6, 10, 150), band("Bell", 300, -2, 1.4)]),
    ("Sibilance Cut", "Vocal", "Vocal", "Softens harsh S and T sounds.", [
        dyn("Bell", 6500, 3, "Above", -8, 1, 40, custom(5000, 10000))]),
    ("Sub Clean-Up", "Bus", "Bus", "Removes rumble below the music that eats headroom.", [
        cut("LowCut", 30, 24), dyn("LowShelf", 60, 0.7, "Above", -4, 20, 200)]),
    ("Tape Lead Polish", "Synth", "Synth & Pad", "Lets a lead sit on top without poking out.", [
        cut("LowCut", 150, 12), band("Bell", 1200, -2, 1.5), dyn("Bell", 3500, 2, "Above", -3, 3, 80),
        band("HighShelf", 9000, 1.5, 0.7)]),
    ("Choir Stack Glue", "Vocal", "Choir & Vocal Bus", "Keeps stacked vocals clear when everyone sings at once.", [
        cut("LowCut", 120, 12), dyn("Bell", 400, 1, "Above", -3, 10, 150), dyn("Bell", 3000, 1.5, "Above", -3, 3, 80),
        band("HighShelf", 10000, 1.5, 0.7)]),
    ("Kick and Bass Split", "Bass", "Bass", "Dips the bass's low end only when the kick hits. Set Side Chain to the kick.", [
        dyn("Bell", 60, 1.5, "Above", -5, 1, 120, custom(40, 120, "External"))]),
    ("Mastering Gentle Tilt", "Master", "Master", "A small brightness/warmth balance for a whole mix.", [
        cut("LowCut", 20, 12), band("Tilt", 1000, 1, 1)]),
    ("Mastering Air Lift", "Master", "Master", "Opens the top of a mix without harshness.", [
        band("HighShelf", 12000, 1.5, 0.7), dyn("Bell", 4500, 2, "Above", -2, 5, 100)]),
    ("Demo to Release", "Master", "Master", "A gentle all-round master EQ for home mixes.", [
        cut("LowCut", 25, 12), dyn("LowShelf", 120, 0.7, "Above", -2, 20, 200), band("Bell", 300, -1, 1),
        dyn("Bell", 3000, 1.5, "Above", -2, 5, 100), band("HighShelf", 10000, 1, 0.7)]),
    ("Bass Solo Clarity", "Bass", "Bass", "Helps a bass solo cut through without turning it up.", [
        cut("LowCut", 30, 12), dyn("Bell", 200, 2, "Above", -4, 8, 120), band("Bell", 800, 2.5, 1),
        band("Bell", 2500, 2, 1.2)]),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".lenspreset"):
            os.remove(os.path.join(OUT, f))
    for i, (name, cat, tt, note, bands) in enumerate(BANK):
        has_dyn = any(b["dyn"]["on"] for b in bands)
        preset = {
            "format_version": 1,
            "name": name,
            "category": cat,
            "track_type": tt,
            "note": note + (LEARN if has_dyn else ""),
            "description": "",
            "bands": [dict(slot=k + 1, on=True, **b) for k, b in enumerate(bands)],
            "globals": {},
        }
        slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
        with open(os.path.join(OUT, "f%02d_%s.lenspreset" % (i, slug)), "w", encoding="utf-8") as fh:
            json.dump(preset, fh, ensure_ascii=False, indent=1)
            fh.write("\n")
    print("wrote", len(BANK), "factory presets")


if __name__ == "__main__":
    main()
