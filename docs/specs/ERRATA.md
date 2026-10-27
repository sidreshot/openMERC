# Errata index

Corrections to approved specs found later. Implementers: check this list before relying on the listed sections.

| Spec | Correction | Where |
|---|---|---|
| SPEC-001 | imports #65/#66 take engine Ghoul objects (amended in place) | SPEC-001, SPEC-010 |
| SPEC-001 | import #55 returns the fog visibility distance | SPEC-313 "Corrections" |
| SPEC-004 | exact argument lists of the engine functions sound modules import; FS_LoadFile third argument ignored; file handles are integers 1–20 | SPEC-AMEND-001 A |
| SPEC-005 | collision BSP lump table and limits | SPEC-313 |
| SPEC-006 | `trigger_activate`, `trigger_sound` descriptions (amended in place) | SPEC-761 |
| SPEC-131 | answers to Q1/Q3/Q5/Q7; changing `sp_language` reloads packages itself | SPEC-313 "Corrections" |
| SPEC-140 | several opcode/built-in meanings corrected | SPEC-750 |
| SPEC-200 | heartbeat timing | SPEC-204 |
| SPEC-201 | svc_effect flag 0x20 is read (adds a 1024-unit trace for the second position) | SPEC-AMEND-001 B |
| SPEC-201 | §33 is an icon, not a colour | SPEC-310 |
| SPEC-201 | ghoul stream instance-state fields | SPEC-013 (when approved) |
| SPEC-302 | `gserver`, `sv_show_strip` (prefixes `STRIP-`), `sp_language` default 0, `sv_violence` bits = suppressed | SPEC-313 "Corrections" |
| SPEC-760 | rotating-door server flags 0x08/0x10/0x20 select swing bounds axis | SPEC-313 "Corrections" |
| SPEC-803 | status-text order | SPEC-310 |
| SPEC-003 | "alternate mode" is the weapon-holstered flag; event 4 carries an argument; slot 33 = sniper scope active; slots 29/30 = clip size | SPEC-600/601 (details: reverse team notes) |
| SPEC-120 | light record read one byte off; node rotation, normal table, vertex modes 2/3, material/slot fields | SPEC-014 |
| SPEC-204 | replaced by the merged revision (validate confirmed against a live master) | SPEC-204 rev. 2026-10-22 |
| SPEC-780 | `stats[4]` is clip capacity | SPEC-600 |
| SPEC-782 | callback 4 = reload started (not restricted fire); callback 12 = owner carries the CTF flag | SPEC-600 |
| SPEC-100 | kind bit 0x4 with alternate name = parental-lock substitute texture; flag 0x1000 = animate at `value` frames/s | SPEC-410 |
| SPEC-101 | lump 19 = fog-region polygons, lump 21 = fog regions, lump 20 feeds Ghoul lights only; alpha image without texinfo 0x800 is not drawn | SPEC-410 |
| SPEC-002, SPEC-400, SPEC-011/012 | cvar callbacks, 0x0800 quad-chain sorting, slot details; one-frame particles are visible; LIGHT_AMBIENT default 0.6 | SPEC-AMEND-002 |
| SPEC-700/701 | the directed-sound event (hearing kind 4) is sent when a player bullet hits the world; record offset 16 is the desired facing while alive | SPEC-710/711 |
| SPEC-722 | the `item_equip_armor` corpse exception applies only to action class 5 | SPEC-711 |
| SPEC-200..203 | black-box captures of the original refute 6 claims (challenge keyed by IP and port; frame game time = (frame+2)×0.1; ghoul instance-state field 12 length = count of list 2; spawn count re-randomised by `map`; 127.0.0.1 not LAN; float presence decided on unrounded values) and add 7 uncovered behaviours. Observed behaviour wins. | net-verification.md |
| SPEC-720 | `dmflags` bits: team play 0x80 (not 0x200), no friendly fire 0x100 (not 0x400), realistic damage 0x10 (not 0x40); team play always on in CTF and Control; realistic-damage zone predicates answered | SPEC-790 "Corrections to other specs" |
| SPEC-780 | force join is `dmflags` 0x4000 and applies to CTF, Control and Conquer; team spawn points used by every team game | SPEC-790 "Corrections to other specs" |
| SPEC-782 | the velocity/body-state spread term applies only in Assassin, Arsenal, CTF, Control and Realistic | SPEC-790 "Corrections to other specs" |
| SPEC-203 | configstrings 10, 32, 33, 34 and 1447..1450 | SPEC-790 section 2 |
| SPEC-143 | `.nvb` layout: the grid header's first value is the size of a second node set, "tail0" is a second link list, tail4 is the editor ignore flag; open question 2 (routing tables) answered | SPEC-740 "Amendments to SPEC-143" |
| SPEC-143 | `.wrs` waypoint flags and connections (open questions 4 and 5) | SPEC-741 "Amendments to SPEC-143" |
| SPEC-750 | ~~12.8 VEHICLE ids other than 1-5 complete at once~~ WITHDRAWN: scripts create an action only for ids 1-5, other ids never signal (SPEC-731 Amendment 1 applies to the vehicle's own command handling only) | SPEC-AMEND-003 6 |
| SPEC-711 | A8: HELI id 47 repaints the commanded helicopter (no spawn); ids 34-36 meanings; action class 5 is used by the dead-on-arrival start | SPEC-731 "Amendments" 2-3, SPEC-732 "Amendments" 1 |
| SPEC-700 | character type codes 9/10 (`m_x_tank`, `m_x_snowcat`) report team 1 | SPEC-731 "Amendments" 4 |
| SPEC-710 | the animal decision set also covers `m_x_raiderdog` | SPEC-732 "Amendments" 2 |
| SPEC-722 | tank/snowcat spawnflag 1 = starts disabled; aiming parameter meanings | SPEC-731 "Amendments" 5 |
| SPEC-760 | B5.1: a mover sound set with fewer than three waves plays wave 0 of the file for the missing entries (not silence) | SPEC-771 B.2 |
| SPEC-203 | configstring 5 is written by `environ_emergency_lights` and `environ_skyfade` | SPEC-771 A |
| SPEC-004 | `s_khz` values, underwater gain only on 16-bit PCM, fade lengths, EAXSnd differences, `s_initsound 0` test vector wrong for Defsnd/A3Dsnd, music intensity, overflow message, `.adp` playable length | SPEC-040 "Proposed amendments to SPEC-004" |
| SPEC-111 | open questions 1-4 answered (header bytes, piece types, trigger, loop flag) | SPEC-040 "Answers to SPEC-111 open questions" |
| SPEC-750 | 9.1: "Renner Error #1" is never raised; registrations always append, older entries stay pending; 9.2 delivery of several entries; 2e: an earlier move-done snaps to the latest destination | SPEC-AMEND-003 1, 2 |
| SPEC-750 | 5.3: a parameter declaration executed again creates a fresh variable (default or inline value), consumes no string | SPEC-AMEND-003 4 |
| SPEC-750 | 11: frame count with zero speed or zero distance (MOVE speed 0 never completes, ROTATE speed 0 at once); answers open question 5 | SPEC-AMEND-003 5 |
| SPEC-750 | 12.7: the table row for id 3 is an error (id 3 has no entry) | SPEC-AMEND-003 3 |
| SPEC-012 | caches and limits: image cache and decoded-chunk heap released at process exit | SPEC-AMEND-004 1 |
| SPEC-803 | menu models are never culled | SPEC-AMEND-004 2 |
| SPEC-040 | EAX 2.0 property detection, obstruction/occlusion via EAX manager, A3D per-pass details, `s_showtossed`/`s_usepvs` have no effect | SPEC-AMEND-004 3 |
