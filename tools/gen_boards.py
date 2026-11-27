#!/usr/bin/env python3
#
# frank-sdk — boards/*.yaml -> board headers, docs pages and boards.json.
#
# The YAML is the only place a pin is written down. From it come:
#   boards/gen/frank_<id>.h   the pico board header (PICO_BOARD=frank_<id>),
#                             with every pin as a FRANK_* macro
#   docs/boards/<id>.md       the pin table
#   boards/gen/boards.json    the same data for the MCP servers
#
# Macro names follow the YAML path: video.hdmi.base -> FRANK_VIDEO_HDMI_BASE,
# audio.i2s.data -> FRANK_AUDIO_I2S_DATA. Every section present also gets
# FRANK_HAS_<PATH> 1 (FRANK_HAS_PS2, FRANK_HAS_AUDIO_I2S, FRANK_HAS_LINK...).
#
# Before writing, each board is checked:
#   - every GPIO exists on its package (RP2350A 0-29, RP2350B 0-47);
#   - no GPIO belongs to two functions unless a `shared` group allows it;
#   - each PIO instance's pins fit one 32-pin window (gpio base 0 or 16);
#   - the slave/master pairs agree on the clock.
#
# Usage: tools/gen_boards.py          regenerate
#        tools/gen_boards.py --check  fail if the generated files are stale
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# SPDX-License-Identifier: MIT

import argparse
import glob
import json
import re
import os
import sys

import yaml

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
GPIO_COUNT = {"rp2350a": 30, "rp2350b": 48}
VREG = {1.10: "1_10", 1.15: "1_15", 1.20: "1_20", 1.25: "1_25", 1.30: "1_30",
        1.35: "1_35", 1.40: "1_40", 1.50: "1_50", 1.60: "1_60", 1.65: "1_65",
        1.70: "1_70", 1.80: "1_80", 1.90: "1_90", 2.00: "2_00"}
# keys that are not pins although their value is a number
NOT_PINS = {"inst", "spi", "bytes", "cpu_mhz", "psram_mhz", "flash_mhz",
            "psram_rxdelay", "flash_rxdelay", "vreg"}
# sections that are metadata, not hardware
META = {"id", "name", "covers", "mcu", "role", "pair", "sources", "pio", "shared", "runtime"}


class BoardError(Exception):
    pass


def size_bytes(v):
    if isinstance(v, int):
        return v
    s = str(v).strip().upper()
    mul = {"K": 1 << 10, "M": 1 << 20}.get(s[-1], 1)
    return int(s[:-1] if mul > 1 else s) * mul


def pin_claims(b):
    """function path -> [gpio], from the hardware sections"""
    claims = {}

    def add(path, *pins):
        claims.setdefault(path, []).extend(p for p in pins if p is not None)

    for sec in ("console",):
        u = b.get(sec, {}).get("uart")
        if u:
            add("console.uart", u["tx"], u["rx"])
    ps = b.get("psram", {})
    if "cs" in ps:
        add("psram", ps["cs"])
    for kind, v in b.get("video", {}).items():
        add(f"video.{kind}", *range(v["base"], v["base"] + 8))
    for key in ("sd", "sd_spi_fallback"):
        sd = b.get(key)
        if not sd:
            continue
        if sd.get("mode") == "sdio":
            add(key, sd["cmd"], sd["clk"], *range(sd["dat0"], sd["dat0"] + 4))
        else:
            add(key, sd["clk"], sd["cmd"], sd["dat0"], sd["dat3"])
    for kind, v in b.get("audio", {}).items():
        add(f"audio.{kind}", *[p for k, p in v.items() if isinstance(p, int)])
    for key in ("ps2", "nespad", "led", "i2c"):
        if key in b:
            add(key, *[p for k, p in b[key].items() if isinstance(p, int) and k not in NOT_PINS])
    # gpio: the rest a board wires (buttons, card detect, a co-processor's
    # lines), one claim each so a clash names the pin's job
    for name, p in (b.get("gpio") or {}).items():
        add(f"gpio.{name}", p)
    if "tape_in" in b:
        add("tape_in", b["tape_in"])
    usb = b.get("usb", {})
    if usb.get("pio_usb_dp") is not None:
        add("usb.pio_usb", usb["pio_usb_dp"], usb["pio_usb_dp"] + 1)
    if usb.get("pio_usb_5v_en") is not None:
        add("usb.pio_usb_5v_en", usb["pio_usb_5v_en"])
    lk = b.get("link")
    if lk:
        add("link", *range(lk["a_data"], lk["a_data"] + 10), *range(lk["b_data"], lk["b_data"] + 10),
            lk["fs"], lk["db_out"], lk["db_in"], lk.get("run"))
    return claims


