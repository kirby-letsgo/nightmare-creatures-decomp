#ifndef NC_DECOMP_CD_FILE_H
#define NC_DECOMP_CD_FILE_H

/* Psy-Q libcd ISO9660 lookup: CD_newmedia caches the path table, CD_searchdir
 * finds a directory in it, CD_cachefile caches one directory's records, and
 * CdSearchFile walks a path through both caches. Every executable links the
 * same object; only the addresses differ. Call return addresses are offsets
 * into the four routines. */

#include "decomp/cd_irq.h"
#include "decomp/cd_read_retry.h"

typedef struct {
    /* the four routines */
    u32 search_file, load_paths, search_directory, cache_directory;
    /* callees */
    u32 printf, int_to_pos, names_equal, read_sectors, copy_identifier, strcmp, compare_bytes;
    /* data: debug level, media change counters, cached directory number */
    u32 debug, media_count, loaded_media, cached_directory;
    /* 64 cached CdlFILE records of 24 bytes, 128 path entries of 44 bytes, sector buffer */
    u32 files, paths, sector;
    /* messages and names */
    u32 depth_message, parent_message, cache_message, search_message, found_message;
    u32 missing_message, volume_error, iso_id, iso_error, path_error, path_start, path_entry;
    u32 path_count, directory_error, directory_start, dot, dot_dot, directory_entry;
    u32 directory_count;
} CdFileLayout;

/* lui at,%hi(table); addu at,at,index: the access then adds %lo(table). */
static void cd_file_index(CPUState *c, u32 table, unsigned index) {
    set_reg(c, 1, cd_wait_hi(table));
    set_reg(c, 1, R(1) + R(index));
}

static inline u32 cd_file_lo(u32 address) {
    return address - cd_wait_hi(address);
}

static void cd_file_store_word(CPUState *c, u32 address, unsigned reg) {
    set_reg(c, 1, cd_wait_hi(address));
    MEM_W32(address, R(reg));
    NC_POLL(c, 1);
}

static void cd_file_load_half(CPUState *c, unsigned reg, u32 address, bool sign_extend) {
    u16 value = MEM_R16(address);
    R(reg) = sign_extend ? (u32)(s32)(s16)value : (u32)value;
    NC_POLL(c, NC_LOAD_CYCLES);
}

/* ISO directory records carry unaligned little-endian extent and length
 * fields. Keep both halves of each guest load/store visible to the clock. */
static void cd_file_copy_directory_word(CPUState *c, unsigned reg, u32 source, u32 dest) {
    R(reg) = nc_lwl(c, source + 3, R(reg));
    NC_POLL(c, NC_LOAD_CYCLES);
    R(reg) = nc_lwr(c, source, R(reg));
    NC_POLL(c, NC_LOAD_CYCLES);
    NC_POLL(c, 1);
    nc_swl(c, dest + 3, R(reg));
    NC_POLL(c, 1);
    nc_swr(c, dest, R(reg));
    NC_POLL(c, 1);
}

/* v0 already contains twice the entry index. Each cached path occupies
 * 44 bytes: directory number, parent, extent sector, and identifier. */
static void cd_file_path_table_offset(CPUState *c, unsigned dest) {
    set_reg(c, 2, R(2) + R(7));
    set_reg(c, 2, R(2) << 2);
    set_reg(c, 2, R(2) - R(7));
    set_reg(c, dest, R(2) << 2);
}

static void cd_file_report_path_entry(CPUState *c, const CdFileLayout *layout) {
    const u32 fields[] = {8, 0, 4}; /* sector, number, parent */
    for (unsigned i = 0; i < 3; i++) {
        cd_file_index(c, layout->paths + fields[i], 16);
        load_word(c, 5 + i, R(1) + cd_file_lo(layout->paths + fields[i]));
    }
    set_reg(c, 4, cd_wait_hi(layout->path_entry));
    set_reg(c, 4, layout->path_entry);
    R(31) = layout->load_paths + 0x238;
    MEM_W32(R(29) + 16, R(18));
    NC_POLL(c, 2);
    nc_call(c, layout->printf);
}

