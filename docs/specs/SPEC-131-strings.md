# SPEC-131 — String packages (`strip/*.sp`)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — all 308 `strip/*.sp` in `pak0` (136), `zmain` (136), `pak2` (33), `gs` (2), `pak3` (1). No game binary was consulted.
Implementation: `tools/formats/strings.py` (uses `tools/formats/vfs.py`)
Related: SPEC-130 (menus reference strings as `^REF^`), SPEC-132 (fonts / character set)

## Purpose

All player-visible text that is not baked into pictures lives in *string packages*: menu labels and help,
mission briefings and objectives, dialogue subtitles, HUD/centre-print messages, deathmatch messages and
scoreboard layouts, key names, server messages, credits. Each package holds numbered, named entries with one
text per language. Menus refer to entries as `^PACKAGE_ENTRY^` (SPEC-130).

Which pak holds which edition **[data]**: `pak0` = original release (English/German/French), `pak2` = Gold
edition updates (bots, Control/Conquer modes, new team), `pak3` = Platinum (`menu_generic` ticker says
"PLATINUM EDITION"), `gs` = GameSpy patch (`str_server`, `sud2`), `zmain` = a Russian localisation of the
pak0 set (all 136 pak0 packages, 133 with changed text).

## Layout

Plain text, CR LF in all 308 files (the tool also accepts LF). Lines are trimmed; blank lines are ignored.
Keywords are upper case and start a line.

```
VERSION 1
ID <int>
REFERENCE <PACKAGE_NAME>
DESCRIPTION "<text>"
COUNT <int>
INDEX <int>
{
   [FLAGS <flag> [<flag> …]]
   [REFERENCE <ENTRY_NAME>]
   [NOTES "<text>"]
   TEXT_<LANGUAGE> "<text>"
   …
}
INDEX <int>
{ … }
```

