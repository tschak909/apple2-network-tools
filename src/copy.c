#include <conio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <apple2_filetype.h>
#include <fujinet-network.h>
/* Low-level SmartPort symbols from fujinet-apple2.lib */
#define SP_PAYLOAD_SIZE 512
#define SP_ERR_OK       0x00
extern unsigned char sp_network;
extern unsigned char sp_payload[];
int8_t sp_read(unsigned char dest, unsigned int len);
#include "copy.h"
#include "catalog.h"
#include "prefix.h"
#include "screen.h"
#include "localprefix.h"

#define MAX_COPY_ENTRIES  48
#define COPY_NAME_LEN     24   /* max stored filename length including NUL */
#define VIS_ROWS          16

/* ------------------------------------------------------------------ */
/* Shared per-entry state                                               */
/* ------------------------------------------------------------------ */

static char          cnames[MAX_COPY_ENTRIES][COPY_NAME_LEN];
static unsigned char cis_dir[MAX_COPY_ENTRIES];
static unsigned char ctag[MAX_COPY_ENTRIES];
static unsigned char ctype[MAX_COPY_ENTRIES];
static unsigned      cblocks[MAX_COPY_ENTRIES];
static struct datetime cmtime[MAX_COPY_ENTRIES];
static unsigned char centry_count;
static unsigned char is_local;

/* I/O scratch buffer used for file copy chunks */
static unsigned char copy_buf[SP_PAYLOAD_SIZE];

/* ------------------------------------------------------------------ */
/* Utilities                                                            */
/* ------------------------------------------------------------------ */

/*
 * Convert an arbitrary filename to a ProDOS-legal name:
 *   - uppercase, max 15 chars
 *   - first char A-Z (scan forward to first alpha; prepend 'F' if none)
 *   - subsequent chars A-Z / 0-9 / period only (illegal chars → '.')
 */
static void to_prodos_name(const char *src, char *dst)
{
    unsigned char i = 0, j = 0;
    char ch;

    /* Find a valid first character (A-Z after uppercasing) */
    while (src[i]) {
        ch = src[i];
        if (ch >= 'a' && ch <= 'z') ch -= 0x20;
        if (ch >= 'A' && ch <= 'Z') { dst[j++] = ch; i++; break; }
        i++;
    }
    if (j == 0) dst[j++] = 'F';   /* no alpha found */

    while (src[i] && j < 15) {
        ch = src[i++];
        if (ch >= 'a' && ch <= 'z') ch -= 0x20;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.')
            dst[j++] = ch;
        else
            dst[j++] = '.';
    }
    dst[j] = 0;
}

/* Remove the last path component from a slash-terminated path string. */
static void trim_last_dir(char *path)
{
    int len = (int)strlen(path);
    if (len == 0) return;
    if (path[len - 1] == '/') len--;
    while (len > 0 && path[len - 1] != '/') len--;
    path[len] = 0;
}

/*
 * Fill out (C string) with the current ProDOS prefix, converted from
 * the Pascal string returned by get_local_prefix().
 * maxlen includes the terminating NUL.
 */
static void get_local_cpath(char *out, unsigned char maxlen)
{
    static char pascal_buf[34];
    unsigned char len;

    get_local_prefix(pascal_buf);
    len = (unsigned char)(unsigned char)pascal_buf[0];
    if (len >= maxlen) len = (unsigned char)(maxlen - 1);
    memcpy(out, pascal_buf + 1, len);
    out[len] = 0;
}

/* Build a network devicespec "Nx:URL/subpath/STAR" (STAR = wildcard). */
static void build_net_spec(unsigned char slot, const char *subpath,
                            char *out, unsigned char outlen)
{
    const char *base = prefix_get(slot);

    out[0] = 'N';
    out[1] = (char)('1' + slot);
    out[2] = ':';
    out[3] = 0;
    strncat(out, base,    outlen - 4);
    strncat(out, subpath, outlen - (unsigned char)strlen(out) - 1);
    strncat(out, "*",     outlen - (unsigned char)strlen(out) - 1);
}

