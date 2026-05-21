#include <conio.h>
#include <string.h>
#include "localprefix.h"
#include "screen.h"

#define MAX_VOLS 14     /* ProDOS max: slots 1-7 x drives 1-2 */

static unsigned char vol_idx[MAX_VOLS]; /* entry index (0-15) per online vol */
static unsigned char vol_count;

static void draw_vol_item(unsigned char i, unsigned char sel)
{
    unsigned char off   = vol_idx[i] * 16;
    unsigned char b     = online_buf[off];
    unsigned char nlen  = b & 0x0F;
    unsigned char slot  = (b >> 4) & 0x07;
    unsigned char drv   = (b >> 7) & 0x01;
    char          name[16];

    memcpy(name, online_buf + off + 1, nlen);
    name[nlen] = 0;

    gotoxy(0, 4 + i);
    if (i == sel) revers(1);
    cprintf("S%c,D%c  /%-32s", '0' + slot, '1' + drv, name);
    if (i == sel) revers(0);
}

void local_prefix_screen(void)
{
    static char   pfx[20];  /* Pascal: 1 len + 1 '/' + 15 name + 1 '/' */
    unsigned char sel = 0, prev, i, c;
    unsigned char off, b, nlen;

    screen_header("SET LOCAL PREFIX");
    screen_footer("I/M:SELECT  RET:SET PREFIX  ESC:BACK");
    screen_msg(20, "SCANNING DEVICES...");

    get_online_volumes();
    screen_msg(20, "");

    /* Build vol_idx: collect entry indices where name_len > 0 */
    vol_count = 0;
    for (i = 0; i < 16 && vol_count < MAX_VOLS; i++) {
        b = online_buf[i * 16];
        if (b == 0) break;          /* terminator */
        if ((b & 0x0F) > 0)
            vol_idx[vol_count++] = i;
    }

    if (vol_count == 0) {
        screen_msg(20, "NO ONLINE VOLUMES FOUND");
        screen_msg(21, "PRESS ANY KEY");
        cgetc();
        return;
    }

    for (i = 0; i < vol_count; i++)
        draw_vol_item(i, sel);

    for (;;) {
        c = cgetc();

        if (is_up(c) && sel > 0) {
            prev = sel--;
            draw_vol_item(prev, sel);
            draw_vol_item(sel,  sel);
        } else if (is_down(c) && sel < vol_count - 1) {
            prev = sel++;
            draw_vol_item(prev, sel);
            draw_vol_item(sel,  sel);
        } else if (c == KEY_ENTER) {
            off  = vol_idx[sel] * 16;
            b    = online_buf[off];
            nlen = b & 0x0F;
            /* Build Pascal path string: /VOLNAME/ */
            pfx[0] = (char)(nlen + 2); /* length = '/' + name + '/' */
            pfx[1] = '/';
            memcpy(pfx + 2, online_buf + off + 1, nlen);
            pfx[nlen + 2] = '/';
            set_local_prefix(pfx);
            screen_msg(20, "PREFIX SET");
            cgetc();
            return;
        } else if (c == KEY_ESC) {
            return;
        }
    }
}
