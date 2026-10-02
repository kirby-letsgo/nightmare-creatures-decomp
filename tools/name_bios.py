#!/usr/bin/env python3
"""Find BIOS call stubs in each executable and write config/symbols/<exe>.bios.txt.

A Psy-Q BIOS stub is:  addiu $t2, $zero, 0xA0|0xB0|0xC0 ; jr $t2 ; addiu $t1, $zero, N
Names follow the psx-spx BIOS function reference.
"""

from __future__ import annotations

import struct
import sys

from gen_splat import DISC, EXES, HEADER_SIZE, ROOT

JR_T2 = 0x01400008

BIOS_NAMES = {
    0xA0: {
        0x39: "InitHeap",
        0x3F: "printf",
        0x42: "Load",
        0x43: "Exec",
        0x44: "FlushCache",
        0x49: "GPU_cw",
        0x70: "_bu_init",
        0x71: "_96_init",
        0x72: "_96_remove",
        0xAB: "_card_info",
        0xAC: "_card_load",
    },
    0xB0: {
        0x07: "DeliverEvent",
        0x08: "OpenEvent",
        0x09: "CloseEvent",
        0x0A: "WaitEvent",
        0x0B: "TestEvent",
        0x0C: "EnableEvent",
        0x0D: "DisableEvent",
        0x12: "InitPAD",
        0x13: "StartPAD",
        0x14: "StopPAD",
        0x15: "OutdatedPadInitAndStart",
        0x17: "ReturnFromException",
        0x18: "ResetEntryInt",
        0x19: "HookEntryInt",
        0x32: "open",
        0x33: "lseek",
        0x34: "read",
        0x35: "write",
        0x36: "close",
        0x41: "format",
        0x42: "firstfile",
        0x43: "nextfile",
        0x44: "rename",
        0x45: "erase",
        0x4A: "InitCARD",
        0x4B: "StartCARD",
        0x4C: "StopCARD",
        0x4E: "_card_write",
        0x4F: "_card_read",
        0x50: "_new_card",
        0x56: "GetC0Table",
        0x57: "GetB0Table",
        0x5B: "ChangeClearPad",
    },
    0xC0: {
        0x0A: "ChangeClearRCnt",
    },
}


def find_stubs(data: bytes, vram: int) -> list[tuple[int, int, int]]:
    stubs = []
    for off in range(HEADER_SIZE, len(data) - 12, 4):
        li_t2, jr, li_t1 = struct.unpack_from("<3I", data, off)
        table = li_t2 & 0xFFFF
        if (
            (li_t2 & 0xFFFF0000) == 0x240A0000
            and table in BIOS_NAMES
            and jr == JR_T2
            and (li_t1 & 0xFFFF0000) == 0x24090000
        ):
            stubs.append((vram + off - HEADER_SIZE, table, li_t1 & 0xFFFF))
    return stubs


def main() -> int:
    for name in EXES:
        data = (DISC / name).read_bytes()
        vram = struct.unpack_from("<I", data, 0x18)[0]
        base = name.lower().replace(".", "_")
        seen: dict[str, int] = {}
        lines = []
        for addr, table, num in find_stubs(data, vram):
            sym = BIOS_NAMES[table].get(num, f"bios_{table:02X}_{num:02X}")
            count = seen.get(sym, 0)
            seen[sym] = count + 1
            if count:
                sym = f"{sym}_{count}"
            lines.append(f"{sym} = 0x{addr:08X}; // type:func size:0xC\n")
        out = ROOT / "config" / "symbols" / f"{base}.bios.txt"
        out.write_text("".join(lines))
        print(f"{out.relative_to(ROOT)}: {len(lines)} stubs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
