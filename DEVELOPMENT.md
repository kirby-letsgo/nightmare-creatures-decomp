# Development

How the port is built, how it fits together, and the tools used to work on it. For playing the
game, see the [README](README.md).

## Ground rules

- The project is licensed **GPLv3** (`LICENSE`); it vendors xBRZ (GPLv3) in `third_party/xbrz/`.
- **Never commit game data or anything derived from it**: no disc images, extracted files,
  `gen/` (recompiled code) or RAM dumps. `.gitignore` covers `roms/`, `build/` and `gen/`.
  Addresses, sizes and symbol names (`config/`) are fine.
- Release builds never link recompiled code (`NC_INTERPRETER_ONLY`), so the published binaries
  contain only this project's code: the handwritten routines in `src/decomp`, with everything
  else interpreted from the player's disc.
- Run `make verify` before pushing. It lints, runs the disc equivalence tests and replays
  against both the recompiled and the release build, and checks the committed inventory. CI has
  no disc, so this is the only place those run.

## Requirements

- CMake 3.20+, a C11/C++17 compiler (Clang, GCC or MSVC)
- SDL3 (`brew install sdl3`), or configure with `-DNC_VENDOR_SDL=ON` to build it from source
- Python 3.12+
- `chdman` (`brew install rom-tools` / `apt install mame-tools`)
- clang-format (`brew install clang-format`)

libchdr, Dear ImGui and (optionally) SDL3 are fetched by CMake at pinned commits.

## Getting started

1. Put your disc image at `roms/Nightmare Creatures.chd`
   (CHD SHA1 `8592a07f87df446601902a137d69075dde4cf206`; `tools/extract.py` checks it).
2. Build:

   ```sh
   make setup     # Python venv: splat, spimdisasm, rabbitizer, ruff
   make extract   # verify the CHD and extract the disc to build/disc/
   make sigs      # download Psy-Q 3.6 library signatures to build/psyq_sigs/
   make configs   # splat configs + BIOS / Psy-Q / entry-point symbols (config/)
   make split     # disassemble every executable to build/splat/
   make recomp    # translate MIPS to C into gen/
   make build     # build/cmake/nightmare
   ```

3. Run `./build/cmake/nightmare`. In a checkout with a `roms/` folder the disc there is picked up
   automatically and the log is written to `./nightmare.log`.

After changing `tools/recomp/`, `config/hooks.txt` or `config/patches.txt`, run `make recomp`
again before `make build`.

## Build flavours

| Command | Output | Game code |
|---|---|---|
| `make build` | `build/cmake/nightmare` | Recompiled C from `gen/` (fast; the development build) |
| `make build-native` | `build/cmake-native/nightmare` | Handwritten routines native, the rest interpreted (what releases ship) |
| `make build-interp` | `build/cmake-interp/nightmare` | Everything interpreted, handwritten routines unused (reference) |
| `make build-watch` | `build/cmake-watch/nightmare` | Recompiled, plus memory watchpoints and function coverage (slower) |

Without a `gen/` folder, `make build` produces the same executable as `make build-native`.

## How it works

```
roms/*.chd ─► tools/extract.py ─► build/disc/ ─► splat ─► build/splat/ (asm, function list)
                                                              │
                                    tools/recomp/recomp.py ◄──┘   config/hooks.txt, patches.txt
                                              │
                                              ▼
                                        gen/<module>/*.c ──► nc_gen (development builds only)
                                                                    │
  src/port/ (runtime) ──────────────────────────────────────────────┴──► nightmare
```

### The game's executables

`SLUS_005.82` is a resident launcher at `0x80010000` that loads one of six overlays into
`0x80018000`: `PSX.EXE` (front end), `PSX2.EXE` (the game), `CREDITS.EXE` and three FMV players
(`STREAM1-3.EXE`). All were built with Psy-Q SDK 3.6. Details: [docs/analysis.md](docs/analysis.md).

