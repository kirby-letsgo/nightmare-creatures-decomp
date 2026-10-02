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

The game boots natively through its intro movies into the main game, and the attract-mode demo
renders 3D gameplay at full speed on the reference software renderer.

| Area | State |
|---|---|
| Recompiler (all 7 executables, ~2,500 functions) | done |
| BIOS HLE, interrupts, DMA, timers | done |
| CD-ROM controller (data, XA routing, CD-DA hooks) | done |
| GTE | done |
| GPU (software, 1x) | done |
| MDEC (FMV) | done |
| SPU audio, XA, CD music | not started |
| Memory card saves | not started |
| Settings: adrenaline, modern controls, volume, upscaling | not started |
| Save states | not started |
| Windows / Linux builds | not yet tested |

## Running

```sh
make recomp && make build
./build/cmake/nightmare --disc "roms/Nightmare Creatures.chd"
```

### Keyboard controls

| PS1 button | Key |
|---|---|
| D-pad | Arrow keys |
| Cross | X |
| Circle | C |
| Square | Z |
| Triangle | S |
| L1 / R1 | Q / W |
| L2 / R2 | 1 / 2 |
| Start | Enter |
| Select | Backspace |

Gamepad support and rebinding are planned.

### Crashes

Each run writes `nightmare.log` in the current directory. If the game crashes, that file ends
with a backtrace in which recompiled functions appear as `<module>_<address>` (for example
`psx2_exe_8001A304`): please include it when reporting a crash.

### Debugging aids (environment variables)

| Variable | Effect |
|---|---|
| `NC_SHOT_EVERY=N` | Save a screenshot every N frames to `build/shots/` |
| `NC_TRACE_STACK=1` | Print the guest (MIPS) call stack once per second |
| `NC_PROFILE=1` | Sample the running guest function at each VBlank; print the top ones every 10 s |
| `NC_FPS=1` | Log the game's frame rate and the VBlank rate once per second |
| `NC_PRESS_START=N` | Tap Start every N frames (skips movies, advances menus) |
