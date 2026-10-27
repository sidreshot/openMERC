# Third-party code

| Component | Where | Origin | License |
|---|---|---|---|
| Yamagi Quake II | `engine/` (imported as the starting point of the engine) | https://github.com/yquake2/yquake2, commit `9dbf699273e2546f3ad14cff3dee6a51a518cd2a` (2026-10-15) | GPL-2.0-or-later; bundled components listed in `engine/LICENSE` |
| Quake II 3.19 (id Software) | basis of Yamagi Quake II | https://github.com/id-Software/Quake-2 | GPL-2.0 |
| Quake II 3.19 game source (id Software) | `game/src/`: `q_shared.h/.cpp`, `g_local.h`, `g_main`, `g_spawn`, `g_utils`, `g_phys`, `g_func`, `g_trigger`, `g_target`, `g_misc`, `g_combat`, `g_cmds`, `g_chase`, `p_client`, `p_view`, `p_hud`, `p_trail` (`.c` → `.cpp`), copied as the base of the G18 game module and changed to SPEC-760/761/780 | https://github.com/id-Software/Quake-2 (commit `372afde46e7defc9dd2d719a1732b8ace1fa096e`) | GPL-2.0 |
| Quake II 3.19 client sound system (id Software) | `modules/sound/src/snd_dma.c`, `snd_mix.c`, `snd_mem.c` (from `client/snd_dma.c`, `snd_mix.c`, `snd_mem.c`), changed to the SoF default sound backend of SPEC-040 (OMsnd 0.2) | https://github.com/id-Software/Quake-2 (commit `372afde46e7defc9dd2d719a1732b8ace1fa096e`) | GPL-2.0 |
| SDL 3 | not in the tree: `SDL3.dll` (vcpkg build) is the window, input and audio-device layer of the engine | https://github.com/libsdl-org/SDL | zlib |
| OpenAL Soft 1.25.2 | not in the tree: linked by `modules/sound` (OMsnd.dll) as `OpenAL32.dll`; its public headers, including the EAX preset table `AL/efx-presets.h`, are used at build time | https://github.com/kcat/openal-soft (vcpkg port `openal-soft`, triplet x86-windows) | LGPL-2.0-or-later |
| {fmt} | not in the tree: `fmt.dll` is a runtime dependency of the vcpkg OpenAL Soft build and is shipped next to it | https://github.com/fmtlib/fmt (vcpkg port `fmt`) | MIT |
| GameSpy "validate" algorithm (reference only, no code copied) | `tools/net/masterquery.py` (written before SPEC-204 existed, from the public description); `engine/src/common/gamespy.c` follows SPEC-204 | public description of the GameSpy master-server validation ("gsseckey"), Luigi Auriemma's gsmsalg, http://aluigi.altervista.org/papers.htm | gsmsalg is GPL-2.0-or-later; both OpenMerc implementations are independent and check against the SPEC-204 test vectors |
| stb_truetype 1.26 (Sean Barrett) | `engine/src/client/menu/header/stb_truetype.h`, TrueType rendering of the menu fonts | https://github.com/nothings/stb | MIT or public domain (dual) |
| Play, Russo One, JetBrains Mono fonts | `data/base/fonts/om/*.ttf`, unmodified, packed into `zz_openmerc.pak` | https://github.com/google/fonts (ofl/), https://github.com/JetBrains/JetBrainsMono | SIL Open Font License 1.1 (`data/base/fonts/om/OFL-*.txt`) |

Files changed after import carry an OpenMerc notice below the original copyright header.