### Running game code: recompiler and interpreter

- **Recompiler** (`tools/recomp/recomp.py`): one C function per MIPS function, operating on a
  `CPUState` (registers, HI/LO, COP0, GTE). Branches become gotos, delay slots are emitted before
  the branch takes effect, indirect calls go through `nc_call`. Mid-function entry points are
  added for `setjmp` return sites and for code addresses used as pointers. Generated files are
  per module, with an address → function table.
- **Interpreter** (`src/port/interp.c`): same semantics, executing instructions from RAM.
  `nc_call` uses a native function when one exists for an address and interprets otherwise, so
  recompiled, hand-decompiled and interpreted code mix freely.
- Both apply the same **patches** (`config/patches.txt`: instructions skipped while a runtime
  flag is set, e.g. adrenaline off) and **hooks** (`config/hooks.txt`: native functions called
  before an instruction, e.g. modern controls, free-look camera, the save-state safe point). The
  recompiler emits them into the generated code; the interpreter reads a table generated at build
  time by `tools/gen_patch_table.py`.

### The runtime (`src/port/`)

| File | Role |
|---|---|
| `main.c` | Startup, window, frame loop (input, video, audio, pacing), debug aids |
| `cpu.c`, `recomp.h` | CPU state, RAM, memory accessors, cycle budget and VBlank/interrupt scheduling |
| `dispatch.c`, `interp.c` | Address → function dispatch; interpreter fallback |
| `bios.c`, `exe.c` | High-level BIOS (events, files, memory card, Load/Exec, interrupts); EXE loading |
| `disc.c`, `raw_disc.c` | CHD, raw BIN/CUE, ISO9660 and disc track layout |
| `hw/` | GPU (software rasterizer, upscaling, xBRZ texture cache), GTE (`gte.c`), SPU (+ reverb, CD audio, XA), CD-ROM controller, MDEC, DMA, timers, IRQ, widescreen helpers |
| `controls.c` | Modern controls and free-look camera (hooks) |
| `savestate.c` | Whole-machine snapshots at the level-loop safe point |
| `input.c`, `settings.c`, `memcard.c` | Bindings, `settings.ini`, `.mcd` memory cards |
| `ui/menu.cpp` | Start screen, settings, button mapping, pause menu (Dear ImGui) |

Both the interpreter and recompiled C charge one guest cycle per instruction, or three for a
load. Branches and their delay slots retire together before the target executes. When the
cycle budget runs out, both paths call `nc_poll` to advance the hardware and deliver pending
interrupts. Frames are paced by the audio device's clock.

### Texture upscaling

With a texture scale above 1 (and internal resolution above 1×), each texture page + palette the
game draws with is decoded to ARGB, scaled with xBRZ and cached (`gpu.c`, `tex_lookup`). VRAM is
tracked in 64×256 regions whose generation counters are bumped by uploads, fills, copies and
drawing; a cached texture is rebuilt when a region it came from changes. Textures rebuilt more
than three times within a second (render-to-texture) are drawn natively for a while. Polygons and
sprites sample the upscaled texture at sub-texel precision; semi-transparency comes from the
original texel.

### Save states

The recompiled game runs on the native call stack, so states are taken and restored only at a
point where that stack always has the same shape: hooks in the level loop (`func_8006E38C`,
`0x8006E7EC` playing / `0x8006E50C` paused). Each module serializes its state through
`StateIO`; files are deflate-compressed (miniz). `NC_STATE_TEST` checks that a restored state
replays byte-identically.

**Cold Continue** uses its own atomically replaced file, `states/resume.state`, with the
safe-point PC and the guest registers BIOS Exec must restore when each active executable
returns. On a fresh launch, `--continue` or the start menu resumes at that PC. The
interpreter reconstructs the guest return path and can still call recompiled functions.
The four manual slots use the existing format. Run `ctest -R continue_game` to save in
one process and compare the restored gameplay with an uninterrupted run in another.

