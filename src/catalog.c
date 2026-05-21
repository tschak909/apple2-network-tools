#include <conio.h>
#include <string.h>
#include <fujinet-network.h>
/* Low-level SmartPort symbols from fujinet-apple2.lib */
#define SP_PAYLOAD_SIZE 512
#define SP_ERR_OK       0x00
extern unsigned char sp_network;
extern unsigned char sp_payload[];
int8_t sp_read(unsigned char dest, unsigned int len);
#include "catalog.h"
#include "prefix.h"
#include "screen.h"

/*
 * Directory entries are stored in a flat static array so we avoid
 * stack pressure from nested directory reads.  On return from a
 * sub-directory we simply reload the parent path.
 */
#define MAX_ENTRIES  100
#define ENTRY_LEN    38    /* 37 visible chars (filename + right-justified size) + NUL */
#define VIS_ROWS     16    /* rows 4-19 used for listing */

static char          entries[MAX_ENTRIES][ENTRY_LEN];
static unsigned char entry_count;

/* Current devicespec being browsed, e.g. "N1:TNFS://server/path/" */
static char cur_spec[96];

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Build cur_spec from slot (0-7) and subpath ("" or "DIR/SUBDIR/"). */
static void build_spec(unsigned char slot, const char *subpath)
{
    const char *base = prefix_get(slot);

    cur_spec[0] = 'N';
    cur_spec[1] = '1' + slot;
    cur_spec[2] = ':';
    cur_spec[3] = 0;
    strncat(cur_spec, base,    sizeof(cur_spec) - 4);
    strncat(cur_spec, subpath, sizeof(cur_spec) - strlen(cur_spec) - 1);
    strncat(cur_spec, "*",     sizeof(cur_spec) - strlen(cur_spec) - 1);
}

/*
 * Append directory-entry bytes from buf into the entries[] array,
 * using partial[] to accumulate across chunk boundaries.
 */
static void parse_chunk(const char *buf, unsigned int len,
                         char *partial, unsigned char *plen)
{
    unsigned int  i;
    unsigned char ch;

    for (i = 0; i < len; i++) {
        ch = (unsigned char)buf[i];

        if (ch == 0x0D || ch == 0x0A || ch == 0x00 || ch == 0x9B) {
            /* trim trailing spaces before storing */
            while (*plen > 0 && partial[*plen - 1] == ' ') (*plen)--;
            /*
             * Flush non-empty entries that start with a real character.
             * 0x9B separates the two lines of a long (>30 char) filename:
             * line 1 has the name, line 2 has only spaces + size — skip
             * line 2 by ignoring entries whose first byte is ' '.
             */
            if (*plen > 0 && partial[0] != ' ' && entry_count < MAX_ENTRIES) {
                partial[*plen] = 0;
                strncpy(entries[entry_count], partial, ENTRY_LEN - 1);
                entries[entry_count][ENTRY_LEN - 1] = 0;
                entry_count++;
            }
            *plen = 0;
        } else if (*plen < ENTRY_LEN - 1) {
            partial[(*plen)++] = (char)ch;
        }
    }
}

/*
 * Open cur_spec as a directory (trans=128 → long filenames on FujiNet),
 * read all entries into entries[], close device.
 * Returns non-zero on success.
 */
