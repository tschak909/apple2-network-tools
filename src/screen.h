#ifndef NET_TOOLS_SCREEN_H
#define NET_TOOLS_SCREEN_H

#define SCREEN_W  40

/*
 * Apple II key codes as returned by cgetc() (cc65 strips bit 7).
 * Arrow keys exist on IIe and later; IJKM are accepted as fallback
 * for Apple II and II+.
 */
#define KEY_UP    0x0B   /* up arrow    (^K) */
#define KEY_DOWN  0x0A   /* down arrow  (^J) */
#define KEY_LEFT  0x08   /* left arrow  (^H) */
#define KEY_RIGHT 0x15   /* right arrow (^U) */
#define KEY_ENTER 0x0D
#define KEY_ESC   0x1B
#define KEY_DEL   0x7F   /* delete */

#define is_up(c)    ((c) == KEY_UP    || (c) == 'I')
#define is_down(c)  ((c) == KEY_DOWN  || (c) == 'M')
#define is_left(c)  ((c) == KEY_LEFT  || (c) == 'J')
#define is_right(c) ((c) == KEY_RIGHT || (c) == 'K')

/* Draw centred title between separator lines at rows 0-2, clear screen first */
void screen_header(const char *title);

/* Draw separator + hint line at rows 22-23 */
void screen_footer(const char *hint);

/* Write a status message at the given row, clearing the line first */
void screen_msg(unsigned char row, const char *msg);

#endif /* NET_TOOLS_SCREEN_H */