## Game variables found so far

Located by RAM diffing, watchpoints and coverage (see below). All in `PSX2.EXE`.

| Address | Type | Meaning |
|---|---|---|
| `0x800CB1CC` | u16 | Player heading (65536 = full turn; turning left increases it) |
| `0x800CB170` / `0x800CB178` | s32 | Player X / Z (world units × 256) |
| `0x800CB160` / `0x800CB164` / `0x800CB168` | s32 | Camera eye X / Y / Z |
| `0x800D7630` | struct | Camera record: position (s16 ×3), angles at +8/+A/+C (yaw `0x800D763A`, 4096 = full turn) |
| `0x800CC012` | s16 | Adrenaline countdown (health drains at 0) |
| `0x8008F1C0` | pad buffer | Raw controller data (`InitPAD`) |

| Function | Role |
|---|---|
| `func_80044724` | Player control (reads the pad at `0x80044778`) |
| `func_80070050` | Pad → game button word (PS1 bit layout) |
| `func_8002B348` → `func_80047948` | Camera update / follow logic |
| `func_80049838` | Copies camera position into the camera record and builds the view |
| `func_8006E38C` | Level loop (one iteration per game frame) |
| `func_80055AE8` | Adrenaline update (gauge draw at `0x80055DCC`, countdown at `0x80055E94`) |

Player control calls `80070050` at `80044778` with `a0 = 800D7C04` and
`a1 = 1` in the delay slot. The `player_input` hook at `80044780` sees the
returned word in `v0` immediately before the game copies it to `s1`.
The first callee, `8006FF34`, scans two 34-byte packets, records usable
ports at `gp + 0x238`, and prefers port one. Only port one enables the
normal decode path. When both are present, the second pad contributes
its active-high buttons in bits 16–31.

## Debugging tools

### Environment variables

| Variable | Effect |
|---|---|
| `NC_HEADLESS=1` | No window or audio, unpaced (about 2× real time): automated runs. Implies `NC_SKIP_MENU` |
| `NC_SKIP_MENU=1` | Boot straight into the game |
| `NC_DATA_DIR=dir/` | Use another folder for settings, memory cards and states (keep test runs away from your real ones) |
| `NC_INPUT=file` | Scripted input: lines of `first_frame last_frame button...` (up down left right cross circle square triangle l1 r1 l2 r2 start select) |
| `NC_PRESS_START=N` | Tap Start every N frames |
| `NC_SHOT_EVERY=N` | Save a screenshot every N frames to `build/shots/` |
| `NC_MENU_SHOT=1` / `settings` / `bindings` | Screenshot the start screen / settings / button mapping page |
| `NC_RAMDUMP=N`, `NC_RAMDUMP_FROM=F` | Dump RAM to `build/ram/` every N frames (from frame F) |
| `NC_STATE_TEST=S,L,D` | Save at VBlank S, load at L, dump RAM at D on both passes (determinism check) |
| `NC_CAM_TEST=F` | Hold a 90° free-look offset from VBlank F |
| `NC_TRACE_STACK=1` | Print the guest call stack once per second |
| `NC_PROFILE=1` | Sample the running guest function each VBlank; print the top ones every 10 s |
| `NC_FPS=1` | Log the game's frame rate and the VBlank rate each second |
| `NC_WAV=path` | Record the audio output to a WAV file |
| `NC_WS_TINT=1` | Widescreen: draw primitives classified as 2D (HUD) in red |

Watch build only (`make build-watch`):

| Variable | Effect |
|---|---|
| `NC_WATCH=addr` | Log stores to that address with a guest backtrace (`NC_WATCH_FROM=F`, `NC_WATCH_SKIP=N`) |
| `NC_COVERAGE=first,last,file` | Count guest function entries between two VBlanks |
| `NC_COVERAGE_CALLERS=addr` | …counting calls to one function per call site instead |

### First-level replay

