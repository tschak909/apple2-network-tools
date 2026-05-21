#include <conio.h>
#include <string.h>
#include <fujinet-fuji.h>
#include "screen.h"
#include "prefix.h"
#include "catalog.h"

/* ------------------------------------------------------------------ */
/* Menu definition                                                      */
/* ------------------------------------------------------------------ */

#define MENU_COUNT 2

static const char *menu_items[] = {
    "SET PREFIX",
    "CATALOG",
};

/* Version string baked in at compile time by the build system */
#ifndef GIT_VERSION
#define GIT_VERSION "dev"
#endif

/* ------------------------------------------------------------------ */
/* Drawing                                                              */
/* ------------------------------------------------------------------ */

static void draw_menu(unsigned char sel)
{
    unsigned char i;

    /* Redraw header each time so returning from a sub-screen is clean */
    screen_header("NET.TOOLS " GIT_VERSION);

    for (i = 0; i < MENU_COUNT; i++) {
        gotoxy(6, 6 + i * 2);
        if (i == sel) revers(1);
        cprintf("%-28s", menu_items[i]);
        if (i == sel) revers(0);
    }

    screen_footer("I/M:SELECT  RETURN:ENTER  ESC:QUIT");
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void main(void)
{
    unsigned char sel = 0, c;

    /* Load prefixes once at startup so catalog_screen works immediately */
    prefix_load();

    draw_menu(sel);

    for (;;) {
        c = cgetc();

        if (is_up(c) && sel > 0) {
            sel--;
            draw_menu(sel);
        } else if (is_down(c) && sel < MENU_COUNT - 1) {
            sel++;
            draw_menu(sel);
        } else if (c == KEY_ENTER) {
            switch (sel) {
                case 0: set_prefix_screen(); break;
                case 1: catalog_screen();    break;
            }
            draw_menu(sel);   /* redraw after returning from sub-screen */
        } else if (c == KEY_ESC) {
            clrscr();
            break;
        }
    }
}