static void cd_file_cache_path_table(CPUState *c, const CdFileLayout *layout) {
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(2) < 2);
    bool verbose = !R(2);
    R(17) = R(16); /* cursor in the path-table sector */
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 4, cd_wait_hi(layout->path_start));
        set_reg(c, 4, layout->path_start);
        cd_read_call(c, layout->printf, layout->load_paths + 0x144);
    }
    set_reg(c, 2, 1);
    R(7) = 0; /* number of paths cached */
    NC_POLL(c, 2);
    set_reg(c, 20, cd_wait_hi(layout->paths + 8));
    set_reg(c, 20, layout->paths + 8);
    set_reg(c, 21, R(20) + 4);
    bool full = false;
    for (;;) {
        load_cd_byte(c, 2, R(17));
        NC_POLL(c, 1);
        bool end = R(2) == 0;
        R(2) = R(7) << 1;
        NC_POLL(c, 2);
        if (end)
            break;
        cd_file_path_table_offset(c, 16);
        set_reg(c, 2, R(16) + R(20));
        cd_file_copy_directory_word(c, 3, R(17) + 2, R(2));
        set_reg(c, 18, R(16) + R(21));
        set_reg(c, 4, R(18));
        load_cd_byte(c, 2, R(17) + 6); /* low byte of parent number */
        set_reg(c, 19, R(7) + 1);
        cd_file_index(c, layout->paths, 16);
        MEM_W32(R(1) + cd_file_lo(layout->paths), R(19));
        NC_POLL(c, 1);
        cd_file_index(c, layout->paths + 4, 16);
        MEM_W32(R(1) + cd_file_lo(layout->paths + 4), R(2));
        NC_POLL(c, 1);
        load_cd_byte(c, 6, R(17));
        R(31) = layout->load_paths + 0x1c8;
        R(5) = R(17) + 8;
        NC_POLL(c, 2);
        nc_call(c, layout->copy_identifier);
        load_cd_byte(c, 2, R(17));
        NC_POLL(c, 1);
        set_reg(c, 2, R(18) + R(2));
        MEM_W8(R(2), 0);
        NC_POLL(c, 1);
        load_cd_byte(c, 3, R(17));
        NC_POLL(c, 1);
        set_reg(c, 2, R(3) & 1); /* ISO identifiers pad to an even length */
        set_reg(c, 2, R(2) + 8);
        set_reg(c, 3, R(3) + R(2));
        load_cd_port(c, 2, layout->debug);
        NC_POLL(c, 1);
        set_reg(c, 2, (s32)R(2) < 2);
        verbose = !R(2);
        R(17) += R(3);
        NC_POLL(c, 2);
        if (verbose)
            cd_file_report_path_entry(c, layout);
        set_reg(c, 7, R(19));
        set_reg(c, 2, (s32)R(7) < 128);
        full = !R(2);
        NC_POLL(c, 2);
        if (full)
            break;
        set_reg(c, 2, cd_wait_hi(layout->sector + 0x800));
        set_reg(c, 2, layout->sector + 0x800);
        set_reg(c, 2, R(17) < R(2));
        bool in_sector = R(2) != 0;
        NC_POLL(c, 2);
        if (!in_sector)
            break;
    }
    if (!full) {
        set_reg(c, 2, (s32)R(7) < 128);
        bool room = R(2) != 0;
        R(2) = R(7) << 1;
        NC_POLL(c, 2);
        if (room) {
            cd_file_path_table_offset(c, 2);
            cd_file_index(c, layout->paths + 4, 2);
            MEM_W32(R(1) + cd_file_lo(layout->paths + 4), 0); /* end marker */
            NC_POLL(c, 1);
        }
    }
    load_cd_port(c, 2, layout->debug);
    cd_file_store_word(c, layout->cached_directory, 0); /* invalidate the file cache */
    set_reg(c, 2, (s32)R(2) < 2);
    verbose = !R(2);
    R(2) = 1;
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 4, cd_wait_hi(layout->path_count));
        set_reg(c, 4, layout->path_count);
        R(31) = layout->load_paths + 0x2b0;
        R(5) = R(7);
        NC_POLL(c, 2);
        nc_call(c, layout->printf);
        set_reg(c, 2, 1);
    }
}

