"""frank-sdk fleet: the run-time board table (frank_board_desc_t) as data.

The field list, the capability bits and the enum names are read from the C
headers in fleet/include (frank-test's, unchanged), so the YAML, the generated
table and the dumper can never disagree with the struct.

Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / "fleet" / "include"


def _strip_comments(t: str) -> str:
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def pin_fields() -> list[tuple[str, str]]:
    """(type, name) of frank_pins_t, in declaration order"""
    t = _strip_comments((INC / "board_desc.h").read_text())
    body = re.search(r"typedef struct \{(.*?)\} frank_pins_t;", t, re.S).group(1)
    out = []
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        m = re.match(r"(u?int\d+_t)\s+(.*)", decl, re.S)
        for name in m.group(2).split(","):
            out.append((m.group(1), name.strip()))
    return out


def caps() -> dict[str, int]:
    t = (INC / "frank_caps.h").read_text()
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (CAP_\w+)\s+\(1ull <<\s*(\d+)\)", t)}


def enum(name: str, header: str = "frank_caps.h") -> list[str]:
    t = _strip_comments((INC / header).read_text())
    body = re.search(r"typedef enum \{([^{}]*)\}\s*" + name + ";", t).group(1)
    out, val = [], 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        if "=" in item:
            n, v = (x.strip() for x in item.split("="))
            val = int(v, 0)
        else:
            n = item
        out.append((n, val))
        val += 1
    return [n for n, _ in sorted(out, key=lambda x: x[1])]


BOARD_IDS = enum("frank_board_id_t")
MCUS = enum("frank_mcu_class_t")
ROLES = enum("frank_role_t")
VIDEO = enum("frank_video_mode_t", "video_mode.h")
PINSIG = enum("pinsig_t", "board_desc.h")


def dumper_source() -> str:
    """C that prints frank_board_table (and the fallback) as JSON"""
    pins = "\n".join(f'    printf("%s\\"{n}\\": %d", first++ ? ", " : "", (int)p->{n});' for _, n in pin_fields())
    return r'''
#include <stdio.h>
#include <string.h>
#include "board_desc.h"

static void str(const char *s) {
    if (!s) { printf("null"); return; }
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else if (*s == '\n') printf("\\n");
        else if ((unsigned char)*s < 0x20) printf("\\u%04x", *s);
        else putchar(*s);
    }
    putchar('"');
}

static void pins(const frank_pins_t *p) {
    int first = 0;
    printf("{");
''' + pins + r'''
    printf("}");
}

static void desc(const frank_board_desc_t *d) {
    printf("{\"id\": %d, \"name\": ", (int)d->id); str(d->name);
    printf(", \"slug\": "); str(d->slug);
    printf(", \"mcu\": %d, \"role\": %d, \"caps\": \"0x%016llx\"", (int)d->mcu, (int)d->role,
           (unsigned long long)d->caps);
    printf(", \"pico_socket\": %d, \"module_memory\": %d, \"pins\": ", d->pico_socket, d->module_memory);
    pins(&d->pins);
    printf(", \"video_default\": %d, \"i2s_lr_swapped\": %d, \"sig\": [", (int)d->video_default, d->i2s_lr_swapped);
    for (unsigned i = 0; i < d->sig_len; i++)
        printf("%s[%d, %d]", i ? ", " : "", d->sig[i].pin, (int)d->sig[i].expect);
    printf("], \"sig_len\": %d, \"flash_bytes\": %u, \"psram_bytes\": %u, \"manual_note\": ",
           d->sig_len, d->flash_bytes, d->psram_bytes);
    str(d->manual_note);
    printf("}");
}

int main(void) {
    printf("{\"table\": [\n");
    for (unsigned i = 0; i < frank_board_table_len; i++) {
        desc(&frank_board_table[i]);
        printf(i + 1 < frank_board_table_len ? ",\n" : "\n");
    }
    printf("], \"fallback\": ");
    desc(frank_board_fallback());
    printf("}\n");
    return 0;
}
'''


def dump(table_c: str | Path, include_dirs: list[str | Path] = ()) -> dict:
    """Build the dumper against a board table source on the host and run it."""
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "dump.c"
        src.write_text(dumper_source())
        exe = Path(d) / "dump"
        cmd = ["cc", "-std=gnu11", "-w", f"-I{INC}"] + [f"-I{i}" for i in include_dirs] + \
              [str(src), str(table_c), "-o", str(exe)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError("the dumper did not build:\n" + r.stderr[-2000:])
        out = subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout
    return json.loads(out)
