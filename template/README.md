# {{NAME}}

{{DESCRIPTION}}

Boards: {{BOARDS}}. Built on [frank-sdk](https://github.com/rh1tech/frank-sdk).

## Build

```
git clone --recursive https://github.com/rh1tech/{{NAME}}
./build.sh {{BOARD0}}          # build-<board>/<board>_{{NAME}}.uf2
./flash.sh {{BOARD0}}          # or copy the .uf2 to the board in BOOTSEL mode
```

## Licence

{{LICENSE}}. Copyright (c) {{YEAR}} Mikhail Matveev <xtreme@rh1.tech>.