static void cd_file_report_media_failure(CPUState *c, const CdFileLayout *layout, u32 format,
                                         u32 resume, bool include_sector) {
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    bool report = (s32)R(2) > 0;
    R(2) = 0;
    NC_POLL(c, 2);
    if (report) {
        if (include_sector)
            load_word(c, 5, R(29) + 24);
        set_reg(c, 4, cd_wait_hi(format));
        set_reg(c, 4, format);
        cd_read_call(c, layout->printf, resume);
        R(2) = 0;
        NC_POLL(c, 2);
    }
}

static bool cd_load_paths(CPUState *c, const CdFileLayout *layout) {
    if (c->budget <= 12)
        return false;
    R(29) -= 64;
    R(4) = 1;
    R(5) = 16; /* primary volume descriptor */
    c->budget -= 3;
    MEM_W32(R(29) + 32, R(16));
    R(16) = layout->sector;
    R(6) = R(16);
    c->budget -= 4;
    MEM_W32(R(29) + 56, R(31));
    c->budget--;
    for (unsigned reg = 21; reg >= 18; reg--) {
        MEM_W32(R(29) + 32 + 4 * (reg - 16), R(reg));
        c->budget--;
    }
    R(31) = layout->load_paths + 0x38;
    MEM_W32(R(29) + 36, R(17));
    NC_POLL(c, 2);
    nc_call(c, layout->read_sectors);
    set_reg(c, 17, R(2));
    set_reg(c, 2, 1);
    bool received = R(17) == R(2);
    R(4) = R(16) + 1;
    NC_POLL(c, 2);
    if (!received) {
        cd_file_report_media_failure(c, layout, layout->volume_error, layout->load_paths + 0x6c,
                                     false);
    } else {
        set_reg(c, 5, cd_wait_hi(layout->iso_id));
        set_reg(c, 5, layout->iso_id);
        R(31) = layout->load_paths + 0x84;
        R(6) = 5;
        NC_POLL(c, 2);
        nc_call(c, layout->compare_bytes); /* check "CD001" */
        bool iso = R(2) == 0;
        NC_POLL(c, 2);
        if (!iso) {
            cd_file_report_media_failure(c, layout, layout->iso_error, layout->load_paths + 0xb0,
                                         false);
        } else {
            set_reg(c, 5, cd_wait_hi(layout->sector + 0x8c));
            set_reg(c, 5, layout->sector + 0x8c);
            cd_file_copy_directory_word(c, 2, R(5), R(29) + 24);
            set_reg(c, 4, 1);
            load_word(c, 5, R(29) + 24);
            R(31) = layout->load_paths + 0xe4;
            R(6) = R(16);
            NC_POLL(c, 2);
            nc_call(c, layout->read_sectors); /* first little-endian path-table sector */
            received = R(2) == R(17);
            NC_POLL(c, 2);
            if (received)
                cd_file_cache_path_table(c, layout);
            else
                cd_file_report_media_failure(c, layout, layout->path_error,
                                             layout->load_paths + 0x114, true);
        }
    }
    load_word(c, 31, R(29) + 56);
    for (unsigned reg = 21; reg >= 16; reg--)
        load_word(c, reg, R(29) + 32 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 64);
    NC_POLL(c, 2);
    return true;
}

/* Copy one path component to the guest stack. Leave the cursor on its
 * delimiter. The original reads the delimiter again before consuming it. */