def shared_ok(shared, f1, f2):
    def hit(group, f):
        # "ps2.kbd" names the keyboard half of the ps2 claim
        return any(f == g or f.startswith(g + ".") or g.startswith(f + ".") for g in group)
    return any(hit(g, f1) and hit(g, f2) for g in shared)


def check(b, claims):
    errs = []
    n = GPIO_COUNT.get(b["mcu"])
    if n is None:
        raise BoardError(f"{b['id']}: mcu must be rp2350a or rp2350b")
    owner = {}
    for f, pins in claims.items():
        for p in pins:
            if not 0 <= p < n:
                errs.append(f"{f}: GPIO{p} does not exist on the {b['mcu'].upper()}")
            for other in owner.get(p, []):
                if other != f and not shared_ok(b.get("shared", []), f, other):
                    errs.append(f"GPIO{p} claimed by {other} and {f} (add a `shared` group if intended)")
            owner.setdefault(p, []).append(f)
    # the PSRAM's RP2350A pin is checked against the A package's own map
    ps = b.get("psram", {})
    if "cs_rp2350a" in ps and not 0 <= ps["cs_rp2350a"] < 30:
        errs.append(f"psram.cs_rp2350a: GPIO{ps['cs_rp2350a']} does not exist on the RP2350A")
    if "cs_rp2350b" in ps and not 0 <= ps["cs_rp2350b"] < 48:
        errs.append(f"psram.cs_rp2350b: GPIO{ps['cs_rp2350b']} does not exist on the RP2350B")
    # PIO: every pin of an instance inside one 32-pin window
    bases = {}
    by_pio = {}
    for func, inst in b.get("pio", {}).items():
        if inst not in (0, 1, 2):
            errs.append(f"pio.{func}: instance must be 0, 1 or 2")
            continue
        # "video" takes video.hdmi, "i2s" audio.i2s, "pio_usb" usb.pio_usb
        fpins = [p for f, ps_ in claims.items() if f == func or f.startswith(func + ".")
                 or f.split(".")[-1] == func for p in ps_]
        if not fpins:
            errs.append(f"pio.{func}: no such function on this board")
        by_pio.setdefault(inst, []).extend(fpins)
    for inst, pins in by_pio.items():
        if not pins:
            continue
        lo, hi = min(pins), max(pins)
        if hi < 32:
            bases[inst] = 0
        elif lo >= 16 and hi < 48:
            bases[inst] = 16
        else:
            users = [f for f, i in b["pio"].items() if i == inst]
            errs.append(f"PIO{inst} ({', '.join(users)}): pins GPIO{lo}..GPIO{hi} do not fit one 32-pin window")
    if errs:
        raise BoardError(f"{b['id']}:\n  " + "\n  ".join(errs))
    return bases, owner


def macro(path):
    return "FRANK_" + "_".join(path).upper().replace("-", "_").replace(" ", "_")


def flatten(prefix, v, out):
    if isinstance(v, dict):
        out.append((macro(["has"] + prefix), "1"))
        for k, x in v.items():
            flatten(prefix + [str(k)], x, out)
    elif isinstance(v, bool):
        out.append((macro(prefix), "1" if v else "0"))
    elif isinstance(v, int):
        out.append((macro(prefix), str(v)))
    elif isinstance(v, float):
        out.append((macro(prefix), repr(v)))
    elif isinstance(v, str):
        out.append((macro(prefix), json.dumps(v)))
        out.append((macro(prefix + [v]), "1"))   # e.g. FRANK_VIDEO_HDMI_BACKEND_HSTX 1
    # lists are documentation only