/*
 * Build a file-operation spec from a directory spec (ending with '*')
 * by stripping the '*' and appending filename.
 */
static void build_file_spec(const char *dir_spec, const char *filename,
                             char *out, unsigned char outlen)
{
    unsigned char base_len;

    strncpy(out, dir_spec, outlen - 1);
    out[outlen - 1] = 0;
    base_len = (unsigned char)strlen(out);
    if (base_len > 0 && out[base_len - 1] == '*')
        out[--base_len] = 0;
    strncat(out, filename, outlen - base_len - 1);
}

/*
 * Convert a C-string path to a Pascal string in out[].
 * out[0] = length, out[1..n] = path bytes.
 */
static void make_pascal_path(const char *cpath, char *out, unsigned char outlen)
{
    unsigned char len = (unsigned char)strlen(cpath);
    if (len > (unsigned char)(outlen - 1)) len = (unsigned char)(outlen - 1);
    out[0] = (char)len;
    memcpy(out + 1, cpath, len);
}

/* ------------------------------------------------------------------ */
/* Local file metadata display helpers                                  */
/* ------------------------------------------------------------------ */

static const char mon_abbr[13][4] = {
    "???",
    "JAN","FEB","MAR","APR","MAY","JUN",
    "JUL","AUG","SEP","OCT","NOV","DEC"
};

static const char *ftype_str(unsigned char t)
{
    static char buf[4];
    switch (t) {
        case 0x00: return "---";
        case 0x04: return "TXT";
        case 0x06: return "BIN";
        case 0x0F: return "DIR";
        case 0x19: return "ADB";
        case 0x1A: return "AWP";
        case 0x1B: return "ASP";
        case 0xB3: return "S16";
        case 0xFA: return "INT";
        case 0xFB: return "IVR";
        case 0xFC: return "BAS";
        case 0xFD: return "VAR";
        case 0xFE: return "REL";
        case 0xFF: return "SYS";
    }
    buf[0] = "0123456789ABCDEF"[(t >> 4) & 0xF];
    buf[1] = "0123456789ABCDEF"[t & 0xF];
    buf[2] = 'H';
    buf[3] = 0;
    return buf;
}

/* Writes exactly 9 chars + NUL into out[10]: "DD-MON-YY" or " NO DATE " */
static void fmt_date(const struct datetime *dt, char *out)
{
    unsigned char day = dt->date.day;
    unsigned char mon = dt->date.mon;
    unsigned char yr  = (unsigned char)(dt->date.year % 100);
    const char   *m;

    if (day == 0 && mon == 0) {
        memcpy(out, " NO DATE ", 9);
        out[9] = 0;
        return;
    }
    if (mon == 0 || mon > 12) mon = 0;
    m       = mon_abbr[mon];
    out[0]  = (char)('0' + day / 10);
    out[1]  = (char)('0' + day % 10);
    out[2]  = '-';
    out[3]  = m[0]; out[4] = m[1]; out[5] = m[2];
    out[6]  = '-';
    out[7]  = (char)('0' + yr / 10);
    out[8]  = (char)('0' + yr % 10);
    out[9]  = 0;
}

/* Derive ProDOS unit_num (slot<<4 | drive<<7) from g_local_prefix volume name */
static unsigned char get_unit_from_prefix(void)
{
    unsigned char i, b, nlen, vnlen = 0;
    char volname[16];
    const char *p = g_local_prefix;

    if (*p == '/') p++;
    while (*p && *p != '/' && vnlen < 15)
        volname[vnlen++] = *p++;
    if (vnlen == 0) return 0;

    for (i = 0; i < 16; i++) {
        b = online_buf[i * 16];
        if (b == 0) break;
        nlen = b & 0x0F;
        if (nlen == 0) continue;
        if (nlen == vnlen &&
            memcmp(online_buf + i * 16 + 1, volname, vnlen) == 0)
            return (unsigned char)(b & 0xF0);
    }
    return 0;
}