static bool cd_file_read_path_component(CPUState *c) {
    load_cd_byte(c, 2, R(16));
    NC_POLL(c, 1);
    bool separator = R(2) == R(20);
    R(17) = R(29) + 16;
    NC_POLL(c, 2);
    if (!separator) {
        set_reg(c, 3, 92);
        load_cd_byte(c, 2, R(16));
        for (;;) {
            NC_POLL(c, 1);
            bool end = !R(2);
            R(2) = (s32)R(18) < 8;
            NC_POLL(c, 2);
            if (end)
                return false;
            load_cd_byte(c, 2, R(16));
            set_reg(c, 16, R(16) + 1);
            MEM_W8(R(17), (u8)R(2));
            NC_POLL(c, 1);
            load_cd_byte(c, 2, R(16));
            NC_POLL(c, 1);
            separator = R(2) == R(3);
            R(17)++;
            NC_POLL(c, 2);
            if (separator)
                break;
        }
        load_cd_byte(c, 2, R(16));
    }
    NC_POLL(c, 1);
    bool directory = R(2) != 0;
    R(2) = (s32)R(18) < 8;
    NC_POLL(c, 2);
    return directory;
}

static bool cd_file_find_parent_directory(CPUState *c, const CdFileLayout *layout) {
    MEM_W8(R(29) + 16, 0);
    NC_POLL(c, 1);
    set_reg(c, 16, R(19));
    set_reg(c, 18, 0); /* depth */
    set_reg(c, 20, 92);
    while (cd_file_read_path_component(c)) {
        set_reg(c, 16, R(16) + 1);
        MEM_W8(R(17), 0);
        NC_POLL(c, 1);
        R(31) = layout->search_file + 0xec;
        R(5) = R(29) + 16;
        NC_POLL(c, 2);
        nc_call(c, layout->search_directory); /* CD_searchdir */
        set_reg(c, 4, R(2));
        set_reg(c, 2, UINT32_MAX);
        bool missing = R(4) == R(2);
        NC_POLL(c, 2);
        if (missing) {
            MEM_W8(R(29) + 16, 0);
            NC_POLL(c, 2);
        } else {
            set_reg(c, 18, R(18) + 1);
            set_reg(c, 2, (s32)R(18) < 8);
            bool more = R(2) != 0;
            NC_POLL(c, 2);
            if (more)
                continue;
        }
        set_reg(c, 2, (s32)R(18) < 8);
        break;
    }
    bool depth_ok = R(2) != 0;
    NC_POLL(c, 2);
    if (depth_ok) {
        load_cd_byte(c, 2, R(29) + 16);
        NC_POLL(c, 1);
        bool have_name = R(2) != 0;
        NC_POLL(c, 2);
        if (have_name)
            return true;
    }
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    bool report = (s32)R(2) > 0;
    R(5) = R(19);
    NC_POLL(c, 2);
    if (report) {
        set_reg(c, 4, cd_wait_hi(layout->parent_message));
        if (depth_ok) {
            set_reg(c, 4, layout->parent_message);
            NC_POLL(c, 2);
            cd_read_call(c, layout->printf, layout->search_file + 0x2b4);
        } else {
            set_reg(c, 4, layout->depth_message);
            R(31) = layout->search_file + 0x13c;
            R(6) = R(18);
            NC_POLL(c, 2);
            nc_call(c, layout->printf);
            R(2) = 0;
            NC_POLL(c, 2);
            return false;
        }
    }
    set_reg(c, 2, 0);
    return false;
}

static void cd_file_print_search_name(CPUState *c, const CdFileLayout *layout, u32 format,
                                      u32 resume) {
    set_reg(c, 4, cd_wait_hi(format));
    set_reg(c, 4, format);
    R(31) = resume;
    R(5) = R(29) + 16;
    NC_POLL(c, 2);
    nc_call(c, layout->printf);
}

