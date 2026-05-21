#ifndef CATALOG_H
#define CATALOG_H

/*
 * Let the user pick a prefix slot (0-7).
 * Draws the prefix list and returns the chosen slot on RETURN,
 * or 255 on ESC/cancel.
 */
unsigned char select_prefix(void);

/* Display a prefix selector then browse the chosen network directory. */
void catalog_screen(void);

#endif
