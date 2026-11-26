#!/usr/bin/env python3
#
# frank-sdk — one-off import of frank-test's board table into boards/fleet/*.yaml.
#
# The values come from the compiled table (fleetlib.dump: a host build of
# core/board_table.c, so every PINS_* macro resolves as the compiler sees it);
# the comments come from the source text: the file's preamble, each entry's
# comment block and inline notes, each signature's notes. Nothing is lost:
# gen_boards.py writes the comments back into the generated table.
#
# Usage: tools/fleet/import_fleet.py FRANK_TEST_DIR
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: MIT

import re
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).parent))
import fleetlib as fl  # noqa: E402

OUT = fl.ROOT / "boards" / "fleet"


class Block(str):
    pass


def block_repr(d, s):
    return d.represent_scalar("tag:yaml.org,2002:str", s, style="|")


yaml.add_representer(Block, block_repr)


def scan(text):
    """the source as (kind, start, end) pieces: comment, string, code"""
    i, out = 0, []
    while i < len(text):
        if text.startswith("/*", i):
            j = text.index("*/", i) + 2
            out.append(("comment", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = len(text) if j < 0 else j
            out.append(("comment", i, j))
            i = j
        elif text[i] == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(("string", i, j + 1))
            i = j + 1
        else:
            out.append(("code", i, i + 1))
            i += 1
    return out


def clean(c):
    """a C comment's text without its delimiters and leading stars"""
    if c.startswith("//"):
        return c[2:].strip()
    c = c[2:-2]
    lines = [re.sub(r"^\s*\* ?", "", l) for l in c.split("\n")]
    if lines:
        lines[0] = lines[0].strip()
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return "\n".join(l.rstrip() for l in lines)


def entries(text):
    """(start, end) of each top-level { ... } in frank_board_table[], plus the
    comment texts before and inside each"""
    t0 = text.index("const frank_board_desc_t frank_board_table[] = {")
    t0 = text.index("{", t0 + 40)
    depth, cur, res, last_end = 0, None, [], t0 + 1
    pieces = scan(text)
    comments = [(s, e) for k, s, e in pieces if k == "comment"]
    for k, s, e in pieces:
        if s <= t0 or k != "code":
            continue
        ch = text[s]
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = s
        elif ch == "}":
            if depth == 0:
                break
            depth -= 1
            if depth == 0 and cur is not None:
                before = [clean(text[a:b]) for a, b in comments if last_end <= a < cur
                          and not re.fullmatch(r"-+", clean(text[a:b]))]
                inside = [clean(text[a:b]) for a, b in comments if cur < a < s]
                res.append((cur, s + 1, before, inside, text[cur:s + 1]))
                last_end = s + 1
                cur = None
    return res


def sig_notes(text):
    """sig name -> comment texts before and inside its array"""
    out = {}
    pieces = scan(text)
    comments = [(s, e) for k, s, e in pieces if k == "comment"]
    prev = 0
    for m in re.finditer(r"static const pinsig_entry_t (sig_\w+)\[\] = \{(.*?)\};", text, re.S):
        before = [clean(text[a:b]) for a, b in comments if prev <= a < m.start()
                  and not text[a:b].lstrip("/* -").startswith("---")]
        inside = [clean(text[a:b]) for a, b in comments if m.start() < a < m.end()]
        out[m.group(1)] = (before, inside)
        prev = m.end()
    return out


def preamble(text):
    m = re.match(r"\s*(/\*.*?\*/)", text, re.S)
    return clean(m.group(1)) if m else ""


def caps_list(mask):
    names = fl.caps()
    out = [n for n, b in sorted(names.items(), key=lambda kv: kv[1]) if mask >> b & 1]
    assert sum(1 << names[n] for n in out) == mask, "a capability bit has no name"
    return out


def to_yaml(e, order, notes, signame, sig_comments):
    pins = {}
    for typ, name in fl.pin_fields():
        v = e["pins"][name]
        if (typ == "int8_t" and v != -1) or (typ != "int8_t" and v != 0):
            pins[name] = v
    assert len({p for p, _ in e["sig"]}) == len(e["sig"]), "a signature names a pin twice"
    d = {
        "order": order,
        "id": fl.BOARD_IDS[e["id"]],
        "name": e["name"],
        "slug": e["slug"],
        "mcu": fl.MCUS[e["mcu"]].replace("FRANK_MCU_", "").lower(),
        "role": fl.ROLES[e["role"]].replace("FRANK_ROLE_", "").lower(),
        "target": None,
        "caps": caps_list(int(e["caps"], 16)),
        "pico_socket": bool(e["pico_socket"]),
        "module_memory": bool(e["module_memory"]),
        "pins": pins,
        "video_default": fl.VIDEO[e["video_default"]].replace("VIDEO_", "").lower(),
        "i2s_lr_swapped": bool(e["i2s_lr_swapped"]),
        "signature_name": signame,
        "signature": {p: fl.PINSIG[x].replace("PINSIG_", "").lower() for p, x in e["sig"]},
        "flash_bytes": e["flash_bytes"],
        "psram_bytes": e["psram_bytes"],
        "manual_note": e["manual_note"],
    }
    if notes[0]:
        d["notes"] = Block("\n\n".join(notes[0]))
    if notes[1]:
        d["inline_notes"] = [Block(n) if "\n" in n else n for n in notes[1]]
    if sig_comments and sig_comments[0]:
        d["signature_notes"] = Block("\n\n".join(sig_comments[0]))
    if sig_comments and sig_comments[1]:
        d["signature_inline_notes"] = [Block(n) if "\n" in n else n for n in sig_comments[1]]
    return d


def main():
    ft = Path(sys.argv[1])
    src = ft / "core" / "board_table.c"
    text = src.read_text()
    dumped = fl.dump(src)
    ents = entries(text)
    assert len(ents) == len(dumped["table"]), (len(ents), len(dumped["table"]))
    sigs = sig_notes(text)
    OUT.mkdir(parents=True, exist_ok=True)
    for i, (e, (a, b, before, inside, body)) in enumerate(zip(dumped["table"], ents)):
        m = re.search(r"SIG\((sig_\w+)\)", body)
        signame = m.group(1) if m else None
        d = to_yaml(e, i, (before, inside), signame, sigs.get(signame))
        (OUT / f"{e['slug']}.yaml").write_text(
            f"# {e['name']}: frank-test's run-time descriptor. Imported from frank-test\n"
            f"# core/board_table.c; edit here, gen_boards.py writes the C table.\n"
            + yaml.dump(d, sort_keys=False, allow_unicode=True, width=100))
    # the fallback and the table's own preamble
    fb = dumped["fallback"]
    fbm = re.search(r"/\* -+ \*/\s*/\* Fallback.*?static const frank_board_desc_t fallback_desc", text, re.S)
    fb_comments = [clean(text[s:e]) for k, s, e in scan(fbm.group(0)) if k == "comment"] if fbm else []
    fbsig = re.search(r"fallback_desc = \{.*?SIG\((sig_\w+)\)", text, re.S)
    d = to_yaml(fb, -1, ([c for c in fb_comments if not c.startswith("---")], []),
                fbsig.group(1) if fbsig else None, sigs.get(fbsig.group(1)) if fbsig else None)
    (OUT / "_fallback.yaml").write_text(
        "# The descriptor detection falls back to: deliberately outside the table.\n"
        + yaml.dump(d, sort_keys=False, allow_unicode=True, width=100))
    (fl.ROOT / "fleet" / "notes" / "table_preamble.txt").write_text(preamble(text) + "\n")
    print(f"{len(dumped['table'])} boards + the fallback -> {OUT}")


if __name__ == "__main__":
    main()