def header(b, bases):
    bid = b["id"].lower()
    g = f"_BOARDS_FRANK_{b['id']}_H"
    L = [
        "// -----------------------------------------------------",
        "// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO",
        "//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES",
        "// -----------------------------------------------------",
        f"// {b['name']}",
        f"// GENERATED by tools/gen_boards.py from boards/{bid}.yaml: do not edit.",
        "// SPDX-License-Identifier: MIT",
        f"#ifndef {g}",
        f"#define {g}",
        "",
        "pico_board_cmake_set(PICO_PLATFORM, rp2350)",
        "",
        f"#define FRANK_TARGET_{b['id']} 1",
        f"#define FRANK_BOARD_ID \"{b['id']}\"",
        f"#define FRANK_BOARD_NAME {json.dumps(b['name'])}",
        f"#define FRANK_TARGET_ROLE_{b['role'].upper()} 1",
        "",
    ]
    if b["mcu"] == "rp2350b":
        L += ["// RP2350B: 48 GPIOs, so PIO needs the movable 32-pin window",
              "#define PICO_RP2350A 0",
              "pico_board_cmake_set_default(PICO_PIO_USE_GPIO_BASE, 1)",
              "#ifndef PICO_PIO_USE_GPIO_BASE", "#define PICO_PIO_USE_GPIO_BASE 1", "#endif"]
    else:
        L += ["#define PICO_RP2350A 1"]
    fl = b.get("flash", {})
    fbytes = size_bytes(fl.get("bytes", "16M"))
    L += ["", "#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1",
          "#ifndef PICO_FLASH_SPI_CLKDIV", "#define PICO_FLASH_SPI_CLKDIV 2", "#endif",
          f"pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, ({fbytes // (1 << 20)} * 1024 * 1024))",
          "#ifndef PICO_FLASH_SIZE_BYTES", f"#define PICO_FLASH_SIZE_BYTES ({fbytes // (1 << 20)} * 1024 * 1024)", "#endif",
          "pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)",
          "#ifndef PICO_RP2350_A2_SUPPORTED", "#define PICO_RP2350_A2_SUPPORTED 1", "#endif", ""]

    def dflt(name, val):
        L.extend([f"#ifndef {name}", f"#define {name} {val}", "#endif"])

    u = b.get("console", {}).get("uart")
    if u:
        L.append("// the console UART (the SDK's default UART)")
        dflt("PICO_DEFAULT_UART", u["inst"])
        dflt("PICO_DEFAULT_UART_TX_PIN", u["tx"])
        dflt("PICO_DEFAULT_UART_RX_PIN", u["rx"])
    sd = b.get("sd", {})
    if sd.get("mode") == "spi":
        dflt("PICO_DEFAULT_SPI", sd["spi"])
        dflt("PICO_DEFAULT_SPI_SCK_PIN", sd["clk"])
        dflt("PICO_DEFAULT_SPI_TX_PIN", sd["cmd"])
        dflt("PICO_DEFAULT_SPI_RX_PIN", sd["dat0"])
        dflt("PICO_DEFAULT_SPI_CSN_PIN", sd["dat3"])
    i2c = b.get("i2c")
    if i2c:
        dflt("PICO_DEFAULT_I2C", i2c["inst"])
        dflt("PICO_DEFAULT_I2C_SDA_PIN", i2c["sda"])
        dflt("PICO_DEFAULT_I2C_SCL_PIN", i2c["scl"])
    if b.get("led", {}).get("plain") is not None:
        dflt("PICO_DEFAULT_LED_PIN", b["led"]["plain"])

    # clocks and the console choice: build options may override them
    c = b["clocks"]
    L += ["", "// clocks (cmake -DFRANK_CPU_MHZ=... etc. override)"]
    dflt("FRANK_CPU_MHZ", c["cpu_mhz"])
    vr = VREG.get(round(float(c["vreg"]), 2))
    if not vr:
        raise BoardError(f"{b['id']}: clocks.vreg {c['vreg']} is not a regulator step")
    dflt("FRANK_VREG", f"VREG_VOLTAGE_{vr}")
    # the PSRAM and flash values are tuned for the board's own clock: at any
    # other (a firmware's -DFRANK_CPU_MHZ) the safe maxima, the delays computed
    L.append(f"#define FRANK_BOARD_CPU_MHZ {c['cpu_mhz']}   // the clock the PSRAM/flash values are tuned for")
    L.append("#if FRANK_CPU_MHZ == FRANK_BOARD_CPU_MHZ")
    dflt("FRANK_PSRAM_MHZ", c["psram_mhz"])
    if "psram_rxdelay" in c:
        dflt("FRANK_PSRAM_RXDELAY", c["psram_rxdelay"])
    dflt("FRANK_FLASH_MHZ", c["flash_mhz"])
    if "flash_rxdelay" in c:
        dflt("FRANK_FLASH_RXDELAY", c["flash_rxdelay"])
    L.append("#else")
    dflt("FRANK_PSRAM_MHZ", min(133, int(c["psram_mhz"])))
    dflt("FRANK_FLASH_MHZ", min(66, int(c["flash_mhz"])))
    L.append("#endif")
    con = b.get("console", {}).get("default", "uart")
    L += ["", "// the console: 1 = USB CDC on the native port, 0 = the UART above (or none)"]
    dflt("FRANK_CONSOLE_USB", 1 if con == "usb" else 0)
    L += ["", "// 1: the firmware finds the board at run time (boards/fleet): nothing board-specific at boot"]
    dflt("FRANK_RUNTIME_BOARD", 1 if b.get("runtime") else 0)

    L += ["", "// pins and capabilities, named after their path in the YAML"]
    out = []
    for k, v in b.items():
        if k in META or k in ("clocks", "flash", "console"):
            continue
        flatten([k], v, out)
    ps = b.get("psram", {})
    out = [(m, v) for m, v in out if not m.startswith("FRANK_PSRAM_BYTES")]
    if ps:
        out.append(("FRANK_PSRAM_BYTES", f"({size_bytes(ps.get('bytes', '8M')) // (1 << 20)} * 1024 * 1024)"))
    for m, v in out:
        L.append(f"#define {m} {v}")
    L += ["", "// PIO instance per driver, and the GPIO base each instance needs"]
    for func, inst in b.get("pio", {}).items():
        L.append(f"#define {macro(['pio', func])} {inst}")
    for inst, base in sorted(bases.items()):
        L.append(f"#define FRANK_PIO{inst}_GPIO_BASE {base}")
    L += ["", f"#endif // {g}", ""]
    return "\n".join(L)


