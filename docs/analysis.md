# Executable analysis

All executables are NTSC-U builds dated 1997-09-26. Each loads at its header's `t_addr`.

| EXE | Load | Text (file offsets) | Entry | Functions (Psy-Q) | Role |
|---|---|---|---|---|---|
| SLUS_005.82 | 0x80010000 | 0xE18–0x4DF4 | 0x80010850 | 100 (81) | Resident launcher. Loads the modules below via BIOS `Load`/`Exec` |
| PSX.EXE | 0x80018000 | 0x15E4–0xE4EC | 0x80025C70 | 305 (234) | Front end (first module loaded) |
| PSX2.EXE | 0x80018000 | 0x2B04–0x6A04C | 0x800817D0 | 967 (287) | Main game |
| CREDITS.EXE | 0x80018000 | 0x148C–0xBB70 | 0x800232F4 | 252 (217) | Credits |
| STREAM1–3.EXE | 0x80018000 | ~0x1600–0x220EC | 0x8002710C / 0x800270E4 | 297 each (249) | FMV players |

The modules are overlays. They share the 0x80018000 slot, and the launcher in low memory stays resident.

## Notes

- The headers say `gp = 0`, but every module sets `$gp` in its startup code. splat detects the value.
  The recompiler must therefore handle `$gp`-relative accesses per module.
- BIOS stubs (`li t2, 0xA0/B0/C0; jr t2; li t1, N`) are named automatically by `tools/name_bios.py`.
  PSX2.EXE uses memory card (`_card_*`, `_bu_init`), event, pad and file I/O calls.
- Every module was built with **Psy-Q SDK 3.6**. `tools/match_psyq.py` matches library objects
  against the lab313ru/psx_psyq_signatures byte signatures. Versions 3.7 and 4.0 match almost nothing.
- PSX2.EXE contains 287 named library functions: LIBGPU 107, LIBCD 55, LIBSPU 33, LIBGTE 29,
  LIBETC 26, LIBC2 20, LIBCARD 7, LIBAPI 4, LIBSND 3, LIBC 2, LIBGS 1. That leaves about 680 game
  functions to recompile and then decompile.
- These library functions are what the PC runtime replaces with native implementations.

## Workflow

```sh
make extract   # CHD → build/disc
make sigs      # download Psy-Q 3.6 signatures to build/psyq_sigs (not committed)
make configs   # regenerate config/splat/*.yaml, BIOS and Psy-Q symbols
make split     # splat → build/splat/<exe>/asm
```
