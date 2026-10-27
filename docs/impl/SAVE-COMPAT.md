# Save compatibility

The engine part of a save (COMM, SHOT, MPCM, the saved cvars) is the 1.07f
format. The game chunks that follow are the private state of the game module
that wrote them, and the original game module and openMERC's own game module
(mode B) cannot read each other's. Loading such a save used to hang.

openMERC saves carry the saved cvar `om_savegame` with the value
`openMERC 1 B` (own modules) or `openMERC 1 A` (original modules). The file
stays a valid save for the original game, which just creates the cvar.

`SG_SaveKind` classifies a save:

| found | kind |
| --- | --- |
| `om_savegame` | its mode |
| level chunk `OMCN` (mode B saves of 0.2.0) | openMERC B |
| level section (`LVLC`/`FTSZ`) without both | original modules |
| no level section, no marker (level-start autosave) | either |

The game module the server loads is predicted from the first
`gamex86` file in the loader's order; openMERC's module contains the export
name `OpenMerc_Module`.

A save the current modules cannot read is refused by `load` with a message,
shown greyed with "Not compatible" in the load menu, cannot be picked there,
and is skipped by "continue".