def doc(b, owner, bases):
    L = [f"# {b['name']} — target `{b['id']}`", "",
         f"Generated from [`boards/{b['id'].lower()}.yaml`](../../boards/{b['id'].lower()}.yaml) by `tools/gen_boards.py`. Do not edit.", "",
         f"- **MCU:** {b['mcu'].upper()}, role {b['role']}" + (f", pair `{b['pair']}`" if b.get("pair") else ""),
         f"- **Covers:** {', '.join(b.get('covers', []))}",
         f"- **Flash:** {b.get('flash', {}).get('bytes', '?')} {b.get('flash', {}).get('part', '')}".rstrip(),
         f"- **PSRAM:** {b.get('psram', {}).get('bytes', '?')} on CS GPIO{b.get('psram', {}).get('cs', '?')}"
         + (f" (GPIO{b['psram']['cs_rp2350a']} with an RP2350A)" if "cs_rp2350a" in b.get("psram", {}) else ""),
         ]
    c = b["clocks"]
    L.append(f"- **Clocks:** CPU {c['cpu_mhz']} MHz at {c['vreg']} V, PSRAM ≤ {c['psram_mhz']} MHz"
             + (f" (RXDELAY {c['psram_rxdelay']})" if "psram_rxdelay" in c else "")
             + f", flash ≤ {c['flash_mhz']} MHz" + (f" (RXDELAY {c['flash_rxdelay']})" if "flash_rxdelay" in c else ""))
    con = b.get("console", {})
    u = con.get("uart")
    L.append(f"- **Console:** {'USB CDC' if con.get('default') == 'usb' else 'UART'} by default"
             + (f"; UART{u['inst']} TX GPIO{u['tx']} / RX GPIO{u['rx']}" if u else ""))
    if b.get("video", {}).get("hdmi"):
        h = b["video"]["hdmi"]
        L.append(f"- **HDMI:** {h['backend'].upper()} on GPIO{h['base']}-{h['base'] + 7} (clock pair GPIO{h['clk']})")
    if b.get("pio"):
        L.append("- **PIO:** " + ", ".join(f"{f} → PIO{i}" for f, i in b["pio"].items())
                 + "; " + ", ".join(f"PIO{i} base {base}" for i, base in sorted(bases.items())))
    L += ["", "| GPIO | Function |", "|---:|---|"]
    for p in sorted(owner):
        L.append(f"| {p} | {' / '.join(owner[p])} |")
    if b.get("shared"):
        L += ["", "Shared (a build uses one of each group): " + "; ".join(" vs ".join(g) for g in b["shared"])]
    L += ["", "Sources: " + "; ".join(str(s) for s in b.get("sources", [])), ""]
    return "\n".join(L)