With the US CHD in `roms/`, run the gameplay check against either build:

```sh
ctest --test-dir build/cmake-interp -R first_level --output-on-failure
ctest --test-dir build/cmake -R first_level --output-on-failure
```

Each run boots with a fresh temporary memory card, tank controls and adrenaline enabled.
`tests/first_level_boot.input` taps Start by VBlank. At the first level-loop safe point,
`NC_LEVEL_INPUT` switches playback to `tests/first_level.input`, timed in game updates.
The test checks heading, X/Z position and adrenaline at checkpoints covering level entry,
the first position change, both turns, the walk and the first adrenaline decrement. A missing disc skips the CTest. A stalled or
incomplete replay fails.

To compare every recorded update, build both flavours with `make recomp`, `make build` and
`make build-interp`, then run:

```sh
python3 tools/test_first_level.py build/cmake-interp/nightmare --compare build/cmake/nightmare
```

Passing the interpreter as both executables checks repeatability. Each replay starts from a
fresh boot. The comparison reports the first differing frame, field and values and returns a
failure. The interpreter and recompiled build agree on all four fields for 401 level updates.
The checkpoints around frames 11 and 376 catch the original timing regression in either build
when running the single-executable CTest.

### Recording a combat replay

Run `nightmare --record-input fight.movie` to capture the digital PS1 controller from boot
through quit. The file must be new. Movies use the original tank controls and fixed camera,
and do not encode changes to settings, memory cards or save-state loads. Use a fresh
`NC_DATA_DIR` for recording and playback with the same settings. Replay with
`NC_HEADLESS=1 nightmare --play-input fight.movie`. The text file stores runs of up to 120
VBlanks with the same pad mask and ends with an explicit end marker.

`tests/first_combat.movie` boots into the first level, takes a hit, and lands four attacks.
At each level update the trace includes player health and cumulative hits dealt and taken.
Each hit counts a health decrease in the damage routine with the player as attacker or target.
Run the fixture against both execution paths with
`python3 tools/test_first_level.py build/cmake-interp/nightmare --compare build/cmake/nightmare --scenario first_combat`.

Add `--trace-damage` to capture the caller and four argument registers at `0x8004E6DC`.
Each player hit also reports the attacker, recipient, base damage entering the health
calculation, and health before and after subtraction. The frame number identifies the
next level checkpoint. With `--compare`, the runner checks the order and values of these
events across both builds.

### Native adrenaline update

The recompiled build calls `src/decomp/adrenaline.c` after the original HUD draw.
It updates the timer, applies the game-mode cadence, clamps an empty meter and drains
health. The instruction-timed path handles guest death callbacks and any update close
enough to a hardware event for an interrupt to arrive midway through it. The release
interpreter continues to execute the disc code and provides the comparison reference.

`ctest -R adrenaline` compares the native update against the disc instructions, including
registers, RAM and remaining cycles, and exercises the movement replay with adrenaline
disabled. To compare both gameplay builds directly, add `--no-adrenaline` to
`tools/test_first_level.py ... --compare ...`.

### Attack contact

Before the player attack loop builds any boxes, `func_80042520` selects an attack
kind from the player's animation state. The recompiled build scans the 24 primary
state/kind pairs first. If there is no match, it checks two secondary pairs when
the actor has the secondary animation and its state is valid. The final fallback
recognizes state 19 with actor flag `0x8000`. A zero result ends the contact pass.
In the first-combat movie, state 5 selects kind 1 and state 24 selects kind 15.

The recompiled build uses `src/decomp/contact.c` in both attack loops. Each loop
compares a center and extent with a box on three axes. Touching the boundary counts
as overlap. Separation on any axis advances the loop to its next candidate.

After an overlap, `func_8004E2A0` evaluates whether the target can take this hit.
The native block at `0x8004E510` first checks the preceding callback's result.
On the normal path, target states 16 and 23 reject the contact. State 15 requires
bit `0x8000` at actor offset `0x330`. Other states pass this check when both bits in
mask `0x00280000` at offset `0x2C8` are clear. Further mode-specific rules and hit
responses run in the original code.