static void cd_file_find_cached_file(CPUState *c, const CdFileLayout *layout) {
    R(31) = layout->search_file + 0x180;
    MEM_W8(R(17), 0);
    NC_POLL(c, 2);
    nc_call(c, layout->cache_directory); /* CD_cachefile */
    bool scan = R(2) != 0;
    NC_POLL(c, 2);
    if (!scan) {
        load_cd_port(c, 2, layout->debug);
        NC_POLL(c, 1);
        bool report = (s32)R(2) > 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (report) {
            set_reg(c, 4, cd_wait_hi(layout->cache_message));
            set_reg(c, 4, layout->cache_message);
            cd_read_call(c, layout->printf, layout->search_file + 0x1ac);
            R(2) = 0;
            NC_POLL(c, 2);
        }
        return;
    }
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(2) < 2);
    bool verbose = !R(2);
    R(18) = 0;
    NC_POLL(c, 2);
    if (verbose)
        cd_file_print_search_name(c, layout, layout->search_message, layout->search_file + 0x1dc);
    set_reg(c, 2, cd_wait_hi(layout->files + 8));
    set_reg(c, 2, layout->files + 8);
    set_reg(c, 16, R(2) - 8); /* cached CdlFILE */
    set_reg(c, 19, R(2));     /* its identifier */
    set_reg(c, 17, 0);
    for (;;) {
        cd_file_index(c, layout->files + 8, 17);
        load_cd_byte(c, 2, R(1) + cd_file_lo(layout->files + 8));
        NC_POLL(c, 1);
        bool end = R(2) == 0;
        R(4) = R(19);
        NC_POLL(c, 2);
        if (end)
            break;
        R(31) = layout->search_file + 0x210;
        R(5) = R(29) + 16;
        NC_POLL(c, 2);
        nc_call(c, layout->names_equal); /* _cmp */
        bool match = R(2) != 0;
        NC_POLL(c, 2);
        if (match) {
            load_cd_port(c, 2, layout->debug);
            NC_POLL(c, 1);
            set_reg(c, 2, (s32)R(2) < 2);
            verbose = !R(2);
            NC_POLL(c, 2);
            if (verbose)
                cd_file_print_search_name(c, layout, layout->found_message,
                                          layout->search_file + 0x240);
            /* Preserve the two load/store groups even for overlapping buffers. */
            for (unsigned i = 0; i < 4; i++)
                load_word(c, 2 + i, R(16) + 4 * i);
            for (unsigned i = 0; i < 4; i++) {
                MEM_W32(R(21) + 4 * i, R(2 + i));
                NC_POLL(c, 1);
            }
            load_word(c, 2, R(16) + 16);
            load_word(c, 3, R(16) + 20);
            MEM_W32(R(21) + 16, R(2));
            NC_POLL(c, 1);
            MEM_W32(R(21) + 20, R(3));
            NC_POLL(c, 1);
            R(2) = R(16); /* address in the cache */
            NC_POLL(c, 2);
            return;
        }
        set_reg(c, 16, R(16) + 24);
        set_reg(c, 19, R(19) + 24);
        set_reg(c, 18, R(18) + 1);
        set_reg(c, 2, (s32)R(18) < 64);
        bool more = R(2) != 0;
        R(17) += 24;
        NC_POLL(c, 2);
        if (!more)
            break;
    }
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    bool report = (s32)R(2) > 0;
    R(5) = R(29) + 16;
    NC_POLL(c, 2);
    if (report) {
        set_reg(c, 4, cd_wait_hi(layout->missing_message));
        set_reg(c, 4, layout->missing_message);
        cd_read_call(c, layout->printf, layout->search_file + 0x2b4);
    }
    set_reg(c, 2, 0);
}

static bool cd_search_file(CPUState *c, const CdFileLayout *layout) {
    if (c->budget <= 1)
        return false;
    load_cd_port(c, 3, layout->loaded_media);
    load_cd_port(c, 2, layout->media_count);
    set_reg(c, 29, R(29) - 80);
    MEM_W32(R(29) + 68, R(21));
    NC_POLL(c, 1);
    set_reg(c, 21, R(4));
    MEM_W32(R(29) + 60, R(19));
    NC_POLL(c, 1);
    set_reg(c, 19, R(5));
    MEM_W32(R(29) + 72, R(31));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 64, R(20));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 56, R(18));
    NC_POLL(c, 1);
    MEM_W32(R(29) + 52, R(17));
    NC_POLL(c, 1);
    bool current = R(3) == R(2);
    MEM_W32(R(29) + 48, R(16));
    NC_POLL(c, 2);
    if (!current) {
        cd_read_call(c, layout->load_paths, layout->search_file + 0x44);
        current = R(2) != 0;
        R(2) = 0;
        NC_POLL(c, 2);
        if (current) {
            load_cd_port(c, 2, layout->media_count);
            cd_file_store_word(c, layout->loaded_media, 2);
        }
    }
    if (current) {
        load_cd_byte(c, 3, R(19));
        set_reg(c, 2, 92);
        bool absolute = R(3) == R(2);
        R(4) = 1;
        NC_POLL(c, 2);
        if (!absolute) {
            R(2) = 0;
            NC_POLL(c, 2);
        } else if (cd_file_find_parent_directory(c, layout)) {
            cd_file_find_cached_file(c, layout);
        }
    }
    load_word(c, 31, R(29) + 72);
    for (unsigned reg = 21; reg >= 16; reg--)
        load_word(c, reg, R(29) + 48 + (reg - 16) * 4);
    set_reg(c, 29, R(29) + 80);
    NC_POLL(c, 2);
    return true;
}