static unsigned char load_directory(void)
{
    static char     partial[ENTRY_LEN];
    unsigned char   plen = 0;

    entry_count = 0;

    screen_msg(20, "OPENING...");

    if (network_open(cur_spec, 6, 128) != FN_ERR_OK) {
        screen_msg(20, "OPEN FAILED - CHECK PREFIX");
        return 0;
    }

    screen_msg(20, "READING DIRECTORY...");

    /*
     * Loop mirroring the nsh apple2-cli pattern:
     *   status -> sp_read bw bytes -> repeat until bw == 0.
     *
     * network_status() triggers NetworkProtocol::status() which calls
     * read(available()), moving all dirBuffer data into receiveBuffer.
     * sp_read() issues a raw SmartPort READ with no internal STATUS call,
     * so it reads from receiveBuffer without seeing the err=136 that both
     * network_read() and network_read_nb() would bail on.
     */
    {
        uint16_t bw;
        uint8_t  conn, err;
        uint16_t to_read;

        do {
            if (network_status(cur_spec, &bw, &conn, &err) != FN_ERR_OK)
                break;
            if (bw == 0)
                break;

            to_read = (bw < (uint16_t)SP_PAYLOAD_SIZE) ? bw : (uint16_t)SP_PAYLOAD_SIZE;
            if (sp_read(sp_network, to_read) != SP_ERR_OK)
                break;

            parse_chunk((const char *)sp_payload, (unsigned int)to_read,
                        partial, &plen);
        } while (entry_count < MAX_ENTRIES);
    }

    /* flush any trailing partial entry that arrived without a delimiter */
    if (plen > 0 && entry_count < MAX_ENTRIES) {
        partial[plen] = 0;
        strncpy(entries[entry_count], partial, ENTRY_LEN - 1);
        entries[entry_count][ENTRY_LEN - 1] = 0;
        entry_count++;
    }

    network_close(cur_spec);
    screen_msg(20, "");
    return entry_count;
}

/*
 * Print entry idx in inverse video, uppercasing every character first.
 * The Apple II inverse character set has no lowercase glyphs: the firmware
 * maps characters via (char & 0x3F), so lowercase letters (0x61-0x7A) land
 * in the inverse punctuation/digit range instead of inverse letters.
 */
static void put_entry_inverse(unsigned char idx)
{
    unsigned char i;
    unsigned char n = (unsigned char)strlen(entries[idx]);
    char ch;

    revers(1);
    for (i = 0; i < SCREEN_W; i++) {
        ch = (i < n) ? entries[idx][i] : ' ';
        if (ch >= 'a' && ch <= 'z') ch -= 0x20;
        cputc(ch);
    }
    revers(0);
}

/* Redraw the full visible window of directory entries. */
static void draw_listing(unsigned char top, unsigned char sel)
{
    unsigned char i;

    for (i = 0; i < VIS_ROWS; i++) {
        gotoxy(0, 4 + i);
        if (top + i < entry_count) {
            if (top + i == sel)
                put_entry_inverse(top + i);
            else
                cprintf("%-40s", entries[top + i]);
        } else {
            cclear(SCREEN_W);
        }
    }
}

/* Redraw a single entry row without touching the rest of the screen. */
static void draw_entry_row(unsigned char idx, unsigned char top,
                            unsigned char sel)
{
    gotoxy(0, 4 + (idx - top));
    if (idx == sel)
        put_entry_inverse(idx);
    else
        cprintf("%-40s", entries[idx]);
}

/* Remove the last path component from a slash-terminated path string. */
static void trim_last_dir(char *path)
{
    int len = (int)strlen(path);
    if (len == 0) return;
    if (path[len - 1] == '/') len--;   /* skip trailing '/' */
    while (len > 0 && path[len - 1] != '/') len--;
    path[len] = 0;
}

/*
 * True if the filename portion of a formatted entry ends with '/'.
 * The firmware left-justifies the filename; the size and trailing space
 * follow, so we scan left-to-right to find where the name ends.
 */
static unsigned char is_dir_entry(const char *e)
{
    unsigned char i = 0;
    while (e[i] && e[i] != ' ') i++;
    return (i > 0 && e[i - 1] == '/');
}

/* Copy just the filename portion (up to the first space) out of an entry. */
static void copy_filename(const char *e, char *out, unsigned char maxlen)
{
    unsigned char i = 0;
    while (e[i] && e[i] != ' ' && i < maxlen - 1) {
        out[i] = e[i];
        i++;
    }
    out[i] = 0;
}

/* Build devicespec for a filesystem op: cur_spec (strip trailing '*') + name. */
static void build_op_spec(char *out, unsigned char outlen, const char *name)
{
    unsigned char base_len;
    strncpy(out, cur_spec, outlen - 1);
    out[outlen - 1] = 0;
    base_len = (unsigned char)strlen(out);
    if (base_len > 0 && out[base_len - 1] == '*')
        out[--base_len] = 0;
    strncat(out, name, (unsigned char)(outlen - base_len - 1));
}

