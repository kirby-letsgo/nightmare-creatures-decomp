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

## Download

Ready-to-run builds for macOS (`.dmg`), Windows (`.exe`) and Linux (`.AppImage`) are on the
[latest release](https://github.com/kirby-letsgo/nightmare-creatures-decomp/releases/tag/latest),
rebuilt on every change. They contain only this project's code: the game itself runs from your
own Nightmare Creatures (USA, SLUS-00582) disc image in `.chd` format, which the start screen asks
for on first launch.

Release builds run the game's code through a built-in MIPS interpreter. Development builds
(below) instead compile the code translated from your disc (`make recomp`) into the executable,
and are the basis for the decompilation work.

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
| SPU audio, XA, CD music, reverb | done |
| Memory card saves | done (slot 1, standard `.mcd` image) |
| Settings: adrenaline off, volumes, fullscreen, filtering | done |
| Upscaling (software, full speed up to ~3x) | done |
| Widescreen 16:9 (3D widened, HUD kept in proportion) | done |
| Modern controls, mouse / right-stick camera | done (experimental) |
| Save states (F5 / F9, 4 slots in the pause menu) | done |
| Save states (F5 / F9, 4 slots in the pause menu) | done |
| Windows / Linux builds | not yet tested |

## Running

```sh
make recomp && make build
./build/cmake/nightmare
```

The start screen asks for your disc image the first time (it is verified against the NTSC-U
dump and remembered). `--disc <path>` overrides it. **Esc** (or the gamepad's Guide button)
opens the menu in game; **F11** toggles fullscreen.

Settings are stored in `settings.ini` next to the memory card (see Saves below).

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

Gamepads work out of the box (Xbox/PlayStation/Switch layouts via SDL): face buttons map by
position (south = Cross, east = Circle, west = Square, north = Triangle), the left stick acts as
the d-pad. Everything above is the default: **Settings > Button mapping** rebinds any PS1 button
for keyboard and gamepad (Esc, F5, F9 and F11 are reserved).

### Save states

**Cmd+S** (Ctrl+S on Windows/Linux) or **F5** quick-saves and **Cmd+R** (Ctrl+R) or **F9** quick-loads (slot 1).
The Cmd/Ctrl shortcuts can be changed, and gamepad buttons assigned (e.g. the stick clicks), in
**Settings > Button mapping**; the pause menu (Esc) has four slots. States
are taken at a fixed point in the game's level loop, so a request made elsewhere (in a movie or
menu) is carried out at the next moment of gameplay. They are stored in a `states/` folder next to
the memory card.

### Saves

Memory card 1 is a standard 128 KiB `.mcd` image, the same format DuckStation and other
emulators use, so existing saves can be copied in. It lives in the user data folder:

- macOS: `~/Library/Application Support/NightmareCreatures/nightmare-port/card1.mcd`
- Linux: `~/.local/share/NightmareCreatures/nightmare-port/card1.mcd`
- Windows: `%APPDATA%\NightmareCreatures\nightmare-port\card1.mcd`

A `card2.mcd` placed next to it is used as memory card 2.

### Crashes

Each run writes `nightmare.log` to the user data folder (next to the memory card; in a
development checkout with a `roms/` folder, to the current directory). If the game crashes, that file ends
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
| `NC_WAV=path` | Record the audio output to a WAV file |
| `NC_SKIP_MENU=1` | Boot straight into the game (scripted runs) |
| `NC_WS_TINT=1` | Widescreen: draw primitives classified as 2D (HUD/menus) in red |
| `NC_DATA_DIR=dir/` | Use another folder for settings and memory cards (test runs) |
| `NC_HEADLESS=1` | No window or audio, unpaced: fast automated runs (implies `NC_SKIP_MENU`) |
| `NC_INPUT=file` | Scripted input timeline (`first last buttons...` per line) |
| `NC_STATE_TEST=S,L,D` | Save at VBlank S, load at L, dump RAM at D on both passes (determinism check) |
| `NC_CAM_TEST=F` | Hold a 90-degree free-look offset from VBlank F (camera testing) |
| `NC_RAMDUMP=N`, `NC_RAMDUMP_FROM=F` | Dump RAM to `build/ram/` every N frames (from frame F) |
| `NC_MENU_SHOT=1` / `settings` | Save a screenshot of the start screen / settings page |

## Credits

- Adrenaline-off patch addresses: SCD (romhacking.net), packaged by
  [lightbulb-sun/nightmare-adrenaline](https://github.com/lightbulb-sun/nightmare-adrenaline) (MIT).
- Hardware documentation: psx-spx (Martin Korth / no$psx); Psy-Q signatures:
  [lab313ru/psx_psyq_signatures](https://github.com/lab313ru/psx_psyq_signatures).