def load_all():
    boards = []
    for path in sorted(glob.glob(os.path.join(ROOT, "boards", "*.yaml"))):
        with open(path) as f:
            b = yaml.safe_load(f)
        if os.path.basename(path)[:-5].upper() != b["id"]:
            raise BoardError(f"{path}: id {b['id']} must match the file name")
        boards.append(b)
    ids = {b["id"]: b for b in boards}
    for b in boards:
        if b.get("pair"):
            p = ids.get(b["pair"])
            if not p:
                raise BoardError(f"{b['id']}: pair {b['pair']} has no board file")
            if p["clocks"]["cpu_mhz"] != b["clocks"]["cpu_mhz"]:
                raise BoardError(f"{b['id']}/{p['id']}: the link needs both halves at one clk_sys")
    return boards


def render():
    files = {}
    index = {}
    boards = load_all()
    # the fleet: frank-test's run-time descriptors, cross-checked against these targets
    sys.path.insert(0, os.path.join(ROOT, "tools", "fleet"))
    import gen_fleet
    fleet_files, fleet_errs = gen_fleet.render({b["id"]: b for b in boards})
    if fleet_errs:
        raise BoardError("the fleet:\n  " + "\n  ".join(fleet_errs))
    files.update(fleet_files)
    for b in boards:
        claims = pin_claims(b)
        bases, owner = check(b, claims)
        bid = b["id"].lower()
        files[f"boards/gen/frank_{bid}.h"] = header(b, bases)
        files[f"docs/boards/{bid}.md"] = doc(b, owner, bases)
        index[b["id"]] = dict(b, gpio={str(p): f for p, f in sorted(owner.items())},
                              pio_gpio_base={str(i): v for i, v in bases.items()})
    files["boards/gen/boards.json"] = json.dumps(index, indent=1, sort_keys=True) + "\n"
    clash_check(files)
    return files


# A board header is included after the fleet's headers in some files and
# before them in others. A macro named like one of the fleet's enum constants
# then silently takes the constant's place: FRANK_ROLE_SINGLE 1 made every
# frank-test settings record default to role 1, master.
def clash_check(files):
    idents = set()
    for h in glob.glob(os.path.join(ROOT, "fleet", "include", "*.h")):
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", open(h).read(), flags=re.S)
        idents |= set(re.findall(r"\b[A-Za-z_]\w*\b", text))
    errs = []
    for rel, text in files.items():
        if rel.endswith(".h"):
            for m in re.findall(r"^#define (\w+)", text, re.M):
                if m in idents and not re.match(r"_BOARDS_", m):
                    errs.append(f"{rel}: #define {m} is a name in fleet/include")
    if errs:
        raise BoardError("macro clashes:\n  " + "\n  ".join(sorted(set(errs))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    try:
        files = render()
    except BoardError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    stale = []
    for rel, text in files.items():
        path = os.path.join(ROOT, rel)
        old = open(path).read() if os.path.exists(path) else None
        if old != text:
            stale.append(rel)
            if not a.check:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write(text)
    if a.check and stale:
        print("stale (run tools/gen_boards.py): " + ", ".join(stale), file=sys.stderr)
        return 1
    print(f"{len(files)} files, {len(stale)} {'stale' if a.check else 'written'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
