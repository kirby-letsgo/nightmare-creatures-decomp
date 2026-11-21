/* US PSX2.EXE gameplay state shared by controls and replay checks. */
#ifndef NC_PORT_GAME_VARS_H
#define NC_PORT_GAME_VARS_H

#define ADDR_PLAYER_HEADING 0x800CB1CCu /* u16, 65536 = full turn, turning left increases it */
#define ADDR_PLAYER_X 0x800CB170u       /* s32, world units << 8 */
#define ADDR_PLAYER_Z 0x800CB178u
#define ADDR_ADRENALINE 0x800CC012u /* s16 countdown */

#endif
