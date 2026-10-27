# LEGAL: replacing the original's fixed tables

Package: legal-clean (C team), 2026-10-26. Owner decision: no data copied from the original in the open tree; the
engine runs SoF without reading SoF.exe. SPEC-145 (import-time extraction) is SUPERSEDED and not implemented.

## Direction table (SPEC-201 App. A, SPEC-014 13)

`engine/src/common/header/om_geodesic.h` (`OM_BuildDirs`, header-only C, used by client, server and Ghoul).
Icosahedron with poles N=(0,0,1), S=(0,0,-1), upper ring U[k]=(r cos 72k, r sin 72k, z), lower ring
D[k]=(r cos(72k+36), r sin(72k+36), -z), z=1/sqrt5, r=2/sqrt5. 20 faces in four bands:

1. (N, U[k], U[k+1]) for k=0..4
2. (U[k+1], D[k], D[k+1]) for k=4,0,1,2,3
3. (D[k], U[k+1], U[k]) for k=0..4
4. (S, D[k+1], D[k]) for k=0..4

Each face (A,B,C) is walked in rows i=0..5: e1=slerp(A,B,i/5), e2=slerp(A,C,i/5), points slerp(e1,e2,j/i), j=0..i.
A point is appended the first time it appears; the two poles are skipped. This yields 250 unit vectors. The layout is
the classic frequency-5 geodesic sphere; the order is chosen so that indices match the protocol's meaning. Checked
against the private appendix (tools/net/om_tables.py --compare-private): same order, max difference 7.75e-7, no
value bit-identical. The Ghoul table is the six axes (+x,-x,+y,-y,+z,-z) followed by these 250.

Effect: a direction sent by an original server decodes to a vector within 1e-6 of what the original shows; the
SHA-256 in SPEC-014 13 and the encoded bits no longer match the original. Model files store indices, not vectors.

## Dictionaries (SPEC-201 App. C, SPEC-203 App. A)

Both dictionaries are empty (`SOF_CS_DICT_SIZE`, `SV_CS_DICT_SIZE`, `kGhoulDictSize` = 0). Writers always use
the literal-string path that already existed; a reader that meets a dictionary code (only an original peer sends
one) reports an error (configstrings) or reads an empty string (Ghoul). Bandwidth cost: a few hundred bytes more
per connection.

## Huffman weights (SPEC-201 App. D)

`OM_HuffWeight`: lowercase letters 100 (eaotinsrlhdc) or 50, '_' 90, '/' and '.' 30, digits 20, uppercase 10,
' ' and '-' 5, others 1. Any weights give a valid code; these favour model and path names.

## Other

- SPEC-202 6.3 move-checksum salt is taken from the generated direction bytes (OpenMerc to OpenMerc only).
- `cl_demo.c`: demo dictionary lookups removed; original demos with dictionary codes do not play.
- Removed generators: `engine/src/ghoul/tools/gen_ghoul_tables.py`, `tools/net/gen_sof_tables.py`.
- Moved to `docs/private/`: SPEC-201 App. A/C/D, SPEC-203 App. A, SPEC-600 2 (descriptor offsets).
- `tools/net/om_tables.py`: Python twin of the generator; `sofnet.py` uses it when docs/private is absent.
- `tools/legal_scan.py`: scans tracked files (or the export with `--export`).

## Tests (2026-10-26)

- Engine x86 Release build (build-LEGAL), game, player and sound modules: built.
- `omghoul_test`: 1240 checks pass, 0 fail. The original-capture string replay now differs in 1735 of 1739
  strings (expected, informational).
- `sofnet.py selftest`: passes with the private tables and with `OM_SOFNET_TABLES=openmerc` (original-only vectors
  skipped).
- `legal_scan.py`: 0 findings; review items listed in docs/LEGAL_NOTES.md.
- In game, D:\Games\OpenMerc-legal-test (no SoF.exe in the folder), tsr1, tests/X1/xrun.ps1 + x1_sweep.cfg, ports
  29680/29681: mode A (original gamex86/player) and mode B (OpenMerc modules) both load the map, render, walk and
  fire, exit code 0; captures in openmerc-data\legal\runs\modeA-tsr1, modeB-tsr1.
- Not tested: play against an original 1.07f server/client (known to be incompatible now).
