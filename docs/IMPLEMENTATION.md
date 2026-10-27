# Implementation packages

| Id | Package | Specs | State |
|---|---|---|---|
| I0 | World loading (BSP v46, m32, lightmaps, collision) | 100–102 | done |
| I2a | Network client, protocol 33 | 200–203 | done (joins original servers) |
| I2b | GameSpy replacement (masters, heartbeats, query responder, server list) | 204 | done (listed on 3 community masters) |
| I5s | Sound backend OMsnd | 004, 110–113 | done (runs in the original executable); I5s-2 done: OMsnd 0.3 matches Defsnd (docs/impl/I5s-2.md) |
| I1a | Engine core: filesystem, startup/config, cvars/commands, savegame container, string packages, sound backend host | 300–303, 131, 313, 004 | done (docs/impl/I1a.md; game-module hooks for I1b) |
| I3 | Ghoul model system library (C++) | 010–014, 120, 121 | done (docs/impl/I3.md) |
| I4a | Renderer: 2D/fonts, sky, fog, detail, effects primitives, cvars | 002, 410, 412, 413, AMEND-002 | done (docs/impl/I4a.md) |
| I7 | Player module (weapons, inventory, prediction) | 003, 600–602 | done (docs/impl/I7.md; SP traces equal the original; MP within run-to-run noise; not covered: holster, knife blood, svc 30 to a predicting client) |
| I4b | Renderer: Ghoul model drawing | 411, 012 | done in the SoF renderer module (docs/impl/I4b.md) |
| I1c | Engine client: Ghoul client system (svc 26/27, menu models, view weapon) handed to the renderer | 010–013, 002, 310, 800–803 | done (docs/impl/I1c.md; tsr1 intro on our engine with original gamex86/player; open: SMG colour I1c-9, x64 sound, view-weapon prediction) |
| I4c | Renderer: remaining I4a features (terrain, avi, flowing-surface scroll, detail on inline models, gl_fogmode 1, gl_monolightmap) | 002, 144, 410, 412, 413 | done (docs/impl/I4c.md; 0-1/255 against ref_gl.dll; open: iraq/water, r_speeds counters; not done: A3D export, gl_drawflat, gl_drawmode) |
| I1b | Engine: game-module host (99 imports), player-module host, Ghoul services for modules; with I2c | 001, 003, 005, 010–013 | campaign runs with the original modules (docs/impl/I1b.md; all 32 SP maps, save/load, gamemap); open: client side of the player module → I1d, fDot/trn1 camera → I1b-2, movement spec → R |
| I2c | Server side of protocol 33 + ghoul stream writer | 200–203, 013 | together with I1b |
| I2d | GameSpy lobby chat (the 13 chat commands; menus' chat pages) | 204 | done (docs/impl/I2d.md; default peerchat.openspy.net; two chat-page display items for I6m) |
| I5 | Client effects and temp entities | 142, 400, 401 | done (docs/impl/I5.md; every temp entity/svc_effect of the original handled; debris, flame, beam match; open: editor text pages, shooter's own weapon effects with I1d) |
| I6m | Menus (RMF runtime) | 130, 800–803 | done (docs/impl/I6m.md; Ghoul frames after I4b, renderer 2D path after I4c) |
| I6h | Client view, HUD, input, demos | 310–312 | done (docs/impl/I6h.md; view weapon after I4b) |
| G18 | Game module, first slice: world core, spawning, physics (G1) + player/client, items, projectiles (G8) | 001, 006, 760, 761, 780–782 | in progress (tested as gamex86.dll inside the original executable) |
| G2 | Game: AI framework, perception, decisions, actions (`game/src/ai/`) | 700, 701, 710, 711 | in progress |
| G3 | Game: bodies, gore, damage (`game/src/damage/`) | 720–722 | code complete for its specs (docs/impl/G3.md); waits on G18 player poses, G2 save hook, AMEND-006 (G3.19, G3.21) |
| G4 | Game: roster, helicopters, vehicles (`game/src/roster/`) | 730–732 | in progress (with G2) |
| G5 | Game: navigation, bots (`game/src/nav/`, `game/src/bots/`) | 143, 740, 741 | done (docs/impl/G5.md; SP navigation and team objectives untested in game) |
| G6a | Game: script VM, cinematics, ROF as a library with a host interface (`game/src/script/`) | 140, 141, 750, 751, 752 | done (library + tests; docs/impl/G6a.md) |
| G6b | Game: script host on entities, script_runner, script savegame wiring (`game/src/scriptglue/`) | 750–752 | done (docs/impl/G6b.md; remote cameras are G18) |
| G7 | Game: props, server effects (`game/src/props/`, `game/src/effects/`) | 770, 771 | in progress |
| G9 | Game: game modes, Ghoul glue (`game/src/modes/`, `game/src/ghoulglue/`) | 790, 791 | code complete (docs/impl/G9.md; 20 mode cases equal the original; waits on AMEND-006 items 12–16) |
| G10 | Game: savegames (`game/src/save/`, `g_save.cpp`) | 752, 303, 013, 700, 003 | done for OpenMerc's module (docs/impl/G10.md; save → load → replay equal on tsr1; cross-module level saves refused by design; needs from G18/G2/G3/G4/G7/G6b listed) |
| V1 | Modern video settings page (our `zz_openmerc.pak` data), desktop defaults, aspect-correct 2D (`scr_aspect`) | 301, 310, 410, 800–803 (DEVIATIONS) | done (docs/impl/V1.md; page, defaults, `scr_aspect`, HUD scaling `scr_hudscale` with captures at 1080p/1440p; open: scope/rank/flag anchoring unverified by capture) |
| P1 | 64-bit / Linux / macOS builds without any original module | all | after all above |
