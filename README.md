# Nightmare Creatures: PC port

A native macOS, Windows and Linux version of **Nightmare Creatures** (Kalisto, 1997), the
PlayStation gothic action game, with the improvements it always needed: the adrenaline meter
can be switched off, modern controls replace the tank controls if you want, and the game runs
upscaled, in widescreen, with save states.

**No game data is included.** You need your own copy of the game: the USA PlayStation disc
(SLUS-00582) as a `.chd` disc image.

## Download

Get the latest build for your system from the
[**latest release**](https://github.com/kirby-letsgo/nightmare-creatures-decomp/releases/tag/latest)
(rebuilt automatically on every change):

| System | File | First launch |
|---|---|---|
| macOS 11+ (Apple Silicon and Intel) | `NightmareCreatures-macos.dmg` | Drag the app to Applications. It isn't notarized, so the first time right-click it and choose **Open**. |
| Windows 10/11 (64-bit) | `NightmareCreatures-windows-x64.exe` | Run it. If SmartScreen warns about an unknown publisher, choose **More info → Run anyway**. |
| Linux (x86-64) | `NightmareCreatures-linux-x86_64.AppImage` | `chmod +x` the file, then run it. |

Each is a single self-contained file; nothing else needs installing.

## Your disc image

The first time you start the game, choose your disc image on the start screen. It is checked
against the known good dump of the USA release and remembered for next time (**Change Disc...**
picks another).

If your dump is a `.bin`/`.cue` pair rather than a `.chd`, convert it with `chdman` (part of MAME
tools: `brew install rom-tools` on macOS, `apt install mame-tools` on Debian/Ubuntu):

```sh
chdman createcd -i "Nightmare Creatures.cue" -o "Nightmare Creatures.chd"
```

## Features

### Gameplay

- **Adrenaline off**: the adrenaline meter, which drains your health if you don't keep
  fighting, can be disabled. Its gauge disappears too.
- **Modern controls** (optional, experimental): push the stick or arrow keys in a direction and
  the character turns to face that way on screen and walks, instead of the original tank
  controls.
- **Mouse / right-stick camera**: look around with the mouse or the right stick; the camera
  swings back behind the player after a moment.
- **Button mapping**: every PS1 button, and the save-state hotkeys, can be rebound for keyboard
  and gamepad.

### Graphics

- **Upscaling**: render the 3D at up to 8× the original resolution (full speed up to about 3×
  on a modern computer).
- **Widescreen (16:9)**: a wider view of the world, with the HUD kept in proportion.
- **Sharp or smooth** screen filtering, fullscreen (also **F11**), and an FPS counter.

### Sound

- Full original sound: effects, CD music and voiced cinematics.
- Separate **master, music and effects** volume.

### Saving

- **Memory card saves** work exactly as on the console and are stored as standard `.mcd`
  memory card images, so you can copy saves to and from emulators such as DuckStation.
- **Save states**: save anywhere in a level and come back to that exact moment.

## Controls

### Keyboard

| PS1 button | Key |
|---|---|
| D-pad | Arrow keys |
| Cross / Circle / Square / Triangle | X / C / Z / S |
| L1 / R1 | Q / W |
| L2 / R2 | 1 / 2 |
| Start / Select | Enter / Backspace |

### Gamepad

Xbox, PlayStation and Switch controllers work out of the box. Buttons map by position (bottom
= Cross, right = Circle, left = Square, top = Triangle), and the left stick works as the d-pad.

### Other keys

| Key | Action |
|---|---|
| **Esc** (gamepad: Guide / PS button) | Menu: resume, save states, settings, quit |
| **Cmd+S** (Windows/Linux: Ctrl+S) or **F5** | Quick save (slot 1) |
| **Cmd+R** (Windows/Linux: Ctrl+R) or **F9** | Quick load (slot 1) |
| **F11** | Fullscreen |

All of these except Esc and F11 can be changed in **Settings → Button mapping**.

## Settings

Open **Settings** from the start screen or the in-game menu (Esc). Changes apply immediately.

| Section | Options |
|---|---|
| Gameplay | Adrenaline system on/off; tank or modern controls; mouse / right-stick camera and its sensitivity; button mapping |
| Audio | Master, music and effects volume |
| Video | Fullscreen; widescreen; sharp or smooth filtering; internal resolution (1×–8×); FPS counter |

## Save states

- **Quick save / quick load** use slot 1 (Cmd+S / Cmd+R, or F5 / F9).
- The **in-game menu** (Esc) has four slots, each showing whether it's empty, with Save and Load
  buttons.
- States can be taken during gameplay in a level (including the game's own pause screen). If you
  press save during a movie or loading screen, the message *"Waiting for gameplay to
  save/load..."* appears and the state is taken as soon as you're back in control.

## Where your files are

Settings, memory cards, save states and the log live in one folder:

| System | Folder |
|---|---|
| macOS | `~/Library/Application Support/NightmareCreatures/nightmare-port/` |
| Windows | `%APPDATA%\NightmareCreatures\nightmare-port\` |
| Linux | `~/.local/share/NightmareCreatures/nightmare-port/` |

| File | Contents |
|---|---|
| `settings.ini` | All settings, including the disc location and button mapping |
| `card1.mcd` | Memory card 1 (put a `card2.mcd` beside it to use memory card 2) |
| `states/slot1.state` … | Save states |
| `nightmare.log` | Log of the last run |

## Reporting problems

If the game crashes or misbehaves, please open an issue with:

- your system (macOS / Windows / Linux, and the version),
- what you were doing when it happened,
- the `nightmare.log` file from the folder above. After a crash it ends with a backtrace that
  shows where it happened.

## How it works

The game's original code runs from your disc image inside a small built-in emulator of the
PlayStation's processor, while graphics, sound, input and saving are reimplemented natively, so
the release builds contain only this project's own code. Alongside that, the project translates
the game's code into C and is gradually turning it into readable source. See
[DEVELOPMENT.md](DEVELOPMENT.md) for building from source and how everything fits together.

## Credits

- **Nightmare Creatures** © 1997 Kalisto Entertainment, published by Activision. This project is
  not affiliated with them; it contains no game data and requires your own copy of the game.
- Adrenaline-off patch addresses: SCD (romhacking.net), packaged by
  [lightbulb-sun/nightmare-adrenaline](https://github.com/lightbulb-sun/nightmare-adrenaline) (MIT).
- PlayStation hardware documentation: psx-spx by Martin Korth (no$psx).
- Psy-Q library signatures:
  [lab313ru/psx_psyq_signatures](https://github.com/lab313ru/psx_psyq_signatures).
- Built with [SDL3](https://libsdl.org), [Dear ImGui](https://github.com/ocornut/imgui) and
  [libchdr](https://github.com/rtissera/libchdr).
