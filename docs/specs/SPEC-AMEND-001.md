# SPEC-AMEND-001: Proposed amendments to SPEC-004 (engine functions used by sound modules) and SPEC-201 (svc_effect flag 0x20)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, gamex86.dll, Defsnd.dll, EAXSnd.dll, A3Dsnd.dll, version 1.07f
Depends on: SPEC-004, SPEC-201, SPEC-300, SPEC-400, SPEC-801
Answers: OPEN_QUESTIONS.md items 2 and 8

## Purpose
Answers two questions the clean team raised while implementing approved specs, as text to merge into the specs.

## Interface / layout

### Amendment A — SPEC-004, "Engine functions imported by name from SoF.exe": exact argument lists
All functions use the C calling convention (caller pops arguments). "string" = pointer to a NUL-terminated 8-bit string.

| Name | Arguments | Result | Behaviour relevant to a backend |
|---|---|---|---|
| Com_Printf | string format, … (printf-style) | none | Formats into an 8192-byte buffer (no length check: longer output corrupts memory in the original; a reimplementation should truncate). The text goes to the console, to the log file when `logfile` is set, and to the system console when `developer` has bit 2 or the engine is dedicated. While a remote-command redirect is active the text is appended to the redirect buffer instead (flushed when full). |
| Com_DPrintf | string format, … | none | Same as Com_Printf, but only when `developer` has bit 1 (value 1, 3, 5, 7 …). |
| Com_Error | int level, string format, … | does not return | Levels: 0 fatal (shuts down and exits with an error box), 1 drop, 2 disconnect, 3 "won error" — the effects of 1–3 are those of SPEC-801 §10 ("Any engine error"). Before any level the message is printed framed by `*` lines, the `current` savegame is deleted and the menus are closed. A second error while the first is handled is fatal: `Recursive Com_Error after: <first message>`. The sound modules only use level 0 and 1. |
| Com_sprintf | string destination, int size, string format, … | none | Formats into a 65536-byte scratch buffer, copies at most size − 1 characters and terminates; when the formatted length is ≥ size it also prints `Com_sprintf: overflow of <length> in <size>`. |
| FS_LoadFile | string name, pointer-to-pointer buffer (may be NULL), int unused | int length or −1 | The third argument is accepted and ignored (the shipped callers pass 0, 1 or leftover register values). Not found → −1 and the caller's buffer pointer is set to NULL. Buffer NULL → only the length is returned. Otherwise the buffer is allocated from zone memory with length + 2 bytes (file, `\n`, NUL; SPEC-300). |
| FS_FreeFile | pointer buffer | none | Releases a buffer from FS_LoadFile (identical to Z_Free). |
| FS_FOpenFile | string name, pointer to int handle | int length or −1 | Exactly two arguments. The handle is a plain integer: 1 … 20 for an open file, 0 when nothing was opened (not found, or all 20 handles in use). A name whose first byte is 0x01 is looked up without that byte and, on success, the resolved name is copied back over the caller's string starting at the 0x01 byte (the host path for a loose file, the normalised name for an archive entry or when `fs_createpak` is set); the caller's buffer must hold at least 128 bytes. The sound modules never use this form. |
| FS_FCloseFile | int handle | none | Handles that are 0 or not open are ignored. |
| Z_Malloc | int size | pointer | One argument (no tag). Memory is zero-filled. Failure is fatal: `Z_Malloc: failed on allocation of <n> bytes` (n = size + 16). |
| Z_Free | pointer | none | Freeing a pointer that did not come from Z_Malloc/FS_LoadFile is fatal: `Z_Free: bad magic`. |
| Z_Touch | pointer | none | Checks the block (`Z_Touch: bad magic` is fatal) and has no other effect. |
| irand | int min, int max | int in [min, max] | Shared engine generator; see SPEC-313 §9. |
| flrand | float min, float max | float in [min, max) | Same generator. |
| Cmd_AddCommand / Cmd_RemoveCommand / Cmd_Argc / Cmd_Argv / Cvar_Get / VectorLength / VectorNormalize / CL_GetEntitySoundOrigin | as in Quake II 3.19 (Cvar_Get: name, default value, flags; returns the cvar) | | unchanged |

Zone blocks carry a 16-byte header in front of the returned pointer; a backend must not depend on it.

### Amendment B — SPEC-201 §5 svc_effect, replace "flags 0x10 and 0x20 are never set and never read"
| Flag | Sent by the 1.07f game | Read by the 1.07f client | Client behaviour |
|---|---|---|---|
| 0x10 | never | never | — |
| 0x20 | never | yes | Every time the client refreshes the effect's attachment (entity-following or bolt-attached effects), the effect's second position is set to the end point of a point trace against world brushes only, from the effect origin along the effect's forward direction for 1024 units. The trace stops at the first world brush; entities are ignored. Effects placed with flag 0x01 are not refreshed (UNVERIFIED that their second position is then left unchanged). |

So a server may set 0x20 together with 0x02 or 0x04 to get "aim along the attachment until the wall" beams, but no
stock game code does so. Byte layout of the message is unchanged: flag 0x20 adds no payload.

## Behaviour
See the tables; they replace the corresponding text of the two specs.

## Constants
| Name | Value |
|---|---|
| Com_Printf buffer | 8192 bytes |
| Com_sprintf scratch buffer | 65536 bytes |
| File handles | 1 … 20 (0 = none) |
| Zone block header | 16 bytes |
| svc_effect flag 0x20 trace length | 1024 units |
| Com_Error levels | 0 fatal, 1 drop, 2 disconnect, 3 won error |

## Test vectors / how to verify
1. A probe sound module that loads `sound/null.wav` through FS_LoadFile once with third argument 12345 and once
   with 0 receives the same length and file contents both times.
2. A probe that calls FS_FOpenFile on an existing file 20 times without closing gets handles 1 … 20; the 21st call
   returns −1 with handle 0.
3. Com_DPrintf output appears with `developer 1` and `developer 3`, not with `developer 2` or `4`.
4. A probe server that sends svc_effect with flags 0x22 for an effect whose emitter draws a line to its second
   position, attached to a player: the line ends on the wall the player faces, and follows the view.

## Open questions
- Whether any third-party game module sets svc_effect flag 0x20 (none known).
