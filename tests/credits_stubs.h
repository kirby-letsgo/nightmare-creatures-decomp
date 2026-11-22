/* Oracle tests supply only their chosen CREDITS.EXE callees. */
#ifndef NC_TESTS_CREDITS_STUBS_H
#define NC_TESTS_CREDITS_STUBS_H

#include "port/recomp.h"

#define EMPTY_MODULE(name)                                                                         \
    const NcFuncEntry name##_table[1];                                                             \
    const unsigned name##_table_len = 0
EMPTY_MODULE(slus_005_82);
EMPTY_MODULE(psx_exe);
EMPTY_MODULE(psx2_exe);
EMPTY_MODULE(stream1_exe);
EMPTY_MODULE(stream2_exe);
EMPTY_MODULE(stream3_exe);
#undef EMPTY_MODULE

#endif