static void cd_file_cache_directory_name(CPUState *c, const CdFileLayout *layout) {
    bool first = R(18) == 0;
    R(2) = 1;
    NC_POLL(c, 2);
    if (first) {
        set_reg(c, 2, cd_wait_hi(layout->dot));
        cd_file_load_half(c, 2, layout->dot, false);
        MEM_W16(R(21) + 8, (u16)R(2)); /* "." */
        NC_POLL(c, 2);
        return;
    }
    bool parent = R(18) == R(2);
    R(4) = R(19);
    NC_POLL(c, 2);
    if (parent) {
        set_reg(c, 2, cd_wait_hi(layout->dot_dot));
        cd_file_load_half(c, 2, layout->dot_dot, true);
        set_reg(c, 3, cd_wait_hi(layout->dot_dot + 2));
        R(3) = (u32)(s32)(s8)MEM_R8(layout->dot_dot + 2);
        NC_POLL(c, NC_LOAD_CYCLES);
        MEM_W16(R(21) + 32, (u16)R(2)); /* ".." */
        NC_POLL(c, 1);
        MEM_W8(R(21) + 34, (u8)R(3));
        NC_POLL(c, 1);
        NC_POLL(c, 2);
        return;
    }
    NC_POLL(c, 2);
    load_cd_byte(c, 6, R(16) + 32);
    R(31) = layout->cache_directory + 0x188;
    R(5) = R(16) + 33;
    NC_POLL(c, 2);
    nc_call(c, layout->copy_identifier); /* copy ISO identifier */
    load_cd_byte(c, 2, R(16) + 32);
    NC_POLL(c, 1);
    set_reg(c, 2, R(19) + R(2));
    MEM_W8(R(2), 0);
    NC_POLL(c, 1);
}

static void cd_file_report_directory_entry(CPUState *c, const CdFileLayout *layout) {
    for (unsigned reg = 5; reg <= 7; reg++) {
        u32 field = layout->files + reg - 5; /* BCD minute, second, sector */
        cd_file_index(c, field, 17);
        load_cd_byte(c, reg, R(1) + cd_file_lo(field));
    }
    cd_file_index(c, layout->files + 4, 17);
    load_word(c, 2, R(1) + cd_file_lo(layout->files + 4));
    set_reg(c, 4, cd_wait_hi(layout->directory_entry));
    set_reg(c, 4, layout->directory_entry);
    MEM_W32(R(29) + 20, R(19));
    NC_POLL(c, 1);
    R(31) = layout->cache_directory + 0x1f4;
    MEM_W32(R(29) + 16, R(2));
    NC_POLL(c, 2);
    nc_call(c, layout->printf);
}

