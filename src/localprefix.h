#ifndef LOCALPREFIX_H
#define LOCALPREFIX_H

#include <stdint.h>

/*
 * 256-byte buffer populated by get_online_volumes().
 * Contains up to 16 entries of 16 bytes each, terminated by a zero byte:
 *
 *   Byte 0  bit  7:    drive  (0 = D1, 1 = D2)
 *   Byte 0  bits 6-4:  slot   (1-7)
 *   Byte 0  bits 3-0:  name_len (0 = not online / error)
 *   Bytes 1..name_len: volume name (not null-terminated)
 */
extern uint8_t online_buf[];

/* Scan all ProDOS devices via ON_LINE ($C5). Returns count of online volumes. */
uint8_t get_online_volumes(void);

/*
 * Set ProDOS system prefix via SET_PREFIX ($C6).
 * path: Pascal string — byte 0 is the length, bytes 1..n are path chars.
 * Example: {9, '/', 'V', 'O', 'L', 'N', 'A', 'M', 'E', '/'}
 */
void __fastcall__ set_local_prefix(const char *path);

/*
 * Get ProDOS system prefix via GET_PREFIX ($C7).
 * buf must be at least 34 bytes; filled as a Pascal string.
 */
void __fastcall__ get_local_prefix(char *buf);

/*
 * Create a ProDOS directory via CREATE ($C0) with storage_type=$0D.
 * pascal_path: Pascal string.  Returns 0 on success, error code on failure.
 */
uint8_t __fastcall__ prodos_mkdir(const char *pascal_path);

/* Screen: list online ProDOS volumes and let user set the system prefix. */
void local_prefix_screen(void);

#endif /* LOCALPREFIX_H */
