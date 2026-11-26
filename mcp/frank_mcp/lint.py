"""frank-sdk ELF checks: the fleet's known traps, found in the built image.

Each finding names the rule (docs/gotchas/<rule>.md says why).
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

FLASH = (0x10000000, 0x11000000)
PSRAM = (0x11000000, 0x12000000)


def _nm(elf: str) -> list[tuple[int, int, str, str]]:
    r = subprocess.run(["arm-none-eabi-nm", "-S", "--defined-only", elf], capture_output=True, text=True, timeout=60)
    out = []
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) == 4:
            out.append((int(p[0], 16), int(p[1], 16), p[2], p[3]))
        elif len(p) == 3:
            out.append((int(p[0], 16), 0, p[1], p[2]))
    return out


def _in(a, r):
    return r[0] <= a < r[1]


def check(elf: str, irq_roots: list[str] | None = None) -> dict:
    if not Path(elf).is_file():
        return {"ok": False, "findings": [{"rule": "no-elf", "detail": f"{elf} does not exist"}]}
    syms = _nm(elf)
    by = {s[3]: s for s in syms}
    f = []

    def find(rule, detail):
        f.append({"rule": rule, "detail": detail})

    # newlib mem* and the 64-bit divisions must be in RAM (IRQs call them)
    for name in ("memcpy", "memset", "memmove", "__aeabi_memcpy", "__aeabi_memset", "__aeabi_uldivmod", "__aeabi_ldivmod"):
        s = by.get(name)
        if s and _in(s[0], FLASH):
            find("mem-in-flash", f"{name} at {s[0]:#x} runs from flash: an IRQ calling it stalls when core 0 saturates XIP")

    # stack: core 0's at least 32 KB
    lim, top = by.get("__StackLimit"), by.get("__StackTop")
    if lim and top and top[0] - lim[0] < 32 * 1024:
        find("stack-small", f"core 0 stack {top[0] - lim[0]} bytes: under 32 KB, it overflows into core 1's (frank-blood)")

    # SRAM left for the heap
    end, heaplim = by.get("end"), by.get("__HeapLimit")
    if end and heaplim and heaplim[0] - end[0] < 4096:
        find("sram-heap-tight", f"{heaplim[0] - end[0]} bytes of SRAM heap: boot allocations take it and the board hangs before HDMI (frank-dos)")

    # the recovery path linked in
    for name, why in (("frank_reboot", "every software reboot must leave PSRAM QPI mode first"),
                      ("frank_safe_mode_if_hung", "a hang must end in BOOTSEL, not a dead board"),
                      ("frank_boot_guard", "three failed boots must enter BOOTSEL")):
        if name not in by:
            find("no-recovery", f"{name} missing: {why} (link frank_platform, call frank_init)")
    if "reset_usb_boot" in by and "frank_reboot" not in by:
        find("raw-reboot", "reset_usb_boot without frank_reboot: the PSRAM stays in QPI mode and the boot ROM hangs")

    # functions the IRQ roots call, placed in flash
    if irq_roots:
        r = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", elf], capture_output=True, text=True, timeout=120)
        calls, cur = {}, None
        for line in r.stdout.splitlines():
            m = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
            if m:
                cur = m.group(2)
                calls[cur] = set()
                continue
            m = re.search(r"\bbl?x?\s+[0-9a-f]+ <([^+>]+)", line)
            if m and cur:
                calls[cur].add(m.group(1))
        seen, todo = set(), list(irq_roots)
        while todo:
            fn = todo.pop()
            if fn in seen:
                continue
            seen.add(fn)
            todo.extend(calls.get(fn, ()))
        for fn in sorted(seen):
            s = by.get(fn)
            if s and _in(s[0], FLASH):
                find("irq-calls-flash", f"{fn} at {s[0]:#x} (reached from {', '.join(irq_roots)}) is in flash")

    # data the DMA must not touch: buffers handed to DMA should not be in PSRAM;
    # flagged here are PSRAM objects named like DMA buffers
    for a, size, t, name in syms:
        if _in(a, PSRAM) and re.search(r"dma|line_?buf|scanline|i2s_buf|audio_buf", name, re.I):
            find("dma-psram", f"{name} at {a:#x} is in PSRAM: DMA there starves HSTX/PIO video (frank-castle, frank-react)")

    return {"ok": not f, "findings": f}
