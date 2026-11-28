# The bench

How a frank-sdk firmware is flashed, talked to and watched, with nobody at the board.
The `frank` CLI (`mcp/.venv/bin/frank`) and the `frank-dev` MCP server do all of it.

## Setup, once per machine

```
cd frank-sdk/mcp && python3 -m venv .venv && .venv/bin/pip install -e ".[test]"
cp bench.example.yaml ~/.config/frank/bench.yaml     # then put your boards' serials in
.venv/bin/frank status                               # what is connected
```

Boards are found by their USB serial number, never by tty name (those move between
plugs). The serial is the firmware's own (its USB CDC console, and the same in BOOTSEL).

## Flash

`frank flash NA build-na/na_fw.uf2` (or `./flash.sh NX` in a firmware repo):

1. `~BOOT~` on the board's USB CDC console, again every 5 s for 40 s (a firmware still
   booting reads its console late);
2. `picotool load -v -x --ser SERIAL`; a load that does not say OK fails;
3. only if the console never answered: the boot ROM's reboot to BOOTSEL over the debug
   probe (`rp2xxx rom_api_call RB`, nothing written over SWD);
4. it waits for the console to come back.

Never ask anyone to press BOOTSEL. If a load fails verify with noisy reads, see
[gotchas/psram-cs-stuck-noisy-flash](gotchas/psram-cs-stuck-noisy-flash.md).

## Talk

`frank cmd NA clocks` runs `~C clocks~` and returns up to its `OK clocks` line.
`~C help~` lists a firmware's commands. Built in: `clocks`, `mem`, `psram`, `bootinfo`,
`crumb`, `prof`, `core1`. Magic words anywhere in the stream: `~BOOT~`, `~RST~`, `~VERB~`, `~PROF~`.

## Watch

- `frank watch DIR`: two frames 10 s apart: *changing*, *frozen* (identical: a hang) or *black*.
  Look at the frames too.
- `frank audio 1000`: RMS and the dominant frequency. The MS2109 stick's pitch is off
  by several percent: check rates with the firmware's own counters.

## When it hangs

A frank-sdk firmware recovers by itself: the watchdog reboots it, safe mode prints the
crumb and the stall PCs (`arm-none-eabi-addr2line -f -e fw.elf PC`) for 8 s, then it
enters BOOTSEL and `frank flash` works again. Three boots in a row that never reached
`frank_boot_ok()` also end in BOOTSEL.
