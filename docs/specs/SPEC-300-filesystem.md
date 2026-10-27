# SPEC-300: Virtual filesystem (directories, PACK archives, search order, write directory)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f
Depends on: SPEC-301 (startup order), SPEC-302 (cvars basedir/cddir/game/user/fs_createpak)

## Purpose
Defines how the engine finds data files: which directories and PACK archives are mounted, in which
priority, how file names are normalised, where the engine writes files, and the file services that
other modules use. Anything not stated here behaves like the Quake II 3.19 filesystem.

## Interface / layout

### Cvars and command-line switches
| Cvar | Default | Flags (SPEC-302) | Command-line form | Meaning |
|---|---|---|---|---|
| basedir | `.` | no-set | `-basedir <dir>` | Root that contains `base/` |
| cddir | `` | no-set | `-cddir <dir>` | Optional second root; `<cddir>/base` is mounted under `<basedir>/base` |
| game | `` | serverinfo, latch | `-game <dir>` | Mod directory under basedir; empty or `base` = no mod |
| user | `./user` | serverinfo, no-set | `-user <dir>` | Per-user directory: top of the search path and the write directory |
| gamedir | `` | serverinfo, no-set | — | Set by the engine to the active mod name (empty for base) |
| fs_createpak | `0` | none | — | Development aid, see Behaviour 8 |

The `-basedir`, `-cddir`, `-game`, `-user` switches are separate argv words: the value is the next word.
`+set basedir X` (etc.) also works, because `+set` commands run before the filesystem starts (SPEC-301).
A value given with a dash switch is normalised (Behaviour 3) before use; the cvar default and `+set` values are not.

### PACK archive layout (little-endian)
| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 4 | char[4] | magic `PACK` (0x4B434150 as int32) |
| 4 | 4 | int32 | directory offset |
| 8 | 4 | int32 | directory length in bytes |

Directory entry (64 bytes, count = directory length / 64, integer division):
| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 56 | char[56] | name, NUL-padded, `/` separators |
| 56 | 4 | int32 | file offset |
| 60 | 4 | int32 | file length |

Archives with more than 10000 entries, or a wrong magic, are silently ignored (not mounted, no message).
No checksum, CRC or "pure server" verification of archives exists in 1.07f.

### File services exported by SoF.exe (used by the sound modules)
| Export | Behaviour |
|---|---|
| FS_FOpenFile(name, out handle) | Opens through the search path; returns file length or -1. The handle is a small integer 1..20; 0 = no file. |
| FS_FCloseFile(handle) | Closes the handle; unknown/0 handles are ignored. |
| FS_LoadFile(name, out buffer) | Whole-file read; returns length or -1. With a null buffer pointer only the length is returned. The buffer has length+2 bytes: the file, then a `\n` byte, then a NUL byte. |
| FS_FreeFile(buffer) | Frees a buffer from FS_LoadFile. |

At most 20 files can be open at once through handles. When all 20 are in use an open reports failure
(-1) even though the file exists.

## Behaviour

### 1. Search path construction (at start)
Mount order (each mount is placed *above* everything mounted before it):
1. If `cddir` is non-empty: game directory `<cddir>/base`.
2. Game directory `<basedir>/base`.
3. (This point is remembered as the *base layer*.)
4. If `game` is non-empty: the mod (Behaviour 5).
5. Game directory `<user>` (the user cvar value, used verbatim as a path).

"Mounting a game directory D" means, in this order:
- D itself (loose files);
- `D/pak0.pak` .. `D/pak9.pak`, each that opens as a valid archive;
- every other `D/*.pak` returned by a directory scan, skipping: the `.`/`..` entries, directories,
  files with the system attribute, and any name whose last 8 characters are `PAK<digit>.PAK`
  (case-insensitive). The scan order is the operating-system enumeration order (alphabetical on NTFS).

Because each mount goes on top, the resulting lookup priority inside one game directory is:
last scanned extra archive > ... > first scanned extra archive > pak9 > ... > pak0 > loose files of D.
Across directories: user > mod > `basedir/base` > `cddir/base`.

Consequences (test vectors):
- The retail `base/` holds `pak0.pak`..`pak3.pak` and `gs.pak`; `gs.pak` is found by the scan, so its files
  override the same names in pak0..pak3.
- A file named `mypak3.pak` or `xpak7.pak` is never mounted (it matches the `PAK<digit>.PAK` suffix rule but
  is not one of pak0..pak9). `pak10.pak` is mounted by the scan.
- A loose file in `base/` does NOT override the same name inside `base/pak0.pak`; a loose file in the user
  directory does.

### 2. Lookup of a file name
1. The requested name is first compared with the link table (Behaviour 6) using the name as given.
2. Otherwise the name is normalised (Behaviour 3) and every mount is tried from highest priority down:
   - loose directory D: the operating-system file `D/<normalised name>` is opened read-only;
   - archive: exact match of the normalised name against the archive's upper-cased entry names.
3. The first hit wins. If nothing matches, the open fails; with developer bit 4 set (SPEC-302) the engine
   prints `FindFile: can't find <name>`.
Inside one archive, if a name occurs twice in the directory, the first directory entry wins.

