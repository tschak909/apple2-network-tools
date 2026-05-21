#ifndef PREFIX_H
#define PREFIX_H

#include <stdint.h>

#define NUM_PREFIXES  8
#define PREFIX_MAXLEN 64   /* matches fujinet-fuji MAX_APPKEY_LEN */

/* Load all prefix URLs from FujiNet appkeys into the static cache. */
void prefix_load(void);

/*
 * Return the cached URL for slot idx (0-7 → N1:-N8:).
 * Auto-loads from appkeys on first call.
 * Returns a pointer to an empty string if the slot has no prefix set.
 */
const char *prefix_get(unsigned char idx);

/* Full-screen editor for the 8 network prefixes. */
void set_prefix_screen(void);

#endif
