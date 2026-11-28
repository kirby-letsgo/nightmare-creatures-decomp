#ifndef NC_DECOMP_PSX_CD_H
#define NC_DECOMP_PSX_CD_H

#include "port/recomp.h"

/* Psy-Q libcd in PSX.EXE (src/decomp/psx_cd.c). */
bool nc_psx_cd_sync(CPUState *c);             /* CD_sync */
bool nc_psx_cd_ready(CPUState *c);            /* CD_ready */
bool nc_psx_cd_write_command(CPUState *c);    /* CD_cw */
bool nc_psx_cd_data_sync(CPUState *c);        /* CD_datasync */
bool nc_psx_cd_read_callback(CPUState *c);    /* cb_read */
bool nc_psx_cd_read_retry(CPUState *c);       /* cd_read_retry */
bool nc_psx_cd_search_file(CPUState *c);      /* CdSearchFile */
bool nc_psx_cd_load_paths(CPUState *c);       /* CD_newmedia */
bool nc_psx_cd_search_directory(CPUState *c); /* CD_searchdir */
bool nc_psx_cd_cache_directory(CPUState *c);  /* CD_cachefile */

#endif