### 3. Name normalisation (case handling)
Applied to every requested name before lookup, and to archive entry names when an archive is mounted:
- `\` becomes `/`;
- ASCII letters become upper case (lookups are case-insensitive; on a case-sensitive host
  filesystem a reimplementation must match loose files case-insensitively);
- trailing spaces are removed;
- if the name contains `/BASE/`, everything up to and including that substring is removed;
- then, if the name contains `/GHOUL/`, everything before `GHOUL/` is removed.
Examples: `maps\kos1.bsp` → `MAPS/KOS1.BSP`; `c:/sof/base/maps/x.bsp` → `MAPS/X.BSP`;
`models/../ghoul/enemy/a.ghb` → `GHOUL/ENEMY/A.GHB`.
`..` is otherwise not rejected by lookups.

### 4. Write directory
All files the engine writes go below the **user** cvar directory, independent of the search path:
`config.cfg`, `configs/CURRENT.cfg`, `save/*.sof`, `sof.log`. Missing intermediate directories are created
before writing config and save files. Screenshots, demos, downloads and console dumps use the current
*game directory* (the most recently mounted directory, which is the user directory in a normal start).
UNVERIFIED for screenshots (renderer module) — verify by taking a screenshot and locating the file.
`paklist.txt` (Behaviour 8) and `stats.log` are written to the process working directory.

### 5. Changing the mod directory
Setting `game` (at start via `-game`/`+set game`, or later — a latched change takes effect when the server
restarts):
- names containing `..`, `/`, `\` or `:` are refused with `Gamedir should be a single filename, not a path`;
- every mount above the base layer is removed — this includes the user directory, which is NOT mounted again
  by a later change (only the start sequence mounts it). UNVERIFIED in game: start without -game, enter
  `game foo`, then `path` — the user directory should no longer be listed;
- on a non-dedicated engine `vid_restart` and `snd_restart` are queued;
- value `base` or empty: `gamedir` and `game` are set to empty and nothing is mounted;
- otherwise `gamedir` = the name, and `<basedir>/<name>` is mounted as a game directory (`<cddir>/<name>` is
  not);
- then the autoexec rule of SPEC-301 runs.

### 6. Links (`link` command)
`link <from> <to>`: if a link with exactly `<from>` exists, an empty `<to>` deletes it, otherwise its target is
replaced; if none exists, a new link is added. A requested name that *starts with* `<from>` (case-sensitive,
before normalisation) is opened directly as the host file `<to>` + rest-of-name; the search path is not
consulted, and if that file does not exist the open fails.

### 7. `path` and `dir` commands
`path` prints `Current search path:`, then one line per mount from highest priority down. Directory mounts
print their path; archive mounts print an empty line (UNVERIFIED — verify with `path` in the original). A line
`----------` is printed just before the first mount of the base layer. Then a blank line, `Links:` and one
line `<from> : <to>` per link.
`dir [pattern]` (default `*.*`) lists, for every mount: a header line for the mount, then the matching entries.
Directory mounts list one directory level using the host wildcard rules (files with their size, and
sub-folders); archive mounts match the pattern against whole entry names (`*` any run of characters including
`/`, `?` one character, `\*`/`\?` literal, case-insensitive). The listing ends with
`<bytes> bytes in <files> files and <folders> folders from <paths> paths`.

### 8. fs_createpak (development aid)
When `fs_createpak` is non-zero, each distinct file name opened from a loose directory (normalised form) is
remembered in first-open order. At program exit, if `fs_createpak` is still non-zero, `paklist.txt` is
written in the working directory with one line `$pak <name>` per remembered file.

### 9. Reading
Reading past the end of a file returns fewer bytes. A read of 0 bytes from a loose file is retried once; a
second 0-byte read, or a read error, is a fatal error (`FS_Read: 0 bytes read` / `FS_Read: -1 bytes read`).

## Constants
| Name | Value |
|---|---|
| PACK magic | `PACK` |
| PACK directory entry size | 64 bytes (name 56) |
| Max entries per archive | 10000 |
| Numbered archives per directory | pak0 .. pak9 |
| Max open handles | 20 (handles 1..20) |
| Max path length used by the engine | 128 bytes |

## Test vectors / how to verify
1. Retail install, no switches: `path` lists (top to bottom) `./user`, one empty line per archive found in
   `./user` (none), `----------`, then the archive lines of `./base` (5 empty lines: gs.pak, pak3..pak0 order
   per Behaviour 1) and `./base`. (Archive lines UNVERIFIED.)
2. Put `menus/m_start.rmf` (different content) loose into `base/` → the game still shows the gs.pak version;
   put it into `user/menus/` → the loose version is used.
3. `exec CONFIGS/default_KEYS.CFG` executes the archive entry `configs/DEFAULT_KEYS.cfg` (case-insensitive).
4. Rename a copy of `gs.pak` to `base/xpak1.pak` and remove `gs.pak` → its menus are not available.
5. `FS_LoadFile` of a 3-byte file `abc` yields buffer bytes `61 62 63 0A 00` and length 3.
6. `link maps/ c:/test/`, then `map kos1`: the map file is read from the host file `c:/test/kos1.bsp`
   (the link prefix is matched on the unnormalised name `maps/kos1.bsp`); if that file is missing the map
   fails to load even though `maps/kos1.bsp` exists in an archive.

## Open questions
- Exact printing of archive mounts by `path`/`dir` (empty line vs. archive name) — needs an in-game check.
- Whether screenshots/demos follow the game directory or the user directory after a runtime `game` change.
