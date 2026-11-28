#ifndef NC_TESTS_CD_FILE_H
#define NC_TESTS_CD_FILE_H

#include "jump_context.h"
#include "port/disc.h"
#include <stdio.h>

/* Addresses of the Psy-Q ISO9660 lookup routines (CdSearchFile, CD_newmedia,
 * CD_cachefile, CD_searchdir), their callees and data in one executable. */
typedef struct {
    u32 search_file, load_paths, search_directory, cache_directory;
    u32 printf, read_sectors;
    u32 debug, media_count, loaded_media, cached_directory;
    u32 files, paths, sector;
} CdFileFixture;

static bool check_cd_search_file(const CdFileFixture *f, bool (*routine)(CPUState *)) {
    const u32 path = 0x80062000, dest = 0x80062100;
    const u32 cache = f->files, table = f->paths;
    const struct {
        const char *path;
        int media, cache_result; /* media < 0 means already current */
        unsigned entries, debug;
        int match; /* index containing target, or -1 for no target */
        bool success;
    } cases[] = {
        {"\\CREDITS.TIM;1", -1, 1, 1, 0, 0, true},
        {"\\DIR\\CREDITS.TIM;1suffix", 1, -1, 64, 2, 63, true},
        {"\\CREDITS.TIM;1", 0, 1, 1, 1, 0, false},
        {"CREDITS.TIM;1", -1, 1, 1, 1, 0, false},
        {"\\ABSENT\\CREDITS.TIM;1", -1, 1, 1, 1, 0, false},
        {"\\", -1, 1, 1, 1, 0, false},
        {"\\\\\\\\\\\\\\\\CREDITS.TIM;1", -1, 1, 1, 1, 0, false},
        {"\\CREDITS.TIM;1", -1, 0, 1, 1, 0, false},
        {"\\CREDITS.TIM;1", -1, 1, 1, 2, -1, false},
        {"\\CREDITS.TIM;1", -1, 1, 64, 0, -1, false},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    MEM_W32(f->printf, 0x03e00008); /* printf returns immediately */
    MEM_W32(f->printf + 4, 0);
    MEM_W32(f->load_paths, 0x03e00008); /* CD_newmedia returns t8 */
    MEM_W32(f->load_paths + 4, 0x03001021);
    MEM_W32(f->cache_directory, 0x03e00008); /* CD_cachefile returns t9 */
    MEM_W32(f->cache_directory + 4, 0x03201021);
    memset(nc_fast_ptr(table), 0, 3 * 44);
    MEM_W32(table + 4, 1);
    MEM_W32(table + 44 + 4, 1);
    memcpy(nc_fast_ptr(table + 44 + 12), "DIR", 4);
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(cache), 0, 64 * 24);
        for (unsigned j = 0; j < cases[i].entries; j++) {
            u32 row = cache + j * 24;
            MEM_W32(row, 123 + j);
            MEM_W32(row + 4, 456 + j);
            strcpy((char *)nc_fast_ptr(row + 8),
                   (int)j == cases[i].match ? "CREDITS.TIM;1" : "OTHER");
        }
        strcpy((char *)nc_fast_ptr(path), cases[i].path);
        u8 expected[24];
        memset(expected, 0xcc, sizeof expected);
        memcpy(nc_fast_ptr(dest), expected, sizeof expected);
        u32 result = 0;
        if (cases[i].success) {
            result = cache + 24 * cases[i].match;
            memcpy(expected, nc_fast_ptr(result), sizeof expected);
        }
        MEM_W32(f->debug, cases[i].debug);
        MEM_W32(f->media_count, 12);
        MEM_W32(f->loaded_media, cases[i].media < 0 ? 12 : 11);
        CPUState state = arguments(dest, path, 0, i & 1 ? 2 : 8192);
        state.r[24] = (u32)cases[i].media;
        state.r[25] = (u32)cases[i].cache_result;
        ok = compare(state, f->search_file, routine, 1) &&
             nc_cpu.r[2] == result && !memcmp(expected, nc_fast_ptr(dest), sizeof expected) &&
             MEM_R32(f->loaded_media) == (cases[i].media == 0 ? 11u : 12u);
        if (!ok)
            fprintf(stderr, "CdSearchFile case %u failed\n", i);
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_path_table(const CdFileFixture *f, bool (*routine)(CPUState *)) {
    const u32 sectors = 0x80060000, trace = sectors + 2 * DISC_DATA_SECTOR;
    const u32 table = f->paths;
    u8 volume[DISC_DATA_SECTOR], paths[DISC_DATA_SECTOR];
    if (!disc_read_data(16, volume))
        return false;
    u32 path_lba = volume[140] | (u32)volume[141] << 8 |
                   (u32)volume[142] << 16 | (u32)volume[143] << 24;
    if (!disc_read_data(path_lba, paths))
        return false;
    enum { DISC_PATHS, BAD_ID, FIRST_ERROR, SECOND_ERROR, EMPTY, SECTOR_LIMIT, ENTRY_LIMIT };
    const struct { unsigned kind, debug; } cases[] = {
        {DISC_PATHS, 2}, {BAD_ID, 1}, {FIRST_ERROR, 0}, {FIRST_ERROR, 1},
        {SECOND_ERROR, 1}, {EMPTY, 0}, {SECTOR_LIMIT, 0}, {ENTRY_LIMIT, 0},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* cd_read logs the sector number, copies the next preloaded sector to
     * the requested destination, and can fail either read. */
    const u32 read_stub[] = {
        0xaf250000, 0x27390004, 0x00c04021, 0x24c90800, 0x8dea0000,
        0x25ef0004, 0xad0a0000, 0x25080004, 0x1509fffb, 0,
        0x032e1026, 0x0002102b, 0x03e00008, 0,
    };
    for (unsigned j = 0; j < sizeof read_stub / sizeof read_stub[0]; j++)
        MEM_W32(f->read_sectors + 4 * j, read_stub[j]);
    MEM_W32(f->printf, 0x03e00008);
    MEM_W32(f->printf + 4, 0x27180001); /* count printf calls */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        unsigned kind = cases[i].kind;
        memcpy(nc_fast_ptr(sectors), volume, sizeof volume);
        u8 *records = nc_fast_ptr(sectors + DISC_DATA_SECTOR);
        memset(records, 0, DISC_DATA_SECTOR);
        if (kind == DISC_PATHS)
            memcpy(records, paths, sizeof paths);
        else if (kind == BAD_ID)
            MEM_W8(sectors + 5, 'X');
        else if (kind == SECTOR_LIMIT || kind == ENTRY_LIMIT) {
            unsigned length = kind == SECTOR_LIMIT ? 24 : 1;
            unsigned stride = 8 + length + (length & 1);
            for (unsigned j = 0; j < (kind == SECTOR_LIMIT ? 64u : 129u); j++) {
                u8 *record = records + stride * j;
                record[0] = (u8)length;
                record[2] = (u8)(42 + j);
                record[6] = 1;
                record[7] = 0xab; /* SDK reads only the low parent byte */
                memset(record + 8, 'A' + j % 26, length);
            }
        }
        memset(nc_fast_ptr(table), 0xcc, 44 * 128);
        memset(nc_fast_ptr(trace), 0, 8);
        MEM_W32(f->cached_directory, 123);
        MEM_W32(f->debug, cases[i].debug);
        CPUState state = arguments(1, 2, 3, i & 1 ? 13 : 8192);
        state.r[14] = kind == FIRST_ERROR ? trace + 4 : kind == SECOND_ERROR ? trace + 8 : 0;
        state.r[15] = sectors;
        state.r[24] = 0;
        state.r[25] = trace;
        bool success = kind != BAD_ID && kind != FIRST_ERROR && kind != SECOND_ERROR;
        bool second_read = kind != BAD_ID && kind != FIRST_ERROR;
        ok = compare(state, f->load_paths, routine, 12) &&
             nc_cpu.r[2] == (u32)success && MEM_R32(trace) == 16 &&
             MEM_R32(trace + 4) == (second_read ? path_lba : 0) &&
             MEM_R32(f->cached_directory) == (success ? 0u : 123u);
        unsigned count = 0;
        if (success) {
            for (unsigned off = 0; ok && off < DISC_DATA_SECTOR && count < 128 && records[off]; count++) {
                u8 *record = records + off;
                u32 entry = table + count * 44;
                ok = MEM_R32(entry) == count + 1 && MEM_R32(entry + 4) == record[6] &&
                     !memcmp(nc_fast_ptr(entry + 8), record + 2, 4) &&
                     !memcmp(nc_fast_ptr(entry + 12), record + 8, record[0]) &&
                     MEM_R8(entry + 12 + record[0]) == 0;
                off += 8 + record[0] + (record[0] & 1);
            }
            if (count < 128)
                ok = ok && MEM_R32(table + count * 44 + 4) == 0;
            else
                ok = ok && !memcmp(nc_fast_ptr(f->sector), records, DISC_DATA_SECTOR);
        } else {
            ok = ok && MEM_R32(table) == 0xcccccccc;
        }
        unsigned prints = cases[i].debug == 2 ? count + 2 : (!success && cases[i].debug);
        ok = ok && nc_cpu.r[24] == prints;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_directory_cache(const CdFileFixture *f, bool (*routine)(CPUState *)) {
    const u32 buffer = f->sector, cache = f->files, trace = 0x80061000;
    u8 volume[DISC_DATA_SECTOR], root[DISC_DATA_SECTOR];
    if (!disc_read_data(16, volume))
        return false;
    u32 sector = volume[158] | (u32)volume[159] << 8 |
                 (u32)volume[160] << 16 | (u32)volume[161] << 24;
    if (!disc_read_data(sector, root))
        return false;
    enum { HIT, READ_ERROR, EMPTY, ROOT, SECTOR_LIMIT, ENTRY_LIMIT };
    const struct { unsigned kind, debug; } cases[] = {
        {HIT, 2}, {READ_ERROR, 0}, {READ_ERROR, 1}, {EMPTY, 0},
        {ROOT, 2}, {SECTOR_LIMIT, 0}, {ENTRY_LIMIT, 0},
    };
    StateIO saved = {0};
    cpu_serialize(&saved);
    NcModule module = nc_active_module();
    nc_set_module(NC_MOD_NONE);
    /* The sector is preloaded. Record the requested LBA and buffer and
     * return the scripted read result. Use the real name and BCD routines. */
    MEM_W32(f->read_sectors, 0xade40000); /* sw a0,0(t7) */
    MEM_W32(f->read_sectors + 4, 0xade50004);
    MEM_W32(f->read_sectors + 8, 0xade60008);
    MEM_W32(f->read_sectors + 12, 0x03e00008);
    MEM_W32(f->read_sectors + 16, 0x01c01021); /* v0 = t6 */
    MEM_W32(f->printf, 0x03e00008);
    MEM_W32(f->printf + 4, 0x27180001); /* count printf calls */
    bool ok = !saved.error;
    for (unsigned i = 0; ok && i < sizeof cases / sizeof cases[0]; i++) {
        unsigned kind = cases[i].kind, entries = 0;
        memset(nc_fast_ptr(cache), 0xcc, 64 * 24);
        memset(nc_fast_ptr(buffer), 0, DISC_DATA_SECTOR);
        if (kind == ROOT) {
            memcpy(nc_fast_ptr(buffer), root, sizeof root);
            for (unsigned off = 0; off < sizeof root && root[off] && entries < 64; off += root[off])
                entries++;
        } else if (kind == SECTOR_LIMIT || kind == ENTRY_LIMIT) {
            unsigned stride = kind == SECTOR_LIMIT ? 128 : 31;
            entries = kind == SECTOR_LIMIT ? 16 : 64;
            for (unsigned j = 0; j < entries; j++)
                MEM_W8(buffer + stride * j, (u8)stride);
        }
        MEM_W32(f->paths + 8, sector);
        MEM_W32(f->cached_directory, kind == HIT ? 1 : UINT32_MAX);
        MEM_W32(f->debug, cases[i].debug);
        memset(nc_fast_ptr(trace), 0, 12);
        const int threshold = 12 + NC_LOAD_CYCLES;
        CPUState state = arguments(1, 2, 3, i & 1 ? threshold + 1 : 4096);
        state.r[14] = kind != READ_ERROR;
        state.r[15] = trace;
        state.r[24] = 0;
        ok = compare(state, f->cache_directory, routine, threshold) &&
             nc_cpu.r[2] == (kind == READ_ERROR ? UINT32_MAX : 1u) &&
             MEM_R32(f->cached_directory) == (kind == READ_ERROR ? UINT32_MAX : 1u) &&
             MEM_R32(trace) == (kind != HIT);
        if (kind != HIT)
            ok = ok && MEM_R32(trace + 4) == sector && MEM_R32(trace + 8) == buffer;
        if (kind == ROOT) {
            unsigned offset = 0;
            for (unsigned j = 0; ok && j < entries; j++) {
                const u8 *record = root + offset;
                u32 entry = cache + j * 24;
                ok = !memcmp(nc_fast_ptr(entry + 4), record + 10, 4);
                if (j < 2)
                    ok = ok && !memcmp(nc_fast_ptr(entry + 8), j ? ".." : ".", j + 2);
                else
                    ok = ok && !memcmp(nc_fast_ptr(entry + 8), record + 33, record[32]) &&
                         MEM_R8(entry + 8 + record[32]) == 0;
                offset += record[0];
            }
        }
        if (kind == HIT || kind == READ_ERROR)
            ok = ok && MEM_R8(cache + 8) == 0xcc;
        else if (entries < 64)
            ok = ok && MEM_R8(cache + entries * 24 + 8) == 0;
        if (kind == ENTRY_LIMIT || kind == SECTOR_LIMIT)
            ok = ok && MEM_R32(cache + (entries - 1) * 24 + 4) == 0 &&
                 MEM_R32(f->paths + 8) == sector;
        unsigned prints = kind == ROOT ? entries + 2 : (kind == READ_ERROR && cases[i].debug);
        ok = ok && nc_cpu.r[24] == prints;
    }
    saved.loading = true;
    cpu_serialize(&saved);
    free(saved.buf);
    nc_set_module(module);
    return ok && !saved.error;
}

static bool check_cd_search_directory(const CdFileFixture *f, bool (*routine)(CPUState *)) {
    const u32 parents = f->paths + 4, name = 0x80060000;
    u8 saved[128 * 44];
    memcpy(saved, nc_fast_ptr(parents), sizeof saved);
    memcpy(nc_fast_ptr(name), "CREDITS", 8);
    const struct {
        unsigned count;
        int match, expected;
    } cases[] = {{0, -1, -1}, {1, 0, 1}, {3, 2, 3}, {3, -1, -1},
                 {128, 127, 128}, {128, -1, -1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(nc_fast_ptr(parents), 0, sizeof saved);
        for (unsigned n = 0; n < cases[i].count; n++) {
            bool match = (int)n == cases[i].match;
            MEM_W32(parents + n * 44, match || n == 1 ? 7 : 2);
            memcpy(nc_fast_ptr(parents + n * 44 + 8), match || n != 1 ? "CREDITS" : "CREDITX", 8);
        }
        for (unsigned j = 0; j < 2; j++) {
            CPUState state = arguments(7, name, 3, j ? 14 : 4096);
            if (j)
                state.r[29] = SCRATCH_BASE + 0x100;
            if (!compare(state, f->search_directory, routine, 13) ||
                nc_cpu.r[2] != (u32)cases[i].expected)
                return false;
        }
    }
    memcpy(nc_fast_ptr(parents), saved, sizeof saved);
    return true;
}

#endif