static void cd_file_cache_directory_records(CPUState *c, const CdFileLayout *layout) {
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(2) < 2);
    bool verbose = !R(2);
    R(18) = 0; /* entry count */
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 4, cd_wait_hi(layout->directory_start));
        set_reg(c, 4, layout->directory_start);
        cd_read_call(c, layout->printf, layout->cache_directory + 0xc8);
    }
    set_reg(c, 2, 1);
    NC_POLL(c, 2);
    set_reg(c, 21, cd_wait_hi(layout->files));
    set_reg(c, 21, layout->files); /* 64 cached entries of 24 bytes */
    set_reg(c, 19, R(21) + 8);
    set_reg(c, 17, 0);
    set_reg(c, 20, R(21));
    for (;;) {
        load_cd_byte(c, 2, R(16));
        NC_POLL(c, 1);
        bool end = R(2) == 0;
        NC_POLL(c, 2);
        if (end)
            break;
        cd_file_copy_directory_word(c, 2, R(16) + 2, R(29) + 24);
        load_word(c, 4, R(29) + 24);
        R(31) = layout->cache_directory + 0x118;
        R(5) = R(20);
        NC_POLL(c, 2);
        nc_call(c, layout->int_to_pos); /* convert extent sector to CD position */
        set_reg(c, 2, R(21) + 4);
        set_reg(c, 2, R(17) + R(2));
        cd_file_copy_directory_word(c, 3, R(16) + 10, R(2));
        cd_file_cache_directory_name(c, layout);
        load_cd_port(c, 2, layout->debug);
        NC_POLL(c, 1);
        set_reg(c, 2, (s32)R(2) < 2);
        verbose = !R(2);
        R(20) += 24;
        NC_POLL(c, 2);
        if (verbose)
            cd_file_report_directory_entry(c, layout);
        set_reg(c, 19, R(19) + 24);
        load_cd_byte(c, 2, R(16));
        set_reg(c, 18, R(18) + 1);
        set_reg(c, 16, R(16) + R(2));
        set_reg(c, 2, (s32)R(18) < 64);
        bool room = R(2) != 0;
        R(17) += 24;
        NC_POLL(c, 2);
        if (!room)
            break;
        set_reg(c, 2, cd_wait_hi(layout->sector + 0x800));
        set_reg(c, 2, layout->sector + 0x800);
        set_reg(c, 2, R(16) < R(2));
        bool in_sector = R(2) != 0;
        NC_POLL(c, 2);
        if (!in_sector)
            break;
    }
    cd_file_store_word(c, layout->cached_directory, 22);
    set_reg(c, 2, (s32)R(18) < 64);
    bool room = R(2) != 0;
    R(2) = R(18) << 1;
    NC_POLL(c, 2);
    if (room) {
        set_reg(c, 2, R(2) + R(18));
        set_reg(c, 2, R(2) << 3);
        cd_file_index(c, layout->files + 8, 2);
        MEM_W8(R(1) + cd_file_lo(layout->files + 8), 0); /* empty name terminates the cache */
        NC_POLL(c, 1);
    }
    load_cd_port(c, 2, layout->debug);
    NC_POLL(c, 1);
    set_reg(c, 2, (s32)R(2) < 2);
    verbose = !R(2);
    R(2) = 1;
    NC_POLL(c, 2);
    if (verbose) {
        set_reg(c, 4, cd_wait_hi(layout->directory_count));
        set_reg(c, 4, layout->directory_count);
        R(31) = layout->cache_directory + 0x274;
        R(5) = R(18);
        NC_POLL(c, 2);
        nc_call(c, layout->printf);
        set_reg(c, 2, 1);
    }
}