In `first_combat.movie`, six contacts reach the target handler. Five cause damage.
At frame 288 the player's attack overlaps the enemy, which is now in state 15 with
the required bit clear. The handler returns before damage selection. Run
`ctest -R contact` for boundary and state cases, including comparison with the US
disc instructions when the disc is available.

### Native hit damage

In the recompiled build, `src/decomp/damage.c` selects base damage and calculates the
target's remaining health. The selector at `0x8004E6DC` indexes the byte table at
`0x800D42C8` by the actor's row (byte at offset `0x388`) and attack kind. Each row
contains 52 bytes. Actor flags and game rules select the row or combine the attacker's
power with a target-specific percentage. The selector writes the resulting power to
the caller's output word and converts it to health points.

In the first-combat replay the incoming attack uses row 4, kind 38, with power 20.
Integer division of `20 * 4096 / 255` gives 321 damage. The player's attacks use row
0, kinds 1 and 15, with powers 15 and 25. The target's entries are both 100 percent,
so these attacks deal 240 and 401 damage.

After selection, the entry at `0x8004EB6C`
subtracts base damage directly. The entry at `0x8004EB30` doubles it when bit
`0x08000000` in the rules word at offset 4 and bits `0x30` in the first word are all clear.
Both paths resume at the original health store at `0x8004EB78`, where the combat replay
counts hits. The original code handles death reactions. Near a scheduled interrupt,
the recompiled build executes the instruction-timed path.

`ctest -R damage` checks the arithmetic, scratch registers and cycle counts. With the US
disc present it compares the selector's registers, output word and cycle count against
the original instructions and checks guest RAM for both health-calculation entries.

### Disc format verification

Playback accepts CHD, a complete 2352-byte-sector BIN, and CUE sheets with one or more
BINARY files. The raw reader checks the 26-track US layout and the CRC32 of the full PSX2.EXE
payload. It maps stored INDEX 00 pregaps and zero-filled PREGAP directives onto the same LBAs
as the CHD reader. Development extraction with `make extract` still takes a CHD.

After extraction, `ctest --test-dir build/cmake-interp -R disc_formats --output-on-failure`
compares the track table and every raw data/audio sector in the merged CUE and bare BIN
against the CHD. To verify additional layouts, pass their paths after the reference CHD:

```sh
build/cmake-interp/test_disc_formats 'roms/Nightmare Creatures.chd' 'path/to/split tracks.cue'
```

### Finding a game variable

1. Write an input script for a controlled experiment (e.g. idle, then turn left for 40 frames).
2. Capture RAM with `NC_HEADLESS=1 NC_DATA_DIR=build/testdata/ NC_INPUT=… NC_RAMDUMP=…` and
   compare snapshots with numpy (values constant while idle, changing monotonically during the
   action, and so on).
3. Confirm with `NC_WATCH` to find the code that writes it, then read that function in
   `build/splat/psx2_exe/asm/`.
4. Use `NC_COVERAGE` to diff which functions run in two situations (e.g. playing vs paused).

Recompiled functions are named `<module>_<address>` (e.g. `psx2_exe_8001A304`), so native
backtraces (crash logs, `NC_TRACE_STACK`, profilers) read as the guest call stack.

### Decompiling a routine

The goal is a release build that needs no interpreter: every original routine replaced by
handwritten C that leaves the CPU in exactly the state the original instructions would,
including the cycle budget. Readable names come later.

1. Pick work from `tools/work_queue.py` (uncovered functions, leaf-first, with Psy-Q names and
   duplicate twins in other executables). Prefer whole routines over fragments.
2. Write `bool nc_<name>(CPUState *c)` in `src/decomp/<file>.c` and declare it in the header.
   Return false without side effects when `c->budget` is too small, as the existing routines do.