/*
 * Display volume space at row 3.
 * Reads the volume directory header (block 2) and scans bitmap blocks.
 * Reuses copy_buf[512] for block reads — must not be called during a copy.
 */
static void show_volume_space(void)
{
    unsigned char unit;
    unsigned int  total, bitmap_ptr, i, j, bmap_blocks;
    unsigned long free_count = 0;
    unsigned int  free_blocks;
    unsigned char b;

    unit = get_unit_from_prefix();
    if (unit == 0) return;

    rdb_unit  = unit;
    rdb_block = 2;
    if (prodos_read_block(copy_buf) != 0) return;

    /* Volume directory header starts at block byte 4 */
    bitmap_ptr = (unsigned int)copy_buf[0x21] |
                 ((unsigned int)copy_buf[0x22] << 8);
    total      = (unsigned int)copy_buf[0x23] |
                 ((unsigned int)copy_buf[0x24] << 8);

    if (total == 0 || bitmap_ptr == 0) return;

    bmap_blocks = (total + 4095U) / 4096U;

    for (i = 0; i < bmap_blocks; i++) {
        rdb_unit  = unit;
        rdb_block = (unsigned int)(bitmap_ptr + i);
        if (prodos_read_block(copy_buf) != 0) break;
        for (j = 0; j < 512; j++) {
            b = copy_buf[j];
            while (b) { free_count++; b = (unsigned char)(b & (unsigned char)(b - 1)); }
        }
    }

    free_blocks = (free_count > (unsigned long)total) ?
                  total : (unsigned int)free_count;

    gotoxy(0, 3);
    cprintf("USED:%-5u FREE:%-5u", total - free_blocks, free_blocks);
}

/* ------------------------------------------------------------------ */
/* Display                                                              */
/* ------------------------------------------------------------------ */

static void draw_copy_row(unsigned char i, unsigned char top, unsigned char sel)
{
    unsigned char is_sel = (i == sel);
    unsigned char nlen   = (unsigned char)strlen(cnames[i]);
    unsigned char j, maxname;
    char          ch;
    char          date_str[10];

    gotoxy(0, (unsigned char)(4 + i - top));

    if (is_sel) revers(1);

    /* col 0: tag marker */
    cputc(ctag[i] ? '*' : ' ');

    if (is_local) {
        /* cols 1-15: filename (15 chars, ProDOS max) */
        maxname = 15;
        for (j = 0; j < maxname; j++) {
            ch = (j < nlen) ? cnames[i][j] : ' ';
            if (is_sel && ch >= 'a' && ch <= 'z') ch -= 0x20;
            cputc(ch);
        }
        /* col 16: dir indicator */
        cputc(cis_dir[i] ? '/' : ' ');
        /* col 17: space */
        cputc(' ');
        /* cols 18-20: file type */
        cputs(ftype_str(ctype[i]));
        /* col 21: space */
        cputc(' ');
        /* cols 22-26: blocks (5 chars right-justified) */
        cprintf("%5u", cblocks[i]);
        /* col 27: space */
        cputc(' ');
        /* cols 28-36: date (9 chars) */
        fmt_date(&cmtime[i], date_str);
        cputs(date_str);
        /* cols 37-39: padding */
        cputs("   ");
    } else {
        /* cols 1-37: filename (37 chars) */
        for (j = 0; j < 37; j++) {
            ch = (j < nlen) ? cnames[i][j] : ' ';
            if (is_sel && ch >= 'a' && ch <= 'z') ch -= 0x20;
            cputc(ch);
        }
        /* col 38: dir indicator */
        cputc(cis_dir[i] ? '/' : ' ');
        /* col 39: padding */
        cputc(' ');
    }

    if (is_sel) revers(0);
}

