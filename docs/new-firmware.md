# A new firmware

```
frank-sdk/mcp/.venv/bin/frank new ~/src/frank-foo frank-foo NX M2 Z0 --video
cd ~/src/frank-foo
./build.sh NX && ./flash.sh NX
```

(or the `frank-dev` MCP tool `new_firmware`). It creates a git repo with frank-sdk as a
submodule (`--local` links this checkout instead, until frank-sdk is published), and:

- `CMakeLists.txt`: `cmake/board.cmake` picks the board, `frank_firmware()` adds the platform,
  the linker script, the console, the image name `<board>_<name>.uf2`; features add
  libraries (`video` → `frank_video`);
- `src/main.c`: the matching example (`hello` or `video`), ready to grow;
- `build.sh`, `flash.sh`, `release.sh` (every board into `release/`), `version.txt`;
- the no-AI-attribution hooks and CI check, `CLAUDE.md` with the house rules, `.mcp.json`
  with both MCP servers.

Nothing is committed.

## The shape of main()

```c
int main(void) {
    frank_init(FW_NAME, FW_VERSION);     // clocks, PSRAM, heap were set up before main
    frank_console_add("foo", cmd_foo, "what foo does");
    /* drivers: frank_video_init(), ... ; core 1: frank_launch_core1(core1_main) */
    frank_watchdog_start(2000);
    frank_boot_ok();
    for (;;) {
        frank_crumb(STAGE, arg);         // where the loop is, for safe mode
        frank_crumb_tick();
        frank_watchdog_feed();
        frank_console_poll(on_key, on_cmd);
        /* the work */
    }
}
```

Read before writing drivers or game code: [gotchas](gotchas/README.md), and the header
of the API you use (`frank/platform.h`, `frank/video.h`).