3. Add a row to `config/splices/<file>.txt`:
   - `<module> <addr> entry nc_<name>(c)` replaces the whole routine at `addr`. Release builds
     call it through the dispatcher (`tools/gen_registry.py`) and the recompiler emits
     `if (nc_<name>(c)) return;` at the routine's start.
   - `<module> <addr> at <C statement>` splices a statement before the instruction at `addr`
     in recompiled code only; every `goto L_<addr>` in it becomes a label. Use it for fragments.
   - `include decomp/<file>.h` once per file, for the declarations.
   Do not add per-address code to `recomp.py`.
4. Add equivalence cases to `tests/<file>.c` (a `*_rules` test without the disc where possible,
   and a `*_disc` test) and register them in `CMakeLists.txt`.
5. Add the covered range to `config/decomp_ranges.txt`, then run `make verify`.

### Adding a patch or hook

- **Patch:** add `psx2_exe <address> <flag>` to `config/patches.txt`, define `bool nc_flag_<flag>`
  in `src/port/patches.c` and set it from settings in `patches_apply()`.
- **Hook:** add `psx2_exe <address> <name>` to `config/hooks.txt` and implement
  `void nc_hook_<name>(CPUState *c)`. It runs before the instruction at that address, with the
  guest registers in `c`.

Then `make recomp && make build` (the interpreter build picks both up automatically).

## Continuous integration and releases

`.github/workflows/build.yml` lints, validates `config/splices`, and publishes the progress table
(from the committed `config/inventory`) as the job summary and a `state.html` artifact. It builds
the release executable on macOS (universal), Windows (MSVC, static runtime) and Linux
(Ubuntu 22.04) with a statically linked SDL3, runs the tests that need no disc (`ctest`), and
packages a `.dmg`, an `.exe` and an AppImage
(`packaging/`). The Android job builds an arm64 APK with the SDK and NDK (see below). Every push to `main`
refreshes the `latest` pre-release when all platform jobs succeed.

To reproduce a release build locally:

```sh
cmake -S . -B build/cmake-release -DCMAKE_BUILD_TYPE=Release -DNC_INTERPRETER_ONLY=ON -DNC_VENDOR_SDL=ON
cmake --build build/cmake-release
sh packaging/package_macos.sh build/cmake-release dev build/NightmareCreatures-macos.dmg
```

## Android APK

The Android package targets arm64 devices running Android 8.0 or newer. The build uses JDK 17,
Python 3, Git, `zip` and the Android SDK. With `ANDROID_HOME` pointing at the SDK and the JDK
on `PATH`, run:

```sh
sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;27.2.12479018" "cmake;3.31.6"
bash packaging/package_android.sh
adb install -r build/android/NightmareCreatures-android-arm64.apk
```

The packager compiles the interpreter, the pinned SDL native library and its matching Java
activity, then signs and verifies the APK. Development builds use package
`org.nightmarecreatures.port.test` and keep a local development signing key in `build/android/`.
For a production build, set `NC_ANDROID_KEYSTORE`, `NC_ANDROID_STORE_PASSWORD` and
`NC_ANDROID_KEY_ALIAS` (also `NC_ANDROID_KEY_PASSWORD` if the key has its own password).
That build uses `org.nightmarecreatures.port`. Android requires a matching certificate and a
nondecreasing version code to update an installed app.

The Android CI job uploads the APK alongside the desktop packages. Main-branch pushes use the
repository secrets `ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_ALIAS`
when configured. Without a release key, CI produces the test package with a runner-local key.
Configure a stable release key for updates that preserve installed saves across rolling releases.

## Roadmap

- Replace every original routine with handwritten C (see "Decompiling a routine"), then drop
  the interpreter from release builds.
- Readable names and structure for the core systems (player, camera, adrenaline).
- A GPU-based renderer, so upscaling above 3× runs at full speed.
- Code signing and notarization for the release builds.
