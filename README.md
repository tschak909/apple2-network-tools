# FujiNet NET.TOOLS for Apple II

A ProDOS utility for browsing and transferring files over a [FujiNet](https://fujinet.online) network adapter on Apple II hardware (IIe enhanced, IIc, IIgs). Navigate network directories, catalog local volumes, and copy files in either direction between a TNFS/FTP server and local ProDOS storage.

## Requirements

- Apple II+, Apple IIe, Apple IIe Enhanced, IIc, or IIgs with at least 64K of RAM.
- FujiNet network adapter
- ProDOS 2.4 or later

## Building

This project uses [MekkoGX](https://github.com/fozzTexx/MekkoGX) and requires the [cc65](https://cc65.github.io) toolchain.

```
make apple2
```

The ready-to-run disk image is written to `r2r/apple2/net.tools.po`.

## Main Menu

```
 LOCAL PREFIX: /RAM/

      SET LOCAL PREFIX
      SET NETWORK PREFIX
      CATALOG
      COPY NET -> LOCAL
      COPY LOCAL -> NET
```

The active local ProDOS prefix is displayed under the header at all times. Arrow keys (or **I**/**M**) navigate; **Return** selects; **Esc** quits.

---

## SET LOCAL PREFIX

Lists all currently online ProDOS volumes (slot, drive, and volume name). Navigate with **I**/**M** (or arrow keys, if you have them) and press **Return** to set the selected volume as the active local prefix. The chosen prefix is used as the destination for NET→LOCAL copies and the source for LOCAL→NET copies.

---

## SET NETWORK PREFIX

Stores up to **8 network base URLs** (N1:–N8:) in FujiNet AppKey storage so they persist across reboots. Each slot holds a full TNFS or FTP URL, e.g.:

```
N1: TNFS://myserver.local/apple2/
N2: FTP://ftp.example.com/pub/apple2/
```

Navigate with **I**/**M** (or arrow keys), press **Return** to edit the highlighted slot. The inline editor supports backspace and **Esc** to cancel.

---

## CATALOG

Browse any of the 8 network prefix slots. Directories are shown with a trailing `/`; each entry shows the filename right-aligned with size information. Navigation:

| Key | Action |
|-----|--------|
| **I** / **↑** | Move up |
| **M** / **↓** | Move down |
| **Return** | Enter directory |
| **Esc** | Go up one directory / exit |

---

## COPY NET → LOCAL

Copies files from a network prefix to the active local ProDOS prefix.

1. Select a network prefix slot (N1:–N8:).
2. Browse directories with **I**/**M**/**Return**/**Esc**.
3. Tag individual files with **Space**; tag all with **\***.
4. Press **Return** on a file (or with tagged files) to begin copying.

**For each file**, a type selection dialog appears:

```
FILE: HELLO.BAS
 BAS  $FC  AUX:$0000   ← pre-selected based on extension
 BIN  $06  AUX:$0000
 TXT  $04  AUX:$0000
 SYS  $FF  AUX:$2000
 INT  $FA  AUX:$0000
 ADB  $19  AUX:$0000
 AWP  $1A  AUX:$0000
 ASP  $1B  AUX:$0000
 CUSTOM TYPE/AUXTYPE...
```

The ProDOS file type is **guessed from the filename extension** (`.bas` → BAS, `.txt` → TXT, `.sys` → SYS with auxtype `$2000`, etc.) and pre-highlighted. Navigate with **I**/**M** to override, or select **CUSTOM** and enter any hex type (`$00`–`$FF`) and auxtype (`$0000`–`$FFFF`). Press **Esc** to skip the current file.

Subdirectories encountered during a tagged copy are created locally via `prodos_mkdir`. Filenames are automatically sanitised to ProDOS conventions (uppercase, max 15 chars, legal characters only).

---

## COPY LOCAL → NET

Copies files from the active local ProDOS prefix to a network destination.

1. Browse the local directory with **I**/**M**/**Return**/**Esc**.
2. The header shows **COPY FROM:** followed by the current path.
3. Row 3 shows **USED** and **FREE** block counts for the current volume.
4. Each entry displays the filename, ProDOS file type (TXT/BIN/BAS/SYS/…), block count, and last-modified date (`DD-MON-YY`).
5. Tag files with **Space** or **\***, then press **Return** to choose a network destination slot and start copying.
6. Press **Esc** to cancel the destination selection and return to browsing.

---

## Key Reference

| Key | Action |
|-----|--------|
| **I** / **↑** | Move selection up |
| **M** / **↓** | Move selection down |
| **Space** | Tag / untag file |
| **\*** | Tag all files |
| **Return** | Select / enter / copy |
| **Esc** | Back / cancel / quit |

---

## Storage

Network prefixes are saved in **FujiNet AppKey** storage (creator `0x4E54` / app `0x01`) and survive power cycles without requiring a separate config file on disk.

## License

See [LICENSE](LICENSE).