static bool cd_cache_directory(CPUState *c, const CdFileLayout *layout) {
    if (c->budget <= 12 + NC_LOAD_CYCLES)
        return false;
    c->budget--;
    R(2) = MEM_R32(layout->cached_directory);
    c->budget -= NC_LOAD_CYCLES;
    R(29) -= 64;
    c->budget--;
    MEM_W32(R(29) + 56, R(22));
    R(22) = R(4);
    c->budget -= 2;
    MEM_W32(R(29) + 60, R(31));
    c->budget--;
    for (unsigned reg = 21; reg >= 17; reg--) {
        MEM_W32(R(29) + 32 + 4 * (reg - 16), R(reg));
        c->budget--;
    }
    bool cached = R(22) == R(2);
    MEM_W32(R(29) + 32, R(16));
    c->budget -= 2;
    if (cached) {
        set_reg(c, 2, 1);
    } else {
        set_reg(c, 4, 1); /* read one sector from the directory table */
        set_reg(c, 2, R(22) << 1);
        set_reg(c, 2, R(2) + R(22));
        set_reg(c, 2, R(2) << 2);
        set_reg(c, 2, R(2) - R(22));
        set_reg(c, 2, R(2) << 2);
        cd_file_index(c, layout->paths + 8 - 44, 2);
        load_word(c, 5, R(1) + cd_file_lo(layout->paths + 8 - 44));
        set_reg(c, 16, cd_wait_hi(layout->sector));
        set_reg(c, 16, layout->sector);
        R(31) = layout->cache_directory + 0x68;
        R(6) = R(16);
        NC_POLL(c, 2);
        nc_call(c, layout->read_sectors);
        set_reg(c, 3, 1);
        bool read = R(2) == R(3);
        NC_POLL(c, 2);
        if (read) {
            cd_file_cache_directory_records(c, layout);
        } else {
            load_cd_port(c, 2, layout->debug);
            NC_POLL(c, 1);
            bool report = (s32)R(2) > 0;
            R(2) = UINT32_MAX;
            NC_POLL(c, 2);
            if (report) {
                set_reg(c, 4, cd_wait_hi(layout->directory_error));
                set_reg(c, 4, layout->directory_error);
                cd_read_call(c, layout->printf, layout->cache_directory + 0x98);
                R(2) = UINT32_MAX;
                NC_POLL(c, 2);
            }
        }
    }
    load_word(c, 31, R(29) + 60);
    for (unsigned reg = 22; reg >= 16; reg--)
        load_word(c, reg, R(29) + 32 + 4 * (reg - 16));
    set_reg(c, 29, R(29) + 64);
    NC_POLL(c, 2);
    return true;
}

/* Find a named child of the requested parent in the cached CD path table. */
static bool cd_search_directory(CPUState *c, const CdFileLayout *layout) {
    if (c->budget <= 13)
        return false;
    R(29) -= 40;
    c->budget--;
    MEM_W32(R(29) + 28, R(19));
    R(19) = R(4); /* parent directory number */
    c->budget -= 2;
    MEM_W32(R(29) + 32, R(20));
    R(20) = R(5); /* name to find */
    c->budget -= 2;
    MEM_W32(R(29) + 16, R(16));
    R(16) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 24, R(18));
    R(18) = layout->paths + 12; /* name in the first record */
    c->budget -= 3;
    MEM_W32(R(29) + 20, R(17));
    R(17) = 0;
    c->budget -= 2;
    MEM_W32(R(29) + 36, R(31));
    c->budget--;

    for (;;) {
        cd_file_index(c, layout->paths + 4, 17);
        load_word(c, 2, R(1) + cd_file_lo(layout->paths + 4));
        NC_POLL(c, 1);
        bool end = R(2) == 0;
        NC_POLL(c, 2);
        if (end)
            break;

        bool same_parent = R(2) == R(19);
        R(4) = R(20);
        NC_POLL(c, 2);
        if (same_parent) {
            R(31) = layout->search_directory + 0x5c;
            R(5) = R(18);
            NC_POLL(c, 2);
            nc_call(c, layout->strcmp); /* strcmp */
            bool match = R(2) == 0;
            R(18) += 44;
            NC_POLL(c, 2);
            if (match) {
                R(2) = R(16) + 1; /* directory numbers start at one */
                NC_POLL(c, 2);
                goto restore;
            }
        } else {
            set_reg(c, 18, R(18) + 44);
        }
        set_reg(c, 16, R(16) + 1);
        set_reg(c, 2, (s32)R(16) < 128);
        bool more = R(2) != 0;
        R(17) += 44;
        NC_POLL(c, 2);
        if (!more)
            break;
    }
    set_reg(c, 2, UINT32_MAX);
restore:
    load_word(c, 31, R(29) + 36);
    load_word(c, 20, R(29) + 32);
    load_word(c, 19, R(29) + 28);
    load_word(c, 18, R(29) + 24);
    load_word(c, 17, R(29) + 20);
    load_word(c, 16, R(29) + 16);
    set_reg(c, 29, R(29) + 40);
    NC_POLL(c, 2);
    return true;
}

#endif
