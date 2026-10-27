# SPEC-102 — `.ifl` image file list

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — all 259 `.ifl` in `pak0` (252) and `pak2` (7), all under `ghoul/`. No game binary was
consulted.
Implementation: `tools/formats/ifl.py`

## Purpose

An `.ifl` lists the images a Ghoul model can show on one surface, in order: alternative skins (`table1.tga`,
`table1A.tga`, …), faces (`ghoul/comskin/f.ifl`, 380/428 entries), or animation frames
(`flashpak_frame1.tga` twice, `flashpak_frame2.tga` twice, …). The name and the line-per-file form match the
public 3ds Max "Image File List" convention; only the subset below occurs in the game data.

## Layout

Plain 8-bit text (all bytes are printable ASCII, CR or LF in the stock files).

| Element | Rule (stock data) |
|---|---|
| line terminator | CR LF in every file that has more than one line (161 files); 98 files are a single line without terminator |
| final terminator | optional (113 files end with CR LF, 146 do not) |
| entry line | exactly one token: a file name, no spaces, no tabs, no path separator |
| extension | always `.tga` (1863/1863 entries) |
| blank lines | only at the end of a file (64 blank lines in 24 files); never between entries |
| comments | none |
| entries per file | 1 (142 files) … 428; total 1863 |

## Semantics

- Entry *k* is image/frame/skin number *k* (0-based). Order matters, and an entry may repeat to hold a frame
  longer (`flashpak.ifl`).
- An entry is resolved relative to the directory of the `.ifl` itself:
  `ghoul/objects/generic/table1/table1.ifl` + `table1A.tga` → `ghoul/objects/generic/table1/table1A.tga`.
  File names differ in case from the pak entries (`table1A.tga`); lookups must ignore case.
- 215 of the 1863 entries name files that are not in any original pak (`f.ifl` alone: 46 in pak0, 58 in pak2;
  for 9 entries a file with that name exists in another directory). Missing entries still take their index
  slot, otherwise later skins would shift. What the game shows for a missing entry is UNKNOWN.
- Which model surface uses which `.ifl`, and how the index is chosen (skin number, animation time), is defined by
  the Ghoul model and game code, outside this format.

## Validation

`python tools/formats/ifl.py validate <extracted paks> <original paks…>`: **259 parsed, 0 failed, 1863 entries,
215 not present in the given paks**. Strict mode checks: no NUL, no blank line between entries, one token per line,
at least one entry.

## Open questions

1. Does the game accept the 3ds Max extras (a frame count after the name, `;` comments, sub-directories)? No stock
   file uses them; a parser should reject or warn until observed.
2. Behaviour when an entry's file is missing (empty skin, default texture, error?).
3. Whether `.m32` is also searched for an entry named `.tga` (all stock entries name `.tga`, and the referenced
   files exist as `.tga`).
4. Frame rate for animated lists (e.g. `flashpak.ifl`).