static void draw_copy_listing(unsigned char top, unsigned char sel)
{
    unsigned char i;

    for (i = 0; i < VIS_ROWS; i++) {
        gotoxy(0, (unsigned char)(4 + i));
        if (top + i < centry_count)
            draw_copy_row((unsigned char)(top + i), top, sel);
        else
            cclear(SCREEN_W);
    }
}

/* ------------------------------------------------------------------ */
/* Network directory loading (same sp_read mechanism as catalog.c)     */
/* ------------------------------------------------------------------ */

static char          net_partial[COPY_NAME_LEN];
static unsigned char net_plen;

static void net_flush_partial(void)
{
    unsigned char i, nlen;
    char *name;

    if (net_plen == 0 || net_partial[0] == ' ' || centry_count >= MAX_COPY_ENTRIES)
        goto done;

    /* trim trailing spaces */
    while (net_plen > 0 && net_partial[net_plen - 1] == ' ') net_plen--;
    if (net_plen == 0) goto done;

    /* extract filename: everything before the first space */
    for (i = 0; i < net_plen && net_partial[i] != ' '; i++);
    nlen = i;
    if (nlen == 0) goto done;

    name = cnames[centry_count];
    if (nlen >= COPY_NAME_LEN) nlen = COPY_NAME_LEN - 1;
    memcpy(name, net_partial, nlen);
    name[nlen] = 0;

    /* directory entries have a trailing '/' — strip it, set cis_dir flag */
    if (nlen > 0 && name[nlen - 1] == '/') {
        name[nlen - 1] = 0;
        cis_dir[centry_count] = 1;
    } else {
        cis_dir[centry_count] = 0;
    }

    ctag[centry_count] = 0;
    centry_count++;

done:
    net_plen = 0;
}

static void net_parse_chunk(const char *buf, unsigned int len)
{
    unsigned int  i;
    unsigned char ch;

    for (i = 0; i < len; i++) {
        ch = (unsigned char)buf[i];
        if (ch == 0x0D || ch == 0x0A || ch == 0x00 || ch == 0x9B) {
            net_flush_partial();
        } else if (net_plen < COPY_NAME_LEN - 1) {
            net_partial[net_plen++] = (char)ch;
        }
    }
}

static unsigned char net_load_dir(const char *spec)
{
    uint16_t bw;
    uint8_t  conn, err;
    uint16_t to_read;

    centry_count = 0;
    net_plen     = 0;

    screen_msg(20, "OPENING...");

    if (network_open(spec, 6, 128) != FN_ERR_OK) {
        screen_msg(20, "OPEN FAILED");
        return 0;
    }

    screen_msg(20, "READING DIRECTORY...");

    do {
        if (network_status(spec, &bw, &conn, &err) != FN_ERR_OK) break;
        if (bw == 0) break;
        to_read = (bw < (uint16_t)SP_PAYLOAD_SIZE) ? bw : (uint16_t)SP_PAYLOAD_SIZE;
        if (sp_read(sp_network, to_read) != SP_ERR_OK) break;
        net_parse_chunk((const char *)sp_payload, (unsigned int)to_read);
    } while (centry_count < MAX_COPY_ENTRIES);

    net_flush_partial();
    network_close(spec);
    screen_msg(20, "");
    return centry_count;
}

/* ------------------------------------------------------------------ */
/* Local directory loading                                              */
/* ------------------------------------------------------------------ */

extern unsigned char __oserror;

static unsigned char local_load_dir(const char *cpath)
{
    DIR           *d;
    struct dirent *de;

    centry_count = 0;

    d = opendir(cpath);
    if (!d) {
        static char emsg[8];
        emsg[0] = 'E'; emsg[1] = ':'; emsg[2] = '$';
        emsg[3] = "0123456789ABCDEF"[(__oserror >> 4) & 0xF];
        emsg[4] = "0123456789ABCDEF"[__oserror & 0xF];
        emsg[5] = 0;
        screen_msg(20, emsg);
        return 0;
    }

    while ((de = readdir(d)) != NULL && centry_count < MAX_COPY_ENTRIES) {
        strncpy(cnames[centry_count], de->d_name, COPY_NAME_LEN - 1);
        cnames[centry_count][COPY_NAME_LEN - 1] = 0;
        cis_dir[centry_count]  = _DE_ISDIR(de->d_type) ? 1 : 0;
        ctag[centry_count]     = 0;
        ctype[centry_count]    = de->d_type;
        cblocks[centry_count]  = de->d_blocks;
        cmtime[centry_count]   = de->d_mtime;
        centry_count++;
    }

    closedir(d);
    return centry_count;
}

