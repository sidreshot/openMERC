# Open questions from the clean team to the reverse team

Questions arise while implementing approved specs. The reverse team answers them by amending the specs
(through the gatekeeper); answered questions are moved to the bottom with the spec revision that resolved them.

## SPEC-004 sound module interface (from the OMsnd implementation, 2026-10-21)

1. [ANSWERED: SPEC-040] The test vector "`s_initsound 0` then `snd_restart`: no errors" fails with the original backend: Defsnd
   crashes (access violation) when this is done inside map tsr1. Is the vector wrong, or does it apply only
   outside a level?
2. [ANSWERED: SPEC-AMEND-001 A] Exact argument lists of the engine functions the backend imports: Com_Printf/Com_DPrintf/Com_Error,
   FS_LoadFile's third argument, the FS_FOpenFile handle type, Z_Malloc. (The Quake II forms were assumed and work.)
3. [ANSWERED: SPEC-040] Music: meaning of music mode 0; what a type-0 piece's flag does; what happens when a play-once piece ends;
   what `forcesong` plays exactly; what follows a designer value of 0xFF.
4. [ANSWERED: SPEC-040] Ambient: loop volume, random-sound intervals (CityRoughNeighborhood has interval 0 but three random waves),
   the fall-off curve of `distance`, whether a level start fades in.
5. [ANSWERED: SPEC-040] Does the 0.3 underwater gain also apply to music?
6. [ANSWERED: SPEC-040] Client-number range for `*` player sounds (1–32 assumed).
7. [ANSWERED: SPEC-040] Does the original play the converter click at the end of `.adp` files? Does a sound with a `cue` chunk loop
   forever when started as a one-shot?

## SPEC-201 network messages (from the effects spec work, 2026-10-21)

8. [ANSWERED: SPEC-AMEND-001 B] svc_effect flag 0x20 is read by the client (SPEC-201 says it is never read) — amend SPEC-201.

## SPEC-204 GameSpy (from the master list / heartbeat / query responder implementation, 2026-10-22)

Black-box observations (original 1.07f dedicated server from `D:\Games\SoF-oracle`, OpenMerc q2ded, the three
community masters; captures with pktmon, tool `tools/net/masterquery.py`). They answer or narrow the spec's own open
questions; please fold them into SPEC-204.

