# SPEC-121 — Ghoul text formats: .gsq, .gpm, .gbm, .gpl, .ifl

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files under `ghoul/` in the stock paks (`pak0.pak`, `pak2.pak`); no binaries were examined.
Related: SPEC-120 (the binary `.ghb` model container these files refer to), SPEC-102 (.ifl).

## Purpose

Small text files that select which `.ghb` models, sequences, body parts, skins and bolt-on
attachments make up a model.  All of them are plain Latin‑1/ASCII text with CR‑LF line ends.
Names are case‑insensitive everywhere (the data mixes `MESO_PLAYER.ghb`, `meso_player`,
`_LBANG`, `_lbang`).

Stock counts: 308 `.gsq`, 56 `.gpm`, 52 `.gbm`, 4 `.gpl`, 259 `.ifl` (pak0 + pak2).

## Common lexical rules (`.gpm`, `.gbm`)

The two "definition" formats are token streams, not line formats:

* `//` starts a comment that runs to the end of the line (seen in `Dekker.gpm`, e.g.
  `{}//hey, look. another comment!`).
* Tokens are: a double‑quoted string (quotes are not part of the value, may contain spaces,
  e.g. `"The December Brigade"`, `"Capture, Inc"`), `{`, `}`, or a run of non‑space
  characters.
* In header positions a value may be written as a bare word, a quoted string, or an
  **empty block** `{}`; `""` and `{}` both mean "empty".  `.gbm` files also use the
  literal word `XXX` as a placeholder in unused positions.
* Line breaks carry no meaning (the files happen to put one item per line).

## .gpm — player model definition (`ghoul/pmodels/*.gpm`)

| # | Token | Example (Dekker.gpm) | Meaning |
|---|---|---|---|
| 1 | dir | `enemy/meso` | model directory relative to `ghoul/` |
| 2 | menu model | `meso_menu1` | `.ghb` used in the player‑setup menu (`<dir>/MESO_MENU1.ghb`) |
| 3 | game model | `meso_player` | `.ghb` used in game (`<dir>/MESO_PLAYER.ghb`) |
| 4 | set | `playerboss` | base name of two `.gsq` lists in `<dir>`: `<set>.gsq` (sequences) and `<set>_poff.gsq` (parts switched off) |
| 5 | team | `"The Order"` | team name; groups in `.gpl` files match these names (see below) |
| 6 | sound dir | `enemy/dth/rad` | voice directory (`dth` = death/pain sounds; `rad`, `arab`, `russ`, `jpn`, `fem`, `skin`) — exact use UNKNOWN |
| 7 | face prefix | `"f_amu_"`, `f_b_6_`, `""`, `{}` | prefix of face skin variants; textures with this prefix exist in `ghoul/comskin` with suffixes `n`, `f`, `b`, `d`, `m`, `p` and `nt1..3`, `ft1..3` (e.g. `f_amu_n.tga`, `f_amu_p.tga`). Empty for some models. |
| 8 | `{ skins }` | `{ a a_xdekker 0  b b_xdekker 0  f f_dekker 0  h h_dekker 0 }` | triples *(material, skin, int)*: the `.ghb` material named *material* (SPEC-120 materials `A`, `B`, `F`, `H`…) is drawn with texture *skin* (`<dir>/a_xdekker.tga`, or `ghoul/comskin/…`). The int is 0 in all stock files (UNKNOWN). |
| 9 | `{ bolt-ons }` | `{ boltons/menuassault.gbm wbolt_hand_r to_wbolt_hand_r 1.0 }` | quadruples *(gbm path relative to `ghoul/pmodels/`, bolt name on this model, bolt name on the bolt‑on, scale)*. Both bolt names are node names in the respective `.ghb` files (SPEC‑120 node list, e.g. `WBOLT_HAND_R` in `MESO_PLAYER.ghb`). Scales seen: 1.0, 1.1428…, 1.1828…, 1.1878… |
| 10 | `{ parts }` | `{ _lbang 0  _rbang 0  _ponytail 0 }` | pairs *(mesh name, 0/1)*: switch a mesh of the game model off (0) or on (1) (`Princess.gpm` uses `_LBANG 1`). Mesh names are SPEC‑120 mesh records. |
| 11 | trailer | `""` | always an empty string in stock data (UNKNOWN) |

Block sizes in stock data: skins 0/9/12 tokens (0, 3 or 4 triples), bolt‑ons 4–24 tokens,
parts 0–20 tokens.  Every block length is an exact multiple of its tuple size in all 56 files.

## .gbm — bolt-on definition (`ghoul/pmodels/boltons/*.gbm`)

Same token rules and the same first seven positions as `.gpm`:

| # | Token | Example | Meaning |
|---|---|---|---|
| 1 | dir | `enemy/bolt` | model directory (all 52 stock files use `enemy/bolt`) |
| 2 | menu model | `acc_nightvision2`, `w_assault_rifle` | `.ghb` for the menu |
| 3 | game model | `acc_nightvision2`, `""` | `.ghb` for the game; empty for the `menu*.gbm` weapons (menu only) |
| 4 | set | `acc_nightvision` | base name of a `.gsq` in `<dir>` (`acc_nightvision.gsq` lists `acc_nightvision`, `fade_nightvision`) |
| 5–7 | unused | `""`, `{}`, `XXX` | placeholders for the `.gpm` team/sound/face positions |
| 8… | skins | `acc_hat_baseball acc_hat_baseball_dgrey 0` | zero or more *(material, skin, int)* triples **without** braces, or `{}` for none |