/* ------------------------------------------------------------------ */
/* File copy operations                                                 */
/* ------------------------------------------------------------------ */

/*
 * Copy one file from a network spec to a local C-string path.
 * Uses STATUS + sp_read to avoid the dirBuffer side-effect issue.
 */
static unsigned char copy_net_to_local(const char *net_spec,
                                        const char *local_path)
{
    uint16_t bw;
    uint8_t  conn, err;
    uint16_t to_read;
    int      fd;

    if (network_open(net_spec, OPEN_MODE_READ, 0) != FN_ERR_OK)
        return 0;

    _filetype = PRODOS_T_BIN;
    fd = open(local_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        network_close(net_spec);
        return 0;
    }

    do {
        if (network_status(net_spec, &bw, &conn, &err) != FN_ERR_OK) break;
        if (bw == 0) break;
        to_read = (bw < (uint16_t)SP_PAYLOAD_SIZE) ? bw : (uint16_t)SP_PAYLOAD_SIZE;
        if (sp_read(sp_network, to_read) != SP_ERR_OK) break;
        write(fd, sp_payload, (int)to_read);
    } while (1);

    close(fd);
    network_close(net_spec);
    return 1;
}

/*
 * Copy one file from a local C-string path to a network spec.
 */
static unsigned char copy_local_to_net(const char *local_path,
                                        const char *net_spec)
{
    int  fd;
    int  nread;

    fd = open(local_path, O_RDONLY);
    if (fd < 0) return 0;

    if (network_open(net_spec, OPEN_MODE_WRITE, 0) != FN_ERR_OK) {
        close(fd);
        return 0;
    }

    while ((nread = read(fd, copy_buf, sizeof(copy_buf))) > 0)
        network_write(net_spec, copy_buf, (uint16_t)nread);

    close(fd);
    network_close(net_spec);
    return 1;
}

/* ------------------------------------------------------------------ */
/* COPY FROM NETWORK → LOCAL                                            */
/* ------------------------------------------------------------------ */

