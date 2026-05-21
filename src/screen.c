#include <conio.h>
#include <string.h>
#include "screen.h"

void screen_header(const char *title)
{
    unsigned char len, x;

    clrscr();
    chlinexy(0, 0, SCREEN_W);
    len = (unsigned char)strlen(title);
    x   = (SCREEN_W - len) / 2;
    gotoxy(x, 1);
    cputs(title);
    chlinexy(0, 2, SCREEN_W);
}

void screen_footer(const char *hint)
{
    chlinexy(0, 22, SCREEN_W);
    gotoxy(0, 23);
    cclear(SCREEN_W);
    gotoxy(0, 23);
    cputs(hint);
}

void screen_msg(unsigned char row, const char *msg)
{
    gotoxy(0, row);
    cclear(SCREEN_W);
    gotoxy(0, row);
    cputs(msg);
}