9. [ANSWERED: SPEC-AMEND-005 GS.9] Masters' reaction to a heartbeat (spec open question): all three answer with **only** `\secure\<challenge>`, sent
   from their UDP port 27900 to the heartbeat's source address. They never sent `\basic\` or `\status\` with it.
   sofvault and 333networks use 6-character challenges. OpenSpy uses 8 characters (`\secure\oyMl8D24` →
   `\validate\a5df+rC3Ni8A`). After the final (quit) heartbeat only OpenSpy queried again, with a plain `\status\`.
   All three listed the OpenMerc server within 14 s of its first heartbeat. Before that, OpenSpy's `sofretail` list
   was empty. In one run sofvault listed the original server although no probe from it arrived within 20 s.
10. [ANSWERED: SPEC-AMEND-005 GS.10] Delisting: entries are not dropped quickly. An original server stopped at about 23:50 was still listed by
    sofvault and 333networks at 00:08 and later (UTC+5). The masters also copy entries from each other: a server
    that sent heartbeats only to sofvault appeared in 333networks' list too.
11. [ANSWERED: SPEC-AMEND-005 GS.11] 333networks returns duplicate records within one list (44 records, 31 unique on 2026-10-22). This is consistent
    with 1.2 ("duplicates are not removed"); OpenMerc merges and de-duplicates before querying.
12. [ANSWERED: SPEC-AMEND-005 GS.12] Heartbeat to 255.255.255.255 (spec open question): with `public 1` and no hosts entry for
    `master.gamespy.com`, pktmon (all components, UDP port 27900) saw **no** packet leave the host (Windows 11).
13. [ANSWERED: SPEC-AMEND-005 GS.13] Query socket bind address (spec open question): 0.0.0.0 in every observed case, including an original instance
    whose game socket was bound to 127.0.0.1:27910 (query socket 0.0.0.0:28911). With 28911 taken, a second instance
    bound 28912: the port search works as described.
14. [ANSWERED: SPEC-AMEND-005 GS.14] Port search range: section 3 says "b = gamespyport + 0…4, ports b…b+99", which covers gamespyport + 0…103
    (28911–29014), but then says "in practice 28911–29110". Which is right? OpenMerc tries gamespyport + 0…199
    for now; that gives the same port whenever one of the first 104 is free.
15. [ANSWERED: SPEC-AMEND-005 GS.15] `sv_violence` of an unlocked original server is **63**: the oracle with default settings, and most live servers.
    Others report 32 or 31. SPEC-302 lists default "0", computed from the locks, polarity UNVERIFIED, with only five
    bits 1…16 named. Please specify the computation: which lock sets which bit, and what bit 32 means. Until then
    OpenMerc reports `0`, so per SPEC-803 a client with a non-zero `cl_violence` would hide it as locked.
16. [ANSWERED: SPEC-AMEND-005 GS.16] Responder edge cases confirmed identical on the original and on OpenMerc:
    - keywords are handled in the fixed order, wherever they are in the request;
    - `\basic\\packets\` repeats the basic block in datagram 1;
    - unknown words and plain garbage get `\final\\queryid\N.1`;
    - a 300-byte request gets no answer and does not advance Qid;
    - a 255-byte request is answered.
17. [ANSWERED: SPEC-AMEND-005 GS.17] Deliberate difference (for the record, no question): OpenMerc sends the shutdown heartbeat only for a public
    multiplayer server, not from every instance (section 2, test vector 8). With real masters configured, the
    original rule would make every client report itself to third-party hosts when it quits.

## G18 game module (world core, player, items, projectiles; 2026-10-22)

The game module shares memory with the engine and with player.dll, but several shared layouts are not in any
approved spec. G18 measured them black-box: an OpenMerc probe module (`game/probe/probe.cpp`) loaded into the
original 1.07f engine set one 32-bit word at a time and a headless client read back what reached the wire;
`Pmove` was called with marker callbacks. Values are in `docs/impl/G18.md`. Please confirm or correct them in
a spec (they are needed by I1b too):

18. [ANSWERED: SPEC-AMEND-005 G18.18] [G18] In-memory entity state (116 bytes; SPEC-202 gives the wire format only). Measured: number 0, origin 4,
    angles 0x10, yaw offset float 0x1C, modelindex 0x20, modelindex2 0x24, frame 0x28, skinnum 0x2C, effects 0x30,
    renderfx 0x34, solid 0x38, sound 0x3C, sound data 0x40, events int[3] at 0x44 and parameters int[3] at 0x50
    (inferred: only event 1 and parameter 1 were observable), effect slots 4 × {short id, bolt, scale} at 0x5C.
    SPEC-752 §5 says the four Ghoul ids sit at entity offsets 100–106 (inside the effect slots by this
    measurement) and that bit 0 of offset 48 (effects) flags Ghoul rendering — which is right?
19. [ANSWERED: SPEC-AMEND-005 G18.19] [G18] In-memory player state (200 bytes; SPEC-780 §1 points to SPEC-202 §4, which has only the wire order).
    Measured layout in docs/impl/G18.md; open: offsets 0x70, 0x7C, 0x80, 0xA0 (no wire field reacted), whether
    0x70 is the view-weapon type, and the exact type of the remote-camera origin (float in 1/8 units).
20. [ANSWERED: SPEC-AMEND-005 G18.20] [G18] User command (24 bytes, ClientThink argument): msec 0, buttons 1, lightlevel 2, lean 3 (signed),
    angles short[3] at 4, forward/side/up short at 10/12/14, fire time float 16, alt-fire time float 20.
21. [ANSWERED: SPEC-AMEND-005 G18.21] [G18] Player-movement block (import #81): Quake II order with the 24-byte command — state 0, command 0x1C,
    snapinitial 0x34, numtouch 0x38, touchents[32] 0x3C, viewangles 0xBC, viewheight 0xC8, mins 0xCC, maxs 0xD8,
    groundentity 0xE4, watertype 0xE8, waterlevel 0xEC, trace 0xF0 (Quake II signature, result by value),
    pointcontents 0xF4, then 0xF8 a callback taking one entity pointer (called with the ground entity when a jump
    starts; SPEC-780's "is entity a client" callback?) and 0xFC a float that stops all movement while positive
    (the knock-back stagger time). The move scale is the extra pmove byte (state +0x1A, 255 = full speed; 0
    stops horizontal movement). Nothing in the block changed with lean ±1: where does Pmove "return" the lean
    (SPEC-780 §5)? Total block size?
22. [ANSWERED: SPEC-AMEND-005 G18.22] [G18] Cvar record: name 0, string 4, latched string 8, flags 0x0C, value float 0x18 (SPEC-003 names only
    0x18). What are 0x10 and 0x14 (0 and 1 for a fresh cvar)?
23. [ANSWERED: SPEC-AMEND-005 G18.23] [G18] Surface record behind trace +40: the engine's in-memory texinfo (72 bytes per record; vecs 0–31, flags
    32 with the material id in the high byte, value 36). SPEC-313 Behaviour 4 describes a separate 500-entry
    texture table; which record does #25 expect, and where are the material and damage fields the game reads?
24. [ANSWERED: SPEC-AMEND-005 G18.24] [G18] Player-state inhibit byte (SPEC-780 §11 "bit 0x01 = attack not re-armed"): the original sends 0 in
    every deathmatch frame, also right after the first spawn and after a respawn (when, by §4 step 11 and §6,
    the attack is not re-armed) — capture `openmerc-data/g18/orig_walk`. What sets the byte, and when?
25. [ANSWERED: SPEC-AMEND-005 G18.25] [G18] Entities before the map entities: the first map entity is number maxclients + 17 in the original
    (maxclients 4 → 21, 8 → 25, 16 → 33), with `sv_globalrankings 1` one less. SPEC-760 B1.3/B1.4 create only
    the 8-entry body queue (maxclients + 1 … + 8). What are the other 8 constant entities (created where, with
    which classname), and the one created while the bot system is initialised?
26. [ANSWERED: SPEC-AMEND-005 G18.26] [G18] SPEC-780 §16 third-person animation: names only idle, ladder, forward jump, lean, grenade throw and
    waves. Which sequences does a deathmatch player model play while walking, running, strafing, moving backwards,
    crouching and crouch-walking, and what selects them (weapon pose table → sequence names)?
27. [ANSWERED: SPEC-AMEND-005 G18.27] [G18] SPEC-760 B1.5 material flags: on `dm/jpndm2` the original's configstring 16 is
    `nyyyyyynnnynnnnyyynnnnnnn`; OpenMerc's table gives the same except position 10 (no material in the table sets
    position 10 alone). Which surface types / materials set which positions, and which sounds does each position
    precache (observed on jpndm2: `Impact/Break/{Metal,Stone,Wood}`, `Impact/GlassBreak/GB{Big,Med,Small}`,
    `Impact/Explosion/Small`, `Impact/Surfs/{stone,wood}`, `impact/surfs/metal1..3`,
    `Player/Land/{Metal,Stone,Water,Wood}`, `player/foot/{metal,stone,wood}/*1..4`)?
28. [ANSWERED: SPEC-AMEND-005 G18.28] [G18] In deathmatch on `dm/jpndm2` (no such items on the map) the original registers the Ghoul models of
    `Items/Pick-ups/Ammo_single/ammo_556`, `ammo_rocket` (with their fade sequences) and `Items/Pick-ups/healthsm`,
    `healthbig`. Which rule precaches them (SPEC-781 / SPEC-790 give none)?
29. [ANSWERED: SPEC-AMEND-006 G18.29] [G18] SPEC-780 §1/§5 userinfo `skin`/`teamname` replacement, measured with the original module
    (`openmerc-data/g18/ui_orig*`, `dumpuser` by rcon, configstring timelines): the userinfo keys get the *plain*
    actual values (`skin` nosuchmodel → `dekker`, `teamname` → `The Order`; `skin` slick without `teamname` →
    `teamname slick`), while the player configstring gets a `*` before each replaced field
    (`G18two\*The Order\*dekker\-1`, `G18bot\*slick\Slick\-1`). A field that is usable is kept as written
    (`skin` Slick → `Slick`; `teamname` SLICK with model slick → `SLICK`: compared case-insensitively). Bots keep
    the `*` (`Kirt\*slick\slick\-1`, they process a copy of their `.bpf` userinfo). SPEC-780 says the userinfo keys
    become `*<value>` in deathmatch — please confirm the configstring/userinfo split. Also: the "stops before the
    client has entered a deathmatch game" point is ClientConnect's own call (before it marks the client
    connected); the engine's call right after it already writes the full, starred entry.
30. [ANSWERED: SPEC-AMEND-006 G18.30] [G18] SPEC-760 B9.3 / SPEC-001 export 0x5C "time = framenum·0.1": measured with a forwarding proxy around the
    original module (`game/probe/rfproxy.cpp`, `openmerc-data/g18/rf_orig/rf.log`), call n of RunFrame returns
    n × 0.1 whatever the argument: the engine calls RunFrame(0), RunFrame(1) while loading and then RunFrame(1),
    RunFrame(2) …, so the game time is (argument + 2) × 0.1 during play. The game counts its own frames (Quake II
    `level.framenum++`). Please state this in SPEC-760 B9.3 / SPEC-001 (it shifts every think of every package by
    two frames).
31. [ANSWERED: SPEC-AMEND-006 G18.31] [G18] SPEC-782 §7 rocket: a player rocket that explodes on a surface also sends a weapon-impact temp entity 6
    at the explosion point: impact kind 0 with the plane normal, direction = −normal, power 20, style 0, debris 0
    (oracle self-rocket captures `openmerc-data/g3/nl_orig_rocket`, `v0_orig_rocket`, `g18/inhib_orig`). Not in
    the spec; which projectiles send it (split rockets, slugs, grenades?) and with which power?
32. [ANSWERED: SPEC-AMEND-006 G18.32] [G18] SPEC-761 §8 alert decay "A −= (A·0.015 + 0.003)·0.7 (min 0)": the oracle has no lower limit. On tsr1
    (single player through G6b's proxy, `openmerc-data/g18/sp_orig_tsr1`) the status meter stats[11] =
    trunc(A × 100) reads −1 after 0.4 s, −2, … −14 after 11.4 s, i.e. A follows A' = 0.9895·A − 0.0021 from 0
    towards −0.2. Across the 31 SP maps (`spsuite_orig_*`) the meter goes negative only on levels that start with
    a cinematic (tsr1, ger1, irq1a …); on the others (arm1, kos2, sib2, irq3a …) it stays 0, rises after the
    player's shot and falls back to exactly 0. OpenMerc's reading: during a cinematic (and with spawning disabled)
    A decays without the lower limit and without sounds; otherwise as specified with min 0. Confirm.
33. [ANSWERED: SPEC-AMEND-006 G18.33] [G18] SPEC-780 §21 single-player start, measured through G6b's proxy: the begin packet of ger1 and tsr1 carries
    three prediction restarts (svc 29) and one svc_ric, arm1 (worldspawn `noweapons 1`) two and no weapon at all
    (stats[6] 0, view weapon 0). OpenMerc's reading: ClientBegin's new inventory (1), the reset (2), the load from
    the persistence cvars (3); weapon mode 0 ("value −1") stops after the reset with the sniper rifle and .44
    precache. The "level-script override (UNVERIFIED trigger)" would then be the weapon mode of SPEC-760 B1.4 — and
    the "−2" kit tsr1 shows is what the player module loads by default. Please confirm the trigger and the restarts.
34. [ANSWERED: SPEC-AMEND-006 G18.34] [G18] AMEND-005 G18.27 "`func_remote_camera` → position 24 = `p`": in the oracle ger1 (two cameras of type
    `volume` 2) has position 24 = `n`; tsr1 and dm/raven, which have a camera of type 4, have `p`. OpenMerc sets it
    only for type-4 cameras (SPEC-751 2.1: "a camera with type 4 also makes the level precache an extra resource
    group") — is that the rule?
35. [ANSWERED: SPEC-AMEND-006 G18.35] [G18] SPEC-751 2.1 flag 2 "watchers become non-solid and invulnerable": while such a camera is active the
    oracle keeps sending the player entity's packed box (entity state `solid` 0xA8180810 on tsr1, ger2, irq1a …,
    `spsuite_orig_*`), so the player is not made SOLID_NOT. What does "non-solid" change (clip mask, contents,
    a flag)? OpenMerc clears the clip mask and keeps the solid type.
36. [ANSWERED: SPEC-AMEND-007 G18.36] [G18] SPEC-780 §16 / SPEC-791 §1 / AMEND-005 G18.25, G18.28 — **Ghoul record order and bolt-on skins at a deathmatch
    level start.** Measured on dm/jpndm2 (captures `rec*_orig_dm_jpndm2`, object records as the client receives them):
    (a) records 1–13 gore models, 14–18 `Items/Projectiles` flashpak, C4, claymore, grenade, shell, 19 armour (the
    weapon precache, SPEC-782 12), then the player models; the weapon-precache sounds (292–341) also come before the
    first model's voices (353). (b) Per model, in list order: the model's record, then its bolt-on records in
    **reverse** .gpm order (shadow: `lrgbackpackblk`, `nightvision` → ACC_NIGHTVISION2 24, ACC_BACKPACK_LRG 25), then
    its body precache (`G_GUTS_LRG` 21 right after dekker's record 20); the voices of a model come before its body
    precache sounds. (c) Every model has a record of its own: 56 records for the 56 models, ghost (F_SIBTROOP) and
    iceman (F_SIBTROOP2) separately although their skins differ only in the face. (d) A `.gbm` skin whose material
    the object lacks is registered on the object's material anyway (`gina_guns.gbm` material `acc_gina_guns` →
    record ACC_GINA_GUNS with skin ACC_HOLSTER_RED; box_rick, brian_beret, joe_head, knife_jim likewise). (e) A
    `.gbm` without skins (`antenna.gbm`, `sword.gbm`) gets a record with **the player model's last skin entry**
    (iceman's `f_sibtroop2` on ACC_ANTENNA, ninja's `h_ninja` on ACC_SWORD) — not an all-skins record (SPEC-791 §1).
    (f) G18.28's UNVERIFIED part: the records of the weapon kinds 18–28 and the goggles (32) are kept — 12 empty
    records between AMMO_ROCKET and MEDKIT. (g) Ammo-box pickups on the map get one-skin records keyed by their skin
    (BOX with AUTOBOX, PISTOLBOX, SHOTGUNBOX, PISTOL2BOX only, in map order) besides the level-start all-skins BOX
    record. (h) A bolt-on record has one sequence (its `.ghl`), no separate base-sequence registration. (i) The
    in-view records come after the mode's 9 mm pickup (W_PISTOL1 155, then KNIFE 156 … THROWHAND 166, each once).
    OpenMerc implements (a)–(h) as observed; with them 155 of 167 records are identical in place, the rest is (i)
    (G9's mode setup precaches its in-view models at once). Is (e) a rule or a leftover value, and is (b)'s
    record/bolt-on/body order inside one model right (only the record order is visible)?
37. [ANSWERED: SPEC-AMEND-007 G18.37] [G18] SPEC-780 §21 single-player loadout and SPEC-780 §15 helpers — two black-box findings without spec text.
    (a) The oracle's single-player begin packet carries one `svc_ric` with one event: header 0x30 (event 0 = player
    variable A, long), value −1 (tsr1 `a6sp_orig_tsr1`, ger2) — none on arm1 (`noweapons`, the −1 path of AMEND-006
    G18.33). OpenMerc now sets variable A to −1 after the loadout's reset (as SPEC-780 §4 does in deathmatch). Is
    that the rule (also for `setweapons 1`), and where in the loadout? (b) The original's first rocket after a
    deathmatch respawn gets entity number 98 where OpenMerc's (without §15 helper volumes) got 97 (`inhib_orig/om`):
    one extra entity per living deathmatch player, presumably the §15 gun volume. When in the frame are the §15
    helpers created and freed, and are they made once and moved (OpenMerc) or re-made every frame like the
    characters' `Nug Buddy` (SPEC-700 §16)? "Not solid" (SPEC-700 §16, server flags 0x82): how do shots then hit
    them (SPEC-780 §5 "a hit on an auxiliary hit volume counts as its player")?
38. [ANSWERED: SPEC-AMEND-007 G18.38] [G18] SPEC-760 §B4.5 toss landing after a touch that changed the movetype. On trn1's end the helicopter (FLY) sits
    in a `func_pushkillbox`; the push's touch kills it and sets NOCLIP with the fling velocity (−560, 0, 0); B4.5's
    landing (normal z > 0.7 → ground entity, MOVETYPE_STEP, step physics at once) then overwrote that and the
    helicopter never moved, while the original's explosion lands 48 units along the fling one frame later (G4,
    `kb_om_g4trn1`). OpenMerc now skips the landing when the movetype changed during the push. Is that the
    original's rule (or does it skip the landing when the entity started inside the toucher)?
39. [ANSWERED: SPEC-AMEND-007 G18.39] [G18] SPEC-780 §16 "Animation playback speed follows the movement speed in 13 classes (1 ÷ speed factor above
    1.875, 1.75, …, 0.5 …)": what is the speed factor? Black box `spd_orig` (jpndm2, forward speeds 25–200 units/s,
    normal movement scale): the player instance's speed index never changes (Ghoul state field 2 is never sent), so
    it is not the movement speed itself. OpenMerc reads it as the movement scale (the value the game writes into the
    pmove block, SPEC-790 B3/B5/B11): index from q = 1 ÷ scale with the listed thresholds, 4 at scale 1, class 5 with
    a leg injury. Is that right, and is the class applied every frame or only when a record is chosen?
40. [ANSWERED: SPEC-AMEND-007 G18.40] [G18] SPEC-780 §21 / AMEND-006 G18.33 "the player module is reset": a reset of the kept inventory object or a new
    object? With a reset (RemoveAll) OpenMerc's single-player `gamemap` (tsr1 → tsr2) faulted at the kit's first weapon
    switch: the kept object still held the previous level's view-weapon instance (G10's report). With a new inventory
    object at the reset, the begin packets match the oracle (`gm_orig2` / `gm_om2`: tsr1 3 prediction restarts + 1
    svc_ric, tsr2 2 + 1, trn1 2 + 1; stats identical on trn1). The oracle creates no inventory at ClientBegin after a
    `gamemap` (one restart fewer than on the first level), so it keeps the object until the loadout. Which is it?
41. [ANSWERED: SPEC-AMEND-007 G18.41] [G18] SPEC-752 3.1 GAME 0x618 "eight int32 mission-statistics counters accumulated over the levels of the mission"
    and SPEC-700 §11 "Counters … enemies killed, friendlies killed, score, dead hostages, total characters killed": which
    are the eight counters (and their order), when exactly are they added (level change, `updateinvfinal`), who resets
    them, and where does the end-of-mission screen read them (stats, layout, cvars)? Needed to fill GAME 0x618 (G10).
42. [ANSWERED: SPEC-AMEND-007 G18.42] [G18] SPEC-780 §16 / AMEND-005 G18.26 airborne and landing sequences of a deathmatch player (`spd_orig`, jpndm2,
    one client walking forward): after the spawn drop (spawn point lift) the original plays `std_l_n_r_2`; walking off
    a ledge it plays `std_jstraitup_up_l_2` while falling and `std_waim_fwd_r_2` after landing; standing with the 9 mm
    it shows `std_a_fwd_p_cb` for 0.2 s, then `std_a_fwd_p_2`. §16 names only the forward jump
    (`std_jrun_fwd_a_a`, horizontal speed > 250) and "a jump is followed by a landing". What are the airborne
    (falling, jumping straight up) and landing choices, and when is the `_cb` standing entry used?

## G6a script VM (SPEC-140/141/750/751/752; 2026-10-22)

1. [ANSWERED: SPEC-AMEND-003] **SPEC-750 9.1 "Renner Error #1".** Running every stock script with the VM shows five that register a signal
   variable again while an earlier registration of it is still pending: `irq1a/hawk_leave.os` 0x064a/0x0667 (two
   actors, same `sig1`, one WAIT_SIGNALS after both); `kos3/garage.os` 0x040a/0x0455 (`sig1`) and 0x042c/0x0477
   (`sig2`), different movers; `irq2a/irq2a_bridge.os` 0x0499 then the watch handler at 0x052b (`sig`; the first
   actor is dead, so its signal never comes); `kos3/ending.os` 0x0ba2/0x0bce register `sig1`/`sig2` for AI runs that
   are never waited for, so the WAIT_SIGNALS at 0x0e6a is satisfied at once and the moves at 0x0eb6/0x0ed8
   re-register while the 0x0e26/0x0e48 moves are pending; `trn1/trn1_heli2.os` 0x1205 then 0x12bd (`sig1`, same
   helicopter, retreat branch). With the rule as written these levels would abort. What exactly raises the error
   (same entity? same kind? only while waiting?), and what happens to the older pending entry otherwise (kept,
   replaced)? Until answered OpenMerc keeps both entries (DEVIATIONS, `strict_signal_reuse`).
2. [ANSWERED: SPEC-AMEND-003] **SPEC-750 12.7** says 341 table entries and lists id 3 among the ids without an entry, but the table has a row
   for id 3 (kind 0, no animation, i.e. the default behaviour). Harmless; please correct the text.
3. [ANSWERED: SPEC-AMEND-003] **SPEC-750 5.3:** a parameter declaration executed again (name already bound): is the binding kept, or is a new
   parameter string consumed? Stock code never jumps back over declarations; OpenMerc keeps the binding.
4. [ANSWERED: SPEC-AMEND-003] **SPEC-750 11:** a timed MOVE/ROTATE with speed 0 (or a duration giving speed 0) and a non-zero distance divides
   by zero in the frame count; OpenMerc uses one frame.

## Gatekeeper notes on approved specs (2026-10-22)

1. [ANSWERED: SPEC-AMEND-005 GK.1] **SPEC-770 5.8 (fall over):** the game-modes analysis (WP10) reports that in the original a prop that should tip
   over is never told which entity to move. Verify in game (push a "can be pushed over" prop, Table 1 spawnflag 4)
   before implementing; implement 5.8 as written only if the prop really falls.


## I3 Ghoul model system (SPEC-010..014, 120, 121; 2026-10-22)

Interpretations OpenMerc chose where the specs leave a gap; please confirm or correct.

1. [ANSWERED: SPEC-AMEND-005 I3.1] [I3] **SPEC-013 4, delta base.** "The base is the state stored for the client's acknowledged frame." An instance
   that was equal to its base in that frame was not written, so nothing is stored for it there, and falling back to
   the baseline would resend it every other frame. OpenMerc uses the newest state stored at or before the
   acknowledged frame (frame offset = current - that frame) and keeps that state when an acknowledgement prunes.
   Is that what the original does? (Check: in a capture, record offsets of a still instance should keep growing.)
2. [ANSWERED: SPEC-AMEND-005 I3.2] [I3] **SPEC-012 5, notes while paused.** The raw phase of a paused instance is A, a *time* (Pause sets A := t),
   so the first method-45 call after Pause sees r >= r0 and delivers up to two cycles of keys and EOS. Implemented
   as written; is this the observed behaviour?
3. [ANSWERED: SPEC-AMEND-005 I3.3] [I3] **SPEC-011 method 4.** Does `on = 1` restart event processing (A := t, cursor reset) on every call or only
   when the instance was off? OpenMerc: only on the off -> on transition. Likewise Pause on an already paused
   instance: OpenMerc keeps the frozen pose (A unchanged).
4. [ANSWERED: SPEC-AMEND-005 I3.4] [I3] **SPEC-012 3, mode 0 at f = 1** (reverse playback with zero phase, or D = 0): x = N gives A = N, outside the
   sequence. OpenMerc wraps to frame 0 (same pose as f = 0).
5. [ANSWERED: SPEC-AMEND-005 I3.5] [I3] **SPEC-012 5, bolt-ons of a parent that is off / has no sequence / has note processing off:** are the
   bolt-ons still processed by the parent's method 45? OpenMerc processes nothing.
6. [ANSWERED: SPEC-AMEND-005 I3.6] [I3] **SPEC-011 method 36 (Clone):** which state is copied (callbacks, user value, event cursor, processing
   flags)? OpenMerc copies everything except id, parent and children.
7. [ANSWERED: SPEC-AMEND-005 I3.7] [I3] **SPEC-012 8, specular without a slot-1 texture:** OpenMerc draws the specular term as an extra additive
   untextured pass (with a specular map: textured with it). Is the term added to the vertex colour instead?
8. [ANSWERED: SPEC-AMEND-005 I3.8] [I3] **SPEC-012 8, projected shadow placement.** The mask basis is "along origin - light point, axes scaled by
   32/size". How are mask texels mapped onto the ground plane z = ground? OpenMerc projects posed vertices along
   the light direction onto the plane, uses world X/Y as the mask axes around the projected origin, and draws the
   mask as a 2*size square. For shadow-only drawing it shifts that centre by the block's directional vector.
9. [ANSWERED: SPEC-AMEND-005 I3.9] [I3] **SPEC-012 8, model lights:** is a light node's visibility track also required for "active in the current
   pose", or only brightness >= 0.01 and multiplier >= 0.01 (OpenMerc)?
10. [ANSWERED: SPEC-AMEND-005 I3.10] [I3] **SPEC-014 7, light kinds 1-4:** the record cannot be skipped without their track list, so OpenMerc fails
    such a file (non-fatal, like a wrong version). For kinds above 4 it reads no further tracks. No stock data.
11. [ANSWERED: SPEC-AMEND-005 I3.11] [I3] **SPEC-012 1, lookups of names whose registration returned 0** (methods 6/15/19 with a name no file has, or
    duplicates): the frozen table maps a name to its lowest id; are unknown names findable? OpenMerc: yes.
12. [ANSWERED: SPEC-AMEND-005 I3.12] [I3] **SPEC-011 method 14, tint quantisation:** value * 255 truncated (OpenMerc) or rounded?
13. [ANSWERED: SPEC-AMEND-005 I3.13] [I3] Deliberate differences (for the record): method 28 detaches a child from a previous parent (the original
    does not check, leaving a dangling entry); `gserver RELOAD` drops only unreferenced files (the original frees
    referenced ones too and crashes); the original's missing bounds checks of `.gpm` fields are replaced by
    truncation; the speed index is clamped to 0..12 when looked up.
14. [ANSWERED: SPEC-AMEND-005 I3.14] [I3] Observation for the game / player modules: stock character and first-person models face **-Y** in model
    space (exporter front view), e.g. MESO_PLAYER and `weapon/inview/pistol1`. SPEC-011 says Ghoul axes follow Quake
    (X forward); the 90-degree yaw therefore has to come from the instance transform the game / player module sets
    (or from the QUAKE_ORIGIN bolt). Which one does the original use?
15. [ANSWERED: SPEC-AMEND-005 I3.15] [I3] Observation, no question: replaying the svc 26 / 27 payloads of five captures (cap1-3, om1, om4) through
    the OpenMerc client decoder decodes every payload to its last byte; re-encoding with the OpenMerc writer
    reproduces all 1739 (per join) string-table records and all object definitions including their baseline states
    bit for bit, except that the original leaves the unused bits after the last Huffman code of a literal string
    non-zero (63 of 1739 strings in cap1). Receivers never read those bits; OpenMerc writes zeros.

## I5 client effects and temp entities (SPEC-142, SPEC-400, SPEC-401; 2026-10-22)

Found while implementing `engine/src/client/fx/`. "OpenMerc:" says what the implementation does meanwhile.

1. [ANSWERED: SPEC-AMEND-005 I5.1] [I5] **SPEC-400 Particle simulation 5, integer alpha.** "alpha += dt·R … The result is truncated and clamped"
   reads as an integer alpha that is truncated every step. Then a rate below 60/s (at 60 steps/s) never raises the
   alpha, and every falling rate loses at least 1 per step (a 155 → 0 fade over 1.5 s ends after 1.3 s). Is the
   stored alpha an integer (OpenMerc: yes), or a float truncated only when the colour byte is written?
2. [ANSWERED: SPEC-AMEND-005 I5.2] [I5] **SPEC-400 Rendering hand-off.** The renderer reads and writes the live alpha-quad records (SPEC-002).
   Does the original call RenderFrame before the effects steps of the frame (so the picture really shows the
   previous state)? OpenMerc steps first and then hands over the lists, which shows each state one frame earlier;
   One Frame particles are still drawn once and level-of-detail marks are read in the next frame.
3. [ANSWERED: SPEC-AMEND-005 I5.3] [I5] **SPEC-401 notation "size w × h".** Which of the two numbers is the first billboard dimension (alpha-quad
   +0x0c, along the camera up axis, SPEC-412 §8)? It matters for the non-square puffs: type 11 "(1.5·size) × size",
   type 43 "(0.5 + 0.08i) × (1.5 + 0.08i)", the fire/smoke trail puffs "2k·h × 2k·w". OpenMerc: the first number is
   the second dimension (+0x10, along right), the second number the first dimension.
4. [ANSWERED: SPEC-AMEND-005 I5.4] [I5] **SPEC-401 type 6, "style > 42 → `Surface out of range!!!`".** Presumably the material (style is 0..2).
   OpenMerc tests the material.
5. [ANSWERED: SPEC-AMEND-005 I5.5] [I5] **SPEC-401 type 6 style 1 with the debris flag: "one impact chunk burst with scale 25"** — how many chunks?
   OpenMerc: 6 (as type 7 and the computer/lit-panel bursts).
6. [ANSWERED: SPEC-AMEND-005 I5.6] [I5] **SPEC-401 type 7 mark.** "then mark and impact sound": with which power and hole sprite (the style byte is
   ignored)? OpenMerc: the power byte and the material's bullet hole, as type 6 style 1.
7. [ANSWERED: SPEC-AMEND-005 I5.7] [I5] **SPEC-401 Electric arc / recursive bolt sprites.** The sprite of the arc and bolt lines is not given.
   OpenMerc: `lightning` (sprite 14). Also "the previous point is pulled back by 6 % of the difference": the
   difference between which two points, and pulled towards what? OpenMerc: the previous point moves 6 % of the way to
   the new point. Recursive bolt: does a branch start at the segment start a or at the new point m (OpenMerc: m)?
8. [ANSWERED: SPEC-AMEND-005 I5.8] [I5] **SPEC-401 type 18 "glass without a direction".** The payload always has a direction byte; when is the
   "without direction" branch taken (direction byte 0? a zero vector)? OpenMerc never takes it.
9. [ANSWERED: SPEC-AMEND-005 I5.9] [I5] **SPEC-401 type 19 rock chunks: "chunk type 16 model"** — which size class (the payload has none)?
   OpenMerc: rock small (debris model 7).
10. [ANSWERED: SPEC-AMEND-005 I5.10] [I5] **SPEC-401 type 31 brightness L** needs the renderer's light sampling at the origin (SPEC-002 slot 0xa4);
    the OpenMerc client cannot call it yet (renderer interface, I4a). OpenMerc: L = 1.
11. [ANSWERED: SPEC-AMEND-005 I5.11] [I5] **SPEC-400 light primitive decay.** The `.eft` light primitive gives radius, colour and lifetime but no
    decay per second for the SPEC-400 dynamic-light call. OpenMerc: 0.
12. [ANSWERED: SPEC-AMEND-005 I5.12] [I5] **SPEC-400 Debris cap, "starting at the next slot to be reused and going from newest to oldest".** The next
    slot to be reused is the oldest record; OpenMerc starts at the most recently written slot and walks backwards.
13. [ANSWERED: SPEC-AMEND-005 I5.13] [I5] **SPEC-400 Decals item 4, "finally a very small value".** Which value? OpenMerc: 0.001 × size.
14. [ANSWERED: SPEC-AMEND-005 I5.14] [I5] **SPEC-401 type 48 falling-leaf behaviour**: the swing rate of the ±70° pitch and the size of the "small push
    along its forward direction" are not given. OpenMerc: pitch 70·sin(4·age), push 2 units/s per step.
15. [ANSWERED: SPEC-AMEND-005 I5.15] [I5] **SPEC-401 "random rotation"** of Flash(x) and other puffs: rand(0, 2π) assumed. **"white"** puffs and
    burning bits: alpha 255 assumed.
16. [ANSWERED: SPEC-AMEND-005 I5.16] [I5] **SPEC-400 attached particles with "Apply Physics"**: is the collision test skipped for particles that move
    in the effect's local frame (OpenMerc: yes, the local offset moves freely)?
17. [ANSWERED: SPEC-AMEND-005 I5.17] [I5] **SPEC-401 impact kind 0 re-trace "against the world … if that trace hits an entity"**: which entities
    count (brush entities only, or every solid entity)? OpenMerc: every solid entity of the frame.
18. [ANSWERED: SPEC-AMEND-005 I5.18] [I5] **SPEC-400 Emitters item 1**: when the next burst time has passed but lies after the end time, is the
    delay still drawn twice? OpenMerc draws only after an emitted burst (draw order is not normative anyway).
19. [ANSWERED: SPEC-AMEND-005 I5.19] [I5] Observation, no question: all 211 stock `.eft` parse exactly with the SPEC-400 field tables read field by
    field (file v1/2/3, emitter v1–3, particle v1–7 rules), matching the SPEC-142 size table and primitive totals;
    all round-trip through a version-3 writer (`engine/src/client/fx/test/eft_test.c`).
20. [ANSWERED: SPEC-AMEND-005 I4b.1, I4b.3 — found later; withdrawn] [I5] (2026-10-25) **SPEC-400 Rendering hand-off 2/3/5 — how client-local Ghoul instances reach the renderer.**
    Debris and Ghoul particles (types 3/4, e.g. `effects/explosion/explode80` in `weapons/world/rocketexplode`) are
    "added as renderer entities". But SPEC-002 entity 0x34 carries only a Ghoul instance *handle*, and SPEC-010
    *Ids and handles* says objects the client creates itself get positive ids and "are not reachable by handle"
    (system method 9 negates its argument on the client). Which entity fields identify such an instance (a pointer
    somewhere, a handle convention, flags), on which system the effects create their objects (client or menu),
    and how scale, part choice, skin and the debris angles are applied (entity angles/origin vs. instance
    transform)? OpenMerc: debris and Ghoul particles simulate fully but are not drawn (shim `fx/fx_ghoul.c`):
    chunks, paper sheets, soda cans and the rocket explosion's model are missing in the pictures.
    Answer already in SPEC-AMEND-005: frame = −(client instance id), flags Ghoul model + bit 28 (ambient only),
    created on the client system. Implemented 2026-10-25 (fx/fx_ghoulmodel.cpp). Still unstated: whether the
    instance transform carries the scale (OpenMerc: uniform scale, no −90° Z rotation) and what the debris
    entity angles mean (OpenMerc: the debris angles as entity angles). Observation (session debris3, same fixed
    eye, Ghoul models kept): after a `misc_generic_chunks_brick` breaks (TE 18, 10 chunks, type 2), OpenMerc shows
    brick chunks lying around the spot; the original's captures 0.3–5 s later show none in view. Where do the
    original's chunks go (spread, scale, culling against the real view)?
    Follow-up resolved by observation (I5, debris4, renderer traces of the original through the capture shim): the
    original draws the chunks (10 new debris entities, frame −k, flags 0x10400000) and the SPEC-400 on-screen cap
    (`fx_maxdebrisonscreen` + 1 = 17) removes them as the shooter's shell casings (type-4 emitter debris, ~10/s)
    arrive: 10, 10, 7, 4, 0 chunks at +0.3/+0.6/+0.9/+1.2/+1.8 s. OpenMerc behaves the same; the captures compared
    earlier were taken at different times after the break. No spec change needed. Side observation: the original's
    entity list holds every live emitter-made debris (not TE 18 chunks) a second time after the debris pass
    (identical record) — invisible for opaque models; recorded in `docs/impl/I5.md`.
21. [ANSWERED: SPEC-AMEND-007 I5.21] [I5] (2026-10-25) Observation for SPEC-002 import 0x68 / SPEC-AMEND-005 I5.8, no question: the OpenMerc GL3
    renderer assumes 162 direction-table entries and, loaded by the OpenMerc engine, gets no table at all (lit quads
    then sample their light straight down). Recorded for I4a/I4c in `docs/impl/I5.md` "Needs".
22. [ANSWERED: SPEC-AMEND-007 I5.22] [I5] (2026-10-25) **SPEC-400 Editor 1 — the text pages are not specified far enough to build.** Given: `fx_edit`
    ≠ 0 shows "a text page" each step, the main-page row labels, that sub-pages "show the labels used in the field
    tables", and that `fx_partadd`/`fx_partsub`/`fx_partadjust` adjust "the selected editor row". Missing: how the
    page is drawn (2D text calls or console/notify lines; font, position, colours, how the selected row is marked),
    how a row is selected and how "Edit" opens / leaves a sub-page (keys, a cvar, the `fxgraphic` menu?), the row
    order and value format of each sub-page (emitter, particle, sound, light, decal, feedback), the step sizes of
    every adjustable field (only repeat/trigger delays: 0.05, clamped 0..100 are given), what "Add New Effect" and the
    type row create/convert, and how the text cvars `fx_graphic`/`fx_sound`/`fx_collide`/`fx_trail` are applied (on
    change, or on a row action). OpenMerc: `fx_load`, `fx_save`, `fx_run`, `fx_edit`/`fx_editsegment` substitution
    and the Continual preview are implemented; no text page and no row editing.
23. [ANSWERED: SPEC-AMEND-007 I5.23] [I5] (2026-10-25) **SPEC-002 alpha-quad flags (+0x38) — bit 0x2000.** Renderer traces of an original spectator
    (capture shim, session beams1) show bit 0x2000 on every quad of the emitter line particles of
    `othermz/mpgbeam1/2` (sprites `lit`, `lita`): 0x2204 before the first draw, 0x3204 after; never on billboards
    (`flareblue`, `gb_orbit10`, `smoke2`: 0x0202/0x1202). SPEC-002 and SPEC-412 do not list it. Who sets it (engine at
    creation, for which lines — emitter lines only, or also temp-entity lines and trails?) and does the renderer read
    it? OpenMerc does not set it; with the OpenMerc renderer the beams look the same as the original's.

## I4a renderer: 2D/fonts, sky, fog, detail, effect primitives, cvars, module ABI (SPEC-002, 410, 412, 413; 2026-10-22)

Black-box observations with the original 1.07f `SoF.exe` running either the original `ref_gl.dll` or the OpenMerc
`ref_omgl.dll` behind a SPEC-002 capture/trace shim (`engine/src/client/refresh/gl3/test/refcap`): same executable,
data, config and console script, frames captured at EndFrame, all renderer calls of a frame logged.

1. [ANSWERED: SPEC-AMEND-005 I4a.1] [I4a] **SPEC-412 §2, depth direction of the 2D projection.** The engine draws Ghoul menu models (main-menu frame,
   buttons, globe) between slot 0xb8(0) and 0xb4(0), i.e. under the 2D projection, with depth testing. The
   original's main menu is reproduced pixel-identically only when depth decreases towards *more negative* z
   (glOrtho near 9999, far −9999); with glOrtho(…, −9999, 9999) the globe is hidden behind the frame. Please state
   the orientation of "depth range −9999…9999" (and whether the 2D projection is the same one in all cases).
2. [ANSWERED: SPEC-AMEND-005 I4a.2] [I4a] **SPEC-413 §6, clearing vs. depth writes.** Slot 0xb4 turns depth writes off, and the next BeginFrame clears
   depth. With depth writes still off a depth clear does nothing; OpenMerc turns depth writes on for the clear
   (otherwise the 3D view shows stale depth). Is that what the original does (where are depth writes re-enabled)?
3. [ANSWERED: SPEC-AMEND-005 I4a.3] [I4a] **SPEC-002 slot 0x54 rotation 4 and 6.** The engine passes rotation 4 (menu tab icons) and 6 (scroll-bar end
   caps); SPEC-412 calls values other than 0–3 undefined. OpenMerc uses rotation & 3; the pictures match the original.
4. [ANSWERED: SPEC-AMEND-005 I4a.4] [I4a] **SPEC-002 import 0x68, size of the direction table.** Light sampling along a direction (slot 0xa4 with
   dir ≠ 255, lit alpha quads) indexes it with a byte; how many entries does it have? OpenMerc assumes 162 and samples
   straight down for larger indices.
5. [ANSWERED: SPEC-AMEND-005 I4a.5] [I4a] **SPEC-002 slot 0x6c, character argument.** The engine passes the character as a signed value (Cyrillic text
   arrives as −64…−1); OpenMerc uses the low byte. Please note it in the slot description.
6. [ANSWERED: SPEC-AMEND-005 I4a.6] [I4a] Environment note, no spec change asked: on this PC (AMD RX 9070, OpenGL presented through DXGI) the original
   `screenshot` command writes all-black TGA files, because it reads the back buffer after the buffer swap
   (SPEC-412 §12). OpenMerc follows the spec (same result); the comparisons use frames captured at EndFrame.
7. [ANSWERED: SPEC-AMEND-005 I4a.7] [I4a] **SPEC-410 terrain sets, SPEC-412 §13 `avi`:** not implemented yet (selection messages of slot 0x38 only).
8. [ANSWERED: SPEC-AMEND-005 I4a.8] [I4a] **SPEC-412 §8, drawn mark on quads drawn alone.** §9 rule 7 gives chain members flag 0x1000; §8 says
   nothing. Frame traces of the original show reused standalone quads keeping flags 0x0202/0x0402, i.e. no mark.
   OpenMerc marks chain members only. Please state it in §8.

## I1a engine core: filesystem, startup, cvars/commands, savegames, string packages, sound host (SPEC-300..303, 131, 313, 004; 2026-10-22)

Black-box runs of the original 1.07f client/dedicated server from the test copy `D:\Games\SoF-test-I1a` (60-command
console probe, `map kos1` auto save, cvarlist/cmdlist); the OpenMerc console output of the same probe is identical
except paths, CPU MHz and `gserver` (Ghoul, I3). Details: `docs/impl/I1a.md`, `docs/impl/I1a-cvar-cmd-diff.md`.

1. [ANSWERED: SPEC-AMEND-005 I1a.1] [I1a] **Cvar change callback timing (SPEC-002 0x10, SPEC-001 #87).** Specified: called at creation and when an
   existing cvar is registered again with a callback. SPEC-313 test vector 2 (`set sp_language 0` "forces a reload"
   when it already is 0) implies it also runs on every assignment, changed or not. OpenMerc calls it on every
   assignment. Please confirm (and whether `Cvar_ForceSet`/`Cvar_FullSet` paths call it).
2. [ANSWERED: SPEC-AMEND-005 I1a.2] [I1a] **SPEC-313 Appendix A, number of key words.** "Fourteen 32-bit words are drawn", but the list (pairs for
   value numbers 0…5, the password key K, the pair for value 6) is 15 words. Which is right — is there a pair for
   value number 0 (`Server`, the password) or not? OpenMerc draws 15 (read-only use; no store on the test PC).
3. [ANSWERED: SPEC-AMEND-005 I1a.3] [I1a] **SPEC-303 SPNM/SPID, "string packages in use".** The original's `auto.sof` of `map kos1` lists
   `dm_generic`, `weapons`, `items`, `general`, `singleplr` (ascending package ID) but not `str_server`, which the
   engine owns from server start. OpenMerc writes the packages with the server owner bit (2). That matches if the
   game module registers `dm_generic`/`weapons`/`items`/`general` through import #47 — please confirm the rule.
4. [ANSWERED: SPEC-AMEND-005 I1a.4] [I1a] **SPEC-300 Behaviour 7, `dir` output.** Observed in the original: a line of 44 `=`, per mount a header of
   11 spaces + the directory (archives: 11 spaces only), matches as `%8i <name>` (archive names in normalised upper
   case), a blank line, `<bytes> bytes in <files> files and <folders> folders from <paths> paths`, the `=` line again.
   Please add it to the spec. Not observed: the line for a folder (OpenMerc prints `   <DIR> <name>`).
5. [ANSWERED: SPEC-AMEND-005 I1a.5] [I1a] **SPEC-300 dash switches, "normalised (Behaviour 3)".** Does this include upper-casing (`-user c:\a\b` →
   `C:/A/B`)? Test vector 3 cannot tell (`.\U3`). OpenMerc upper-cases on Windows and keeps the case elsewhere.
6. [ANSWERED: SPEC-AMEND-005 I1a.6] [I1a] **SPEC-302 Behaviour 8, cpuid.** The `<model>` part of `Processor :` comes from a table that is not
   specified (empty on the test CPU, as in the original). Observed: `cpu_mmx` stays "0" although MMX is reported —
   please confirm that cpuid never sets `cpu_mmx`/`cpu_amd3d`, and give the MHz measurement method if it matters.
7. [ANSWERED: SPEC-AMEND-005 I1a.7] [I1a] **SPEC-302 command list, names only.** Behaviour and output of `ping <address>`, `serverstatus`,
   `reset_predn` (and `adddownload <file>` beyond its usage text `Usage: adddownload <file>`, observed) are not
   specified. Observed usage texts: `winstart` without argument prints `Usage: start <parameter>`; `subliminal`
   prints `WARNING: Subliminal messages enabled`; `userweapons`/`useritems`/`userammo`/`usermisc` print
   `Weapons:`/`Items:`/`Ammo:`/`Misc:` and one `%-20s%s` line per cvar, newest first. Please fold into SPEC-302.
8. [ANSWERED: SPEC-AMEND-005 I1a.8] [I1a] **SPEC-004 sound struct #3 +0 (connection state).** Only 8 = active is specified; OpenMerc writes
   0 uninitialised, 1 disconnected, 3 connecting, 4 connected. Which values does the original use? Also: which are
   "the engine's built-in sounds" registered between S_BeginRegistration and the sound configstrings?
9. [ANSWERED: SPEC-AMEND-005 I1a.9] [I1a] **SPEC-313 §1, quoted values.** `zmain/strip/c4.sp` has a `TEXT_ENGLISH` value whose closing quote is
   followed by a space, so by the rule the value runs on over the next lines up to a quote at a line end. OpenMerc
   follows the rule. Confirm that the original does the same (the entry then swallows the following lines).
10. [ANSWERED: SPEC-AMEND-005 I1a.10] [I1a] Observations, no question: the original's `path` prints exactly as SPEC-300 B7 says (archive mounts as
    empty lines, `----------` before the base layer, `Links:` and `<from> : <to>`); `gamedir` does not exist after a
    plain start (it is created by a mod change); an original dedicated server started with `+set logfile` while the
    user directory does not exist exits with an access violation (OpenMerc creates the directory); `allow_download`
    is registered with "0" (SPEC-302 lists "1" / "0"); all 162 chunks of the original's `auto.sof` verify with the
    MD4-XOR checksum of SPEC-303.

## G6b script wiring (SPEC-750 1/3, SPEC-751 1/2, SPEC-752 4; 2026-10-22)

Black-box observations with the original 1.07f game module in single player (dedicated server forced to `deathmatch
0` by the G6b proxy module, `game/src/scriptglue/test/`), headless client captures.

1. [ANSWERED: SPEC-AMEND-005 G6b.1] [G6b] **Who uses the worldspawn `script` runner?** SPEC-760 B1.4 only says the runner is created. Observed: the
   level's script (`tsr1/tsr1pre`) runs right after the client's `Begin` (the `tsr1` string package is registered
   and `cl_touch` is executed next in the server log), and SPEC-752 vector 1 shows activator 1. `tsr1pre` ends with
   `use(startintro)`, so the whole intro depends on it. OpenMerc uses the runner from ClientBegin with the client as
   activator, in every mode, on every Begin (a FINISHED script ignores later uses). Please specify the exact point
   (ClientBegin, PutClientInServer, first frame?) and whether it happens in deathmatch and after a savegame load.
2. [ANSWERED: SPEC-AMEND-005 G6b.2] [G6b] **SPEC-751 2.6 "render flag 0x4000".** The original sends **effects** bit 0x4000 on entity 1 while a remote
   camera is active (SPEC-780 11 step 12 also says effects); renderfx stays 0. Please correct SPEC-751.
3. [ANSWERED: SPEC-AMEND-005 G6b.3] [G6b] **SPEC-751 2.3 step 5 with spawnflag 8.** Activation aims the camera at its target even with spawnflag 8 (tsr1
   `introcam1`, spawnflags 10: the first remote angles seen are the aim at `introcam1n` plus the first ROF delta);
   flag 8 only stops the re-aim of 2.5. Please confirm in the spec text.
4. [ANSWERED: SPEC-AMEND-005 G6b.4] [G6b] **Move-done snap and linking (SPEC-750 3.2).** Is the entity relinked when the move-done event snaps its
   origin? OpenMerc relinks (collision then matches the drawn position).
5. [ANSWERED: SPEC-AMEND-005 G6b.5] [G6b] **Script SOUND on an entity outside the client's hearable set** is not sent at all (expected for PHS
   delivery; a SOUND on the player arrives one frame after a PRINT issued just before it). No question; recorded for
   test authors.
6. [ANSWERED: SPEC-AMEND-005 G6b.6] [G6b] **SPEC-751 test vector 6** ("Could not open .ROF file 'trainhemi.rof'" in trn1) was not seen in the server
   log during the first 40 s of trn1 with the original. Is it printed with developer output only, or later?
7. [ANSWERED: SPEC-AMEND-005 G6b.7] [G6b] **Built-in 5 spawning `func_wall` with `model` `*31` and `origin`** produces an entity that moves on the
   server (MOVE signal after 31 frames) but is never sent to the client near the player start of tsr1. Is a brush
   model spawned by a script expected to be visible (is the model set and the entity linked by the spawn path)?
   OpenMerc's (G18 `func_wall` through the G6b spawn path) is sent and seen moving.

## I6m menu runtime (SPEC-130, SPEC-800..803; 2026-10-22)

Black-box comparisons: the original 1.07f `SoF.exe` (test copy, capture shim of I4a) and OpenMerc open the same
top-level menus at 640×480 (`engine/src/client/menu/test/`, pairs in `openmerc-data/shots/I6m/`).

1. [ANSWERED: SPEC-AMEND-005 I6m.1] [I6m] **SPEC-802 `input`, text colour.** The spec draws the value in `tint`. The original draws the text of an
   active field (`global`, or hovered) in `atint`: `gs/m_game` (`tint normaltext atint hilitetext`, `global`) shows
   the player name white, `m_start` shows `noname_` white. OpenMerc uses `atint` while active. Please confirm.
2. [ANSWERED: SPEC-AMEND-005 I6m.2] [I6m] **SPEC-803 `serverdetail` / `players` with no selection.** The original draws nothing in these widgets,
   not even the title line, while `menu_server` is not a listed server (join page with an empty list; their scroll
   bars are drawn). OpenMerc follows the observation. Please confirm the rule (and whether rows are drawn without
   titles in some state).
3. [ANSWERED: SPEC-AMEND-005 I6m.3] [I6m] **Listed file names are upper case** (answers SPEC-803 open question 7 for archives): the map-list list
   (`files "maplists" "*" "lst"`) shows and stores `RAVDM` (SPEC-802 test vector 4 says `RavDM`), the map chooser
   (`filebox "maps" "dm/*" "bsp"`) shows `DM/AIRPORTDM`. Consistent with SPEC-300 §3 (archive entry names are
   upper-cased). Unknown for loose files in the user directory. OpenMerc upper-cases every listed name.
4. [ANSWERED: SPEC-AMEND-005 I6m.4] [I6m] **SPEC-801 §9, when is the pointer drawn?** No pointer appears in any capture of the original while its
   window has no focus (captures on a separate desktop). Is the pointer hidden while the mouse is not active
   (SPEC-312 §8), or only not drawn by the capture? OpenMerc always draws it (as specified).
5. [ANSWERED: SPEC-AMEND-005 I6m.5] [I6m] **SPEC-800 §5.2 `<config x>`: when does the file run?** "executes `exec x.cfg` immediately" — with Quake II
   `exec` the file is inserted into the command buffer and runs after the `menu` command, i.e. after the list
   widgets have read their (stale) `menu_*` shadow cvars at page build. OpenMerc executes the file synchronously
   (the lists show the current settings). Check: `set gl_mode 4; menu video` in the original — does the resolution
   row show 800x600 at once?
6. [ANSWERED: SPEC-AMEND-005 I6m.6] [I6m] **SPEC-803 §1 events while waiting.** "Events are processed ... except while the current menu screen is in
   its waiting-for-an-animation state", but the EOS event of a `control` model is what ends that state. OpenMerc
   processes the events of every model every frame. Which models' events are skipped while waiting?
7. [ANSWERED: SPEC-AMEND-005 I6m.7] [I6m] **SPEC-803 §1 `time` with `oneshot`.** Stock background models combine both (`web_folders ... oneshot
   time 7.35`). OpenMerc shows the static pose at `time` (as the `time` row says). Confirm that `time` wins.
8. [ANSWERED: SPEC-AMEND-005 I6m.8] [I6m] Observations, no question: every stock popup (`quit`, `disconnect`, `gfxinfo`, `defaults`) and the
   `disclaimer` page match the original pixel-exactly in frame, border and text placement (outline columns/rows
   identical; `disclaimer` 98.3 % identical pixels). With the stock `zmain` texts, OpenMerc's parser reports the
   `Unrecognised text keyword` cascade of SPEC-800 §4 in `dm_game_assassin.rmf` (the Russian `MENU_DM_GENERIC_DM2`
   contains inner quotes); not yet compared with the original's developer output.

## I6h client view, HUD, input, demos (SPEC-310, 311, 312; 2026-10-22)

Black-box observations: demos recorded by the original 1.07f client on an original dedicated server and played
in both engines, screenshot pairs at the same demo frames, scripted console runs in both engines.

1. [ANSWERED: SPEC-AMEND-005 I6h.1] [I6h] **SPEC-310 §2.1/§2.4 big-digit colour.** The spec gives no colour for the clip digits. In the original
   the unlit segments of `pics/interface2/no_<d>` are dark green, i.e. the digits are drawn in HUD green (the
   panel's "icon" colour), not white. OpenMerc draws them HUD green; please state it.
2. [ANSWERED: SPEC-AMEND-005 I6h.2] [I6h] **SPEC-310 test vector 6 (`crosshair 7`).** In the original `crosshair 7` followed by `crosshair` prints
   `"crosshair" is "7"` (no immediate clamp; same output in OpenMerc). The forcing to 4 can only happen when the
   HUD reads the cvar. Please say when (first drawn frame? only while in a level?).
3. [ANSWERED: SPEC-AMEND-005 I6h.3] [I6h] **SPEC-310 §12.2 weapon kick.** Which view angle does the interpolated weapon kick (SPEC-202 field 22, one
   component) add to? OpenMerc adds it to the pitch.
4. [ANSWERED: SPEC-AMEND-005 I6h.4] [I6h] **SPEC-311 header deathmatch byte.** The original client wrote 1 in a game on a deathmatch server
   although nothing set its `deathmatch` cvar; consistent with SPEC-203 (serverdata copies the server's byte into
   the client cvar). Suggest SPEC-311 says "the client's deathmatch cvar, i.e. the last serverdata value".
5. [ANSWERED: SPEC-AMEND-005 I6h.5] [I6h] **SPEC-310 §9 loading progress per configstring.** Is a full frame really drawn for every configstring
   below 1415 while loading (about 1250 per join of dm/jpndm2, and all of them in one demo block)? OpenMerc sets
   `menu_loading` only at the level-registration steps for now.
6. [ANSWERED: SPEC-AMEND-005 I6h.6] [I6h] **SPEC-311 timedemo with multiplayer demos.** The cheat protection of SPEC-310 §14 forces `timedemo` 0
   whenever configstring 31 is neither `1` nor empty, which is the case for every demo recorded on a deathmatch
   server. Does timedemo work at all with such demos in 1.07f?
7. [ANSWERED: SPEC-AMEND-005 I6h.7] [I6h] **Command buffer and demos/servers (observation).** In both engines a script that still holds `wait`s
   delays the stufftexts of the server or the demo (`precache`), so joining or playback stalls; the harness starts
   scripts only once in the level (original: out-of-band `cmd`; OpenMerc: test cvar `cl_omexecactive`).
8. [ANSWERED: SPEC-AMEND-005 I6h.8] [I6h] **SPEC-312 §5.1 / §5.3 observation.** `toggleconsole` while disconnected queues `demo` and prints
   `Unknown command "demo"` in OpenMerc as specified; the original was not checked through its console key (no
   OS input is allowed); `keys.cfg` (bindings, names, faves, ignore, ff, cvar integer rule) gives identical
   output in both engines (129 lines).
9. [ANSWERED: SPEC-AMEND-005 I6h.9] [I6h] **SPEC-312 §8 zoom factor and wide windows.** F = "horizontal field of view of the view last rendered".
   OpenMerc may widen the horizontal fov for non-4:3 windows (`horplus`); it then uses the widened value. The
   original only has 4:3 modes, so this is a deliberate extension, no question.

## G9 game modes and Ghoul glue (SPEC-790, SPEC-791; 2026-10-22)

Black-box observations: original 1.07f dedicated server (test copy of the oracle) with scripted headless clients
(`game/test/g9/modetest.py`, captures in `openmerc-data/g9/orig_<case>/`). They correct or narrow SPEC-790.

1. [ANSWERED: SPEC-AMEND-005 G9.1] [G9] **SPEC-790 §2 configstrings 33 and 10 in Conquer.** Observed values are `1`/`0` for both: 33 = `1` with team
   play (else `0`), 10 = `1` with numbered teams (dmflags 0x8000), else `0` — not `-1` and not the truncated `3276`.
   Both are written once by the first client's end of frame (the cache cleared at level setup), not every frame.
2. [ANSWERED: SPEC-AMEND-005 G9.2] [G9] **SPEC-790 B6 Assassin, choosing a target.** A joining player's status layout already names his target,
   in the same packet as the "entered" message, and the players without a target that choose one at that moment
   are shown their new target at once; no end-of-frame status follows. So a choice sets the target directly (the
   pending/target transition is only seen after kills). Please confirm.
3. [ANSWERED: SPEC-AMEND-005 G9.3] [G9] **SPEC-790 B6 icons (svc 33).** A player's own slot gets icon 1 in his table (sent at his entry). A player
   who enters as a spectator gets no entries and appears in nobody's table. Please state the self entry and the
   spectator side ("0 when V is a spectator" — also when X is?).
4. [ANSWERED: SPEC-AMEND-005 G9.4] [G9] **SPEC-790 B3/B6/B7 status refresh.** The 32-frame refresh resends the status layout without moving the
   help time (it stops when the 5 s / 30 s run out). `help` in a mode without a status layout draws nothing but keeps
   stats[9] bit 0x1 set for 30 s (and the scoreboard refresh stops).
5. [ANSWERED: SPEC-AMEND-005 G9.5] [G9] **SPEC-790 B7 Arsenal status count.** After the fresh kit the status says "You have 6 weapons remaining":
   the count excludes the hand pseudo-weapon (open question of SPEC-790 answered).
6. [ANSWERED: SPEC-AMEND-005 G9.6] [G9] **SPEC-790 §4 force join developer lines.** `player %d %s attempting to join` prints the joiner's current
   team (0), and the per-slot lines run over slots 1…maxclients **including the joiner** (`player 1 Alpha val 0`
   when Alpha joins), with empty names for free slots.
7. [ANSWERED: SPEC-AMEND-005 G9.7] [G9] **SPEC-790 B12 CTF/Control team rows.** The team rows come before the player rows, unsorted, in the order the
   teams are met — not by sum and not by team number: Blue first while slot 0 is Blue and a Red player leads
   (`orig_ctf`), Red first when slot 0 is Red and all scores are 0 (`orig_ctf_forcejoin`, 10-25). Consistent with
   grouping the players in client-slot order (G9's choice) or in ascending score order; case `ctf_order` tells them
   apart. The player rows are always column x = 0 (team 1) first. Spectators continue at y = (longest column) × 32.
   Confirmed slot order (as G9.7 says): `orig_ctf_order`, Blue Alpha (slot 0) at 0, Red Bravo at −2 → Blue row first.
8. [ANSWERED: SPEC-AMEND-005 G9.8] [G9] **SPEC-790 B12 Conquer with numbered teams.** The `CONQUER_LAYOUT_SCOREBOARD_TEAM` rows reach the client
   with **no argument data** (svc 34, not 36) — from the original module and equally from OpenMerc's, which passes
   the four arguments of the spec. So the 1.07f engine packs nothing for this entry (format
   `client_conq_sb %hd %hd %hu %hd %hu %hu ` with a trailing space); SPEC-201/SPEC-313 should say why (I1b needs it).
9. [ANSWERED: SPEC-AMEND-005 G9.9] [G9] **SPEC-313 map list vs SPEC-790 B2.** With the default `sv_maplistfile` (`ravdm`) the engine replaces
   `sv_maplist` at every deathmatch map start, so a `sv_maplist` set by the server owner is lost unless
   `sv_maplistfile` is empty (observed: dm/jpndm2 → dm/raven from `maplists/ravdm.lst`). Worth a note in SPEC-790.
10. [ANSWERED: SPEC-AMEND-005 G9.10] [G9] **SPEC-781 9 / SPEC-790 B8 flag item ownership.** G18's item code leaves the CTF/Control flag rules to the
    modes; G9 implements SPEC-781 §9/§10 (touches, recovery, drops, timed/inaccessible return, control switches).
    SPEC-781 says an `item_ctf_flag` without `count` uses the default count 1, i.e. it becomes the **Blue** flag;
    please confirm (stock maps always set `count`?).
11. [ANSWERED: SPEC-AMEND-005 G9.11] [G9] **SPEC-790 B11 single-player meter.** The stealth value source (SPEC-761 world alert) is not named as a
    number the game keeps; G9 takes it from `G_StealthValue()` (game core).
12. [ANSWERED: SPEC-AMEND-006 G9.12] [G9] **SPEC-790 §1 step 2 `trigger_conquer` registration.** The spec moves every `trigger_conquer`'s origin to
    its box centre "in every mode". Observed on dm/gerctb1: the brush's baseline keeps origin (0, 0, 0) in
    deathmatch 1 (G18 capture) and is at the box centre (4, −256, 252) in deathmatch 7 (`orig_conquer_numbered`).
    So only Conquer registers zones (G9 does so since 2026-10-25); please correct the spec.
13. [ANSWERED: SPEC-AMEND-006 G9.13] [G9] **SPEC-791 test vector 6 (`ghoul_model`).** On the original dedicated server (`developer 1`, `gimme
    ghoul_model`, 30 s) the entity is spawned (box solid, Ghoul-drawn, in front of the player) but neither
    `my sequence has ended! hooha!` nor `getting rid of seq-end callback` is ever printed — the `EOS` note callback
    does not fire there. OpenMerc's module behaves the same. The vector can only be checked with a client that runs
    the Ghoul animation (listen server), if at all.
14. [ANSWERED: SPEC-AMEND-006 G9.14] [G9] **SPEC-790 B6 Assassin scoring, "A's pending target := V's target".** With two players (each the other's
    target) Alpha kills Bravo with the pistol (`orig_assassin_kill`): Alpha +4, and afterwards Alpha gets no status
    layout and no icon change — so his target stays Bravo. Taken literally the rule would make Alpha his own target
    (V's target is A). G9 leaves A's pending target unchanged when V's target is A; please state the rule.
15. [ANSWERED: SPEC-AMEND-006 G9.15] [G9] **SPEC-790 B13 CTF game log teams.** `orig_ctf_fraglimit_team` (CTF, `dmflags` 32, Alpha on Blue,
    teamname = skin) writes **no** team block, only the client block; the spec lists `Team`/`Score`/`Flag Captures`
    per team for CTF. Case `ctf_log_tp` (`dmflags` 160, team-play bit set) writes none either. G9 now forms the log's
    team blocks like the default scoreboard's team rows (players whose team name differs from their skin), which
    gives none for stock setups; please state when the original writes team blocks and with which team name.
16. [ANSWERED: SPEC-AMEND-006 G9.16] [G9] **SPEC-791 B1/B2 vs the body bolt-on registry (SPEC-722 "Bolt-ons and skins").** Observed on the wire
    (G4's registration suite over 32 SP maps; `orig_sp_reg_arm1/jpn1`): every hand weapon `Enemy/Bolt/w_*` (no skin
    named) is registered with **no** skins — not with all skins as "callers pass all-skins = 1 whenever they do
    not name a skin" would give; a bolt-on with a skin registers it **once, on the material named like the model**
    (`acc_headset` → `ACC_HEADSET` only, not its second material; `w_briefcase` → `W_BRIEFCASE=RUBBISH`), and on
    the material that takes it when no material has the model's name (`acc_ecto_glasses` → `ACC_GLASSES`). G9's
    registry hook now does exactly that and the bolt-on objects match the original on arm1/jpn1. Please state the
    registry's rule in SPEC-791/722.

## G2 character AI (SPEC-700, 701, 710, 711; 2026-10-22)

1. [ANSWERED: SPEC-AMEND-005 G2.1] [G2] **SPEC-710 SDSO lethal / bolt fields (UNVERIFIED there).** The stock tsr1 intro gives `ai(introswatkiller, 42,
   ..., flags 0x380)` (target 0x80, int 0x200, flag 0x100) and its script then waits for the next move's done signal;
   the original continues, so the attack must end — it does when 0x100 on an attack is the *lethal* flag (target set to
   health 1, the action ends when the target is dead) and the 0x200 int is the target-bolt selector. OpenMerc uses
   that reading; SPEC-750 12.1 describes 0x100 as "ends when the movement state reaches landed". Please confirm.
2. [ANSWERED: SPEC-AMEND-005 G2.2] [G2] **SPEC-711 class 5 "1.5 × speed".** SPEC-011's speed indices multiply the *duration* (index 8 = 1.5). Is the
   special death played with index 8 (slower) or a faster index (≈ 0.67 duration)? OpenMerc uses index 8.
3. [ANSWERED: SPEC-AMEND-005 G2.3] [G2] **SPEC-700 rule 15 floor height.** "The stored floor height" (record 264) — where is it written? OpenMerc
   stores the `quake_ground` bolt height relative to the origin (SPEC-722 ground-offset event) and smooths the box
   bottom towards it.
4. [ANSWERED: SPEC-AMEND-005 G2.4] [G2] **SPEC-711 A7.3 jitter aim.** "keeps the previous aim angles moved towards the desired direction": at what
   rate? OpenMerc uses the desired direction plus the error terms each think.
5. [ANSWERED: SPEC-AMEND-005 G2.5] [G2] **SPEC-722 / SPEC-700 "movement value above 9"** (footsteps): OpenMerc reports the length of the AI velocity
   estimate (units per think).
6. [ANSWERED: SPEC-AMEND-005 G2.6] [G2] **SPEC-710 C7 and team-0 civilians.** Characters on the player's team never perceive the player (SPEC-701 §5
   rule 1), so their retreat decision has no target; test vector 6 still expects a civilian to cower when shot near.
   OpenMerc lets kind 6 run its own range test without a target when fear > 0. Is that the original's path?
7. [ANSWERED: SPEC-AMEND-005 G2.7] [G2] **SPEC-700 rule 10 death step 4** "skill score factor (skill spec)": not in SPEC-760; OpenMerc uses the
   difficulty score of SPEC-760 B9.2.
8. [ANSWERED: SPEC-AMEND-005 G2.8] [G2] **SPEC-700 rule 16** helper entity "chained to the character": by which link (teamchain, chain)? OpenMerc sets
   only `owner`.
9. [ANSWERED: SPEC-AMEND-005 G2.9] [G2] **Reaction latency while an idle action plays.** A use/alert while a 3.6 s idle (`std_istand_n_mrs_2`, D3
   "the animation's length") plays is acted on only when it ends (SPEC-700 rule 7 step 9). On nyc1 (`extra3`) the
   original seems to start its point_combat walk right after the script's use. Is there an interrupt on alert, or is
   the idle action shorter (another duration rule for looping records)?
   *Observed 2026-10-25 for script `ai` commands*: the original starts the commanded action at once on kos1
   (`introjohn` move, OpenMerc 1.8 s later; a `std_jdrop` 3.6 s later), sib1 (`std_igunup` +1.9 s), sud2
   (`cch_eheadset_to` +3.4 s, the whole cutscene then runs late) and tsr2 (four hall characters +2.0 s).
   *Answered by SPEC-AMEND-005 G2.9* (class-0 idles last 0 s, the decisions re-plan every think); applied.
10. [ANSWERED: SPEC-AMEND-006 G2.10] [G2] **SPEC-700 rule 15 / SPEC-722 ground offset for `m_x_mcharacter`.** On kos1 `introjohn` (Mullins model) the
   original keeps the woken character at origin z 96; OpenMerc's floor smoothing (stored floor height = the
   `quake_ground` bolt height) raises it to 102.9, i.e. that bolt lies ~7 units below the box bottom. Is the stored
   floor height something else than the raw bolt height (offset, scale, only some families)?
11. [ANSWERED: SPEC-AMEND-006 G2.11] [G2/G3] **SPEC-722 Firing with a target-bolt selector.** Is a shot with a selector (SPEC-710 SDSO field 60, the
   `i200` operand: 1 head … 5 gut) aimed at the bolt's current position, and is the type's aim spread applied to it?
   The tsr1 intro's lethal pistol shot (`ai(introswatkiller, 42, introdeadswat, i200=1)`, ~60 units) kills with
   the first round and `std_shead_2frnt_n_n` in all 6 original runs; with spread 0.1 (type 3) it would often miss.
   Also: which bolts are the five selectors? (tsr2: `hallraider1` with i200=5 and a machine gun also kills its
   running target at ~300 units at once, again with `std_shead_2frnt_n_n`.)
12. [ANSWERED: SPEC-AMEND-006 G2.12] [G2] **SPEC-AMEND-005 G2.9 and path/point waits.** With class-0 play-in-place actions lasting one think, the C12
   rule "waitaction1 … waitaction6 play in order" would switch animation every think. The original plays each for
   its animation length (tsr2 `cch_escantalk_lt/rt` alternate every ~1.6 s; nyc1 point wait `std_itouchnuts_n_a_n`
   3 s then `std_ilooking_n_a_a`). OpenMerc requests the same waitaction again while the body still plays it and
   advances when its animation has ended. Is that the original's rule (and is a spawnflag-2 list "played" then)?
13. [ANSWERED: SPEC-AMEND-006 G2.13] [G2] **SPEC-750 12.4 / SPEC-710 D3 for script kinds 5 and 6.** `ai(blowback1, 72)` (`std_sblown_2bk_n_n`, a
   death record) throws the character back 26 units up / 200 back and leaves it lying dead in the original (tsr2),
   i.e. the record becomes a class-3 death action as in D3, not a play-in-place. OpenMerc now maps a death-class
   record of a play-in-place command to class 3. Confirm (and whether kind 5 records go through D3 too).
14. [ANSWERED: SPEC-AMEND-006 G2.14] [G2/G3] **Cow idle choice with SPEC-AMEND-005 G2.9.** With the stand choice asked every think, the SPEC-722 cow
   rule (keep the current stationary animation while not done, else 1/60 `std_imoo_n`, 1/60 `std_ilooking_n`) and
   "a looping record without 0x00400000 is always done" make a cow switch every ~3 s; the three sud2 cows stay in
   `std_i_n` for the first 11 s of the original (no switch in ~330 thinks). Is the cow re-planned less often (animal
   decision set), or does a looping idle count as "not done" for this rule?
   OpenMerc meanwhile keeps D3's "animation's length" for the idle actions of cow/bull bodies (sud2: two runs, all
   five characters match the original, 1.00); the human idles follow G2.9.
15. [ANSWERED: SPEC-AMEND-006 G2.15] [G2] **SPEC-700 rule 1.1 "gravity 0.01".** A soldier spawned 50 units above the floor (`gimme m_irq_msoldier1`,
   arm1) falls in the original with full gravity (z −8, −16, −24 per frame, lands in 0.3 s); with entity gravity
   0.01 under the step physics of SPEC-760 B4.6 it sinks ~1 unit per 0.2 s. Which field does the 0.01 go to?
   OpenMerc sets the entity gravity to 1.0 for characters.
16. [ANSWERED: SPEC-AMEND-006 G2.16] [G2] **SPEC-710 C4/C5 and "ready to fire".** The kos1 shotgunner of the original fires exactly every 3.2 s;
   between shots it plays the cock record and then the aim stance (`std_a_fwd_ms_2`, crouched `cch_a_fwd_ms_2`) and
   turns with the target; it never plays fire records back to back. C4/C5 say "attack" whenever the reaction age
   allows it, and fire records with a `fire` note shoot unpaced (SPEC-711 A4). Where does the original consult
   "ready to fire" (SPEC-722) before choosing an attack — in the decision, or in the body's attack choice (falling
   back to the aim list)? OpenMerc: in the decision (`PlanAttack`), also aiming instead while the target is more
   than ~44° off the owner's facing. The cadence fits F = T-d: skill 2 (T-d 1.0) → 4.0 × 1.0 × 0.75 = 3.0 s + the
   next think + the `fire` note = 3.2 s; skill 3 (toughness 4, T-d 0.3) → 1.6 s = fire + cock records back to back
   (`orig_kos1_walk_sk2/sk3`).
17. [ANSWERED: SPEC-AMEND-007 G2.17] [G2/G3] **SPEC-AMEND-006 G2.14 Verify fails: cows without a target do not switch idles.** The amendment predicts that
   a cow with no target asks its idle choice every think and switches about every 3 s. Black box (arm1, cheats 1):
   `gimme m_x_bull` spawned 100 units in front of the player and facing away from him, once with `ninja` sent
   before the spawn and once without — the bull stays in `std_i_n` for 23 s in both runs (`orig_arm1_bull_ninja`,
   `orig_arm1_bull`). `ninja` is effective in these runs: a `gimme m_irq_msoldier1` at the same spot keeps
   alternating `std_istand`/`std_ilooking` for 20 s with it and stops at 10.0 s without it (`orig_arm1_sold_ninja`,
   `orig_arm1_sold`). So either the cow's plan does not ask the idle choice every think when it has no target, or
   the looping `std_i_n` is "not done" for the cow rule after all. OpenMerc keeps the animation's length as the
   duration of cow idle actions (sud2 and arm1 bulls stay in `std_i_n` as in the original).
18. [ANSWERED: SPEC-AMEND-007 G2.18] [G2] **Reaction of a character that perceives the player behind it during a look-around.** Black box (arm1,
   `gimme m_irq_msoldier1` spawned 100 units in front of the player, facing away): the original's soldier ends its
   `std_ilooking_n_a_a` at 10.0 s and then stays in `std_istand_n_a_n` for 15 s and more without turning (yaw −90
   throughout); with `ninja` it keeps looking around, so it did perceive the player (`orig_arm1_sold`,
   `orig_arm1_sold_ninja`). OpenMerc perceives him at 10.4 s (sight, wide cone of the turned head) and its combat
   decision (kind 2, priority 3) runs to him (`std_r_n_pk_n`, class 7) and keeps running in place against him
   (`om_arm1_sold`). Which C4/C5 branch keeps the original's character standing, not turning, with the player
   ~100 units behind it — a reaction-delay stand whose record cannot turn, or no attack record (weapon class `pk`)
   and nothing queued as for the cows of G2.14? Is the gimme soldier armed in the original?
   Update (G2, same day): reproduced in OpenMerc by two changes — arm1 has no navigation data, so the C5 back-off
   (range code 2: the soldier carries a sniper rifle, ideal distance 512) gets no flee point (SPEC-740 rule 6
   "direct"; OpenMerc had invented one), and the first sighting sets the reaction moment, so may-attack is false and
   C5 stands; with the player behind it the record goes stale and the moment is renewed every think. Remaining
   question: does the first player record set the reaction moment (SPEC-700 rule 14 only names records older than
   1 s), and is a "told" record of a trigger use excluded? (kos1: the shotgunner told by its trigger is never
   startled in the original; OpenMerc excludes told records.)

19. [PARTLY ANSWERED: SPEC-AMEND-008 G2.19] [G2] **SPEC-701 §5 / SPEC-700 rule 5: view cone of an awake but not yet alerted character.** Black box
    (kos1 and tsr1 flythroughs 2026-10-25, `openmerc-data/g2/fly4_orig_kos1` vs `fly4_om_kos1`, `fly_orig_kos1`):
    the player (god mode, cheats) stands still on the ground 160 units from a character for 4 s, 60–165° off its
    facing (kos1 `armorguy2` 38, `pistolguy` 135 in its `std_igunup` path wait, the `t22` grunts 326/375 facing
    +x with the player south of them, `wallguy` 167 behind it; 20 s in the noclip-only runs for 271/280/316). The
    original's characters keep their idle for the whole dwell; OpenMerc's perceive them within 0.5–5 s (sight,
    wide cone cos −0.2 because SPEC-700 rule 5 sets view mode 2 on waking), G3's stand choice then answers
    `std_xstartled_n_a_a` and combat follows. Either waking does not switch an un-alerted character to the wide
    cone (SPEC-701's "set when the character becomes active" would then mean alerted, not awake), or sight has a
    further condition for still players. The same runs show the opposite for `wallguy` (167): the original turns
    (yaw 0 → −162) and aims up (`std_a_up_mrs_2`) at a player that OpenMerc never perceives (dwell 165° behind
    it, 161 units, proximity radius 75). Which cone applies to awake, un-alerted characters, and what did
    `wallguy` perceive?
    *SPEC-AMEND-008 G2.19 (static, 2026-10-26)*: waking does set the wide cone and sight has no extra condition for
    still targets, so the difference is downstream of sight (current-action rule, reaction moment or throttled
    sense update); which one, and what `wallguy` perceived, stays open until the amendment's black-box tests 1–2
    are run. The reaction-moment rule in its point 3b also bears on G2.18's remaining question.


## G5 navigation and bots (SPEC-143, SPEC-740, SPEC-741; 2026-10-22)

Black-box results (original 1.07f engine, OpenMerc and original game modules, dm/jpndm2; tool
`game/src/bots/test/bottest.py`, journal `docs/impl/G5.md`) and spec gaps:

1. [ANSWERED: SPEC-AMEND-005 G5.1] [G5] **SPEC-001 "flagged as having no network connection"** is server flag **0x200** (`svflags`): with it the
   engine silently drops single-entity sends (SP_Print with the bot's entity, welcome print) to a bot's client
   slot; without it the first one stops the server (`SZ_GetSpace: overflow without allowoverflow set`). Bisected
   over server-flag bits 0x100–0x80000000; entity flags (`flags`) have no effect. Presumably this is "the bot flag"
   of SPEC-741 3 — please confirm and name it in SPEC-741/SPEC-760 3.
2. [ANSWERED: SPEC-AMEND-005 G5.2] [G5] SPEC-741 2: the fill step and the serving step run in an order that serves a fill request in the frame
   that created it (oracle: fill bots exactly 1.5 s apart); the spec lists serving (3) before filling (4).
3. [ANSWERED: SPEC-AMEND-005 G5.3] [G5] SPEC-741 5/11: what the fire-time and alternate-fire-time fields of a bot's user command hold (bots'
   userinfo has no `predicting` key, so the player module treats them as predicting). OpenMerc: the level time
   while the button is pressed, else 0.
4. [ANSWERED: SPEC-AMEND-005 G5.4] [G5] SPEC-741 5/10: how "the medkit is selected" and "selects the grenade … the throw happens through normal fire
   control" map onto the inventory (SPEC-601 item requests `itemnext`/`itemuse`?). OpenMerc: `itemnext` until the
   type is selected; the selected grenade/flash pack is used with `itemuse` at the next fire.
5. [ANSWERED: SPEC-AMEND-005 G5.5] [G5] SPEC-741 6 noise record: when a footstep (kind 2) follows a weapon noise (kind 0) within the 1 s window,
   which kind is kept? OpenMerc keeps the latest kind and the maximum loudness.
6. [ANSWERED: SPEC-AMEND-005 G5.6] [G5] SPEC-741 13.5 "jumps … when the waypoint is a jump waypoint, when it is kind 2 and less than 26 s remain":
   both clauses name kind 2 — is the first one about the previous waypoint?
7. [ANSWERED: SPEC-AMEND-005 G5.7] [G5] SPEC-741 14.4/14.8: is a single waypoint (no link to its neighbour ids) a path? Routes: which juncture side
   counts as "next/previous along the same path" when two junctures leave the same waypoint; what the bot does when
   the start or the destination waypoint is in no path. OpenMerc: paths have ≥ 2 waypoints, sides ordered by id,
   no route → roaming.
8. [ANSWERED: SPEC-AMEND-005 G5.8] [G5] SPEC-741 14.9 "return along the remembered way": OpenMerc goes back to the waypoint it came from.
9. [ANSWERED: SPEC-AMEND-005 G5.9] [G5] SPEC-740 6 finish: for |m|² < 0.1 "the (tiny) direction is accepted" — stored unnormalised (OpenMerc) or
   normalised?
10. [ANSWERED: SPEC-AMEND-005 G5.10] [G5] SPEC-740 11 cone helper with equal radii divides by zero in the original; OpenMerc uses a cylinder.
11. [ANSWERED: SPEC-AMEND-005 G5.11] [G5] SPEC-740 13: position of the version-10 extra block in a `.nav` file (OpenMerc: after the second node set).

## I4b Ghoul model drawing in the 3D view (SPEC-411, SPEC-002, SPEC-011; 2026-10-22)

Black-box observations with OpenMerc's `ref_omgl.dll` and the original `ref_gl.dll` inside the original SoF.exe
(capture shim traces, paused frames with `fixedtime`; docs/impl/I4b.md).

1. [ANSWERED: SPEC-AMEND-005 I4b.1] [I4b] **SPEC-002 entity 0x34 / SPEC-411 §5 step 1, "the low 16 bits of `frame`" are a signed handle.** Instances
   the client creates itself (tumbling weapon debris in the tsr1 intro) arrive with `frame` = −1 (0xFFFFFFFF) and
   flags 0x10400000, and the original draws them; with the low 16 bits taken unsigned (65535) the client system
   (which negates the argument of method 9) finds nothing. OpenMerc passes `(short)(frame & 0xFFFF)`. Please state
   the rule (sign-extended low 16 bits, or the whole 32-bit value?).
2. [ANSWERED: SPEC-AMEND-005 I4b.2] [I4b] **SPEC-002 RenderFrame / SPEC-411 §3:** the original appends the Ghoul model lights after the engine's
   dlights **without changing the engine's `num_dlights`** (after the call the count is unchanged and the model
   lights lie past it), consistent with "the renderer copies the top-level record". A renderer that increments the
   engine's count makes a scene that is drawn again unchanged (paused game) accumulate copies of the lights. Worth a
   sentence in SPEC-002 *Dynamic-light write-back*.
3. [ANSWERED: SPEC-AMEND-005 I4b.3] [I4b] SPEC-411 open question 4 / SPEC-AMEND-002 1.11 (who sets 0x1000 and bit 28), observed in traces of tsr1 and
   nyc2: 0x1000 on character entities (and on Ghoul entities still without an instance handle, frame 0); bit 28
   (0x10000000) only on client-created debris instances (negative handles, see 1). Props, vehicles and the view
   weapon had neither.
4. [ANSWERED: SPEC-AMEND-005 I4b.4] [I4b] SPEC-411 open question 1 (probe direction frame): implemented exactly as §5 step 8 states (vector in the
   entity's axes from the un-negated angles), the `ghl_light_method 1` frames are pixel-identical to the original's
   (with the original Ghoul system drawing) — the renderer-side formula is confirmed; whether the Ghoul system then
   rotates it again is I3's side.
5. [ANSWERED: SPEC-AMEND-005 I4b.5] [I4b] For I3 (no spec question): SPEC-012 *Lighting* says GL-light linear attenuation 0.75/radius for world and
   dynamic lights, SPEC-411 §6.7 says 1/R; `ghoul_draw.cpp` uses 0.75/radius. Which one is right? (Only matters
   for `ghl_light_method` > 2.5 with `ghl_gllight_attenuation 1`.)
12. [ANSWERED: SPEC-AMEND-005 G5.12] [G5] SPEC-741 `bot_wp_edit`: in the oracle, `+set bot_wp_edit 1` on the command line, `set bot_wp_edit 1` by rcon
    before or after a `map` (the module is reloaded) all leave the cvar at `"0"` (rcon query right after the set), the
    log shows `Waypoints loaded and placed from file.` and `makewp` etc. are not commands (they end up as chat). How is
    waypoint edit mode entered in 1.07f (cvar callback forcing 0? another precondition)? OpenMerc follows the spec text
    (the cvar enables edit mode).

## G3 damage model, gore, bodies (SPEC-720, SPEC-721, SPEC-722; 2026-10-22)

Observations from G3's unit tests (mock Ghoul models built from the stock `.ghb`/`.gsq` data) and black-box runs of
the original module in the original engine (`game/src/damage/test/g3dm.py`, `g3run.py`, `g3cap.py`; docs/impl/G3.md).

1. [ANSWERED: SPEC-AMEND-005 G3.1] [G3] **SPEC-721 2.7 bolt order (answered by observation).** The killing-blow `gore/bloodcloud_s` goes on the zone's
   **gore bolt** when it has one, else on the body-side cap bolt: DM self-rocket on `MESO_PLAYER` (torso blast from the
   front, zones 7, 5, 3, 13) gives the clouds on `GBOLT_CHEST_F`, `GBOLT_SHOULDER_LF`, `GBOLT_SHOULDER_RF`, `GBOLT_GUT_F`
   (not `GBOLT_CAP_HIPS_CHEST_TIGHT` for zone 7). Please write the order into 2.7 (and say whether the blood-pool
   tracking of 2.7/7 uses the same order).
2. [ANSWERED: SPEC-AMEND-005 G3.2] [G3] **Player pain sounds on a killing hit (answered by observation).** SPEC-720 B13 plays `impact/gore/impact<n>`
   and `impact/player/hit<n>` for every hit with damage left; the original plays neither when the hit kills the player
   (same DM self-rocket). OpenMerc: only when the player survives the hit (SPEC-780 §16 says "for living players").
3. [ANSWERED: SPEC-AMEND-005 G3.3] [G3] SPEC-720 10.2: body records that map to **no zone** (the owner-part test finds nothing) — "No Ghoul hits at all
   ... does nothing" covers only the empty list. OpenMerc keeps the damage unscaled without a zone. Also: a record on a
   shown **wound mesh** (`gz_*`) — does it belong to that zone, or can the ray test never report wound meshes?
   OpenMerc maps it to the zone.
4. [ANSWERED: SPEC-AMEND-005 G3.4] [G3] SPEC-720 10.2 "entry = the first body record **before** the exit record": with a single body record (entry =
   exit) OpenMerc uses it as the entry and has no exit zone.
5. [ANSWERED: SPEC-AMEND-005 G3.5] [G3] SPEC-720 10.1: "a zone without gore bolt wins by smallest distance" — distance of what? Zones sharing an owner
   and both without gore bolt (male thighs 16/17, `_rtightthigh`) can then never be told apart; OpenMerc takes the first.
6. [ANSWERED: SPEC-AMEND-005 G3.6] [G3] SPEC-720 B4 "blood splat behind the point (SPEC-401, size 200, heavy variant 1/6)": TE 3 has no size or
   variant; OpenMerc follows SPEC-771 C.2 (impact descriptor of a 128-unit trace along the shot). What are "200" and
   "heavy"?
7. [ANSWERED: SPEC-AMEND-005 G3.7] [G3] SPEC-720 11.5 "a debris/blood puff (SPEC-401) at the point" for every hit with a non-zero point: which temp
   entity/effect? Not sent by OpenMerc yet.
8. [ANSWERED: SPEC-AMEND-005 G3.8] [G3] SPEC-721 §4 sends **two** flesh debris bursts at the same position; SPEC-771 C.2 drops a TE 18 burst with the
   same material within 0.1 s of the previous one, so the second burst would never be sent. Which rule wins?
9. [ANSWERED: SPEC-AMEND-005 G3.9] [G3] SPEC-721 sever groups abbreviated as "`_rthigh`…" (civilian) and "`_rtightthigh`…" (female thighs): the full
   lists are missing. OpenMerc completes them after the male pattern (thigh mesh, the lower-leg zone, then the calf
   group: `_rcalf`, `_rcalfcuff`, `_cap_rcalfcuff_rfoot`, bolt `abolt_calf_r`, `_rfoot`; female `_rtightcalf`, bolt
   `abolt_calf_r`, `_rheels`).
10. [ANSWERED: SPEC-AMEND-005 G3.10] [G3] SPEC-721 TV5 (brain shot on a meso soldier wearing `_helmethead`): no stock `enemy/meso` model has a
    `_headhelmet` mesh (checked in all 41 meso `.ghb`); only `enemy/ecto` has it. Is the vector meant for ecto, or does
    the meso body use another open-skull mesh?
11. [ANSWERED: SPEC-AMEND-005 G3.11] [G3] SPEC-721 §6 severed piece: "physics parameters 0.5, 1.0, 0.1, 0.7, 0.5" — OpenMerc reads them in SPEC-782's
    column order (gravity, elasticity, ground friction, air friction, buoyancy-like). The piece's instance: is it paused
    in the character's pose, or does it keep playing? Where is the piece origin relative to the instance (OpenMerc
    shifts the clone's transform so the piece-side cap bolt is the entity origin)? Lifetime without the class-13 limb AI?
12. [ANSWERED: SPEC-AMEND-005 G3.12] [G3] SPEC-720 10.2 b: "the character is armoured above half health" — armour percentage > 50 (OpenMerc), or health
    > half while armoured?
13. [ANSWERED: SPEC-AMEND-005 G3.13] [G3] SPEC-722 death rule 11 "shoulder zones by sever segment 1 or 4 / 2 or 3": the segment numbering is not given;
    OpenMerc maps zones 3, 6 → right and 4, 5 → left (the male-table sentence) for every family; the civilian zone 6
    (torso segment) has no shoulder death.
14. [ANSWERED: SPEC-AMEND-005 G3.14] [G3] SPEC-003 method 31 (AbsorbArmor) third argument: SPEC-720 passes value 12 (a float fraction); the game header
    declares an int. OpenMerc passes trunc(value 12).
15. [ANSWERED: SPEC-AMEND-005 G3.15] [G3] Harness note (no spec question): the parental-lock store of this PC keeps at least `lock_blood` on (only
    `gore/noblood*` effects, no wound skins seen on hit characters in the original); `set lock_* 0` in a config has
    no effect. Gore with the locks off cannot be compared black box here.

Added 2026-10-25 from black-box runs with the locks off for the game module only (`game/src/damage/test/g3shim.cpp`
answers the module's `lock_*` reads; DM self-rocket `openmerc-data/g3/v0_*_rocket*`, `v8_*`; PvP pistol `pvp_*_1`).
OpenMerc follows the observations below; please confirm or correct in the spec.

16. [ANSWERED: SPEC-AMEND-006 G3.16] [G3] SPEC-721 rule 3.4: after a gib every handed-over zone has its **body-side and piece-side cap bolt parts
    switched off** on the body (part flags of the bolts, e.g. `GBOLT_CAP_CHEST_LBICEP_TIGHT` and
    `GBOLT_CAP_LBICEP_CHEST_TIGHT` for zone 10, both head cap bolts for the head); the piece keeps them on. Is this
    part of every hand-over (also rule-3 severing outside a gib)? OpenMerc does it in every hand-over.
17. [ANSWERED: SPEC-AMEND-006 G3.17] [G3] SPEC-721 rule 8.6, gib limb beyond the piece limit (no piece spawned): the original showed the body cap
    (`_CAP_HIPS_RTHIGH_BULKY`) and switched off the zone's cap bolts, but left the owner (`_RBULKYTHIGH`) and the rest
    of the sever group (calf, foot) shown — i.e. no rule-3.5 walk and no rule-3.7 owner hide without a piece. One
    sample; OpenMerc does the same.
18. [ANSWERED: SPEC-AMEND-006 G3.18] [G3] SPEC-721 §6: gib pieces played no `impact/gore/limbfall.wav` on touching the world (two pieces, four
    `gore/danglyblood`), so the limbfall sound looks rule-3-only. OpenMerc: gib pieces send only dangly blood.
19. [ANSWERED: SPEC-AMEND-006 G3.19] [G3] SPEC-720 §13 "if the damage is not lethal the body plays a pain reaction": a deathmatch player hit in the
    left upper arm plays `std_sarmunhurt_lt_a_n` for 0.2 s, then the locomotion pose returns. Which pain choice of
    SPEC-722 applies to player bodies (no AI state: disarm, limb injuries), and what ends it after 0.2 s? Not yet in
    OpenMerc (G3/G18 boundary).
20. [ANSWERED: SPEC-AMEND-006 G3.20] [G3] Same run: each pistol1 hit (attack 3, damage 45, 61 units, no falloff) on the player's upper arm (zone 10,
    scale 0.5) took 21 health, not trunc(45 × 0.5) = 22. Where does the missing point come from?
21. [ANSWERED: SPEC-AMEND-006 G3.21] [G3] SPEC-720 10.5: "a second estimator (used when the character is in certain postures, UNVERIFIED which)". Which
    postures (or other state) select it, and what are its exit zones for the side cases? OpenMerc (interim): the
    current record's posture is crouched (1) or sitting (4); exits as in the first estimator; up to 5 zones drawn from
    the right set, 4 from the left.
22. [ANSWERED: SPEC-AMEND-006 G3.22] [G3] SPEC-722 "Savegames": the body image layout is private; OpenMerc writes its own layout inside chunks with the
    original ids and sizes (zero padded) and re-creates the body on the entity stored in the image. Only needed if
    original savegames must be read (out of scope per SPEC-722) — no answer required.
23. [ANSWERED: SPEC-AMEND-007 G3.23] [G3] Black-box run for SPEC-AMEND-006 G3.20 (2026-10-25, `openmerc-data/g3/g320_orig_front`): same spot and
    distance (61 units, attacker's eye 59 units above the floor), target turned to face the shooter, `stats[5]`
    (armour) 0 throughout. Each pistol1 hit on the left front shoulder (zone 5, scale 1, wound mesh
    `GZ_BULKYCHEST_LFSHLDR`, then the exit `GZ_BULKYCHEST_B` on the killing third shot) took **39** health
    (100 → 61 → 22 → dead), not 45; the arm hits of the first run took 21, not 22. So the loss is not in the zone rule
    alone and grows with the damage: both runs fit health loss = trunc(d × (1 − d / 337.5)) with d = 45 × scale
    (39.0 and 21.0), which is numerology from two points. Something between the shot and the health subtraction
    reduces player damage (realistic dmflags off; female factor not applicable; no armour). The 400-unit run of the
    item was not made (the noclip placement of the target is real-time and fragile). OpenMerc gives 45 / 22.
    Which rule removes 6 of 45 points on a deathmatch player?

## I7 player module (SPEC-003, SPEC-600, SPEC-601, SPEC-602; 2026-10-25)

Black-box observations of the original 1.07f player.dll behind the I7 tracing proxy (single player with `fixedtime`,
and a predicting client on a dedicated server; traces in `openmerc-data\i7`, method in `docs/impl/I7.md`). OpenMerc
follows the observed behaviour where it differs from the spec.

1. [ANSWERED: SPEC-AMEND-006 I7.1] [I7] SPEC-601 Interface 2: a new inventory has player variable B = -1 (on), not 0.
2. [ANSWERED: SPEC-AMEND-006 I7.2] [I7] SPEC-601 Interface 3 / SPEC-600 Behaviour 10: the idle-variation counter (block 0x270) is not set by the
   constructor. Observed values: 0 in fresh memory (single-player server inventories), otherwise text-like heap
   words (0x434F524F, 0x52415F51, 0x57455448 ...) that drop by one with every inventory created in the process, so
   predicting clients and dedicated-server inventories practically never choose an idle variation or a stance
   transition, while single-player ones do. Is the field really left uninitialised (and decremented once during
   construction)? OpenMerc: 0 for single-player/listen server inventories, 0x40000000 on the client side and on a
   dedicated server (no client API in the process). Consequence in the original: a
   predicting client and its server can be in different stances (pistol1: server `fire1_b`, client `fire1_a` after
   `elbow`, because the server selects without a draw and the predictor draws `ready_a`).
3. [ANSWERED: SPEC-AMEND-006 I7.3] [I7] SPEC-602 Behaviour 2: with a predicting owner the server also clears both pending fire times (server
   imports 14 and 16) when a reload is attempted (after the "reloading enabled" check, before the clip checks) and
   when the new weapon is drawn at the end of a switch, and the pending time of the refired mode at every refire,
   also where the button decides the refire (flamethrower `fire1_1` → `fire1_idle`, MPG `fire2` → `fire2_loop`).
   Without the last rule the client's own refire time, arriving after the server's refire, starts a new attack
   after the release (seen with OpenMerc: no `fire1_2`/`fire2_end`, one extra fuel/battery unit). The getters
   13/15 are asked only while the weapon is ready (both modes, also without an alternate attack). Not in the
   spec; please confirm the exact points (dry reload with a full clip, automatic reload, unpaid refire).
4. [ANSWERED: SPEC-AMEND-006 I7.4] [I7] SPEC-602 Behaviour 4: at a predicted switch the server chooses the new instance's idle (one idle choice:
   variation counter used, stance words may change) *before* it starts the put-away on the old instance; the state
   block's sequence name is the put-away's. At the end of the put-away the draw starts without a second idle choice.
   The client follower, when it adopts the server's instance, makes an idle choice and then starts the draw.
5. [ANSWERED: SPEC-AMEND-006 I7.5] [I7] SPEC-600 Behaviour 10: every draw applies the draw stance, also the end of a predicted switch and the client
   follower's draw (pistol1 `ready_a` = stance a, sub-pose cleared; pistol2 stance a; sniper "drawn" with word 0x208
   = 1). Please state it for those paths.
6. [ANSWERED: SPEC-AMEND-006 I7.6] [I7] SPEC-600 test vector 4: shell-by-shell loading takes N+1 `reload_loop` sequences for N shells (each loop end
   loads one shell and sets "ready"; the loop that finds the clip full or no shells left is followed by
   `reload_2`), not N.
7. [ANSWERED: SPEC-AMEND-006 I7.7] [I7] SPEC-600 Behaviour 10, machinegun: releasing the button goes idle at once with the spread kept (as assault
   rifle and machine pistol); `fire1_2` is not reached. Flamethrower release: idle choice (looping sound reset,
   variation counter used), then `fire1_2` and a second looping-sound reset (the reset also clears the loop data
   byte).
8. [ANSWERED: SPEC-AMEND-006 I7.8] [I7] SPEC-600 Behaviour 2/7: a refired sequence's note at 0 ms is delivered on the next think (pistol1 held: first
   shot at once, then 0.4 s, then every 0.3 s on a 10 Hz server), not in the think that refires.
9. [ANSWERED: SPEC-AMEND-006 I7.9] [I7] SPEC-600 Behaviour 6: a refire that cannot be paid (e.g. MPG burst with 5 of 10 units) starts the automatic
   reload (rounds moved, server import 4 called) and then goes idle, which replaces the reload animation.
10. [ANSWERED: SPEC-AMEND-006 I7.10] [I7] SPEC-600 Behaviour 10, automatic weapons: the shot time (block 0x27C) is recorded before the round is paid;
    a note that finds the clip empty starts the automatic reload at once (single rockets and the machinegun grenade
    record the time too).
11. [ANSWERED: SPEC-AMEND-006 I7.11] [I7] SPEC-601 Behaviour 4, goggles: battery steps happen when now > next (strict), so at 10 Hz a step takes
    0.4 s on and 0.6 s off rather than 0.3 / 0.5 s. Please confirm the comparison.
12. [ANSWERED: SPEC-AMEND-006 I7.12] [I7] SPEC-600 Behaviour 10, sniper: the idle variation needs word 0x208 (set by draw and reload); before that the
    plain idle is chosen without touching the variation counter. The saved zoom becomes 60 when `scopein` ends.
13. [ANSWERED: SPEC-AMEND-006 I7.13] [I7] SPEC-003 interface details: every boolean method result is tested by the game as a full 32-bit EAX (one-byte
    results broke `heretic`/`elbow`); method 64 ORs the persistence weapons into the caller's mask (the game pre-fills
    it); fire imports 2/3 receive the owner-context pointer as their first ("unused") argument; method 8 calls method
    7 through the table; item messages after `itemuse`/`itemdrop` are `PrintId(owner, 0x30E, newCount, displayName)`.
14. [ANSWERED: SPEC-AMEND-006 I7.14] [I7] SPEC-600 Behaviour 8, hand pseudo-weapon end: method 11 turns the best weapon into a slot request (code 8 +
    slot), "ready" becomes -1 (state stays 5), and that request is processed on the next think although "ready" is
    -1; the new weapon is drawn at once. `itemdrop` of a whole slot (goggles) prints no message.
15. [ANSWERED: SPEC-AMEND-006 I7.15] [I7] SPEC-600 Behaviour 10, slugger: `ready_idle` after the draw counts as an idle variation (counter := 2); on a
    predicting client this is what ends the heap-text counter of item 2, so idle variations appear on the client
    after the slugger has been drawn once.
16. [ANSWERED: SPEC-AMEND-006 I7.16] [I7] SPEC-003 export 11 (rebuild after a full restore): the original registers a new model object for the
    current weapon (common import 17 again, no sound/effect/attack precache) instead of using the object cached by
    export 7; the instance is made from that new object. Intended, and does the cache keep the old object?
17. [ANSWERED: SPEC-AMEND-006 I7.17] [I7] SPEC-600 Behaviour 10, machine pistol "2 rounds for the first shot (UNVERIFIED, derived)": observed, starting
    the attack does not touch the pre-paid flag (block 0x264); the first note is free while the flag is still set
    (a new inventory has 1; refires set it) and costs a second round when it is 0. So the first burst after `bigelbow`
    takes 1 round for its first shot, later presses 2 (predicting owner on a dedicated server, `mpbig-orig-orig`).

## I1c engine client: the Ghoul client system (SPEC-010..013, 002, 202, 310, 803; 2026-10-25)

Black-box observations: the original 1.07f client's renderer calls through I4a's capture shim (`ref_omcap`, one-frame
traces of RenderFrame / 2D calls) and the original Ghoul system's GL-table calls (`ref_omgl` in the original SoF.exe,
`r_om_ghltrace` with vertex colours); traces in `openmerc-data/I1c/` and `D:\Games\SoF-test-I1c\omgl_ghltrace.txt`.

1. [ANSWERED: SPEC-AMEND-006 I1c.1] [I1c] **SPEC-002 refdef 0x48-0x50, the game clock.** Implemented as the SPEC-202 section 5 estimate (5 % pull
   toward each svc_frame game time, jump beyond 0.5 s, advanced by the client frame time, frozen while paused). In
   original demo playback the clock runs 0.289-0.293 s ahead of the refdef client time and creeps toward 0.3 s
   (25 traces over 1 s), consistent with that estimate. Please confirm that 0x50 is that estimate.
2. [ANSWERED: SPEC-AMEND-006 I1c.2] [I1c] **SPEC-002 entity, how the client fills Ghoul entities** (observed, not in SPEC-002/310): packet Ghoul
   entities carry the angles with pitch and roll negated (auto-rotating ones: pitch 0, the auto-rotate yaw, the
   roll as sent), frame = oldframe = handle, alpha 1.0, a light-output pointer, byte 0x5c = 255; the viewer's own entity
   has flags 0x400002 and its origin is the refdef view origin (after the 1/16 nudge); the view weapon is a zeroed
   entity (alpha 0, no light output, 0x5c = 0) whose origin is the view origin before the nudge. Worth a sentence in
   SPEC-310 section 13 / SPEC-002.
3. [ANSWERED: SPEC-AMEND-006 I1c.3] [I1c] **SPEC-012 section 8, model lights of bolt-ons.** "world flag 0: it lights only this model". In the
   original main menu the four omni lights of `m_body_main` (bolted to the `b_web_home_ingame` root) also light
   the globe, rings and doors (other models bolted to the same root): their vertex colours carry the lights'
   colours (teal 0, 1, 0.87 and warm 0.79, 0.69, 0.53). OpenMerc carries the model omni lights of every instance
   drawn earlier in the same instance-method-41 call to the instances drawn after it. Does the original collect
   them in drawing order (OpenMerc) or over the whole hierarchy first?
4. [ANSWERED: SPEC-AMEND-006 I1c.4] [I1c] **SPEC-012 section 8, shade model.** Every instance draw of the original Ghoul system begins with
   ShadeModel(SMOOTH) and ends with ShadeModel(FLAT) (GL-table trace); not in the spec. OpenMerc does the same.
5. [ANSWERED: SPEC-AMEND-006 I1c.5] [I1c] **Answer to I3-9 (light node activity).** A light node lights only while its visibility track shows it:
   in-view weapon models carry a `WORLD_OMNI` muzzle light visible in 4-8 of about 210 frames; in the original it
   never lights the walls between shots, in OpenMerc it did until the visibility test was added.
6. [ANSWERED: SPEC-AMEND-006 I1c.6] [I1c] **SPEC-010 system method 24, specular argument.** Which value does the client pass (OpenMerc:
   `ghl_specular` > 0.5 and `ghl_light_method` > 0.5, as the renderer's method 2)?
7. [ANSWERED: SPEC-AMEND-006 I1c.7] [I1c] **SPEC-202 2.4 / SPEC-002, auto-rotate yaw.** The original's auto-rotate yaw advances 100 degrees per
   second but is not anglemod(client time x 100) (19.995 at client time 19.897 s); it looks tied to the real-time
   clock. Which clock? (The Quake II formula in `cl_sof.c` uses the client time.)
8. [ANSWERED: SPEC-AMEND-006 I1c.8] [I1c] Observation, no question: the original draws the whole main-menu model tree in one Set3DInMenu(0) /
   Set2D(0) pair after the backdrop pictures (`b_web_home_ingame` is the only root; the others are bolted to it),
   as SPEC-803 section 1 describes.
9. [ANSWERED: SPEC-AMEND-006 I1c.9] [I1c] Observation for I3 / I4c (no spec question): on this PC the original renderer's driver configuration
   (`exec drivers/*.cfg` at Init, SPEC-002) leaves `ghl_light_method 3` and `ghl_shadows 2` in the original client's
   config; the OpenMerc engine with the Yamagi-hosted `ref_gl3` keeps the defaults 2 / 1. A Ghoul GL-table trace of
   the original drawing the in-view SMG (`openmerc-data/I1c/gt6/f253.ghl.txt`) shows the hardware path of SPEC-411
   6.7 with a two-pass specular material: pass 1 opaque with lighting, AMBIENT_AND_DIFFUSE (0,0,0), EMISSION 0,
   SHININESS 11.31, SPECULAR (0.6,0.6,0.6), one texture (GL name 809), polygon offset (0.1, 0.1); pass 2 additive
   (ONE, ONE), depth writes off, AMBIENT_AND_DIFFUSE (1,1,1), SHININESS 0, SPECULAR 0, another texture (807; which one is the specular map was not identified). Six GL
   lights from world lights (colour 0.4 x the record colour, linear attenuation 1/R, position w = 1). OpenMerc's
   SMG under the same light differs by a blue cast (weapon box +7..+12 in blue; pistols match within 2), with
   `ghl_light_method` 1 it matches. SPEC-012's "specular as an extra pass" wording does not say which pass is
   opaque.

## I2d GameSpy lobby chat (SPEC-204 §5, SPEC-302, SPEC-803 §3; 2026-10-25)

Black-box runs of the original 1.07f client (test copy `D:\Games\SoF-test-I2d`) against a scripted local IRC server
(`tools/net/chattest.py fake`) and against OpenSpy's Peerchat (`peerchat.openspy.net:6667`) through a logging relay;
scripts in `tests/I2d/`, traces and captures in `openmerc-data/I2d/` (details in `docs/impl/I2d.md`). OpenMerc
follows the observations where they differ from the spec text.

1. [ANSWERED: SPEC-AMEND-006 I2d.1] [I2d] **5.1 ERROR before the welcome** (spec UNVERIFIED and open question): confirmed — console and public log
   `Connected to <server>:<port>!`, the room list is cleared, then the client sends nothing more (no LIST, no PONG).
2. [ANSWERED: SPEC-AMEND-006 I2d.2] [I2d] **5.1 CTCP.** "Other CTCP requests except ACTION are ignored" does not match: a CTCP `VERSION` in a
   private message is shown as plain text (`^3[^28bob: VERSION^3]`, the \x01 bytes removed). Only PING is answered
   (`NOTICE <sender> :\x01PING <arg>\x01`, as specified) and not shown. Please correct.
3. [ANSWERED: SPEC-AMEND-006 I2d.3] [I2d] **5.4 `gsc_ban`.** The original was never seen sending `MODE #<channel> +b *!*@<host>`: only `WHOIS <user>`
   goes out, also when the server answers with a complete WHOIS (311 with a host, 319, 312, 317, 318) or with a 311
   whose host is `*`, and with the client operator of the channel. Under which condition is the ban sent, if ever?
   OpenMerc sends only the WHOIS.
4. [ANSWERED: SPEC-AMEND-006 I2d.4] [I2d] **5.2 rooms: every LIST is a request of its own.** Two LISTs sent at once and both answered give two lines
   `5 channels found.` (each counts only its own 322 replies; the developer lines restart at `Channel 0`) and every
   room twice in the rooms widget (each request clears the list when it is sent, the replies of both are added). An
   unanswered LIST reports `0 channels found.` after 30 s — also when some 322 replies came without a 323. LISTs sent
   at 0 s and 10 s that are never answered report at about 30 s and 40 s (separate limits). Please add.
5. [ANSWERED: SPEC-AMEND-006 I2d.5] [I2d] **Timeout messages.** "gives up after 30 seconds and then reports failure": LIST prints
   `0 channels found.` (item 4); NAMES prints `Failed to list users for ` **without** the channel name (spec:
   `… for #<channel>`); TOPIC and WHOIS print nothing. Unknown: whether an error numeric (e.g. 403) ends a pending
   NAMES request early (OpenMerc waits for 366 or the limit).
6. [ANSWERED: SPEC-AMEND-006 I2d.6] [I2d] **5.4/5.5 topics of other channels.** A topic reply or TOPIC change for a channel other than the current
   one does not change `menu_topic` (`gsc_gettopic sof` while in `#opchan` leaves `#opchan: …`). The table says
   `menu_topic := #<channel>: <topic>` for `gsc_gettopic` of any channel.
7. [ANSWERED: SPEC-AMEND-006 I2d.7] [I2d] **SPEC-803 §3, lifetime of the chat logs.** "The engine keeps, while menus exist": in the original, lines
   that arrive while the menus are closed are lost, and the logs are empty when the menus open again (`menuoff`,
   chat line, `menu chat` → empty log). OpenMerc does the same (`MC_Add` drops lines while the menus are closed,
   `MC_ClearLogs` when they close). Please state it.
8. [ANSWERED: SPEC-AMEND-006 I2d.8] [I2d] **SPEC-803 §3 chat drawing with a long log.** When the log has more lines than the widget, the original
   shows the newest line at the widget's bottom (scroll bar at the bottom). Read literally (page content height
   lines·L, page scrolled to the bottom, drawing from y + height − 4 with the widget's own frame height) the newest
   line would sit above the visible bottom by the scroll amount — which is what the OpenMerc menu runtime (I6m)
   shows now. Does the widget's height follow lines·L, or does the drawing ignore the page scroll?
9. [ANSWERED: SPEC-AMEND-007 I2d.9] [I2d] **Join completion order (SPEC-AMEND-006 I2d.6, black-box 2026-10-25, `tests/I2d/orig_amend006.cfg`).** Events
   that arrive between the join's names and its mode reply: another user's JOIN is put into the join's user list
   without a `joined` line (`6 users found` at the join); a PRIVMSG and a TOPIC change are delivered after
   `Successfully entered/created channel` and the user list, and only then come the join's own `Topic for … is`
   (from 332, which stays in `menu_topic`) and `modechange` lines. Please state this order; and what a PART, KICK or
   MODE of another user does in that window (OpenMerc: PART/KICK remove the user from the join's list silently,
   MODE is held like a message).
10. [ANSWERED: SPEC-AMEND-007 I2d.10] [I2d] **`gsc_join` on a connection that is not registered** (after an ERROR before the welcome): the original
    writes `^4Joining channel: #<channel>` to the public log, sends nothing, and the current channel does not change
    (a later `gsc_join` of the same channel after a reconnect sends JOIN). `gsc_talk` there shows no echo. Unknown:
    `gsc_part` and `gsc_private` in that state (OpenMerc: nothing at all), and whether `gsc_join` behaves the same
    after the server closed the connection (OpenMerc: nothing, no socket).
11. [ANSWERED: SPEC-AMEND-007 I2d.11] [I2d] **SPEC-AMEND-006 I2d.8 on the private page.** `a_private.rmf` has no vertical bar, so by rule 2 the page
    never scrolls while the widget grows to N·L: with more private lines than fit, the newest would be below the
    visible area. Is that the original's picture (OpenMerc follows the rule as written)?

## G7 props and server effects (SPEC-770, SPEC-771; 2026-10-25)

Black-box runs of the original module in the original engine against OpenMerc's (`game/src/props/test/g7test.py`,
captures in `openmerc-data/g7/`, docs/impl/G7.md). "Answered by observation" = the original's behaviour was measured;
please write it into the spec.

1. [ANSWERED: SPEC-AMEND-006 G7.1] [G7] **SPEC-770 §3 step 5, combined right angles (answered by observation).** Every rotation reads the extents
   of the *class box*; the x minimum is **not** carried over from an earlier rotation. Test map `g7rot` (28 pitch/yaw/
   roll combinations of `misc_generic_wpn_minimi`): all packed boxes identical to the original only with that rule.
   Shipped data uses it: 45 props have two right angles (`light_generic_caged` `90 90|180|270 0` in nyc1, nyc2, sib1,
   jpncon1, koscon1; `misc_generic_barrel_chemical` `90 180 0` in nyc2; `misc_generic_fan_big` `180 90 0` in irqcon1;
   `environ_steamgen` `90 0 90` in ger3). Example nyc2: caged light (−3,−3,−10)/(3,3,5) at `90 270 0` is sent with the
   packed box (3, 3, 3, 3); the spec's rule would give a y half-width of 10. This also answers the shared-framework
   open question "verify by searching the shipped entity lumps".
2. [ANSWERED: SPEC-AMEND-006 G7.2] [G7] **SPEC-770 §5.8 / gatekeeper note / SPEC-791 open question 1 (answered by observation).** The prop does not
   fall: `g7fall`, `misc_generic_table1` spawnflag 4, used three times by a player from 62 units (and once while
   touching it): no change of origin, angles or box in the original. The use key itself works in the same setup
   (`misc_generic_urinal` flushes on every press). OpenMerc keeps the specified fall behind cvar `g7_fallover 1`.
3. [ANSWERED: SPEC-AMEND-006 G7.3] [G7] **SPEC-771 B.2 (confirmed, UNVERIFIED can go).** A `func_door` with `soundName ButtonElevator1` (one-wave
   set) fired by a timer (`g7door`): start sound `Ambient/Buttons/HiTech/Elevator1.wav`, moving loop sound field
   `Ambient/Gen/AirPlane/AirBy1.wav` (data 248) and stop sound `AirBy1.wav`.
4. [ANSWERED: SPEC-AMEND-006 G7.4] [G7] **`environ_bloodrain` and lock_blood.** SPEC-721 §13 (citing SPEC-006) lists `environ_bloodrain` among the
   effects suppressed by `lock_blood`; the original sends the `environ/bloodrain` effect slot for all four
   entities of sud2 (single player) on this PC, where the parental lock keeps `lock_blood` on (flesh debris and
   blood splats are suppressed in the same setup, G3 item 15). Is the suppression client-side only? OpenMerc now
   always sends the slot.
5. [ANSWERED: SPEC-AMEND-006 G7.5] [G7] **SPEC-771 A.2 `environ_fire`, registration order (answered by observation).** "The three fire sounds and the
   four fire effects are always registered at spawn" — order measured with five first fires (`g7ord1..4`, ger2):
   the effect of the size first; then, when the fire starts with sound, its loop sound; then the sounds
   `FireBig`, `FireMed`, `FireSmall` and the effects `environ/firesmall2`, `gbfire`, `environ/firelarge2`,
   `environ/firelarge3` (already registered ones keep their index). Matters for identical configstrings.
6. [ANSWERED: SPEC-AMEND-006 G7.6] [G7] **SPEC-770 6B.5 security cameras, precache order (answered by observation).** A panning camera (spawnflag 4)
   registers its loop sound `MoveLP.wav` before the class precaches (`Alarm.wav`, `MoveLP.wav`): first camera of
   ger2 (spawnflags 13) → `MoveLP`, `Alarm`; first camera of dm/raven (spawnflags 1) → `Alarm`, `MoveLP`.
7. [ANSWERED: SPEC-AMEND-006 G7.7] [G7] **SPEC-770 §5.7, frame of the part bounds.** "its model bounds at the current pose, placed at its bolt
   position": the bounds must be turned by the part's bolt (and the entity's) rotation before the test. `g7brk2`:
   `environ_explosion` 46 units from a `misc_generic_bench`, damage point (529.97, −450, 22.77) (SPEC-720 §14.1);
   the original shoots the top off (TE 18 5×1, material 17, surface 14 + grey TE 45 at the top's bolt
   (531, −476.75, 17)). The top's bounds in its own axes are (−17.4, −1.9, −56.8)/(0, 2.4, 3.2) (its length on local
   z) and do not contain the point; turned by the bolt they are (−1.9, −3.2, 0)/(2.4, 56.8, 17.4) and do. Please
   state the space (method 51 `root` = the bolt matrix without translation?).
8. [ANSWERED: SPEC-AMEND-006 G7.8] [G7] **SPEC-770 6A.2 bench, "completely free" trace (answered by observation).** A bench whose origin lies inside
   another prop's box (`g7shot`: bench inside a `misc_generic_table1`) still falls when shot from the front: the
   trace start inside a box does not count as blocked, only a fraction < 1 does.
9. [ANSWERED: SPEC-AMEND-006 G7.9] [G7] **SPEC-770 7A `misc_iraq_tree_palm` "made non-solid after the setup" (answered by observation).** The
   entity is not linked again: the original keeps sending the packed box of the setup's link (irq1a,
   (23, 30, 67, 67) for `angle 270`) although the tree is not solid.
10. [ANSWERED: SPEC-AMEND-006 G7.10] [G7] **SPEC-770 6A.5 big fan, use while running down (answered by observation, 2026-10-22).** "When on (or
    accelerating) it first writes the full-speed record once, then decelerates": the deceleration starts from the
    *current* speed, and a use while decelerating behaves like a use while on (dm/raven test map, `speed 3`, two
    uses 4 s apart).
11. [ANSWERED: SPEC-AMEND-006 G7.11] [G7] **`misc_generic_radar_dish` of dm/nycctb2 does not fall in the original.** It is placed with its box bottom
    24 units above the surface below (origin z 699, box z −15); the original keeps it at 699 (no landing event), the
    OpenMerc movetype-10 physics (SPEC-760 §B4.7) drops it to 675 with landing event 10, while a `misc_generic_radio`
    placed 26 units above a floor falls in both (`g7drop`). What keeps the dish up — a ground/solid test at spawn,
    or the bolt-on dish? (Physics is G18's; reported here because only this prop shows it.)
12. [ANSWERED: SPEC-AMEND-007 G7.12] [G7] **`misc_test_gun_auto` has no effects bit 0x200 (answered by observation).** SPEC-770 6A.7 says turrets set
    entity effects bit 0x200; the developer test gun does not: test map `g7all3` (dm/raven + every class absent from
    the stock maps), the original sends it with effects 0 in every frame and gives it no baseline (Ghoul-only entity
    without effects). The other turrets keep the bit (stock maps). Please state the exception in 6A.7.
13. [ANSWERED: SPEC-AMEND-007 G7.13] [G7] **Classes that stop the original game (answered by observation, for the spec's missing-model questions).**
    Placed alone with default keys on dm/raven (`g7one_*` maps): `misc_generic_car_rolls` (model absent) and
    `misc_newyork_news_machine` (model absent) end the original server at spawn ("SetSimpleGhoulModel couldn't find
    ghoul sequence car/news", then the process exits); `environ_trainarm_spawn` ends it at its first use (creating the
    arm, model absent); `environ_raingen` as a point entity (no `model` key) stops the map load with
    `ERROR: PF_setmodel: NULL`; `func_musicControl` used by a non-player (as SPEC-771 B.5 says). The other missing-model
    classes (`misc_generic_chunks_metal/wood/glass/rock/wall`, `misc_bosnia_truck_chunks`, `misc_bosnia_rubble1/2`,
    `misc_bosnia_antipersonnel_jack`, `misc_bosnia_landmine`, `misc_generic_blinds`, `misc_generic_motorcycle`,
    `misc_generic_stoplight`, `misc_generic_street_name_sign`, trees, cartons, `misc_tokyo_buddha`, `misc_uganda_train_wheel`,
    `misc_newyork_trash_pile`) spawn invisible and the level runs (all their baselines and frame states then equal
    OpenMerc's). OpenMerc does not reproduce the stops (it skips the missing instance); please confirm that is
    acceptable, and close SPEC-770's open questions on these classes.
14. [ANSWERED: SPEC-AMEND-007 G7.14] [G7] **Skin registration of four classes (answered by observation).** `misc_generic_cash_register`,
    `misc_generic_comp_monitor`, `misc_generic_fire_extinguisher` and `misc_newyork_gumball` register **every skin of
    their model directory** at spawn (object definitions: `register`, `registerd`; `damaged`, `monitor`; `damaged`,
    `exting`; `gumball`, `gumball2` — directory order), i.e. their model is created without a skin and the skin is put
    on the material afterwards, like the classes marked "applied after the setup". Classes whose skin is passed to the
    setup (`barrel_chemical`, `chair_office`, `desk_phone`, `bench`) register only that skin. Evidence: 32 SP maps and
    dm/raven (G9's `sptest.py` cases `sp_reg_arm1`/`sp_reg_jpn1` found it first); OpenMerc now does the same. Please
    mark these four rows "applied after the setup".
15. [ANSWERED: SPEC-AMEND-007 G7.15] [G7] **SPEC-AMEND-006 G7.7 verify (bench at `angle 90`).** Done as `g7bench90`: a bench at `angle 90` and one at
    `angle 0`, each with the explosion at the same relative point and a static obstacle blocking the fall trace. The
    angle-0 top is shot off (grey puff at its bolt). For the angle-90 bench the debris is not delivered to the test
    client (outside its hearable set), but the angle-0 bench's own debris burst, sent in the same frame, is dropped by
    the SPEC-771 C.2 same-material rule — so a surface-14 burst was sent just before it, i.e. the angle-90 top was shot
    off too. Same capture from OpenMerc with the G7.7 root (bolt rotation then entity rotation).

## G4 roster, helicopters, vehicles, special characters (SPEC-730, 731, 732; 2026-10-25)
1. [ANSWERED: SPEC-AMEND-006 G4.1] [G4] **SPEC-731 A open question 2 (answered by observation).** Rotor scales: `ROTOR_TAIL` 0.25 (Hind 0.375),
   `ROTOR_MAIN` 1.25 (Hind 1.875) — instance scales of the rotor bolt-ons in the original's captures (trn1, jpn3).
2. [ANSWERED: SPEC-AMEND-006 G4.2] [G4] **SPEC-731 A §9.3 "yaw of (point − origin)" (answered by observation).** The target yaw is the exact
   atan2 direction in (−180°, 180°], not Quake II's integer `vectoyaw` in [0, 360): after a snap the original sends
   the negative value (e.g. −2.8° instead of −4.2° for a −3.8° heading); turning steps are anglemod. The turn step
   reports the error before the step: a snap within 9° gives "facing" only on the next frame, whose roll eases to 0
   at 2°/frame (trn1 face turn: −7.0 → −5.6 → −1.4 → 0). Proposed amendment to §9.3.
3. [ANSWERED: SPEC-AMEND-006 G4.3] [G4] **SPEC-731 A §9.5 strafe arrival snaps (contradicted by observation).** The original's strafe of 2092 units
   (trn1, id 22) stops 52 units short of the destination, at the first frame where |origin − P1| ≤ v on every axis;
   the origin is not set to P1 (legs do end exactly on P1). Proposed amendment to §9.5.
4. [ANSWERED: SPEC-AMEND-006 G4.4] [G4] **"Along the bolt's direction" (SPEC-731 B §10 shell trace, §11 machine-gun shots, A §8/§13 gun) = the
   bolt's z axis (answered by observation).** All stock muzzle bolts (`FLASH_CANON`, `FLASH_MACHINE_GUN`,
   `FLASH_BARREL`, `FLASH_ROCKET1/2`, snowcat `_FLASH1`) have z along the barrel; the original's irq2b cannon shell
   hits (856.0, 33.8, −54.8), which only the z axis reproduces.
5. [ANSWERED: SPEC-AMEND-006 G4.5] [G4] **SPEC-731 B §2 snowcat hull model.** The hull is `enemy/snowcat` / `body` (BODY.ghb; the stock data has no
   `snowcat.gsq`), as the table says; §1's "sequence set `snowcat`" names no model file. The original registers
   BODY.GHL (skins LIGHTS, SNOWCAT, TREADS) on sib1/sib2.
6. [ANSWERED: SPEC-AMEND-006 G4.6] [G4] **Helicopter go-to timing (open).** The original needs one frame more than SPEC-731 A §8/§9.4 imply at the
   start of a go-to (script MOVE end → first leg move: 4 frames vs 3) and at its end (leg arrival → next command's
   first step: 4 vs 3); other commands (face, strafe, instant ids) match frame by frame. Which step costs the frame
   (clear-spot search, go-to routine installed through the selector, completion) is not specified.
7. [ANSWERED: SPEC-AMEND-006 G4.7] [G4] **Per-type skin registration order (SPEC-730 §4, open).** The sets match the original on every SP map, but
   the order of registration differs for some types (e.g. `m_kos_mbrute1a`: original h_h_6_s, h_w_3_s, h_w_5_c;
   `m_x_mmerc`: face skins before b/a/h). The order fixes the skin indices sent to clients; the roster tables give
   the variants in choice order only. `gz_armor` comes after `c_blood`/`c_black` (observed).
8. [ANSWERED: SPEC-AMEND-006 G4.8] [G4] **SPEC-731 A §9.2 pitch on a vertical leg (test vector A5 contradicted in part).** On kos2 (`helitakeoff`,
   test map triggering the script after the client joined) the original climbs the 224 units of the take-off leg
   with the speed profile of §9.2 (+2.7, +5.3 … +24, … +2.7, then 2 per frame) but its pitch stays 0 the whole
   time ("nose-down pitch building up and easing off" does not happen): with no horizontal distance q and r count
   as 1 (pitch target 0). The script's ROF starts on the frame after the arrival (no level-out frames).
9. [ANSWERED: SPEC-AMEND-006 G4.9] [G4] **SPEC-731 A §9.9 / B §9.1 frame of the aim step (answered by observation, to be confirmed).** The
   helicopter's gun (base and barrel), gunner head and pilot head stay at rest while it flies legs at up to 45°
   of pitch and rolls ±50° in turns (trn1); the pilot head only yaws, by 18° per update, during face turns and at
   the start of a leg. So the target point and "500 units ahead" are expressed in a frame turned by the entity's
   yaw only — not by the full entity angles (game convention or the Ghoul entity matrix of SPEC-411).
10. [ANSWERED: SPEC-AMEND-006 G4.10] [G4] **SPEC-731 A §9.9 pilot head limits (contradicted by observation).** On trn1 the original's pilot head yaws
    in steps of 18° (0.314 rad) to about −79° (−1.37 rad) when the look point is behind (face turns, leg starts),
    beyond the specified yaw range −0.3927 … 0.7854 rad, and it never pitches — also on the 540-unit descent with the
    look point far below. Which limits apply, and is the pilot's pitch axis disabled? The crew heads are bolted with a
    90°-turned attach bolt (`HEAD.ghb` `TO_BODY`), so "yaw/pitch about the part's own z/y axes" (SPEC-791 §11) needs
    the axis convention stated for such parts.
11. [ANSWERED: SPEC-AMEND-007 G4.11] [G4] **anglemod: 16-bit or not (SPEC-731 A header and SPEC-AMEND-006 G4.2 vs SPEC-760 B3.6a).** SPEC-731 and G4.2
    say anglemod truncates to 1/65536 turn ("SPEC-760: 16-bit truncation"); SPEC-760 B3.6a says the shared `anglemod`
    has no 16-bit quantisation (and G18's `q_shared.cpp` follows B3.6a). OpenMerc's helicopter turn step uses a local
    16-bit form (G4.2). The difference (≤ 0.0055° per step) is below what a capture shows; which statement is right?
12. [ANSWERED: SPEC-AMEND-007 G4.12] [G4] **SPEC-AMEND-006 G4.2 rule 4 and G4.6 rule 7 against trn1 measurements (after applying them).** Applied as
    written; result (`a6_om_g4trn1` vs `r_orig_g4trn1_120`, helicopter 147, frames = server frames):
    (a) go-to start: now frame-for-frame identical to the original (first leg positions equal from the command on;
    the multi-leg go-to's second leg starts on the same frame, 436, in both);
    (b) final leg arrival A → next scripted HELI command's first step: original A + 4 (twice: 146 → 150, 472 → 476),
    OpenMerc A + 3 as G4.6 says (the script queues that command in the frame the go-to completes);
    (c) roll in the frame after a face turn's snap: original −7.0, −7.0 (snap), −5.6, −1.4, 0 (change < 2.8° in the
    first frame), OpenMerc −7.0, −7.0, −4.2, 0 (arrive's 4°/frame level-out right after the snap, G4.2 rule 4); the
    same < 2.8° first step follows the strafe's arrival-test frame (original −14.1, −14.1, −11.2, −7.0, −2.8, 0).
    A trial variant (snap call reports its error, "facing" one call later with a 2° roll ease; go-to done one frame
    after reaching G) reproduced (b) and (c) exactly but moved the script MOVE after trn1's first face turn 2 frames
    later than the original (original: roll 0 at 174, MOVE moves at 175), so the frame relation between "action
    done" and the script's next step cannot be settled from captures. Question: what does the original do in the
    frame after the snap / strafe arrival test (roll step), and where is the go-to's extra frame at the end?
13. [ANSWERED: SPEC-AMEND-007 G4.13] [G4] **SPEC-731 A §9.5 "every frame the gun fires (if on)" (contradicted by observation).** trn1 strafe (id 22, gun
    on, 33 strafe frames): the original fires 30 rounds (`weapons/othermz/heli`), from the 4th frame (v = 21.3) to the
    frame before the arrival test, and none while v ≤ 20 — the barrel starts spinning on that 4th frame. OpenMerc now
    fires only when v > 20 (the "else" of "when v ≤ 20 the barrel stops", like the leg rule of §9.2). Whether rockets
    follow the same rule is not observed (no stock strafe with rockets on was captured).
14. [ANSWERED: SPEC-AMEND-007 G4.14] [G4] **SPEC-AMEND-006 G4.10 verify (pilot head, trn1 face turn of 180°).** Visible head yaw per update (part x axis,
    packed rotation): original −18.3, −36.1, −54.2, −71.7, −77.4, −78.7 (held); OpenMerc with the ±1 rad limit −18.3,
    −36.1, −54.2, −57.2 (held); both return identically from −43.1 on. In OpenMerc's aim frame the measured yaw of
    these heads equals the visible yaw, so ±1 rad stops the visible turn at 57.3°; the original's limit shows at
    −78.7° and is approached with shrinking last steps (5.7°, 1.3°). The mapping G4.10 calls UNVERIFIED for
    `TO_BODY`-bolted heads is needed to reproduce it.
15. [ANSWERED: SPEC-AMEND-007 G4.15] [G4] **SPEC-731 A §9.7 look-around turn and §11.3 combat facing: banked?** §9.7 says the hovering helicopter
    "turns towards it (§9.8 turn)" and §9.3 banks "only in the turn routine, §9.8, and when requested" without saying
    who requests it. jpn3 auto-combat (`g4jpn3c`): the original rolls up to about 11° in every in-place turn
    (look-around and combat facing); OpenMerc turned them unbanked and now banks them. Please state which turns
    request the bank.
16. [ANSWERED: SPEC-AMEND-007 G4.16] [G4] **SPEC-731 A §13.1 attack turn and combat step frames (jpn3 `g4jpn3d`, original vs OpenMerc, identical paths
    for the first 24 s of auto-combat).** (a) "turn towards the attack point (no bank). While not facing, the turn
    routine runs": the original's turn towards the player banks (roll −11.2 … +11.2) and levels out at 4°/frame
    (11.2, 7.0, 2.8, 0) before the dip — OpenMerc now uses the turn routine with bank and arrive for it. (b) After a
    combat go-to arrives (frame A) the next leg moves at A + 4 in the original; with G4.6's frame rules (arrive,
    go-to routine at G, selector installs the combat routine, pattern chosen with "routine done", next combat step,
    go-to routine, leg) OpenMerc needs A + 7. Which of these happen in the same frame in combat?
17. [ANSWERED: SPEC-AMEND-007 G4.17] [G4] **Weapon choice in auto-combat with rockets on (jpn3: ids 24 and 26 queued, spawnflag 4, `volume` 10).**
    §11.2/§13.1/§9.7 make every attack against a visible target a rocket attack (t = −1/−2) and fire normal-rate
    rockets during hover-fire. Measured with the player in view: original run 1 — two single-rocket dips, then it
    leaves; 7 gun rounds and 1 rocket explosion in 50 s; original run 2 — hovers 20 s facing the player firing 84
    gun rounds and no rocket at all. OpenMerc: a rocket dip every 0.8 s from the hover point (45–50 rocket
    explosions, 66–86 rounds). What makes the original use the gun (time-mode attacks) and fire almost no rockets
    while rockets are on?

## I4c renderer: terrain, liquids, flowing, fog mode 1, mono lightmaps, avi (SPEC-410, 412, 413; 2026-10-25)

Black-box observations with `ref_omgl.dll` and the original `ref_gl.dll` in the same SoF.exe (docs/impl/I4c.md).

1. [ANSWERED: SPEC-AMEND-006 I4c.1] [I4c] **SPEC-410 Liquids, environment-map second pass (contradicted).** Probe textures (loose files in the user
   directory) show that the original never draws the environment map on a liquid: with an environment map that
   encodes its own texture coordinates, nothing of it appears (irq2a `iraq/water`, sud2 `uganda/2_blood1` with its
   second-pass name pointed at `Iraq/env_irq`). Instead the **base** texture appears at sphere-map coordinates of the
   face normal, colour constant (no light, no wave colour), independent of `gl_modulate`; with an opaque base texture
   this hides the warped first pass completely. OpenMerc draws a second pass with the base texture, sphere-mapped,
   colour 1, blend (source alpha, one minus source alpha): exact for opaque probes, but the stock `iraq/water`
   (texture alpha 170) stays lighter than the original (mean 6–7 of 255; a second-pass colour 0.5 would fit the
   stock water at 2.5 but contradicts the opaque probe). Please give the exact second pass: texture bound, blend,
   colour/alpha source, and whether the first pass is drawn differently for such liquids.
2. [ANSWERED: SPEC-AMEND-006 I4c.2] [I4c] **SPEC-410 Far fog wall from a solid view leaf.** With the eye in a solid leaf of sud3 (1640 272 −552, all
   leaves visible), the original draws the far wall in region 5's colour (30, 20, 45), OpenMerc (smallest D of the
   processed overlay vertices) in region 4/6/15's (128, 55, 16). Which region gives the wall colour when the view leaf
   has no region — is "smallest D" taken over a different set of vertices, or is the comparison different?

## I5s-2 sound backend behaviour (SPEC-040, SPEC-004, SPEC-AMEND-001; 2026-10-25)

Black-box comparison of Defsnd 1.07f with OMsnd inside the original client (scripted network sessions through a
relay that sets the player-state music/ambient fields, adds svc_sound_info/svc_sound/entities; process-loopback
captures). Method, data and numbers: `docs/impl/I5s-2.md`. OpenMerc follows the observed behaviour.

1. [ANSWERED: SPEC-AMEND-006 I5s-2.1] [I5s-2] **SPEC-040 1.3, `.adp` distance.** Observed: an `.adp` started by svc_sound at 500, 2500 or ~5700 units
   with attenuation 1 is at full volume from its first 2 ms and plays to its last sample (2n − 38). The spec clears
   the distance multiplier "after the first spatialization" and lets "the first mixed block" be attenuated; taken
   literally (Quake II paint loop) the first block would be at 100/255 at 500 units, and at 2500 units (volume 0)
   the start would be held and the last block cut. Is the multiplier cleared before the first spatialization?
2. [ANSWERED: SPEC-AMEND-006 I5s-2.2] [I5s-2] **SPEC-040 3.2, change to general set 0.** Observed: when the player-state field goes from 14 to 0 the
   ElevatorShaft loop stops at once (no 2000 ms fade-out); changes between non-zero sets crossfade over 2000 ms as
   specified (also the level start 0 → 14 fades in). Is the crossfade skipped whenever the new index is 0?
3. [ANSWERED: SPEC-AMEND-006 I5s-2.3] [I5s-2] **SPEC-040 1.1 order.** "If the client is not in the active state, left = right = 255" is listed before the
   listener rule, but a local sound in the menus plays at 153 (gain 153 · 179/65536 = 0.418), i.e. the listener
   rule applies first (Quake II order). Please confirm and reorder.
4. [ANSWERED: SPEC-AMEND-006 I5s-2.4] [I5s-2] **SPEC-AMEND-001 A, Cvar_Get.** Called with the three listed arguments, SoF.exe 1.07f crashes (access
   violation right after `------- sound initialization -------`); with a fourth argument NULL it works. Is the fourth
   argument the change callback of the cvar object (SPEC-002 +0x10)? Please add it to the argument list.
5. [ANSWERED: SPEC-AMEND-006 I5s-2.5] [I5s-2] **S_StopAllSounds and music.** Observed (`stopsound` during music): ~0.2 s gap (the mixed-ahead part of the
   DMA buffer is cleared), then the music continues on its old timeline; tracks, positions and levels are kept.
   SPEC-040 does not say what S_StopAllSounds does to the music and ambient state; please state it.
6. [ANSWERED: SPEC-AMEND-006 I5s-2.6] [I5s-2] **SPEC-301 command line (engine, for I1a).** With 51 words (program name included) SoF.exe 1.07f dies
   with an access violation 0.2 s after start (exit code 0xC0000005, nothing in sof.log); 48 words run. SPEC-301
   says more than 50 words is the fatal error `argc > MAX_NUM_ARGVS`; is that error raised before anything can
   show it (so a crash is the visible result), and is the limit exactly 50?
3. [ANSWERED: SPEC-AMEND-006 I5s-2.3] [I4c] **SPEC-410 World diagnostics, `gl_showtris`.** The original 1.07f shows no outlines at all for
   `gl_showtris` 1, 2, 3 or 4 (ger1 with `gl_fogmode 2`, irq3b with fog; cvar set in the console after start-up or on
   the command line; multitexture available). Is the outline pass reachable only in the two-pass (no multitexture)
   path, or behind another condition? OpenMerc follows the observation for SoF maps (no outlines).
4. [ANSWERED: SPEC-AMEND-006 I5s-2.4] [I4c] **SPEC-413 §7 `r_speeds 1` counters.** Same paused irq3b spawn view: original `352:wp 659:fp`, OpenMerc
   (fp = region polygons drawn by the overlay passes) `346:wp 619:fp`; tsr1 `548:wp … 0:qp` vs `530:wp … 15:qp` (the
   original prints 0 alpha quads while 15 are submitted). What exactly do `wp`, `fp` and `qp` count (inline-model
   faces, sky polygons, the far wall, faces of the second fog pass; quads only of some kind)?

## F5 SPEC-AMEND-005 follow-ups for G5, G6b, I3, I6h, I6m (2026-10-25)

1. [ANSWERED: SPEC-AMEND-006 F5.1] [F5] **SPEC-741 14.4 `bot_wp_verifypaths`, condition of the error line.** jpndm2 with cheats on a listen server:
   the original prints `Paths verified. 0 errors reported.` at the edit-mode start, OpenMerc (juncture lists built by
   SPEC-AMEND-005 G5.7) reports `ERROR! Path 1 has two junctures coming off of waypoint 1`: path 1 holds two entries at
   waypoint 1, the mirror of path 6's end juncture (129 -> 1) and the mirror of its own loop juncture (52 -> 1); the
   duplicate rule ("same other path at the same waypoint") keeps both. When exactly is the error line printed (or is
   one of the two entries not created)?
2. [ANSWERED: SPEC-AMEND-006 F5.2] [F5] **SPEC-741 editor commands `bot_wp_pathinfo` and `bot_wp_verifypaths`.** Typed in the console of the original
   (listen server, cheats, edit mode active, `bot_wp_whichpath` answering), both printed nothing at all. Do they print
   only under another condition (developer level, through another print), or are they not reachable in 1.07f?
3. [ANSWERED: SPEC-AMEND-006 F5.3] [F5] **SPEC-741 14.5 spawn table trace.** OpenMerc now ignores client 1 in the trace from a spawn point to its
   waypoint (with no ignore, a player standing on the point hid it and the path numbers differed from the original's;
   with client 1 ignored they equal the original's on jpndm2). Please confirm the ignored entity (or mask) of that
   trace.
4. [ANSWERED: SPEC-AMEND-006 F5.4] [F5] **SPEC-741 editor `bot_wp_botroute`.** The original prints `DESTINATION: node 72 (-64 -960 56)` (node before
   the origin; the spec lists origin first) and prints nothing more for a bot whose destination is a spawn waypoint
   (route search fails). When is `Man, I don't know. Try again.` printed?
5. [ANSWERED: SPEC-AMEND-007 F5.5] [F5] **SPEC-741 14.4/14.5 with SPEC-AMEND-006 F5.1/F5.3 on other maps (black box, listen server, cheats, edit
   mode).** jpndm2 now matches exactly (every `bot_wp_whichpath`, `bot_wp_pathinfo 1/2/6/14/15`, 0 errors), but the
   verification lines differ elsewhere. Original / OpenMerc error waypoints: nycdm5 {49} / {49, 75}; plantdm
   {48, 92, 125} / {48, 71, 92, 116, 125}; labdm {10, 120, 144, 248, 261, 266} / {49, 73, 120, 248, 261, 266};
   factorydm {} / {133, 134}; irqdm1 {146 in path 8} / {146 in path 4}; gerctf1 {5} / {5, 181}; sudctf2 {} /
   {307, 308}. Two causes are visible:
   a. **Spawn table.** irqdm1 `bot_wp_whichpath 1..312`: original paths 12 -> 1, 15 -> 2, 16 -> 3, 19 -> 4, 20-50 -> 5,
      ... and waypoints 1-11, 13, 14, 17, 18 in no path (15 spawn waypoints for 20 spawn points); OpenMerc (F5.3 trace
      from each waypoint to the spawn point, ignoring entity 1, mask 0x02010003, fraction exactly 1) marks 1-19 (19
      spawn waypoints), so its numbering is 4 lower from waypoint 20 on. The file's own `dm` section also maps many
      spawn points to waypoint 0. What makes the original reject the nearer waypoints (the spawn point origin used,
      the trace start/end height, or another condition)?
   b. **Juncture records.** nycdm5 (spawn waypoints 1-10, same as the file): original `bot_wp_pathinfo 6` lists
      `(73, path 0, 0)` and `(75 -> path 2: 34)` only, `bot_wp_pathinfo 2` lists `(32, path 0, 0)` and `(34 -> path 6:
      75)` only. By F5.1 path 6's end 75 (cheapest candidate 33, cost 9919.6; 34 13297.2; 77 13761.3) adds `75 -> 33`
      to paths 6 and 2, and path 8's end 77 (only candidate 75) mirrors `75 -> 77` into path 6. The original has
      neither. Which rule refuses them? (A model where a path END holds at most one record - an end that already has
      one does not search, and a mirror into an end that already has one is refused, while records at waypoints
      inside a path accumulate - reproduces jpndm2 and nycdm5, but not plantdm/labdm.)
   Also seen: an end with no candidate link gets a dead-end record `...with path 0 between nodes <end> and 0`
   (jpndm2 path 2 at 53, nycdm5 paths 2 and 6); OpenMerc now does the same. `bot_wp_pathinfo` with a number above the
   path count prints a large unrelated number (`265043096 is not a valid path.`), and `bot_wp_pathinfo 15` on jpndm2
   (15 paths) lists path 15, so the valid range is 1 ... paths (not paths - 1).
6. [ANSWERED: SPEC-AMEND-007 F5.6] [F5] **SPEC-411 6.7 / SPEC-012 *Lighting* - colour of the OpenGL lights (`ghl_light_method` > 2.5).** The
   original's Ghoul GL-table trace (ref_omgl inside SoF.exe, demo `orig1.dm2`, the SMG view weapon) sets DIFFUSE and
   SPECULAR of every world light to colour x (1 - `LIGHT_AMBIENT`): 0.4 x at the default 0.6 at every traced moment
   (20-33 s, so no fade), 0.2 x after `gserver LIGHT_AMBIENT 0.8`, 0.8 x after `gserver LIGHT_AMBIENT 0.2`
   (`openmerc-data/F5/ghl/`). SPEC-411 6.7 says (C, 1). OpenMerc now uses colour x (1 - LIGHT_AMBIENT) for world and
   dynamic lights. Does the factor also apply to dynamic lights and to model (file) omni lights, and to the fast
   directional form?

## I1b module host and protocol-33 server (SPEC-001, 003, 010, 202, 203, 313; 2026-10-25)
Evidence for all items: `docs/impl/I1b.md` (tools `engine/src/server/tests/gtrace/` - a tracing proxy game module
run under the original engine - `sprun.ps1`, `demostates.py`).
1. [ANSWERED: SPEC-AMEND-007 I1b.1] [I1b] **SPEC-313 s7 / SPEC-001 #53: the filter callback has three arguments.** Under the original 1.07f engine
   the callback receives (candidate trace, start point, sweep end point): the start point is the very pointer the
   game passed as the first argument of #53 (same address in every call); the third points to the end point of the
   entity sweep - the world hit point (start + world fraction x (end - start), matched in every logged call) or the
   end when the world was not hit. The original game module reads them: with NULLs it faults on the first offered
   candidate; with stray stack words it faulted at random (kos1, most single-player runs, whenever a shot was traced
   past a character). OpenMerc now passes both. Please add the signature to SPEC-313 s7 (and whether the third is
   exactly "the world trace's end position" when the world was hit at fraction 1).
2. [ANSWERED: SPEC-AMEND-007 I1b.2] [I1b] **SPEC-202 s3.1: "sound-only entities farther than 1200 units are dropped".** On kos1 the original sends 39
   looping-sound entities with renderfx 0x80 at up to 4678 units (client demo, every frame); all have attenuation
   class 0 (sound data 248). OpenMerc now applies the 1200 cut only to attenuation classes 1..7. Is the cut tied to
   the attenuation class (or to something else, e.g. single player)?
3. [ANSWERED: SPEC-AMEND-007 I1b.3] [I1b] **SPEC-010 "Lifetimes and resets": order at server shutdown.** Resetting the server system (method 8)
   before the game's Shutdown made the original game module destroy freed instances (hang or fail-fast at
   `killserver`/`quit`). OpenMerc: drop-all (15), game Shutdown, reset (8), unload. Please state the original order.
4. [ANSWERED: SPEC-AMEND-007 I1b.4] [I1b] **SPEC-301 / SPEC-203: commands after `map`.** The original holds back the commands that follow `map` in
   the same buffer (`+map kos1 +exec x`) until the local client's `begin`, before the client is active (`record`
   in x prints "You must be in a level to record."), as Quake II's deferred buffer does. Not in a spec; OpenMerc
   restored the Quake II behaviour.
5. [ANSWERED: SPEC-AMEND-007 I1b.5] [I1b] **SPEC-311 `serverrecord` in single player**: the original wrote only the configstring block (2594 bytes)
   in 40 s of kos1 and no frame blocks (killed process, so a buffered tail may be missing). Are frames written in
   single player?
6. [ANSWERED: SPEC-AMEND-007 I1b.6] [I1b] **SPEC-001 "How the engine drives the exports": frame counter and returned time.** Logged under the
   original engine (dedicated, kos1, proxy module): `RunFrame(0)`, `RunFrame(1)` while loading, then counters 1, 2, 3
   ... - the first server frame passes 1 again, and the counter equals the wire frame number (first frame 1). The
   original game returns 0.1, 0.2, 0.3, 0.4 ... for these calls, i.e. its own time advanced 0.1 per call, not
   "counter x 0.1" as the spec says (counter 1 returned 0.3); on the wire game time = (frame + 2) x 0.1
   (net-verification.md). OpenMerc follows the observed sequence since 10-25.

## G10 game-module savegames (SPEC-752, SPEC-303, SPEC-001 exports 0x10-0x1C/0x40/0x44; 2026-10-25)

Black-box runs of the original 1.07f module (listen games on a never-shown desktop and a hidden dedicated server in
`D:\Games\SoF-test-G10`; saves decoded with `game/src/save/test/soflist.py`). Evidence: `docs/impl/G10.md`.
1. [ANSWERED: SPEC-AMEND-007 G10.1] [G10] **SPEC-752 3.1 open question 3 (GAME 0x63C-0x697).** Decoded from the original's automatic saves at skills
   0, 1, 2 and 4 against SPEC-760 B9.2: 0x63C int (always 0); 0x640-0x658 T-a ... T-g (floats); 0x65C encumbrance
   value (int); 0x660 S-a; 0x664 T-h; 0x668 cheats (int); 0x66C T-i (int); 0x670-0x684 alert gain, alert fade time,
   max-alert duration, spawn interval at max alert, max characters near, spawn interval; 0x688 saves allowed
   (= sk_saving, constant); 0x68C S; 0x690 T; 0x694 difficulty score. OpenMerc writes this layout. Please confirm,
   and say what 0x63C is.
2. [ANSWERED: SPEC-AMEND-007 G10.2] [G10] **Save limit (SPEC-001 export 0x40, SPEC-760 B9.2 "saves allowed").** Observed: the limit is per level.
   GAME 0x688 keeps sk_saving; the count left is in the level record (LVLC 0x4EC: 1, then 0 over two saves at skill
   3); loading a save gives that level's count back. Messages (`singleplr`): after a save with n >= 2 left
   SAVES_N_LEFT (n), 1 left SAVES_ONE_LEFT, 0 left SAVES_USED_UP; with none left SAVES_NO_MORE and the save is
   refused. OpenMerc does the same. Please confirm that a new level starts with the full count and what `savesleft`
   prints for -1 (OpenMerc: `unlimited`).
3. [ANSWERED: SPEC-AMEND-007 G10.3] [G10] **SPEC-752 3.2 / SPEC-780 1: persistent fields of CLNT.** Read from the original's saves (a level-change
   automatic save and full saves): userinfo 204 (512), name 716 (16), best-weapon string 736 (8), remembered weapon
   id 752, field of view 756 (int), connected 760 (= 0x2F8 of SPEC-752 1.2), health 764, maximum health 768; the
   old pmove state follows at 0x32C; inventory presence 844. OpenMerc's CLNT uses exactly these, so automatic saves
   load in either module (verified: the result depends only on the loading module). Please confirm, and list the
   other fields of 732-843 (four bytes at 732, 744-751, 772-811).
4. [ANSWERED: SPEC-AMEND-007 G10.4] [G10] **SPEC-752 3.4 "relinked immediately": all entities.** After an original save every entity in use is
   linked, also those that were never linked before (link bounds -1/+1 around the origin, the engine values at
   0x168-0x18C change as for a linked entity). OpenMerc links every entity after writing it. Please confirm.
5. [ANSWERED: SPEC-AMEND-007 G10.5] [G10] **SPEC-752 1.2 "the player module's two post-load hooks (SPEC-003)".** Which two exports are they? OpenMerc
   calls nothing in ReadLevel itself; ClientBegin sends svc 29, svc 30 (+ slot 78) and calls export 11 (SPEC-780 2).
6. [ANSWERED: SPEC-AMEND-007 G10.6] [G10] **Ghoul objects on ReadLevel.** SPEC-752 1.2 frees level memory and re-creates the model objects from GGOB.
   OpenMerc's Ghoul glue destroys every object of the server Ghoul system there (as at a level spawn), also the
   player module's cached in-view weapon models; OpenMerc then forgets and precaches them again (exports 8 and 7).
   Does the original keep the spawn's objects (and the player module's) alive across ReadLevel?
7. [ANSWERED: SPEC-AMEND-007 G10.7] [G10] Informative: the original engine's `save` on a dedicated server ends the process before the game module is
   asked (export 0x40 is not called; the same with OpenMerc's module) - `save` needs a listen game. After a `load`
   in a listen game the game is paused (SPEC-303 step 6) until `paused` is cleared.

## I1d engine client: the client side of the player module (SPEC-003, SPEC-400, SPEC-602, SPEC-013; 2026-10-25)
1. [ANSWERED: SPEC-AMEND-008 I1d.1] [I1d] **Who plays the view weapon's notes in single player.** SPEC-003 client imports 4 and 6 and SPEC-600
   Behaviour 5 ("`effect` and `sound` notes are played only in client context") describe the predicting client.
   In single player / listen games nothing predicts, the module's client imports are never called (I7's proxy
   traces), yet the original client plays the `sound` notes (own entity, channel 1, volume 0.6, no attenuation),
   starts the `effect` notes and the fire effects of the replicated view-weapon instance (player-state field 1), at
   the times the server's sequence replication gives (OMsnd logs of `tests/I1d/sp_a.cfg`). OpenMerc registers its
   own note callbacks on that client instance and processes its events to game clock + 0.001 s once per client
   frame (`cl_player.c`). Please confirm the owner (engine) and the limit.
2. [ANSWERED: SPEC-AMEND-008 I1d.2] [I1d] **Import 4 `*` (random volume).** Range not given; OpenMerc uses flrand(0.3, 0.6). No shipped weapon note
   uses `*` (`openmerc-data\I1d\notes.txt`), so it is unobservable here.
3. [ANSWERED: SPEC-AMEND-009 D1] [I1d] **Sniper zoom sound** (SPEC-400 "View weapon" 3, `Weapons/Sniper/Zoom.wav` every 0.2 s while the zoom
   changes, entity 1 channel 0 volume 0.6 attenuation 1 in the original's log): not a player-module note; not
   implemented in the OpenMerc client view yet (I6h/I1d boundary).
4. [ANSWERED: SPEC-AMEND-009 D2, G decision] [I1d] **Instance id reuse against the two Ghoul streams (SPEC-013).** SPEC-010 says freed instance ids are
   reused. When a module destroys a view instance and creates the next one in the same server frame, the new
   instance takes the old id; the removal goes in the reliable stream (section D), the new instance's state in the
   frame's unreliable stream, and the two are not ordered against each other (the reliable message waits for its
   acknowledgement). The original 1.07f module creates the new instance before it destroys the old one (observed
   handles alternate 30/31 at weapon switches), so the case may never arise there. What does the original client
   do with a record whose object id differs from the instance it holds under that id? OpenMerc: the server does not
   write an instance whose id still has a queued removal for that client; the client destroys an instance whose
   object differs from the record's and creates the new one (`ghoul_net.cpp`, I1d).
   *G decision (SPEC-AMEND-009 D2)*: the client follows the original (match by id, no object compare, delta applied
   to the existing instance; drop the destroy-and-recreate); the server keeps the avoidance so an original 1.07f
   client never receives the case.
   *G note (2026-10-26, later)*: the decision stands on its own merits (simpler client, identical behaviour); the
   1.07f-compatibility rationale is moot after the user decision that drops 1.07f network compatibility.
5. [ANSWERED: SPEC-AMEND-008 I1d.5] [I1d, for I7/G] **A server inventory without an owner context.** The game's `elbow`/`playlevel`/`bigelbow`
   re-create the inventory; a 1.07f module given no owner context by the game still writes the player state
   ("works on whatever context it had last", game/src/g_cmds.cpp). OpenMerc's module skips the write (no player
   state), so with a game build that predates the context fix (a5e9c76, 2026-10-25 12:23) the player state keeps
   the old view instance. Does the original module keep a module-wide last context, or does the original game
   always set one? (The current game source sets it.)

## Needs between packages answered by SPEC-AMEND-008 (G4, G7, G18; 2026-10-26)

Items raised as "Needs" in `docs/impl/G4.md` and `docs/impl/G7.md` (not numbered questions above); recorded here so
the answers can be traced.

1. [ANSWERED: SPEC-AMEND-008 G4/G18] [G4, for G18] **trn1 `func_pushkillbox` fling lost** (`docs/impl/G4.md` Needs from
   G18, run `kb_om_g4trn1`): the original's toss physics has no movetype-changed check; the fling survives because a
   push trace that starts inside the killbox reports no plane. Check the plane of OpenMerc's start-solid push trace
   (test in the item).
2. [ANSWERED: SPEC-AMEND-008 G7] [G7, for G18] **Knockback of movetype-10 props whose fall trace is blocked**
   (`docs/impl/G7.md` Needs from G18, `g7bench90`, `g7shot`): SPEC-760 B4.7's all-solid stop is confirmed and must be
   restored.
3. [ANSWERED: SPEC-AMEND-008 G4] [G4, for G3] **Helicopter ricochet sparks: position and type** (`docs/impl/G4.md` Needs
   from G3, KNOWN_ISSUES): position from the hit zone's stored reference point (the entity origin for the stock
   helicopters), type uniform among 0, 10, 14; the single direction byte 99 is still UNVERIFIED.

Not answered by SPEC-AMEND-008: I1d.3 (implementation note) and I1d.4 (queued for the reverse team).

## Items answered by SPEC-AMEND-009 (G18, G4, I1b-2; 2026-10-26)

Open items listed in `docs/KNOWN_ISSUES.md` and `docs/impl/` (not numbered questions above); recorded here so the
answers can be traced.

1. [ANSWERED: SPEC-AMEND-009 A1–A6] [G18] **Remote cameras and seat usage** (KNOWN_ISSUES "benches/seat usage, remote
   cameras"): dead activator ignored (A1), all-clients mode and the single saved fov (A2), flag-2
   watcher non-solid with god cleared on release (A3), one "no camera" frame on a nested release (A4), shots at a
   watcher hit only its helper volumes (A5); no separate seat behaviour beyond SPEC-770 props (A6).
   *A1 confirmed by SPEC-AMEND-010 A1*: the tested value is the entity's death state (2 = dead: player death, forced
   team/model-change suicide, character death actions, vehicle deaths, silent AI removal; reset to 0 on put-in-server
   and level read); the check applies to client activators only.
2. [ANSWERED: SPEC-AMEND-009 B2] [G4] **ger4 flamethrower/fireball precache** (`docs/impl/G18.md` ger4 row,
   KNOWN_ISSUES): not the Dekker bosses (OpenMerc's attacks 4 / 29 are right) but the precache spawn of ger4's
   `spawner_boosterpack` classnames (`m_cas_ffemale`, attack 18/19, player weapon 12). C to measure which half
   OpenMerc misses. `cin/ger4/s54d1.adp` belongs to G6b's cinematic-sound routing. B1: G4 12/14/16/17 stay as
   answered by SPEC-AMEND-007; measurements are C's.
3. [ANSWERED: SPEC-AMEND-009 C1] [I1b-2] **tsr1 "Failed fDot test"**: the print is the module's side-contact rule;
   extra prints under OpenMerc come from engine-side player movement or trace/link results (SPEC-314 movement).
4. [ANSWERED: SPEC-AMEND-009 C2] [I1b-2] **Landing event lost on slopes**: SoF's step-slide keeps the raised slide's
   vertical velocity, compares 3D distances, has no step-down phase; ground test is "trace hit no entity".
5. [ANSWERED: SPEC-AMEND-010 C3] [I1b-2] **C3 trn1 intro camera timing**: resolved, not a defect. The intro starts at
   the local client's ClientBegin (worldspawn script, AMEND-005 G6b.1), not at level spawn; I1b compared server frames
   with wall-clock time, i.e. the begin delay. G6b's caption-anchored comparison was identical. Verify with the
   amendment's test (ClientBegin frame vs first-caption frame in both engines).

## Items answered and opened by SPEC-AMEND-011 (own weapon fire sound, mode A; 2026-10-26)

Answered:

1. [ANSWERED: SPEC-AMEND-011 R11.1-R11.6] **No own fire sound for pistol or shotgun** (user report, mode A): the sound
   is the sound primitive of the first-person muzzle effect (`effects/weapons/playermz/*.eft`), started by the client
   engine from the view weapon's `effect` note; the server sends nothing for it. Also answers SPEC-600's open question
   on how first-person `sound` / `effect` notes reach the player in a local game.
2. *Gate 2 fixed by C*: commit `d06e62b` on `engine-services` makes the fov == 95 test (and the zoomed sniper effect
   and zoom mouse factor) use the player-state fov before the widescreen widening; 1280x720 test went from 0 of 4 to
   4 of 4 fire sounds. Not yet merged to main or deployed to `D:\Games\OpenMerc`.

Still to be checked by tests (C; implementation behaviour, not reverse-team questions):

3. Notes at exactly 0 ms (shotgun `fire1`, pistol `fire1_b`) are delivered when the sequence starts (R11.3, R11.6 2).
4. The local player's own entity is in the client frame and its effects bit 0x4000 is clear, so the muzzle effect is
   not detached before its sound fires (R11.2 gate 6).
5. `sound` notes (reload and handling) play on the own entity, channel 1, volume 0.6, attenuation 0 / ATTN_NONE
   (R11.4, R11.6 6; SPEC-AMEND-008 I1d.2).

Open (reverse team):

6. [ANSWERED in part: SPEC-AMEND-014 R14.8; OPEN for coupling] **Identity of the input-freeze flag**: game module
   exports Set/Get of a cinematic-freeze byte; engine cvar `cl_cinematicfreeze` exists; AMEND-011 gate 1 is the same
   family. Exact game→client coupling still OPEN (see AMEND-014 open question 2).
7. [ANSWERED: SPEC-AMEND-014 R14.9] **Notes at exactly the sequence start (0 ms)**: original honours them; a
   backend that drops zero-time notes silences shotgun fire. Still to be confirmed by OpenMerc tests (item 3 above).
8. [OPEN: SPEC-AMEND-011] **View-model attachment placement**: does the attachment refresh for the view-weapon
   instance use the view model's render placement? Does not affect audibility (attenuation 0), only where the
   flash is drawn.

## Items answered and opened by SPEC-AMEND-012 (save / load; MODEB-GAPS Q1, Q12; 2026-10-26)

Answered:

1. [ANSWERED: SPEC-AMEND-012 R12.1–R12.3] **What a full save / load writes and restores**: game section (`GAME` /
   `CLNT` / `INVN` + attached inventory/Ghoul owner hooks) and level section (`FTSZ`, level record, every in-use
   entity with string/object/AI/Ghoul/body/gore chunks including `EBIA`/`ZGIA`). Corpses and live NPCs that are still
   entities are written; pure client debris/effects are not. Load frees level-tagged memory, checks `FTSZ`, restores
   into original slots, remaps Ghoul ids, then rebuilds each client's view-weapon. Answers MODEB-GAPS Q1.
2. [ANSWERED: SPEC-AMEND-012 R12.3] **`auto.sof` on level change (Q12)**: in SP, after spawn, `map` / `gamemap` write
   a **game section only** (comment starts with ENTERING); no level section. Health/inventory ride persistence cvars
   and the game record (see also AMEND-013). `map` deletes any existing `auto.sof` before the new map loads.

Still to be checked by tests (C):

3. Save census on kos1 / ger1 / ger3 (R12.1 test) and mid-mission vs `auto.sof` load (R12.2 test).
4. OpenMerc load crashes on ger1/ger3/kos1: clear or remap every entity link that is not written (including
   `helperlink`, already suspected by the menu stream); then re-run the census.

Open (reverse team):

5. [OPEN: SPEC-AMEND-012] Exact console / dialog text of the `FTSZ` mismatch error (wording not copied into the
   spec).
6. [OPEN: SPEC-AMEND-012] Whether every "transient static" class on ger1/ger3 is an entity attachment or a pure
   effect (black-box entity-class census before save vs after load).

## Items answered and opened by SPEC-AMEND-013 (player module across level change; MODEB-GAPS Q2; 2026-10-26)

Answered:

1. [ANSWERED: SPEC-AMEND-013 R13.1–R13.3] **Stale player.dll pointers across level change (Q2)**: player `Shutdown`
   is a no-op and `Init` only returns success; every game init must call `GetPlayerClientAPI` /
   `GetPlayerServerAPI`, which re-copy host imports and re-fill export method tables into the current module image.
   Inventory and health cross via persistence cvars written before shutdown and read after spawn — not via dangling
   pointers. The three crashing OpenMerc transitions are the acceptance test.

Still to be checked by tests (C):

2. After every map change, player export table pointers equal a fresh `GetPlayer*API` result; no call uses a
   pre-`ShutdownGame` value (R13.1 / R13.2 / R13.3 tests), including any inventory/weapon object pointers kept in
   entity or client records.

Open (reverse team):

3. [OPEN: SPEC-AMEND-013] Whether the client-side player module is unloaded at all during a local `gamemap` (or
   only the server side cycles). Reimplementations that skip re-querying `GetPlayerClientAPI` still break either way.
4. [OPEN: SPEC-AMEND-013] Exact identity of the "player notified of level exit" method (role known; confirm by
   black-box that inventory cvars change before `ShutdownGame`).

## Items answered and opened by SPEC-AMEND-014 (MODEB-GAPS Q3–Q11 + gore / blood pools; 2026-10-26)

Answered:

1. [ANSWERED: SPEC-AMEND-014 R14.1, R14.6] **Skin / variant names vs model paths (Q3, Q8)**: names like
   `ACC_BOX_HIP_BRWN`, `MESO/B_BASIC`, `COMSKIN/F_BAB_*` are skin / bolt-on / sequence-list identifiers, not model
   file paths. Looking them up as models yields "not found". Same class of bug for MP preview.
2. [ANSWERED: SPEC-AMEND-014 R14.2] **String ID 0x060e / friendly-loss text (Q4)**: package 6 entry 14 =
   `INNOCENT_HIT` / "Check your fire!" from **pak2** `strip/singleplr.sp` (pak0 COUNT is 14, so entry 14 is missing
   without the override). Related: `MISSION_FAILED`, `FRIENDLY_CASUALTIES`.
3. [ANSWERED: SPEC-AMEND-014 R14.3] **Gut death (Q5)**: zone `GZ_GUT_FRONT` → sequence `std_sgut_2side_n_n`; if
   missing, print `Couldn't find gut death!` and fall through. Exact tsr2 model still OPEN.
4. [ANSWERED in part: SPEC-AMEND-014 R14.4] **God / heretic vs friendly-loss fail (Q6)**: `maxDeadHostages` (default
   3, −1 disables) kills every player when the limit is reached. Whether `god` / `heretic` block that kill is
   UNVERIFIED — do not special-case until a 1.07f capture exists.
5. [ANSWERED: SPEC-AMEND-014 R14.5] **SP stats mid-game (Q7)**: published via game-module stats cvars
   (`stat_playedtime_base`, `stat_guyskilled`, …) and `getgamestats`; empty OpenMerc page = wrong menu path or not
   reading the cvars.
6. [ANSWERED: SPEC-AMEND-014 R14.7] **Per-weapon alt fire (Q9)**: owned by SPEC-600; no extra global "reload always
   cancels alt fire" rule beyond the per-weapon rows.
7. [ANSWERED: SPEC-AMEND-014 R14.9] **0 ms sounds and invalid `environ_soundgen` (Q11)**: original delivers 0 ms
   notes; bad `soundName` prints `Soundgen with invalid name!` and removes the entity (SPEC-771 B.4).
8. [ANSWERED: SPEC-AMEND-014 R14.10] **Extra "meat chunks" on dismemberment (owner report)**: original flesh debris
   is TE 18 chunk type 32 drawn with **rock** models; gut visuals are `Enemy/bolt/g_guts*` bolt-ons; body debris
   burst (SPEC-721 item 8) has **no caller** in 1.07f. Original does not throw `chunks_meat` / `sm_meat` on
   dismemberment. **Conflict to resolve**: the owner sees the chunks in mode A (original `gamex86.dll`), so C must
   census TE 18 / effect names / drawn models in mode A before changing game code — the source may be the engine
   client's chunk→model map, shipped effect files, or leftover Quake II gibs.
9. [ANSWERED: SPEC-AMEND-014 R14.11] **Persistent floor blood pools**: **not** original. Original `gore/bloodpool`
   is a short-lived client effect (sprite), suppressed by `lock_blood`. Persistent pools are a planned post-parity
   modern feature (owner request); keep them out of parity tests and behind their own toggle.

Still to be checked by tests (C):

10. Mode A census of dismemberment debris (TE 18 types, models drawn, effect names) vs R14.10; remove any
    `chunks_meat` / Quake II gib / body-debris-burst path that is not in SPEC-721 callers.
11. Pak2 `singleplr.sp` is mounted so 0x060e resolves; SP stats menu reads the published cvars; skin strings are
    never passed to open-model.

Open (reverse team):

12. [OPEN: SPEC-AMEND-014 R14.4 / Q6] God / heretic vs B9.4 kill — 1.07f black-box needed.
13. [OPEN: SPEC-AMEND-014 R14.8 / Q10] Exact game→client cinematic-freeze coupling (also closes the rest of
    AMEND-011 gate 1).
14. [OPEN: SPEC-AMEND-014 R14.3 / Q5] Which tsr2 model lacks `std_sgut_2side_n_n`.
15. [OPEN for C audit, recorded by R: SPEC-AMEND-014 R14.10] Whether any OpenMerc sever path still calls a
    body-debris-burst equivalent or throws `chunks_meat`.

## Items answered and opened by SPEC-AMEND-016 (GFX-modern section 7: glow, sun, shadows, fog; 2026-10-26)

Answered:

1. [ANSWERED: SPEC-AMEND-016 R16.1] **Flag 0x1000 (SPEC-100 open question 2)**: time-based texture animation at the
   `.m32` rate (small `value` = frames per second); such faces get normal lightmaps and are not self-lit. SURF_LIGHT
   (0x1) only matters at compile time. 1.07f has no bloom / glow / HDR pass.
2. [ANSWERED: SPEC-AMEND-016 R16.2] **Worldspawn `_sun*` keys**: never read at runtime; compiler input baked into
   lightmaps. Direction convention recovered from lightmaps: `_sun_angle` = yaw / pitch of light travel;
   `_sun` / `_sun_target` = helper `light` aimed at the named `info_null`; `_sun_vector` = light travel vector.
3. [ANSWERED: SPEC-AMEND-016 R16.3] **Lump 20 type 3**: always skipped by the Ghoul world-light set (as are radius
   > 1500); compile-time sun samples only.
4. [ANSWERED: SPEC-AMEND-016 R16.4] **Realtime sun shadows in 1.07f**: none; only Ghoul blob / projected-mask
   shadows (SPEC-411). Stencil entry points are loaded but never used. CSM is a modern addition.
5. [ANSWERED: SPEC-AMEND-016 R16.5, confirms SPEC-410] **Fog region kind**: converted at load but never used; all
   hardware fog for regions is linear.
6. [ANSWERED: SPEC-AMEND-016 R16.6] **Terrain fog vs region fog**: never combined; terrain fog only on terrain quads,
   objects only region fog. No stock case (`trn1` has neither active).
7. [ANSWERED: SPEC-AMEND-016 R16.7] **Ghoul self-lit materials**: self-illumination P4 (175 / 1030 stock materials
   > 0, 52 effectively fullbright) and additive blend (70); "off" lamp variants still contain GLOW materials.

Still to be checked by tests (C):

8. Bloom seeds follow what is actually drawn (lamp on / off variants), not the model file; 0x1000 screens do not
   bloom by default (owner rule: only what really glows).
9. CSM direction from the map keys matches the baked shadows on irq1a, tsr1, arm3, nyc1 (top-down light render vs
   in-game); no directional light on sibctf1 / ger3; no double shadows with Ghoul blob / mask on.
10. Volumetric fog driven by lump 21 only; any use of the fog kind is recorded in `docs/DEVIATIONS.md`.

Open (reverse team):

11. [OPEN: SPEC-AMEND-016 R16.2] nycdm4 `_sun_vector` does not match its lightmaps (weak night sun); confirm in game
    or with the top-down light render.
12. [OPEN: SPEC-AMEND-016 R16.2] Compiler defaults when keys are missing: direction for jpn2 / sib3 (inferred
    straight down), intensity for kos1 (no `_sun_light`), and whether jpn1 `_sunlight` was honoured.
13. [OPEN: SPEC-AMEND-016 R16.2] Precedence between name and angle / vector direction keys (no stock map has two).