void copy_from_net_screen(void)
{
    static char net_subpath[80];
    static char cur_spec[96];       /* current dir spec (ends with '*') */
    static char src_spec[96];       /* per-file source spec */
    static char local_dest[34];     /* local destination C path (from prefix) */
    static char dst_path[64];       /* per-file local destination */
    static char prodos_name[16];    /* ProDOS-translated filename */
    static char pascal_buf[34];     /* scratch Pascal string for prodos_mkdir */
    static char title[SCREEN_W + 1];

    unsigned char slot, sel = 0, top = 0, prev, c, i;
    unsigned char need_reload = 1;
    unsigned char tagged_count;

    is_local = 0;

    slot = select_prefix();
    if (slot == 255) return;

    get_local_cpath(local_dest, sizeof(local_dest));

    net_subpath[0] = 0;

    for (;;) {
        if (need_reload) {
            need_reload = 0;
            sel = 0; top = 0;

            build_net_spec(slot, net_subpath, cur_spec, sizeof(cur_spec));

            strncpy(title, cur_spec, SCREEN_W - 1);
            title[SCREEN_W - 1] = 0;
            screen_header(title);
            screen_footer("SPC:TAG  *:ALL  RET:CD/COPY  ESC:BACK");

            if (!net_load_dir(cur_spec)) {
                screen_msg(21, "PRESS ANY KEY");
                cgetc();
                screen_msg(21, "");
                if (net_subpath[0] == 0) return;
                trim_last_dir(net_subpath);
                need_reload = 1;
                continue;
            }

            draw_copy_listing(top, sel);
        }

        c = cgetc();

        if (c == ' ') {
            ctag[sel] ^= 1;
            draw_copy_row(sel, top, sel);
        } else if (c == '*') {
            for (i = 0; i < centry_count; i++) ctag[i] = 1;
            draw_copy_listing(top, sel);
        } else if (is_up(c) && sel > 0) {
            prev = sel--;
            if (sel < top) {
                top = sel;
                draw_copy_listing(top, sel);
            } else {
                draw_copy_row(prev, top, sel);
                draw_copy_row(sel,  top, sel);
            }
        } else if (is_down(c) && sel < centry_count - 1) {
            prev = sel++;
            if (sel >= top + VIS_ROWS) {
                top = (unsigned char)(sel - VIS_ROWS + 1);
                draw_copy_listing(top, sel);
            } else {
                draw_copy_row(prev, top, sel);
                draw_copy_row(sel,  top, sel);
            }
        } else if (c == KEY_ENTER) {
            if (cis_dir[sel]) {
                /* navigate into directory */
                strncat(net_subpath, cnames[sel],
                        sizeof(net_subpath) - strlen(net_subpath) - 2);
                strncat(net_subpath, "/",
                        sizeof(net_subpath) - strlen(net_subpath) - 1);
                need_reload = 1;
            } else {
                /* copy: all tagged, or just current if none tagged */
                tagged_count = 0;
                for (i = 0; i < centry_count; i++)
                    if (ctag[i]) tagged_count++;
                if (tagged_count == 0) ctag[sel] = 1;

                for (i = 0; i < centry_count; i++) {
                    if (!ctag[i]) continue;

                    to_prodos_name(cnames[i], prodos_name);
                    build_file_spec(cur_spec, cnames[i], src_spec, sizeof(src_spec));

                    strncpy(dst_path, local_dest, sizeof(dst_path) - 1);
                    dst_path[sizeof(dst_path) - 1] = 0;
                    strncat(dst_path, prodos_name,
                            sizeof(dst_path) - strlen(dst_path) - 1);

                    screen_msg(20, prodos_name);

                    if (cis_dir[i]) {
                        make_pascal_path(dst_path, pascal_buf, sizeof(pascal_buf));
                        prodos_mkdir(pascal_buf);
                    } else {
                        copy_net_to_local(src_spec, dst_path);
                    }
                }

                screen_msg(20, "DONE - PRESS ANY KEY");
                cgetc();
                screen_msg(20, "");
                /* clear tags and redraw */
                for (i = 0; i < centry_count; i++) ctag[i] = 0;
                draw_copy_listing(top, sel);
            }
        } else if (c == KEY_ESC) {
            if (net_subpath[0] == 0) return;
            trim_last_dir(net_subpath);
            need_reload = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* COPY FROM LOCAL → NETWORK                                            */
/* ------------------------------------------------------------------ */

void copy_to_net_screen(void)
{
    static char local_subpath[64];
    static char local_root[34];
    static char local_cur[64];
    static char net_dest_base[96];
    static char dst_spec[96];
    static char src_path[64];
    static char title[SCREEN_W + 1];

    unsigned char slot, sel = 0, top = 0, prev, c, i;
    unsigned char need_reload = 1;
    unsigned char tagged_count;

    is_local = 1;

    /* Use the local prefix set at startup / via SET LOCAL PREFIX */
    strncpy(local_root, g_local_prefix, sizeof(local_root) - 1);
    local_root[sizeof(local_root) - 1] = 0;
    local_subpath[0] = 0;

    /* Populate online_buf so get_unit_from_prefix() can find the volume */
    get_online_volumes();

    for (;;) {
        if (need_reload) {
            need_reload = 0;
            sel = 0; top = 0;

            strncpy(local_cur, local_root, sizeof(local_cur) - 1);
            local_cur[sizeof(local_cur) - 1] = 0;
            strncat(local_cur, local_subpath,
                    sizeof(local_cur) - strlen(local_cur) - 1);

            strncpy(title, "COPY FROM: ", sizeof(title) - 1);
            strncat(title, local_cur,
                    sizeof(title) - (unsigned char)strlen(title) - 1);
            screen_header(title);
            screen_footer("SPC:TAG  *:ALL  RET:CD/COPY  ESC:BACK");

            if (!local_load_dir(local_cur)) {
                screen_msg(21, "PRESS ANY KEY");
                cgetc();
                screen_msg(20, "");
                screen_msg(21, "");
                if (local_subpath[0] == 0) return;
                trim_last_dir(local_subpath);
                need_reload = 1;
                continue;
            }

            draw_copy_listing(top, sel);
            show_volume_space();
        }

        c = cgetc();

        if (c == ' ') {
            ctag[sel] ^= 1;
            draw_copy_row(sel, top, sel);
        } else if (c == '*') {
            for (i = 0; i < centry_count; i++) ctag[i] = 1;
            draw_copy_listing(top, sel);
        } else if (is_up(c) && sel > 0) {
            prev = sel--;
            if (sel < top) {
                top = sel;
                draw_copy_listing(top, sel);
            } else {
                draw_copy_row(prev, top, sel);
                draw_copy_row(sel,  top, sel);
            }
        } else if (is_down(c) && sel < centry_count - 1) {
            prev = sel++;
            if (sel >= top + VIS_ROWS) {
                top = (unsigned char)(sel - VIS_ROWS + 1);
                draw_copy_listing(top, sel);
            } else {
                draw_copy_row(prev, top, sel);
                draw_copy_row(sel,  top, sel);
            }
        } else if (c == KEY_ENTER) {
            if (cis_dir[sel]) {
                strncat(local_subpath, cnames[sel],
                        sizeof(local_subpath) - strlen(local_subpath) - 2);
                strncat(local_subpath, "/",
                        sizeof(local_subpath) - strlen(local_subpath) - 1);
                need_reload = 1;
            } else {
                /* Tag current if nothing tagged */
                tagged_count = 0;
                for (i = 0; i < centry_count; i++)
                    if (ctag[i]) tagged_count++;
                if (tagged_count == 0) ctag[sel] = 1;

                /* Ask for network destination */
                slot = select_prefix();
                if (slot == 255) {
                    /* Cancelled — restore header/footer */
                    screen_header(title);
                    screen_footer("SPC:TAG  *:ALL  RET:CD/COPY  ESC:BACK");
                    draw_copy_listing(top, sel);
                    show_volume_space();
                } else {
                    build_net_spec(slot, "", net_dest_base,
                                   sizeof(net_dest_base));

                    for (i = 0; i < centry_count; i++) {
                        if (!ctag[i]) continue;
                        if (cis_dir[i]) continue;

                        strncpy(src_path, local_cur, sizeof(src_path) - 1);
                        src_path[sizeof(src_path) - 1] = 0;
                        strncat(src_path, cnames[i],
                                sizeof(src_path) - strlen(src_path) - 1);

                        build_file_spec(net_dest_base, cnames[i],
                                        dst_spec, sizeof(dst_spec));

                        screen_msg(20, cnames[i]);
                        copy_local_to_net(src_path, dst_spec);
                    }

                    screen_msg(20, "DONE - PRESS ANY KEY");
                    cgetc();
                    screen_header(title);
                    screen_footer("SPC:TAG  *:ALL  RET:CD/COPY  ESC:BACK");
                    screen_msg(20, "");
                    for (i = 0; i < centry_count; i++) ctag[i] = 0;
                    draw_copy_listing(top, sel);
                    show_volume_space();
                }
            }
        } else if (c == KEY_ESC) {
            if (local_subpath[0] == 0) return;
            trim_last_dir(local_subpath);
            need_reload = 1;
        }
    }
}
