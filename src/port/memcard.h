/* Memory cards stored as 128 KiB raw images (card1.mcd / card2.mcd) in the user data folder. */
#ifndef NC_PORT_MEMCARD_H
#define NC_PORT_MEMCARD_H

#include "port/recomp.h"

#include <stdbool.h>

/* dir must end with a path separator. Slot 1 is created (formatted) if missing. */
void memcard_init(const char *dir);
bool memcard_present(int port);

/* Raw 128-byte frame access (BIOS _card_read/_card_write). */
bool memcard_read_frame(int port, u32 frame, u8 out[128]);
bool memcard_write_frame(int port, u32 frame, const u8 in[128]);

/* Filesystem used by BIOS open/read/write/firstfile/erase on "bu00:" / "bu10:". */
int memcard_file_size(int port, const char *name);
bool memcard_create(int port, const char *name, u32 blocks);
bool memcard_erase(int port, const char *name);
int memcard_rw(int port, const char *name, u32 pos, u8 *buf, u32 len, bool write);
/* Finds the next file matching pattern in a block after `after` (start with 0). Returns the
 * block number or -1. */
int memcard_find(int port, const char *pattern, int after, char name_out[21], u32 *size_out);
int memcard_free_blocks(int port);
void memcard_format(int port);

#endif
