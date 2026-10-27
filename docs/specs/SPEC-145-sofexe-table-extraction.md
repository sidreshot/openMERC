# SPEC-145: Import-time extraction of the engine's fixed tables from the user's own SoF.exe

Status: SUPERSEDED by user decision 2026-10-26: no dependency on SoF.exe; tables are replaced by own/GPL/generated data (see docs/DEVIATIONS.md "Fixed tables", docs/impl/LEGAL-tables.md). Not implemented.
Former status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Evidence: static analysis of the PE file format plus a working prototype run on four SoF.exe builds (1.07f stock
and three patched/backup variants of the user's install); all extracted tables are byte-identical to the tables
currently generated into the OpenMerc tree. No table data is reproduced here.
Describes: SoF.exe 1.07f (and patched builds that keep the same data), file-format level only
Depends on: SPEC-014 §13, SPEC-201 Appendices A, C, D, SPEC-203 Appendix A
Amends: SPEC-014 §13, SPEC-201 Appendices A/C/D, SPEC-203 Appendix A (replace the listed table contents with a
reference to this procedure and the verification hashes below)

## Purpose

Four network-relevant tables of the original engine must match bit for bit for protocol compatibility. They are
currently copied into the specs and into generated source files. This spec replaces the copies by a procedure that
reads the tables at install/import time from the SoF.exe the user owns, so that neither the specs nor OpenMerc's
public source contain the original data.

## Interface

A tool (e.g. part of the asset importer) that takes the path of a SoF.exe and writes a private data file
(suggested: `<basedir>/openmerc/sof_tables.bin` or one file per table) that the engine loads at start. The engine
refuses to start network play (or the whole game, gatekeeper's choice) when the file is missing or a hash differs,
and prints which table failed.

Output tables:

| id | content | count | element encoding | canonical bytes for hashing |
|---|---|---:|---|---|
| T1 | unit direction vectors used for direction compression (SPEC-201 App. A) | 250 | 3 × float32 LE | the 3000 raw bytes, in file order |
| T2 | 256-entry direction table (SPEC-014 §13) | 256 | 3 × float32 LE | derived: the six axis vectors (+x, −x, +y, −y, +z, −z in the order SPEC-014 §13 gives), then T1 |
| T3 | Huffman symbol weights (SPEC-201 App. D) | 256 | float32 LE | the 1024 raw bytes |
| T4 | Ghoul string dictionary (SPEC-201 App. C) | 3713 | NUL-terminated byte string | the strings joined by one LF (0x0A), no trailing LF |
| T5 | configstring dictionary (SPEC-203 App. A) | 1927 | NUL-terminated byte string | as T4 |

## Behaviour

1. **PE parsing** (public Microsoft PE/COFF format). Read the DOS header, the PE signature, the COFF header and the
   optional header (ImageBase, SizeOfImage), and the section table. A virtual address V is inside the image when
   ImageBase ≤ V < ImageBase + SizeOfImage; it maps to a file offset through the section whose virtual range
   contains V − ImageBase (offset = PointerToRawData + (V − ImageBase − VirtualAddress)), and is invalid when it
   falls outside a section's raw data.
2. **Scanned sections**: every section with raw data whose characteristics mark it as initialized data
   (IMAGE_SCN_CNT_INITIALIZED_DATA, 0x40) and not code (IMAGE_SCN_CNT_CODE, 0x20 clear). Scan positions step by
   4 bytes from the section's raw start.
3. **T1 recognizer**: at each position, count consecutive 12-byte triples of finite float32 values whose squared
   length differs from 1 by at most 1e-4. A run of 200 or more triples is a candidate; continue scanning after it.
   In 1.07f there is exactly one candidate and it is 250 triples long. Accept it only if its SHA-256 matches.
4. **T3 recognizer**: at each position, read 256 float32 values; a window qualifies when every value v satisfies
   0 < v ≤ 1 and the window has more than 16 distinct values; continue after a qualifying window. 1.07f gives
   about two dozen qualifying windows; T3 is the one whose SHA-256 matches (it is also the only one with fewer than
   64 distinct values — use that only as a diagnostic, the hash decides).
5. **T4/T5 recognizer**: at each position, read 32-bit little-endian values as pointers while each one (a) lies
   inside the image, (b) maps to file bytes that form a string of at most 255 bytes terminated by NUL with no byte
   below 9 and no byte 127, and (c) is strictly greater (unsigned bytewise comparison) than the previous string.
   A run of 1000 or more pointers is a candidate; continue after it. 1.07f has exactly two candidates: 3713
   entries (T4) and 1927 entries (T5). Identify each by count and SHA-256.
6. **Verification**: every table must match its hash below. On mismatch, report "unsupported SoF.exe build" with
   the SoF.exe SHA-256, and do not write that table.
7. The tool must not need any other file and must not modify SoF.exe.

## Constants (verification hashes, SHA-256 of the canonical bytes)

| table | SHA-256 |
|---|---|
| T1 | 860c845e109cb7481e787074ca00ade9986efa00fa0dd710dfcaf502264671f7 |
| T2 | 3362bacbb561fe2c394b7413fa17d0b9facc93b58c165a65e90b1211fb9cf86f (as SPEC-014 §13) |
| T3 | 5b8d6f0fef1ea564945bc144797124c37f6a9a795d4509e9830d68cd8662c384 |
| T4 | 86a02d187a329781885955b0215009f5687de98abc273589d3cb07f872537fa1 |
| T5 | ed7431c33027f106a85cd63f558e00df947811e93e1aa4eb0639dcf6786ec953 |

Known SoF.exe builds that pass (SHA-256 prefixes of the whole file): 5BBFE7F7 (stock 1.07f), 22E3B9AA (a patched
build in the user's install), and two backup variants in the same install. Other builds: untested.

## Test vectors

1. Run the tool on the stock 1.07f SoF.exe: five tables written, all hashes match, T1 has 250 entries, T4 starts
   with the string "1" and ends with "_TURRET", T5 starts with "*1".
2. Run it on a file with one byte of T3 changed: T3 reported as mismatch, the other tables pass.
3. Run it on a non-SoF PE (e.g. a system DLL): no candidates, clear error.
4. Engine start without the data file: the refusal message names the missing file.

## Open questions

1. Gatekeeper/user: should the engine fail entirely without the file, or only disable network play (single player
   does not use T3–T5 except through the local loopback, which uses the same encoder)?
2. Whether gamex86-derived tables in other specs (animation, gore, prop, AI and weapon tables) should get the same
   treatment. Those come from gamex86.dll, not SoF.exe, and are not covered
   here.