| Line | Where | Meaning / observed values |
|---|---|---|
| `VERSION n` | header, 1st | always `1` (308/308) |
| `ID n` | header | package number, 0–253; unique among the effective packages (156 after pak override) |
| `REFERENCE NAME` | header | package name, `[A-Z0-9_]`. Equals the file name upper-cased except `str_server.sp` → `SV_TEXT` (all copies) |
| `DESCRIPTION "…"` | header | author comment |
| `COUNT n` | header | number of entries in the file (308/308 agree) |
| `INDEX n` | entry | entry number; strictly increasing, ≤ 184 (< 256); **gaps allowed** (16 files, e.g. `pak2/dm_control.sp` 0,1,4,6,8…) |
| `{` / `}` | entry | on their own lines |
| `FLAGS …` | entry, optional | space-separated names from the table below (1101 entries) |
| `REFERENCE NAME` | entry, optional | entry name. Missing in 2 entries (`menu_kosdm2` #1, `sib3` #2: reachable only by number). Duplicated within a package in 8 packages (see below) |
| `NOTES "…"` | entry, optional | author comment (18 entries) |
| `TEXT_ENGLISH` / `TEXT_GERMAN` / `TEXT_FRENCH` / `TEXT_BRITISH` | entry | text per language. English in every entry (5990); German 5747, French 5517, British 243 |

Order inside an entry: `FLAGS` (when present) usually first, then `REFERENCE`, `NOTES`, texts; the parser
accepts any order.

### Quoted values

- The value runs from the **first** to the **last** double quote on the line. Values contain unescaped inner
  quotes (101 lines in all copies, e.g. `"JOHN: Say "hi" to him for me."`, `"*xv -80 yv 10 string "…%d…""`), so a
  quote-to-next-quote rule fails; the last-quote rule parses every file. Values never span lines.
- The only escape is the two characters `\n` = line break (2827 uses). TAB characters occur literally inside
  56 values.
- Characters are single bytes. Western texts are **Windows-1252** **[data: the stock fonts have a glyph at
  0x9F (Ÿ), which exists in cp1252 but not Latin-1, and glyphs at 0xA3 £, 0xA9 ©, 0xAE ®, 0xBF ¿, 0xC0–0xFF]**.
  In `zmain` the `TEXT_ENGLISH` slot holds **Russian in Windows-1251** (zmain also replaces the fonts with
  Cyrillic glyphs at 0xC0–0xFF, SPEC-132); its German/French texts are the originals with some bytes changed
  (0x83, 0x9C appear in German text, 124 times) — a conversion artefact. Files carry no code-page marker.

### Inline codes inside texts

| Code | Count | Meaning |
|---|---|---|
| `$P_RED` `$P_GREEN` `$P_YELLOW` `$P_CYAN` | 66 / 60 / 69 / 33 | colour prefix for console/HUD messages; no terminator, text follows directly (`$P_REDSUICIDE FLOOD…`) **[hyp: sets the print colour]**. Only these four occur; the full set is UNKNOWN |
| `%s` `%d` `%hd` `%hu` | 1384 / 55 / 261 / 75 | C printf conversions filled by the game |
| `%p` `%ps` `%n` | 832 / 42 / 218 | game-specific: in menu emotes (SPEC-130 `<emote>`), `%n` = the speaking player and `%p` = the target player **[data]**; in packages `%p` stands for a player name in kill/flag messages **[hyp]**; `%ps` UNKNOWN (possessive?) |

### Flags

| Flag | Entries | Where it occurs → meaning **[hyp]** |
|---|---|---|
| `SP_FLAG_CAPTIONED` | 875 | level packages (`tut`, `sud3`, `irq2b`…), lines like `JOHN: …` / `FACTORY WORKER: <…>` → dialogue subtitle |
| `SP_FLAG_ALWAYS_PRINT` | 173 | with CAPTIONED or CENTERED; computer/radio lines and CTF/Arsenal events → shown even when subtitles are off |
| `SP_FLAG_CENTERED` | 174 | objectives, `GENERAL` warnings (`AUTO-SAVING GAME`) → centre-screen print |
| `SP_FLAG_TYPEAMATIC` | 26 | level intro "Location / Date / Time" texts → typewriter effect |
| `SP_FLAG_CREDIT` | 30 | `TSR1` credits: value = 9 `;`-separated fields `x;y;a;b;c;R;G;B;picture`, e.g. `50;50;2;1;0.5;255;0;0;console/logo_raven`. x, y look like percent of the screen (10–88), R G B 0–255, picture = `pics/<picture>.m32` (90/90 exist). a = 2–3, b = 1, c = 0.5: hold / fade times (UNKNOWN) |
| `SP_FLAG_LAYOUT` | 28 | deathmatch packages: text is a Quake II–style HUD layout program (`*xv -80 yv 10 string "…"`, `ctf_team_sb %hd %hd %hu %hd %hd`, `*` = `LAYOUT_RESET`) |

### Duplicate entry names

`menu_generic` `QUIT` ("Quit" / "EXIT GAME"), `menu_dm_generic` `DMFLAGS`, `MOVESCALE`, `START`, `key` `AUX25`,
`ENTER`, `menu_chat` `CREATE`, `menu_keys` `KNIFE`, `ger1` `LOCKED`. Which copy a name lookup returns is
UNKNOWN (open question 3).

## Semantics

- **Reference syntax** **[data]**: `^PKG_ENTRY^` = package `PKG` (header `REFERENCE`) + `_` + entry
  `REFERENCE`. Package names contain underscores (`MENU_DM_GENERIC`), so a reference is split at the longest
  package name that is a prefix and has the entry. `^…^` occurs in menu text, labels, tips, and inside command
  strings (`sset menu_nss ^MENU_DM_GENERIC_NSS^`).
- A menu loads packages with `<strings name>` → `strip/name.sp` (SPEC-130).
- An entry text used as a `<list>` label is a comma-separated list (`"No,Yes"`,
  `"Standard,Assassin,Arsenal,CTF,Realistic,Control,Conquer"`) **[data]**.
- **Language selection** **[data + hyp]**: cvar `sp_language` chooses the text slot. The language menu sets
  `sp_language 0` for the US flag, `2` for the French flag, `3` for the UK flag (`m_language*.rmf`), and
  `endgame.rmf` hides the end screen for `sp_language 1` — so 0 = ENGLISH, 1 = GERMAN, 2 = FRENCH,
  3 = BRITISH. Only 243 entries have `TEXT_BRITISH`; falling back to `TEXT_ENGLISH` when a slot is missing is
  **[hyp]**. The language command runs `reloadall` (reload packages) **[hyp]**.
- **Numbers**: (ID, INDEX) identify an entry without its name; IDs < 256 and INDEX < 256 would fit a 16-bit
  `ID·256 + INDEX` code, a plausible network/script form — UNKNOWN (open question 4).
- **Pak override**: the copy in the highest-priority pak replaces the whole package. With zmain above pak2/pak3,
  42 menu references of the Gold/Platinum menus become unresolvable (zmain's packages are 1.0-era); with zmain
  below pak2/pak3, all resolve but Gold-edition packages are English. The real order is UNKNOWN (SPEC-130 open
  question 1). `vfs.py` defaults to `gs, pak3, pak2, zmain, zsond, pak1, pak0`.

## Validation

`python tools/formats/strings.py validate --out <dir>`:

- **308 files parsed, 0 errors**. Checks: header present, VERSION 1, COUNT = entries, INDEX increasing and
  < 256, every entry has English text, known flags and language keys, file name = package name.
- Warnings (stock data quirks, listed above): file-name mismatch `str_server`/`SV_TEXT` (4 copies), duplicate
  entry names (16 package copies), index-only entries (4 copies). 16 packages have INDEX gaps (allowed).
- `^REF^` check over every `.rmf` and `.cfg` in every pak: **2409 references, 2403 resolve**, 0 shadowed
  (default order), **6 never defined in any package**: `MENU_DM_GENERIC_CONQUER_TITLE`,
  `MENU_DM_GENERIC_CONQUER_DESCR` (`pak2/menus/dm_conquer.rmf`), `MENU_SCALE_FOG` (after `</stm>` in
  `m_scale.rmf`), `MENU_KOSDM2_INFO`, `MENU_IRQ3A_RECOMMENDED`, `MENU_SUD3_RECOMMENDED`. With zmain above
  pak2/pak3 (`--order gs,zsond,zmain,pak3,pak2,pak1,pak0`): 2361 resolve, 42 shadowed, 6 missing.
- Output: one JSON per package copy in `openmerc-data/ui/strings/<pak>/strip/*.sp.json` (text decoded
  cp1252; zmain English slot decoded cp1251 per value by a tool heuristic), report in
  `openmerc-data/ui/strings_validation.txt`.

## Open questions (with black-box checks)

1. Full list of `$P_` colour codes and their colours. Check: in the original, trigger a flood-protection
   message (say text 10× quickly → `$P_RED…FLOOD PROTECTION`) and a `say_team` line (`$P_GREEN`); screenshot.
2. `%ps` meaning and `%p` substitution. Check: CTF flag pickup message (`$P_YELLOW%p has acquired…`) on a
   local server; screenshot the console.
3. Duplicate `REFERENCE` resolution (first or last copy). Check: `menu quit` shows "Quit" or "EXIT GAME".
4. Is the (ID, INDEX) pair used on the wire or in scripts? Check: capture a server→client print of a
   deathmatch message (e.g. frag message) and look for a 16-bit code instead of text.
5. Fallback for missing `TEXT_BRITISH`/`TEXT_GERMAN`/`TEXT_FRENCH`. Check: `set sp_language 3; reloadall`
   and open a menu whose entries lack British text.
6. Meaning of flags (subtitle on/off interplay with `cl_subtitles`, typewriter speed, credit fields a/b/c).
   Check: play the first minutes of `tsr1`/`trn1` with `cl_subtitles 0` and `1`; watch the credits.
7. Which code page the engine assumes (affects only non-ASCII text): render `^MENU_GENERIC_…^` German umlauts
   and compare with SPEC-132 glyph pages.
