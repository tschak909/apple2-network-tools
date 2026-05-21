#include <conio.h>
#include <string.h>
#include <fujinet-fuji.h>
#include "prefix.h"
#include "screen.h"

/*
 * Appkey identity for NET.TOOLS.
 * creator_id must be non-zero; 0x4E54 = 'NT' (NetTools).
 */
#define AK_CREATOR  0x4E54
#define AK_APP      0x01

static char          prefixes[NUM_PREFIXES][PREFIX_MAXLEN];
static unsigned char loaded = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static void appkey_setup(void)
{
    fuji_set_appkey_details(AK_CREATOR, AK_APP, DEFAULT);
}

static void prefix_save(unsigned char idx)
{
    uint16_t len = (uint16_t)strlen(prefixes[idx]);
    appkey_setup();
    fuji_write_appkey(idx, len, (uint8_t *)prefixes[idx]);
}

/*
 * Draw a single row in the prefix list.
 * sel is the currently highlighted index; this item is highlighted
 * only when idx == sel.
 */
static void draw_item(unsigned char idx, unsigned char sel)
{
    char disp[35];   /* 34 visible chars + null */

    gotoxy(0, 4 + idx);
    if (idx == sel) revers(1);

    strncpy(disp, prefixes[idx], 34);
    disp[34] = 0;

    cprintf("N%c: %-34s", '1' + idx,
            prefixes[idx][0] ? disp : "(EMPTY)");

    if (idx == sel) revers(0);
}

/*
 * Simple single-line editor.
 * Displays the current value of prefixes[idx] for editing.
 * Saves to appkey on Enter; discards on ESC.
 * Uses rows 13-15 for the edit area (below the 8-item prefix list).
 */
static void edit_prefix(unsigned char idx)
{
    char         buf[PREFIX_MAXLEN];
    unsigned char pos, saved, c;

    strncpy(buf, prefixes[idx], PREFIX_MAXLEN - 1);
    buf[PREFIX_MAXLEN - 1] = 0;
    pos   = (unsigned char)strlen(buf);
    saved = 0;

    /* Edit prompt */
    gotoxy(0, 13);
    cprintf("EDIT N%c: (RET=SAVE ESC=CANCEL)  ", '1' + idx);
    gotoxy(0, 14);
    cclear(SCREEN_W);
    gotoxy(0, 14);
    cputs(buf);
    gotoxy(0, 15);
    cclear(SCREEN_W);

    cursor(1);

    for (;;) {
        gotoxy(pos, 14);
        c = cgetc();

        if (c == KEY_ENTER) {
            saved = 1;
            break;
        } else if (c == KEY_ESC) {
            break;
        } else if ((c == KEY_LEFT || c == KEY_DEL) && pos > 0) {
            /* backspace: erase the character behind the cursor */
            pos--;
            buf[pos] = 0;
            gotoxy(pos, 14);
            cputc(' ');
        } else if (c >= 0x20 && c < 0x7F && pos < PREFIX_MAXLEN - 1) {
            buf[pos] = c;
            pos++;
            buf[pos] = 0;
            cputc(c);
        }
    }

    cursor(0);

    if (saved) {
        strncpy(prefixes[idx], buf, PREFIX_MAXLEN - 1);
        prefixes[idx][PREFIX_MAXLEN - 1] = 0;
        prefix_save(idx);
    }

    /* Clear the edit rows */
    for (c = 13; c <= 15; c++) {
        gotoxy(0, c);
        cclear(SCREEN_W);
    }
}

/* ------------------------------------------------------------------ */
/* Public interface                                                     */
/* ------------------------------------------------------------------ */

void prefix_load(void)
{
    unsigned char i;
    uint16_t      count;

    appkey_setup();
    for (i = 0; i < NUM_PREFIXES; i++) {
        memset(prefixes[i], 0, PREFIX_MAXLEN);
        fuji_read_appkey(i, &count, (uint8_t *)prefixes[i]);
        /* fuji_read_appkey zero-fills the buffer first, so no explicit
           null-termination is needed, but guard anyway */
        prefixes[i][PREFIX_MAXLEN - 1] = 0;
    }
    loaded = 1;
}

const char *prefix_get(unsigned char idx)
{
    if (!loaded) prefix_load();
    return prefixes[idx];
}

void set_prefix_screen(void)
{
    unsigned char sel = 0, prev, i, c;

    prefix_load();   /* always re-read from appkeys for fresh data */

    screen_header("SET NETWORK PREFIX");
    screen_footer("I/M:SELECT  RET:EDIT  ESC:BACK");

    for (i = 0; i < NUM_PREFIXES; i++)
        draw_item(i, sel);

    for (;;) {
        c = cgetc();

        if (is_up(c) && sel > 0) {
            prev = sel--;
            draw_item(prev, sel);
            draw_item(sel,  sel);
        } else if (is_down(c) && sel < NUM_PREFIXES - 1) {
            prev = sel++;
            draw_item(prev, sel);
            draw_item(sel,  sel);
        } else if (c == KEY_ENTER) {
            edit_prefix(sel);
            draw_item(sel, sel);   /* refresh after edit */
        } else if (c == KEY_ESC) {
            break;
        }
    }
}
