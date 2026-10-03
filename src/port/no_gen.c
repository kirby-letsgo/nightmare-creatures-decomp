/* Empty function tables for builds without recompiled code (gen/): everything is interpreted. */
#include "port/recomp.h"

#define NC_EMPTY_TABLE(mod)                                                                        \
    const NcFuncEntry mod##_table[1];                                                              \
    const unsigned mod##_table_len = 0;
NC_EMPTY_TABLE(slus_005_82)
NC_EMPTY_TABLE(psx_exe)
NC_EMPTY_TABLE(psx2_exe)
NC_EMPTY_TABLE(credits_exe)
NC_EMPTY_TABLE(stream1_exe)
NC_EMPTY_TABLE(stream2_exe)
NC_EMPTY_TABLE(stream3_exe)
