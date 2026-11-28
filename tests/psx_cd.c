#include "cd_events.h"
#include "cd_dma_wait.h"
#include "cd_file.h"
#include "cd_read.h"
#include "decomp/psx_cd.h"
#include "port/bios.h"
#include "port/disc.h"
#include "port/exe.h"
#include "port/hw/hw.h"

#include <stdio.h>

/* PSX.EXE addresses of the same libcd objects CREDITS.EXE links (tests/credits_sdk.c). */
static const CdEventFixture psx_events = {
    .vsync = 0x8001b9bc, .check_callback = 0x8001bd54, .getintr = 0x80023174,
    .puts = 0x8001bba0, .print = 0x8001a640, .flush = 0x800240d8,
    .ports = 0x80027b30, .callbacks = 0x8002786c, .results = 0x80027e24,
    .deadline = 0x80027e3c, .status = 0x80027b48, .ready = 0x8002396c,
    .sync = 0x800236ec,
    .writer = 0x80023c38,
};

static const CdRetryFixture psx_retry = {
    0x80025518, 0x80027b84, 0x800252f4, 0x80022bbc,
    0x80022cf4, 0x8001bba0, 0x80022aac, 0x80018db8,
    0x80018dd0, 0x80025d88, 0x80026e5c, 0x8002786c,
    0x80022f88, 0x80018da0, 0x8001b9bc,
};

static const CdDmaWaitFixture psx_dma_wait = {
    0x800244e8, 0x8001b9bc, 0x8001bba0, 0x8001a640, 0x800240d8,
    0x80027b74, 0x80027890, 0x80027b48, 0x80027e3c, 0x80018ba8,
};

static const CdFileFixture psx_file = {
    .search_file = 0x80024844, .load_paths = 0x80024b48, .search_directory = 0x80024e24,
    .cache_directory = 0x80024ecc, .printf = 0x8001a640, .read_sectors = 0x80025170,
    .debug = 0x80027878, .media_count = 0x80027884, .loaded_media = 0x80027b7c,
    .cached_directory = 0x80027b78, .files = 0x80027e48, .paths = 0x80028448,
    .sector = 0x80029a48,
};

static const struct {
    const char *name;
    bool (*routine)(CPUState *);
    int threshold; /* largest budget the routine declines */
} routines[] = {
    {"CD_sync", nc_psx_cd_sync, 11},
    {"CD_ready", nc_psx_cd_ready, 12},
    {"CD_cw", nc_psx_cd_write_command, 1},
    {"CD_datasync", nc_psx_cd_data_sync, 9},
    {"cb_read", nc_psx_cd_read_callback, 8},
    {"cd_read_retry", nc_psx_cd_read_retry, 7},
    {"CdSearchFile", nc_psx_cd_search_file, 1},
    {"CD_newmedia", nc_psx_cd_load_paths, 12},
    {"CD_searchdir", nc_psx_cd_search_directory, 13},
    {"CD_cachefile", nc_psx_cd_cache_directory, 12 + NC_LOAD_CYCLES},
};

/* Without the disc: a budget too small for the first block leaves the machine
 * untouched, so the dispatcher can interpret the routine instead. */
static bool check_declines(void) {
    for (unsigned i = 0; i < sizeof routines / sizeof routines[0]; i++)
        for (int budget = -1; budget <= routines[i].threshold; budget++) {
            StateIO before = {0}, after = {0};
            nc_cpu = arguments(0x80061000, 0x80062000, 3, budget);
            cpu_serialize(&before);
            bool ran = routines[i].routine(&nc_cpu);
            cpu_serialize(&after);
            bool ok = !ran && !before.error && !after.error && before.len == after.len &&
                      !memcmp(before.buf, after.buf, before.len);
            free(before.buf);
            free(after.buf);
            if (!ok) {
                fprintf(stderr, "%s ran or changed state with budget %d\n", routines[i].name,
                        budget);
                return false;
            }
        }
    return true;
}

int main(int argc, char **argv) {
    if (argc == 1)
        return check_declines() ? 0 : 1;
    if (argc != 2)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 77;
    fclose(file);
    ExecInfo exe;
    if (!disc_open(argv[1]) || !exe_load("cdrom:\\PSX.EXE;1", &exe))
        return 1;
    bool pass = true;
#define CHECK(name, test)                                                                          \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            fprintf(stderr, "%s failed\n", name);                                                  \
            pass = false;                                                                          \
        }                                                                                          \
    } while (0)
    CHECK("CD_sync", check_cd_command_sync(&psx_events, nc_psx_cd_sync));
    CHECK("CD_ready", check_cd_ready_driver(&psx_events, nc_psx_cd_ready));
    CHECK("CD_cw", check_write_cd_command(&psx_events, nc_psx_cd_write_command));
    CHECK("CD_datasync", compare_cd_dma_wait(nc_psx_cd_data_sync, &psx_dma_wait));
    CHECK("cd_read_retry", compare_cd_read_retry(&psx_retry, nc_psx_cd_read_retry));
    CHECK("cb_read", compare_sector_ready(&psx_retry, nc_psx_cd_read_callback));
    CHECK("CD_newmedia", check_cd_path_table(&psx_file, nc_psx_cd_load_paths));
    CHECK("CdSearchFile", check_cd_search_file(&psx_file, nc_psx_cd_search_file));
    CHECK("CD_cachefile", check_directory_cache(&psx_file, nc_psx_cd_cache_directory));
    CHECK("CD_searchdir", check_cd_search_directory(&psx_file, nc_psx_cd_search_directory));
    CHECK("declines", check_declines());
#undef CHECK
    printf("psx_cd: %s\n", pass ? "ok" : "FAILED");
    return pass ? 0 : 1;
}