/*
 * Draw a centered input dialog (rows 9-12) and read a string from the user.
 * Returns 1 on RETURN with non-empty input, 0 on ESC.
 * maxlen must be <= 30 to fit the dialog field.
 */
static unsigned char prompt_string(const char *title, char *out, unsigned char maxlen)
{
    unsigned char len = 0;
    unsigned char c;

    out[0] = 0;
    gotoxy(0, 9);  cprintf("+--------------------------------------+");
    gotoxy(0, 10); cprintf("| %-36s |", title);
    gotoxy(0, 11); cprintf("| NAME: %-30s |", "");
    gotoxy(0, 12); cprintf("+--------------------------------------+");

    for (;;) {
        gotoxy(8, 11);
        cprintf("%-30s", out);
        gotoxy(8 + len, 11);

        c = cgetc();
        if (c == KEY_ENTER) {
            if (len > 0) return 1;
        } else if (c == KEY_ESC) {
            return 0;
        } else if ((c == KEY_DEL || c == KEY_LEFT) && len > 0) {
            out[--len] = 0;
        } else if (c >= 0x20 && c < 0x7F && len < maxlen) {
            out[len++] = (char)c;
            out[len] = 0;
        }
    }
}

/* Build a display title from cur_spec that fits in SCREEN_W chars. */
static void make_title(char *title)
{
    unsigned char len;
    strncpy(title, cur_spec, SCREEN_W - 1);
    title[SCREEN_W - 1] = 0;
    len = (unsigned char)strlen(cur_spec);
    if (len > SCREEN_W - 1) {
        /* show the last (SCREEN_W-2) chars with a leading '<' */
        title[0] = '<';
        strncpy(title + 1, cur_spec + len - (SCREEN_W - 2), SCREEN_W - 2);
        title[SCREEN_W - 1] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Prefix selector                                                      */
/* ------------------------------------------------------------------ */

static void draw_prefix_item(unsigned char idx, unsigned char sel)
{
    const char *url = prefix_get(idx);
    char        disp[35];

    gotoxy(0, 4 + idx);
    if (idx == sel) revers(1);

    strncpy(disp, url, 34);
    disp[34] = 0;
    cprintf("N%c: %-34s", '1' + idx, url[0] ? disp : "(EMPTY)");

    if (idx == sel) revers(0);
}

/*
 * Let the user pick a prefix slot.
 * Returns 0-7 on success, 255 on ESC/cancel.
 */
unsigned char select_prefix(void)
{
    unsigned char sel = 0, prev, i, c;

    screen_header("CATALOG - SELECT PREFIX");
    screen_footer("I/M:SELECT  RET:BROWSE  ESC:BACK");

    for (i = 0; i < NUM_PREFIXES; i++)
        draw_prefix_item(i, sel);

    for (;;) {
        c = cgetc();

        if (is_up(c) && sel > 0) {
            prev = sel--;
            draw_prefix_item(prev, sel);
            draw_prefix_item(sel,  sel);
        } else if (is_down(c) && sel < NUM_PREFIXES - 1) {
            prev = sel++;
            draw_prefix_item(prev, sel);
            draw_prefix_item(sel,  sel);
        } else if (c == KEY_ENTER) {
            if (prefix_get(sel)[0] != 0) return sel;
            screen_msg(20, "PREFIX IS EMPTY - USE SET PREFIX FIRST");
        } else if (c == KEY_ESC) {
            return 255;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Directory browser                                                    */
/* ------------------------------------------------------------------ */

/*
 * Non-recursive browser: maintains a mutable subpath string and
 * reloads the directory listing whenever the user navigates in or out.
 * Large buffers are static to avoid stack overflow on the 6502.
 */
static void browse_directory(unsigned char slot)
{
    static char    subpath[80];  /* current path relative to prefix */
    static char    newdir[ENTRY_LEN];
    static char    title[SCREEN_W];
    static char    newname[32];       /* mkdir user input */
    static char    dirname[ENTRY_LEN]; /* rmdir: name extracted from entry */
    static char    opspec[128];       /* devicespec for fs operations */

    unsigned char  sel = 0, top = 0, prev, c, nlen;
    unsigned char  need_reload = 1;

    subpath[0] = 0;

    for (;;) {
        if (need_reload) {
            need_reload = 0;
            sel = 0;
            top = 0;

            build_spec(slot, subpath);
            make_title(title);
            screen_header(title);
            screen_footer("I/M:MOVE  C:MKDIR  R:RMDIR  RET:CD  ESC");

            if (!load_directory()) {
                /* load_directory showed "OPEN FAILED" at row 20 */
                screen_msg(21, "PRESS ANY KEY TO GO BACK");
                cgetc();
                if (subpath[0] == 0) return;
                trim_last_dir(subpath);
                need_reload = 1;
                continue;
            }
            if (entry_count == 0) {
                screen_msg(20, "EMPTY DIRECTORY");
                screen_msg(21, "PRESS ANY KEY TO GO BACK");
                cgetc();

                /* Go up one level, or exit if already at root */
                if (subpath[0] == 0) return;
                trim_last_dir(subpath);
                need_reload = 1;
                continue;
            }

            draw_listing(top, sel);
        }

        c = cgetc();

        if (c == 'C') {
            if (prompt_string("CREATE DIRECTORY", newname, 30)) {
                build_op_spec(opspec, sizeof(opspec), newname);
                if (network_fs_mkdir(opspec) == FN_ERR_OK) {
                    need_reload = 1;
                } else {
                    draw_listing(top, sel);
                    screen_msg(20, "MKDIR FAILED");
                }
            } else {
                draw_listing(top, sel);
            }
        } else if (is_up(c) && sel > 0) {
            prev = sel--;
            if (sel < top) top = sel;
            if (top == sel) {
                draw_listing(top, sel);
            } else {
                draw_entry_row(prev, top, sel);
                draw_entry_row(sel,  top, sel);
            }
        } else if (is_down(c) && sel < entry_count - 1) {
            prev = sel++;
            if (sel >= top + VIS_ROWS) {
                top = sel - VIS_ROWS + 1;
                draw_listing(top, sel);
            } else {
                draw_entry_row(prev, top, sel);
                draw_entry_row(sel,  top, sel);
            }
        } else if (c == KEY_ENTER) {
            if (is_dir_entry(entries[sel])) {
                copy_filename(entries[sel], newdir, ENTRY_LEN);
                strncat(subpath, newdir,
                        sizeof(subpath) - strlen(subpath) - 1);
                need_reload = 1;
            }
        } else if (c == 'R') {
            if (!is_dir_entry(entries[sel])) {
                screen_msg(20, "NOT A DIRECTORY");
            } else {
                copy_filename(entries[sel], dirname, ENTRY_LEN);
                nlen = (unsigned char)strlen(dirname);
                if (nlen > 0 && dirname[nlen - 1] == '/')
                    dirname[--nlen] = 0;
                gotoxy(0, 20);
                cclear(SCREEN_W);
                gotoxy(0, 20);
                cprintf("REMOVE %.26s? (Y/N)", dirname);
                c = cgetc();
                if (c == 'Y' || c == 'y') {
                    build_op_spec(opspec, sizeof(opspec), dirname);
                    if (network_fs_rmdir(opspec) == FN_ERR_OK) {
                        need_reload = 1;
                    } else {
                        screen_msg(20, "RMDIR FAILED");
                    }
                } else {
                    screen_msg(20, "");
                }
            }
        } else if (c == KEY_ESC) {
            if (subpath[0] == 0) return;
            trim_last_dir(subpath);
            need_reload = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                   */
/* ------------------------------------------------------------------ */

void catalog_screen(void)
{
    unsigned char slot;

    slot = select_prefix();
    if (slot == 255) return;

    browse_directory(slot);
}
