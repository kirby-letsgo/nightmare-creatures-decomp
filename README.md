# Nightmare Creatures: PC port

A native macOS / Windows / Linux port of Nightmare Creatures (PS1, NTSC-U `SLUS-005.82`).
The game code is statically recompiled from the original executables and then gradually
replaced with hand-decompiled C.

Planned additions over the original:

- Adrenaline system can be turned off
- Modern camera-relative analog controls, as an alternative to tank controls
- Master / music / SFX volume
- Internal-resolution upscaling and texture filtering

**This repo contains no game data.** You need your own disc dump.

## Requirements

- CMake 3.20+, a C11 compiler, SDL3
- Python 3.12+
- `chdman` (macOS: `brew install rom-tools`; Debian/Ubuntu: `apt install mame-tools`)

## Getting started

1. Put your dump at `roms/Nightmare Creatures.chd`
   (expected CHD SHA1 `8592a07f87df446601902a137d69075dde4cf206`).
2. Run:

   ```sh
   make setup     # Python venv with splat / spimdisasm / ruff
   make extract   # verify the CHD and extract files to build/disc/
   make build     # build the native executable
   ```

## Status

Phase 0: tooling and extraction. See `docs/` for progress notes.
