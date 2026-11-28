/* PSX.EXE (front end) copies of the Psy-Q libcd routines that the launcher and
 * CREDITS.EXE also link, through the shared layout-driven implementations. */

#include "decomp/psx_cd.h"
#include "decomp/cd_file.h"
#include "decomp/cd_read_retry.h"

/* CD_sync, CD_ready, CD_cw and CD_datasync share the timeout reporting. */
static const CdWaitLayout psx_cd_wait = {
    .deadline = 0x80027e3c,
    .command_names = 0x80027890,
    .status = 0x80027b48,
    .timeout_message = 0x80018aa0,
    .report_format = 0x80018ab0,
    .vsync = 0x8001b9bc,
    .puts = 0x8001bba0,
    .print = 0x8001a640,
    .flush = 0x800240d8,
    .ports = 0x80027b30,
    .callbacks = 0x8002786c,
    .results = 0x80027e24,
    .getintr = 0x80023174,
    .check_callback = 0x8001bd54,
    .sync_name = 0x80018b28,
    .ready_name = 0x80018b30,
    .error_message = 0x80018acc,
    .error_format = 0x80018ad8,
    .unknown_message = 0x80018af4,
    .unknown_format = 0x80018b08,
    .irq_table = 0x80018b14,
    .writer = 0x80023c38,
    .sync = 0x800236ec,
    .verbose_format = 0x80018b3c,
    .missing_format = 0x80018b44,
    .command_name = 0x80018b54,
    .dma_control = 0x80027b74,
    .dma_name = 0x80018ba8,
};

bool nc_psx_cd_sync(CPUState *c) {
    return sync_cd_command(c, &psx_cd_wait, 0x800236ec);
}

bool nc_psx_cd_ready(CPUState *c) {
    return wait_cd_ready(c, &psx_cd_wait, 0x8002396c);
}

bool nc_psx_cd_write_command(CPUState *c) {
    return write_cd_command(c, &psx_cd_wait);
}

bool nc_psx_cd_data_sync(CPUState *c) {
    static const CdWaitSite site = {0x80024550, 0x800245a0, 0x800245f0, 0x800245f8, 17, 16, true};
    return sync_cd_dma(c, &psx_cd_wait, &site);
}

static const CdReadRetryLayout psx_read_retry = {
    .entry = 0x80025518,
    .state = 0x80027b84,
    .set_sync = 0x80022b8c,
    .set_ready = 0x80022ba4,
    .status = 0x80022a00,
    .vsync = 0x8001b9bc,
    .lid_message = 0x80018db8,
    .puts = 0x8001bba0,
    .fast = 0x80022cf4,
    .retry_message = 0x80018dd0,
    .control = 0x80022bbc,
    .last_pos = 0x80022a30,
    .flush = 0x80022aac,
    .mode = 0x80022a10,
    .pos_to_int = 0x800230f4,
    .callback = 0x800252f4,
    .get_sector = 0x80022f88,
    .mismatch_message = 0x80018da0,
};

bool nc_psx_cd_read_callback(CPUState *c) {
    return handle_cd_read(c, &psx_read_retry);
}

bool nc_psx_cd_read_retry(CPUState *c) {
    return cd_read_retry(c, &psx_read_retry);
}

static const CdFileLayout psx_cd_file = {
    .search_file = 0x80024844,
    .load_paths = 0x80024b48,
    .search_directory = 0x80024e24,
    .cache_directory = 0x80024ecc,
    .printf = 0x8001a640,
    .int_to_pos = 0x80022ff0,
    .names_equal = 0x80024b24,
    .read_sectors = 0x80025170,
    .copy_identifier = 0x800251dc,
    .strcmp = 0x80025210,
    .compare_bytes = 0x80025274,
    .debug = 0x80027878,
    .media_count = 0x80027884,
    .loaded_media = 0x80027b7c,
    .cached_directory = 0x80027b78,
    .files = 0x80027e48,
    .paths = 0x80028448,
    .sector = 0x80029a48,
    .depth_message = 0x80018bb4,
    .parent_message = 0x80018bd0,
    .cache_message = 0x80018be8,
    .search_message = 0x80018c04,
    .found_message = 0x80018c24,
    .missing_message = 0x80018c30,
    .volume_error = 0x80018c40,
    .iso_id = 0x80018c6c,
    .iso_error = 0x80018c74,
    .path_error = 0x80018ca4,
    .path_start = 0x80018cc8,
    .path_entry = 0x80018ce8,
    .path_count = 0x80018cfc,
    .directory_error = 0x80018d20,
    .directory_start = 0x80018d40,
    .dot = 0x80018d5c,
    .dot_dot = 0x80018d60,
    .directory_entry = 0x80018d64,
    .directory_count = 0x80018d80,
};

bool nc_psx_cd_search_file(CPUState *c) {
    return cd_search_file(c, &psx_cd_file);
}

bool nc_psx_cd_load_paths(CPUState *c) {
    return cd_load_paths(c, &psx_cd_file);
}

bool nc_psx_cd_search_directory(CPUState *c) {
    return cd_search_directory(c, &psx_cd_file);
}

bool nc_psx_cd_cache_directory(CPUState *c) {
    return cd_cache_directory(c, &psx_cd_file);
}
