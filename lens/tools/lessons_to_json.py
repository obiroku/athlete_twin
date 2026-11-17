#!/usr/bin/env python3
"""Convert LESSONS.md (the only source of Coach text) into JSON files in
Resources/coach. Every ID is kept. Run from the lens/ folder:

    python3 tools/lessons_to_json.py

The build embeds the JSON with BinaryData; a Catch2 test checks that the
JSON and LESSONS.md contain the same IDs.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "LESSONS.md")
OUT = os.path.join(ROOT, "Resources", "coach")


def sections(text):
    """Split on '## N. Title' headings."""
    parts = re.split(r"^## (\d+)\. (.+)$", text, flags=re.M)
    out = {}
    for i in range(1, len(parts), 3):
        out[int(parts[i])] = parts[i + 2]
    return out


def quoted(s):
    return re.findall(r'"([^"]*)"', s)


def table_rows(body):
    rows = []
    for line in body.splitlines():
        line = line.strip()
        if not line.startswith("|") or set(line) <= set("|-: "):
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        rows.append(cells)
    return rows[1:]  # drop header


def explain(body):
    e = {"width": [], "templates": {}}
    for line in body.splitlines():
        m = re.match(r"^- (Q < 0\.7|0\.7 ≤ Q < 2|2 ≤ Q < 6|Q ≥ 6): \"(.+)\"$", line)
        if m:
            bounds = {"Q < 0.7": [0, 0.7], "0.7 ≤ Q < 2": [0.7, 2], "2 ≤ Q < 6": [2, 6], "Q ≥ 6": [6, 1e9]}[m.group(1)]
            e["width"].append({"min": bounds[0], "max": bounds[1], "text": m.group(2)})
            continue
        m = re.match(r"^- ([a-z_]+): \"(.+)\"$", line)
        if m:
            e["templates"][m.group(1)] = m.group(2)
    # Special lines with several quoted parts.
    for line in body.splitlines():
        if line.startswith("- stereo suffix"):
            e["templates"]["stereo_suffix"] = quoted(line)[0]
        elif line.startswith("- {detector}:"):
            q = quoted(line)
            e["templates"]["detector_internal"] = q[0]
            e["templates"]["detector_custom"] = q[1]
            e["templates"]["detector_external"] = q[2]
        elif line.startswith("- shelf dynamic:"):
            q = quoted(line)
            e["templates"]["shelf_dynamic_replace"] = q[0]
            e["templates"]["shelf_dynamic_low"] = q[1]
            e["templates"]["shelf_dynamic_high"] = q[2]
        elif line.startswith("- activity suffix:"):
            q = quoted(line)
            e["templates"]["activity_none"] = q[0]
            e["templates"]["activity_always"] = q[1]
    return e


def zones(body):
    out = {}
    for name, spec in table_rows(body):
        zl = []
        for part in spec.split(" · "):
            m = re.match(r"^(.+?) (\d+(?:\.\d+)?k?)(?:–(\d+(?:\.\d+)?k?)|(\+))$", part.strip())
            if not m:
                sys.exit("bad zone: " + part)

            def hz(s):
                return float(s[:-1]) * 1000 if s.endswith("k") else float(s)

            lo = m.group(2)
            hi = m.group(3)
            # "1.5–4k": the k applies to both ends.
            if hi and hi.endswith("k") and not lo.endswith("k"):
                lo_hz = hz(lo + "k") if float(lo) < 100 else hz(lo)
            else:
                lo_hz = hz(lo)
            zl.append({"name": m.group(1), "lo": lo_hz, "hi": hz(hi) if hi else None})
        if name.startswith("Default"):
            for t in ("Default", "Bus", "Synth & Pad"):
                out[t] = zl
        else:
            out[name] = zl
    return out


def steps(body):
    out = {}
    for line in body.splitlines():
        m = re.match(r"^- (step_[a-z]+): \"(.+)\"$", line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def tips(body):
    out = []
    for cells in table_rows(body):
        # Tip text may contain {a|b} choices, which split the table cell.
        tid, when, text = cells[0], cells[1], "|".join(cells[2:])
        out.append({"id": tid, "when": when, "text": text.strip('"')})
    return out


def recipes(body):
    out = []
    for block in re.split(r"^### ", body, flags=re.M)[1:]:
        head, *lines = block.splitlines()
        m = re.match(r"^(rec_[a-z_]+) — (.+?) \[(.+)\]$", head.strip())
        if not m:
            sys.exit("bad recipe heading: " + head)
        rec = {"id": m.group(1), "title": m.group(2), "trackTypes": [t.strip() for t in m.group(3).split(",")], "steps": []}
        for line in lines:
            sm = re.match(r'^\d+\. (?:\[([^\]]+)\] )?"(.+)"(?: \((.+)\))?$', line.strip())
            if sm:
                rec["steps"].append({"highlight": sm.group(1) or "", "text": sm.group(2), "check": sm.group(3) or ""})
        out.append(rec)
    return out


def lessons(body):
    out = []
    for block in re.split(r"^### ", body, flags=re.M)[1:]:
        head, *lines = block.splitlines()
        m = re.match(r"^(les_[a-z_]+) — (.+)$", head.strip())
        if not m:
            sys.exit("bad lesson heading: " + head)
        text = "\n".join(lines).strip()
        tryit = ""
        tm = re.search(r"\n?Try it: (.+)$", text)
        if tm:
            tryit = tm.group(1).strip()
            text = text[: tm.start()].strip()
        # Join wrapped lines; keep list items on their own lines.
        paras = []
        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            if re.match(r"^(\d+\.|-) ", line) or not paras:
                paras.append(line)
            elif re.match(r"^(\d+\.|-) ", paras[-1]) and line[:1].isupper():
                # A capitalised line after a list item starts a new paragraph;
                # lowercase lines continue a wrapped item.
                paras.append(line)
            else:
                paras[-1] += " " + line
        out.append({"id": m.group(1), "title": m.group(2), "text": "\n".join(paras), "tryIt": tryit})
    return out


def ear(body):
    rows = table_rows(body)
    levels = []
    for lvl, band, choices, pas in rows:
        levels.append({"level": int(lvl), "band": band, "choices": choices, "pass": pas})
    msgs = {}
    for line in body.splitlines():
        if line.startswith("- Instructions:"):
            msgs["instructions"] = quoted(line)[0]
        elif line.startswith("- Right:"):
            q = quoted(line)
            msgs["right"] = q[0]
            msgs["wrong"] = q[1]
        elif line.startswith("- Level up:"):
            msgs["level_up"] = quoted(line)[0]
    return {"levels": levels, "messages": msgs}


def glossary(body):
    return [{"term": t, "definition": d} for t, d in table_rows(body)]


def controls(body):
    return {c: t.strip('"') for c, t in table_rows(body)}


def main():
    text = open(SRC, encoding="utf-8").read()
    s = sections(text)
    data = {
        "explain": explain(s[1]),
        "zones": zones(s[2]),
        "steps": steps(s[3]),
        "tips": tips(s[4]),
        "recipes": recipes(s[5]),
        "lessons": lessons(s[6]),
        "eartrainer": ear(s[7]),
        "glossary": glossary(s[8]),
        "controls": controls(s[9]),
    }
    os.makedirs(OUT, exist_ok=True)
    for name, value in data.items():
        with open(os.path.join(OUT, name + ".json"), "w", encoding="utf-8") as f:
            json.dump(value, f, ensure_ascii=False, indent=1)
            f.write("\n")
    print("tips", len(data["tips"]), "recipes", len(data["recipes"]), "lessons", len(data["lessons"]),
          "glossary", len(data["glossary"]), "controls", len(data["controls"]), "templates", len(data["explain"]["templates"]))


if __name__ == "__main__":
    main()
