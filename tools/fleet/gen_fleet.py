"""frank-sdk fleet: boards/fleet/*.yaml -> boards/gen/frank_fleet.c, docs/fleet.md,
and the cross-check against the build targets (boards/*.yaml).

Every pin a fleet board shares with its target (`target:`) must be the same pin,
or be listed under `target_differences:` with the reason. That is the one place
the run-time descriptors (frank-test) and the build-time targets meet.

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import json
from pathlib import Path

import yaml

import fleetlib as fl

FLEET = fl.ROOT / "boards" / "fleet"


class FleetError(Exception):
    pass


def load() -> tuple[list[dict], dict | None]:
    boards, fallback = [], None
    for p in sorted(FLEET.glob("*.yaml")):
        d = yaml.safe_load(p.read_text())
        d["_file"] = p.name
        if p.name == "_fallback.yaml":
            fallback = d
        else:
            boards.append(d)
    boards.sort(key=lambda d: d["order"])
    orders = [d["order"] for d in boards]
    if orders != list(range(len(boards))):
        raise FleetError(f"fleet: `order` must run 0..{len(boards) - 1} without gaps, have {orders}")
    return boards, fallback


GPIO_LIMIT = {"rp2350a": 30, "rp2350b": 48, "any": 48}


def validate(d: dict) -> list[str]:
    errs = []
    fields = dict((n, t) for t, n in fl.pin_fields())
    caps = fl.caps()
    lim = GPIO_LIMIT[d["mcu"]]
    for k, v in d.get("pins", {}).items():
        if k not in fields:
            errs.append(f"pins.{k}: no such field in frank_pins_t")
        elif fields[k] == "int8_t" and not 0 <= v < lim and not k.endswith(("_len",)):
            errs.append(f"pins.{k}: GPIO{v} does not exist on {d['mcu']}")
    for c in d.get("caps", []):
        if c not in caps:
            errs.append(f"caps: unknown {c}")
    for p, lvl in (d.get("signature") or {}).items():
        if not 0 <= int(p) < lim:
            errs.append(f"signature: GPIO{p} does not exist on {d['mcu']}")
        if f"PINSIG_{lvl.upper()}" not in fl.PINSIG:
            errs.append(f"signature: GPIO{p} level {lvl}")
    if f"FRANK_BOARD_{d['id'].replace('FRANK_BOARD_', '')}" not in fl.BOARD_IDS:
        errs.append(f"id {d['id']} is not in frank_board_id_t")
    return [f"{d['_file']}: {e}" for e in errs]


# target (boards/*.yaml) path -> the frank_pins_t field it must equal
def target_pins(t: dict, fleet_mcu: str) -> dict[str, int]:
    out = {}

    def put(field, v):
        if v is not None:
            out[field] = v
    u = t.get("console", {}).get("uart")
    if u:
        put("uart_tx", u["tx"])
        put("uart_rx", u["rx"])
    sd = t.get("sd")
    if sd:
        put("sd_clk", sd["clk"])
        put("sd_cmd", sd["cmd"])
        put("sd_dat0", sd["dat0"])
        if sd.get("mode") == "sdio":
            put("sd_dat1", sd["dat0"] + 1)
            put("sd_dat2", sd["dat0"] + 2)
            put("sd_cs", sd["dat0"] + 3)
        else:
            put("sd_cs", sd["dat3"])
    v = t.get("video", {})
    base = (v.get("hdmi") or v.get("vga") or v.get("composite") or {}).get("base")
    put("video_base", base)
    a = t.get("audio", {})
    if a.get("i2s"):
        put("i2s_data", a["i2s"]["data"])
        put("i2s_clk_base", a["i2s"]["bclk"])
        put("i2s_mclk", a["i2s"].get("mclk"))
    if a.get("pwm"):
        put("pwm_l", a["pwm"].get("left"))
        put("pwm_r", a["pwm"].get("right"))
    ps = t.get("psram", {})
    put("psram_cs", ps.get("cs_rp2350a") if fleet_mcu == "rp2350a" and "cs_rp2350a" in ps else ps.get("cs"))
    p2 = t.get("ps2", {})
    for a_, b_ in (("kbd_clk", "ps2_kb_clk"), ("kbd_dat", "ps2_kb_dat"), ("mouse_clk", "ps2_ms_clk"), ("mouse_dat", "ps2_ms_dat")):
        put(b_, p2.get(a_))
    n = t.get("nespad", {})
    for a_, b_ in (("clk", "pad_clk"), ("latch", "pad_latch"), ("data", "pad_d1"), ("data2", "pad_d2")):
        put(b_, n.get(a_))
    led = t.get("led", {})
    put("led_plain", led.get("plain"))
    put("led_ws2812", led.get("ws2812"))
    i2c = t.get("i2c", {})
    put("i2c_sda", i2c.get("sda"))
    put("i2c_scl", i2c.get("scl"))
    put("pio_usb_dp", t.get("usb", {}).get("pio_usb_dp"))
    put("tape_in", t.get("tape_in"))
    lk = t.get("link", {})
    for a_, b_ in (("a_data", "link_a_data"), ("b_data", "link_b_data"), ("fs", "link_fs"),
                   ("db_out", "link_db_out"), ("db_in", "link_db_in"), ("run", "link_run")):
        put(b_, lk.get(a_))
    return out


def cross_check(d: dict, targets: dict[str, dict]) -> list[str]:
    tid = d.get("target")
    if not tid:
        return []
    t = targets.get(tid)
    if t is None:
        return [f"{d['_file']}: target {tid} has no boards/{tid.lower()}.yaml"]
    want = target_pins(t, d["mcu"])
    have = d.get("pins", {})
    allowed = d.get("target_differences") or {}
    errs = []
    fields = {n for _, n in fl.pin_fields()}
    for f in sorted(fields):
        w, h = want.get(f), have.get(f)
        if f not in want:
            continue   # the target does not describe it: nothing to compare
        if w != h and f not in allowed:
            errs.append(f"{d['_file']}: {f} is {'NC' if h is None else h} here, {w} in boards/{tid.lower()}.yaml")
    for f in allowed:
        if f in want and want.get(f) == have.get(f):
            errs.append(f"{d['_file']}: target_differences.{f} is listed but the pins agree: remove it")
    return errs


# ---------------------------------------------------------------- C

def c_str(s):
    return "NULL" if s is None else json.dumps(s, ensure_ascii=False)


def c_comment(text, indent=""):
    lines = text.replace("*/", "* /").replace("/*", "/ *").split("\n")
    return "\n".join([indent + "/* " + lines[0]] + [indent + " * " + l if l else indent + " *" for l in lines[1:]]) + " */"


def sig_array(name, sig, notes, inline):
    L = []
    if notes:
        L.append(c_comment(notes))
    L.append(f"static const pinsig_entry_t {name}[] = {{")
    for n in inline or []:
        L.append(c_comment(n, "    "))
    for p, lvl in sig.items():
        L.append(f"    {{ {int(p)}, PINSIG_{lvl.upper()} }},")
    L.append("};")
    return "\n".join(L)


def entry(d, static_name=None):
    pins = d.get("pins", {})
    L = []
    if d.get("notes") and not static_name:
        L.append(c_comment(d["notes"]))
    L.append(f"static const frank_board_desc_t {static_name} = {{" if static_name else "{")
    if d.get("notes") and static_name:
        L.insert(0, c_comment(d["notes"]))
    for n in d.get("inline_notes") or []:
        L.append(c_comment(n, "    "))
    caps = " | ".join(d.get("caps") or []) or "CAP_NONE"
    L.append(f"    .id = {d['id']}, .name = {c_str(d['name'])}, .slug = {c_str(d['slug'])},")
    L.append(f"    .mcu = FRANK_MCU_{d['mcu'].upper()}, .role = FRANK_ROLE_{d['role'].upper()},")
    L.append(f"    .caps = {caps},")
    L.append(f"    .pico_socket = {str(d['pico_socket']).lower()}, .module_memory = {str(d['module_memory']).lower()},")
    pl = []
    for typ, n in fl.pin_fields():
        v = pins.get(n, -1 if typ == "int8_t" else 0)
        pl.append(f".{n} = {'PIN_NC' if v == -1 else v}")
    L.append("    .pins = {")
    for i in range(0, len(pl), 4):
        L.append("        " + ", ".join(pl[i:i + 4]) + ",")
    L.append("    },")
    L.append(f"    .video_default = VIDEO_{d['video_default'].upper()}, .i2s_lr_swapped = {str(d['i2s_lr_swapped']).lower()},")
    if d.get("signature_name"):
        L.append(f"    .sig = {d['signature_name']}, .sig_len = {len(d['signature'])},")
    else:
        L.append("    .sig = NULL, .sig_len = 0,")
    L.append(f"    .flash_bytes = {d['flash_bytes']}u, .psram_bytes = {d['psram_bytes']}u,")
    L.append(f"    .manual_note = {c_str(d.get('manual_note'))},")
    L.append("};" if static_name else "},")
    return "\n".join(L)


def render_c(boards, fallback) -> str:
    pre = (fl.ROOT / "fleet" / "notes" / "table_preamble.txt").read_text().rstrip("\n")
    L = ["/*", " * GENERATED by tools/gen_boards.py from the YAML in boards/fleet: do not edit.",
         " * The run-time board table (frank_board_desc_t), frank-test's core/board_table.c",
         " * before it moved here. The notes below are that file's, kept with their entries.",
         " * SPDX-License-Identifier: MIT", " */", "", c_comment(pre), "",
         '#include "board_desc.h"', "", "#include <stddef.h>", ""]
    seen = set()
    for d in boards + ([fallback] if fallback else []):
        n = d.get("signature_name")
        if n and n not in seen:
            seen.add(n)
            L += [sig_array(n, d["signature"], d.get("signature_notes"), d.get("signature_inline_notes")), ""]
    L += ["const frank_board_desc_t frank_board_table[] = {", ""]
    for d in boards:
        L += [entry(d), ""]
    L += ["};", "", "const unsigned frank_board_table_len =",
          "    sizeof(frank_board_table) / sizeof(frank_board_table[0]);", ""]
    if fallback:
        L += [entry(fallback, "fallback_desc"), "",
              "const frank_board_desc_t *frank_board_fallback(void) { return &fallback_desc; }", ""]
    return "\n".join(L)


def render_doc(boards) -> str:
    L = ["# The fleet: frank-test's run-time descriptors", "",
         "Generated from [`boards/fleet/`](../boards/fleet) by `tools/gen_boards.py`. Do not edit.",
         "Each descriptor is what frank-test's detection can identify; `target` is the",
         "frank-sdk build target it runs as, cross-checked pin for pin.", "",
         "| # | Board | slug | MCU | role | target | video | signature pins |", "|---:|---|---|---|---|---|---|---:|"]
    for d in boards:
        L.append(f"| {d['order']} | {d['name']} | `{d['slug']}` | {d['mcu']} | {d['role']} | "
                 f"{('`' + d['target'] + '`') if d.get('target') else '—'} | {d['video_default']} | {len(d.get('signature') or {})} |")
    L.append("")
    return "\n".join(L)


def render(targets: dict[str, dict]) -> tuple[dict[str, str], list[str]]:
    boards, fallback = load()
    errs = []
    for d in boards + ([fallback] if fallback else []):
        errs += validate(d)
    for d in boards:
        errs += cross_check(d, targets)
    clean = [{k: v for k, v in d.items() if not k.startswith("_")} for d in boards]
    files = {
        "boards/gen/frank_fleet.c": render_c(boards, fallback),
        "boards/gen/fleet.json": json.dumps(clean, indent=1, ensure_ascii=False) + "\n",
        "docs/fleet.md": render_doc(boards),
    }
    return files, errs