The game‑model `.ghb` of a bolt‑on contains the `to_*` node named in the `.gpm`
(e.g. `TO_ABOLT_HEAD_T`), presumably the point aligned with the host's bolt node.

## .gsq — sequence / part list (`*.gsq`)

Line based: one item per non‑empty line, surrounding white space ignored.  A line
`#name` includes `name.gsq`; it is looked up in the directory of the including file, then in
`ghoul/comskin` (`#commoves`, `#playmoves`, `#pistolmoves` live there; `#tightboss_poff`
is in `enemy/meso`).  All 335 includes in stock data resolve this way; no include cycles.

Two kinds, distinguished by the file name:

* **`<name>.gsq`** — the animation sequences of a model.  Items are either `.ghb` file names
  in the same directory (object models: `banner.gsq` = `BANNER`, `PAIN`; each `.ghb` then
  contributes its sequences) or sequence names inside the model's `.ghb` files
  (`pistol1.gsq` = `pistol1` followed by `fire1_a`, `fire1_b`, …; `commoves.gsq` =
  `cch_ecch2walk_n_a_n`, … matching sequences of `enemy/meso/MESO_*.ghb`).  Cross‑check:
  1113 of 1181 non‑include items name an existing sequence or `.ghb`; the remaining 68
  (e.g. `std_esalute_n_mrs_n`) name sequences that are not present in any stock `.ghb`.
* **`<set>_poff.gsq`** — "parts off": mesh names (SPEC‑120 mesh records) to hide for a body
  type, e.g. `tightboss_poff.gsq` hides all `GZ_*` gore zones, all `_Cap_*` gore caps and the
  non‑"boss" alternatives of every body part.  5752 of 6520 items name a mesh of some
  `enemy/meso/*.ghb` (the rest belong to other model families or are stale).

Applying a `.gpm`: hide every mesh listed by `<set>_poff.gsq` (includes expanded), then apply
the `.gpm` parts block (0 = hide, 1 = show).  Verified visually: `MESO_PLAYER.ghb` with
`Dekker.gpm` renders one consistent body (render `meso_player_dekker_f819.png`).

## .gpl — player list (`ghoul/pmodels/*.gpl`)

Line based: names of `.gpm` files (without extension, lower case), separated into groups by
blank lines.  Groups correspond to teams: in `ctf.gpl` the group
`dekker gimp shadow tank silencer stiletto` are exactly the models whose `.gpm` team is
"The Order", `sabre … princess` are "Ministry of Sin", etc.; `everybody.gpl` ends with the
"Noteam" individuals.  `pak2` adds a "Capture, Inc" group and `individuals.gpl`.
Which list the game uses for what (menu pages, CTF teams) is UNKNOWN.

## .ifl — image file list (`*.ifl`)

The file format itself is specified in SPEC-102 (`tools/formats/ifl.py`); this section only
records how Ghoul models use it.  Line based: one image file name per line, relative to the `.ifl`'s directory
(`banner.ifl` = `banner1.tga` … `banner5.tga`; `trashcan.ifl` = `trashcan.tga`).  A `.ghb`
material slot refers to a texture by name; the name resolves to `<name>.ifl` (list) or
directly to `<name>.tga` in the model directory or `ghoul/comskin`.  The material slot's
per‑frame "image index" (SPEC‑120) selects the entry, which animates textures
(`FLASHPAK` cycles 0…9, `tv` cycles 0…5).  For character materials the list holds all
available skins (`enemy/meso/a.ifl` lists 66 `a_*.tga` bodies) and the `.gpm` chooses one.
Longest stock lists: 437 and 387 lines.

## Validation

`tools/formats/ghoul_text.py validate <ghoul dir>`: 259 `.ifl`, 308 `.gsq` (all includes
resolved), 56 `.gpm`, 4 `.gpl`, 52 `.gbm` parsed, 0 failures.

## Open questions

1. `.gpm` skin/`.gbm` skin integer (always 0) — hypothesis: skin variant index or flags;
   verify by changing it to 1 in a copy and observing the menu model.
2. `.gpm` trailer string (always `""`) — hypothesis: optional extra file/name; verify by
   setting a value and watching the console for a load error naming it.
3. `.gpm` face prefix suffix letters `n/f/b/d/m/p`, `nt1..3` — hypothesis: face states
   (normal, firing/fear, blink, dead, mouth/talk, pain; `t` = talking frames); verify by
   observing a player's face in game while idle, shooting, hurt, dead.
4. Use of the sound directory (`enemy/dth/<voice>`) — hypothesis: death/pain voice set;
   verify by listening to a player model dying.
5. Which `.gpl` list is used where — verify in the multiplayer player‑setup menu.
