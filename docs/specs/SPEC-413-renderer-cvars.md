# SPEC-413: Renderer cvars, texture loading, colour correction and start-up
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: ref_gl.dll, version 1.07f (engine-side facts from SoF.exe where marked)
Depends on: SPEC-002 (renderer interface), SPEC-100 (`.m32`), SPEC-010 (Ghoul system methods), SPEC-301 (start-up and
config), SPEC-302 (cvars, flags, `cpuid`), SPEC-802 (menu gamma preview). Sister drafts: SPEC-410 (world), SPEC-411
(models), SPEC-412 (2D, fonts, effects drawing).

## Purpose

This spec lists every console variable the OpenGL renderer creates or reads, with default, flags and effect, and
specifies the renderer behaviour that is not tied to one kind of geometry:

- start-up: OpenGL driver choice, video mode setting, window and pixel format, extension use, video-card detection
  and the driver configuration files;
- per-frame set-up: buffer clearing, projection, buffer swap, statistics and warning overlays;
- image management: name lookup, caching, substitution, loading of `.m32` files, mip-level selection, internal
  formats, filtering, freeing;
- colour correction of images (gamma, brightness, contrast) and the meaning of `gl_modulate`.

World, model and 2D/effects behaviour selected by cvars is described in SPEC-410, SPEC-411 and SPEC-412; the cvar
table below gives a one-line summary and the reference.

## Interface / layout

### Console commands added by the renderer

| Command | Effect |
|---|---|
| `imagelist` | Lists loaded images (format below). |
| `gl_strings` | Prints four lines: `GL_VENDOR: <s>`, `GL_RENDERER: <s>`, `GL_VERSION: <s>`, `GL_EXTENSIONS: <s>` (the label is printed in colour 7, the value in colour 1, SPEC-002 colour codes). |
| `screenshot`, `rawshot` | See SPEC-412. |

All four are removed again when the renderer shuts down.

`imagelist` output:
1. `M = Skin, S = Sprite, W = wall, P = Pic, D = Detail. F = Font`
2. `------------------`
3. For every image slot in use, in slot order: one type letter (`M` skin, `S` sprite, `W` wall, `P` pic, `D` detail,
   `F` font; a space for sky, raw and environment images), then ` <w> <h> RGB: <name>` with width and height printed
   right-aligned in 3 columns. `RGB` is printed for every image.
4. `Total texel count   : <sum of level-0 width × height of the listed images>`
5. `Total image count   : <number of listed images>`

### Cvar table

Flags use SPEC-302 values: 1 archive, 0x20 integer, 0x40 literal. "Default" is the value the renderer passes when it
creates the cvar; when the engine has created the cvar first (for example `vid_fullscreen`, `vid_ref`, `gl_mode`),
the engine's default stands (SPEC-302) and only the flags are added.

#### Start-up, mode and driver

| Cvar | Default | Flags | Effect |
|---|---|---|---|
| `gl_driver` | `opengl32` | 1 | Name of the OpenGL library to load. Empty: try the driver list (see *Driver selection*). |
| `gl_mode` | `3` | 1 | Video mode number, valid 3–9 (table under Constants). Changing it restarts the renderer on the next frame. |
| `vid_fullscreen` | `0` | 1 | Non-zero: fullscreen mode. Changing it restarts the renderer on the next frame. |
| `gl_bitdepth` | `0` | 1 | Fullscreen colour depth; 0 = keep the desktop depth. Set to 0 by the renderer whenever a windowed mode is set, and when the operating system cannot change depth (message `gl_bitdepth requires Win95 OSR2.x (or later) or WinNT 4.x (or later)`). |
| `gl_displayrefresh` | `0` | 1 | Non-zero: fullscreen refresh rate in Hz requested from the display driver. |
| `gl_allow_cds` | `0` | 0x20 | No effect in 1.07f: mode setting always behaves as if it were 1 (the renderer overwrites the numeric value with 1, the text stays as typed). |
| `gl_allow_software` | `0` | 0x20 | Non-zero: accept a non-accelerated (generic software) OpenGL implementation. |
| `vid_xpos`, `vid_ypos` | (engine) | — | Read when a window is created: top-left position of a windowed-mode window. |
| `vid_ref` | `gl` | 1 | Created with the engine's default. The renderer marks it modified to request its own restart (see *Per-frame*). |
| `vid_card` | `[not set yet]` | 1 | Name of the detected video card family. Written by card detection. |
| `cpu_memory_level` | `0` | 0 | Memory class 1–3 computed by the engine (`cpuid`, SPEC-302). Selects `drivers/memory<N>.cfg`. |
| `cpu_speed_level` | `0` | 0 | Processor class 2–4 computed by the engine. Selects `drivers/cpu<N>.cfg`. |
| `cpu_memory_using` | `0` | 1 | Memory class the driver configuration was last applied for. |
| `cpu_amd3d` | `0` | 0 | Set by the engine when 3DNow! is present. With `use_amd3d` it selects an alternative implementation of an internal culling routine; the result is the same (UNVERIFIED by test). |
| `use_amd3d` | `1` | 0 | See `cpu_amd3d`. |
| `cpu_mmx`, `use_mmx` | `0`, `1` | 0 | Created but not used by the renderer. |
| `gl_ext_multitexture` | `1` | 1 | 0: do not use `GL_ARB_multitexture` even when available (world drawing then uses separate passes, SPEC-410). Read at start-up. |
| `gl_ext_anisotropic` | `1` | 1 | With `GL_EXT_texture_filter_anisotropic` available: non-zero sets maximum anisotropy 2.0 on mip-mapped image types, 0 sets 1.0. Re-applied to all images when changed. |
| `gl_s3_s3tc` | `1` | 1 | Non-zero and the driver reports `GL_S3_s3tc` or `GL_ARB_texture_compression`: cvar `s3tc_avail` is set to 1 (else 0). The renderer itself does not read `s3tc_avail`; compression is selected only through `r_isf`/`r_iaf`. |
| `gl_ext_swapinterval` | `1` | 1 | Created but not used. |
| `gl_swapinterval` | `1` | 1 | When changed (and at start-up): if `WGL_EXT_swap_control` is available, the swap interval is set to its integer value (1 = wait for vertical retrace). |
| `gl_drawbuffer` | `GL_BACK` | 0 | When changed: draw buffer = front buffer if the value is `GL_FRONT` (any case), else back. Buffers are swapped at the end of a frame only while the value is `GL_BACK` (any case). |
| `gl_finish` | `0` | 0 | Non-zero: wait for the GL to finish at the start of every 3D frame. |
| `gl_log` | `0` | 0 | No effect (no GL call log exists in 1.07f). |
| `gl_scissor_broken` | `0` | 1 | Non-zero: the 2D clip-rectangle calls of SPEC-002 (0x8c–0x9c) do nothing. |
| `gl_gentextures_broken` | `0` | 1 | Non-zero: texture object names for special textures are allocated by the renderer from a counter instead of by the GL. |
| `gl_rj` | `0` | 1 | Created but not used. |
| `sc_framerate` | `30` | 0 | Created by the renderer, read only by the engine (not specified here). |
| `fs_createpak` | `0` | 0 | Filesystem pak-building mode (SPEC-300). Renderer effect: an image that names an alternate texture also loads that alternate, so both end up in the pak. |
| `lock_textures` | `0` | 0 (engine adds 0x1080) | Part of the engine's parental lock group (`lock_*`). Non-zero: images whose `.m32` `kind` has bit 0x4 are replaced by their alternate texture (see *Alternate textures*). The renderer can only see it because it is created while hidden cvars are accessible (SPEC-002 import 0x4c). |
| `gl_vendor`, `gl_renderer`, `gl_version` | — | — | Set at start-up to the driver's GL strings. |
| `s3tc_avail` | — | — | Set at start-up, see `gl_s3_s3tc`. |

#### Images and colour

| Cvar | Default | Flags | Effect |
|---|---|---|---|
| `gl_picmip` | `0` | 1 | Number of mip levels dropped when an image is uploaded (all image types; see *Mip-level selection*). Applies to images loaded after the change. |
| `gl_pictip` | `0` | 1 | Texture-memory saving mode. Non-zero: every image whose `.m32` names an alternate texture is replaced by it, and images with `.m32` field 0x2D8 = 1 drop one more mip level. |
| `gl_skymip` | `0` | 0 | Non-zero: sky images drop one more mip level (SPEC-410). |
| `gl_texturemode` | `GL_LINEAR_MIPMAP_NEAREST` | 1 | Filter mode (names under Constants). An unknown name prints `Bad filter name` and changes nothing. Re-applied to all loaded images when changed. |
| `r_isf` | `GL_SOLID_FORMAT` | 1 | Internal GL format for images without alpha (format names under Constants). |
| `r_iaf` | `GL_ALPHA_FORMAT` | 1 | Internal GL format for images with alpha. |
| `vid_gamma` | `0.0` | 1 | Gamma of the image colour table. |
| `vid_brightness` | `0.5` | 1 | Brightness of the image colour table. |
| `vid_contrast` | `0.5` | 1 | Contrast of the image colour table. |
| `gl_modulate` | `1` | 1 | Light multiplier for lightmaps and light probes (SPEC-410). Read only when the renderer starts. |
| `gl_monolightmap` | `0` | 1 | Lightmap colour reduction mode, first character significant (SPEC-410). |
| `gl_nobind` | `0` | 0 | Debug: every texture bind uses the console character set image instead (all surfaces show that texture). |
| `gl_textureclamp` | `1` | 0 | Shifts world polygon texture coordinates by whole numbers; no visible effect (SPEC-410). |

#### Frame, view and debugging

| Cvar | Default | Flags | Effect |
|---|---|---|---|
| `r_norefresh` | `0` | 0 | Non-zero: the 3D view is not rendered (SPEC-002). |
| `r_drawworld` | `1` | 0 | 0: world surfaces are not drawn (SPEC-410). |
| `r_drawentities` | `1` | 0 | 0: entities are not drawn (SPEC-411). |
| `r_fullbright` | `0` | 0 | Non-zero: no lightmaps (SPEC-410). |
| `r_lightlevel` | `0` | 0 | Output: light level at the view origin, written every 3D frame (see *Per-frame*). |
| `r_novis` | `0` | 0 | Non-zero: ignore visibility data (SPEC-410). |
| `r_nocull` | `0` | 0 | Non-zero: no frustum culling of boxes (SPEC-410). |
| `gl_lockpvs` | `0` | 0 | Non-zero: keep the current visible set while moving (SPEC-410). |
| `r_speeds` | `0` | 0 | Statistics (see *Statistics*). |
| `r_warnings` | `0` | 0 | Performance warning tint (see *Statistics*). |
| `r_nearclipdist` | `0` | 0 | Near clip distance. **Set to 4 every time the renderer starts.** |
| `r_farclipdist` | `65535.0` | 0 | Far clip distance. |
| `gl_cull` | `1` | 0 | 0: no back-face culling of world geometry. |
| `gl_clear` | `0` | 1 | Non-zero: clear the colour buffer every frame. |
| `gl_ztrick` | `0` | 0 | Non-zero: Quake II depth-buffer trick (see *Clearing*). |
| `gl_drawmode` | `0` | 0 | Debug overdraw view: depth test off, black background, world and brush models blended at 40 % (SPEC-410). |
| `gl_drawflat` | `0` | 0 | Debug: surfaces drawn without textures (SPEC-410, SPEC-412). |
| `gl_showtris` | `0` | 0 | Debug: polygon outlines of world surfaces; values 1–4 select variants (SPEC-410). |
| `gl_lightmap` | `0` | 0 | Debug: draw lightmaps only. Affects only the two-pass world path used without multitexture (SPEC-410). |
| `gl_dynamic` | `1` | 0 | 0: world lightmaps are no longer updated after loading (no dynamic lights, no light-style changes) (SPEC-410). |
| `gl_saturatelighting` | `0` | 0 | Lightmap blending variant; affects only the two-pass world path used without multitexture (SPEC-410). |
| `gl_dlightintensity` | `2.0` | 1 | Model-light write-back factor (SPEC-002, SPEC-411); clamped to 4. |
| `gl_detailtexturing` | `1` | 1 | 0: no detail/environment pass; 1: on; 2: only for surfaces within `gl_detailtexturingdistance`; 3 and above: diagnostic views (SPEC-410). |
| `gl_detailtexturingdistance` | `250` | 1 | Distance limit of the detail pass (SPEC-410). |
| `gl_offsetfactor`, `gl_offsetunits` | `-2.0`, `-2.0` | 1 | Polygon offset of the detail pass and of decal quads; both 0 = no offset (SPEC-410, SPEC-412). |
| `gl_effectfill` | `100.0` | 1 | Effect fill budget (SPEC-002, SPEC-412). |
| `gl_showeffectfill` | `0.0` | 0 | Debug: prints `Fill used is <f>` every frame (SPEC-412). |
| `gl_fogmode` | `0` | 1 | Fog region rendering: 0 = overlay pass, 1 = OpenGL fog per surface, 2 = off, 3 = debug view (SPEC-410). |
| `gl_fogverts` | `0` | 1 | Fog volume vertex-array path (SPEC-410). |
| `gl_fogcolor` | `-1` | 0x40 | Override: `"R G B"` (0–255) replaces the fog colour of the map when its value is ≥ 0 (SPEC-410). |
| `gl_fogdensity`, `gl_fogstart`, `gl_fogend`, `gl_fogtime`, `gl_fogdensityrange` | `-1` | 0x40 | Overrides of the corresponding fog-volume parameter when ≥ 0 (SPEC-410). |
| `ghl_*` (23 cvars) | see SPEC-010 Constants | — | Ghoul model rendering. Created by the renderer at start-up. Defaults: SPEC-010; effect of each value: the cvar table of SPEC-411. `ghl_poly_fact`, `ghl_poly_unit` and `ghl_mip` are read only when the renderer starts; `ghl_shadow_tilt` has no effect. |

### Driver data files

| File | Format | Use |
|---|---|---|
| `drivers/drivers.txt` | tokens separated by `;`, CR, LF, tab or space | Ordered list of OpenGL library names to try. Stock: `opengl`, `opengl32`, `3dfxvgl`, `3dfxogl`. |
| `drivers/cards.txt` | tokens (same separators) taken in pairs: card name, pattern | Card detection. A pattern is a comma-separated list of substrings (converted to lower case). Stock (pak2) examples: `radeon radeon`, `ragemaxx dual,rage,128`, `geforce geforce`, `geforce quadro`, `tnt2 riva,tnt2`, `voodoo3 voodoo3`, `gdi gdi`. |
| `drivers/alldefs.cfg`, `drivers/<card>.cfg`, `drivers/memory<N>.cfg`, `drivers/cpu<N>.cfg` | console scripts | Executed after a card or memory change (see *Card detection*). |

The stock configuration files set scalability cvars, for example `gl_pictip 1` in `memory1.cfg`/`memory2.cfg`,
`r_isf GL_COMPRESSED_RGB_S3TC_DXT1_EXT` and `r_iaf GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` in the pak2 `geforce.cfg`.

## Behaviour

### 1. Start-up (export Init, SPEC-002)

In this order:
1. The water/terrain warp sine table is halved (all 256 entries × 0.5). This happens on **every** renderer start, so
   each `vid_restart` halves the warp amplitude again (Quake II behaviour). The stored table peaks at 8 world units,
   so the first start gives 4.
2. Prints `ref_gl version: GL 0.01` and registers all cvars and the four commands. `r_nearclipdist` is set to 4.
3. Records the operating system: depth changes (`gl_bitdepth`) are allowed on Windows NT 4 or later and on Windows 95
   builds above 1110 (OSR2).
4. Driver selection and mode setting (2 and 3 below). If no driver succeeds, Init returns −1.
5. Validates `r_isf` and `r_iaf` (see *Internal formats*).
6. Extensions: `GL_ARB_multitexture` (else `GL_SGIS_multitexture`), `GL_EXT_compiled_vertex_array`,
   `WGL_EXT_swap_control` entry points are looked up.
7. Prints and stores the GL strings: `GL_VENDOR: <s>`, `GL_RENDERER: <s>`, `GL_VERSION: <s>` (each also copied into
   the cvar of the same lower-case name), `GL_EXTENSIONS: <s>`, `GL_MAX_TEXTURE_SIZE: <n>`.
8. Card detection (4 below).
9. Prints, in order: `...allowing CDS` (always, see `gl_allow_cds`); `...enabling WGL_EXT_swap_control` or
   `...WGL_EXT_swap_control not found`; `...using GL_ARB_multitexture (<n> TMUs)`, `...ignoring GL_ARB_multitexture`
   (cvar 0; multitexture is then disabled) or `...GL_ARB_multitexture not found`;
   `...GL_EXT_texture_filter_anisotropic available` followed by `...using …`/`...ignoring …`, or `... not found`;
   `s3tc_avail` set as described, with `...using GL_S3_s3tc` or `...ignoring GL_S3_s3tc` when the extension exists.
10. Default GL state (5 below), image system start (registration counter 1, `gl_modulate` latched, colour table built
    from `vid_gamma`, `vid_brightness`, `vid_contrast`, 1.0), world-model system start, default images
    (`pics/console/notexture`, `pics/console/conchars`), font registry (SPEC-412), Ghoul renderer start-up (SPEC-010,
    SPEC-411), movie recording start-up (SPEC-412), one full colour + depth clear.
11. Returns 1.

### 2. Driver selection

- If `gl_driver` is empty, each name from `drivers/drivers.txt` is tried in order: `gl_driver` is set to it,
  `Attempting gl_driver : <name>` is printed at developer level, and the mode is set. The first success wins.
- Otherwise the current `gl_driver` is tried first (same message); if that fails, the list is tried as above.
- A driver is the named library loaded through the Windows search path (`opengl32` → `opengl32.dll`). If it cannot be
  loaded: `ref_gl::R_Init() - could not load "<name>"`.
- A driver whose name does not contain `opengl32` is treated as a stand-alone driver: pixel-format selection uses its
  own `wgl` pixel-format entry points instead of the system ones.
- If neither `drivers/drivers.txt` nor the configured driver works, start-up fails.

### 3. Mode setting

Results of one attempt: ok, "invalid fullscreen" (fullscreen failed but a window was made) or "invalid mode".

1. The safe mode is reset to 3 at every mode-setting call.
2. Attempt (`gl_mode`, fullscreen = integer of `vid_fullscreen`). The modified flags of `gl_mode` and `vid_fullscreen`
   are cleared. Prints `Initializing OpenGL display` and `...setting mode <n>:`; an unknown mode prints
   ` invalid mode` (result "invalid mode"); else ` <w> <h> FS` or ` <w> <h> W`.
3. Fullscreen attempt: prints `...attempting fullscreen`, then `...using gl_bitdepth of <n>` or
   `...using desktop display depth of <n>`, `...calling CDS: `. The display is switched to w × h (with depth if
   `gl_bitdepth` ≠ 0, refresh if `gl_displayrefresh` ≠ 0) as a temporary fullscreen change. Success: ` ok`, window
   created fullscreen. Failure: `failed`, `...calling CDS assuming dual monitors:` and a second try with width 2w; on
   success the window is created fullscreen, else ` failed`, `...setting windowed mode`, the display is restored and a
   window is created (result "invalid fullscreen" when that worked).
4. Windowed attempt: `gl_bitdepth` := 0, prints `...setting windowed mode`, display restored, window created.
5. On failure of step 2 the driver library is reloaded and, in order:
   - if `gl_bitdepth` is 0, one retry with depth 16 (on failure prints
     `ref_gl::R_SetMode() - 16bit unavailable in this mode` and depth goes back to 0);
   - the driver is reloaded again (failure: `R_SetMode() - failed to reload driver`);
   - "invalid fullscreen": `vid_fullscreen` := 0, prints
     `ref_gl::R_SetMode() - fullscreen unavailable in this mode`, retries windowed;
   - "invalid mode": `gl_mode` := 3, prints `ref_gl::R_SetMode() - invalid mode`, retries mode 3 fullscreen if that
     changed `gl_mode`; failure prints `ref_gl::R_SetMode() - could not revert to safe mode`.
   - Any final failure prints `ref_gl::R_Init() - could not R_SetMode()` and unloads the driver.

Window: class and title `SoF`, icon resource 101 of the engine. Windowed: caption, system menu and minimise box, at
(`vid_xpos`, `vid_ypos`), client area w × h. Fullscreen: borderless pop-up, top-most, at (0, 0).
Pixel format request: RGBA, 24-bit colour, 32-bit depth, double-buffered. Printed after creation:
`GL_PFD: color(<n>-bits) Z(<n>-bit)`. If the GL renderer string (lower case) contains `generic`, the context is
refused with `GLimp_Init() - no hardware acceleration detected`, unless `gl_allow_software` ≠ 0 and the pixel format
is not marked generic-accelerated. Other failures print the messages listed under Constants and destroy the window.
The new size is reported to the engine (SPEC-002 import 0x54).

### 4. Card detection and driver configuration

1. If `drivers/cards.txt` cannot be read: `Error: Could not find card definition file`; nothing below happens.
2. The modified flags of `vid_card` and `cpu_memory_using` are cleared.
3. The GL renderer string is converted to lower case. The pairs are tested in file order; a pair matches when every
   comma-separated substring of its pattern occurs in that string. On the first match `vid_card` := card name.
4. `cpu_memory_using` := `cpu_memory_level`.
5. If either cvar changed value in steps 3–4 (that is, on first run, after a card change or after a memory change —
   both are archived), these are executed immediately, in order: `exec drivers/alldefs.cfg`,
   `exec drivers/<vid_card>.cfg`, `exec drivers/memory<integer of cpu_memory_level>.cfg`,
   `exec drivers/cpu<integer of cpu_speed_level>.cfg`.

Consequence: the configuration files overwrite user settings only when hardware changes; normal restarts keep them.
If no pattern matches, `vid_card` keeps its old value; a memory change then still executes
`drivers/<old vid_card>.cfg` (on a first run that is `drivers/[not set yet].cfg`, which does not exist).

### 5. Default GL state (set at start-up)

Clear colour (1, 0, 0.5, 0.5); cull face "front" (Quake II convention); 2D texturing on; face culling off; blending
off; current colour white; polygons filled; flat shading; alpha test on with "greater than 0.05"; depth test on;
texture filter from `gl_texturemode`; texture wrap repeat; blend function source alpha / one minus source alpha;
texture environment replace; swap interval from `gl_swapinterval` when it is marked modified.

The renderer keeps a cache of enable flags (blend, cull, alpha test, depth test, fog, texture coordinate generation S
and T, polygon offset fill), blend function, alpha function, depth function and fog parameters and only sends
changes. The number of avoided calls is reported by `r_speeds 3`. It has no other visible effect.

### 6. Per-frame

**Begin frame** (SPEC-002 slot 0xb0):
1. If `gl_mode` or `vid_fullscreen` is marked modified, `vid_ref` is marked modified, so the engine reloads the
   renderer before the next frame (full `vid_restart`).
2. `gl_bitdepth` change check (start-up step 3 message); draw buffer := back.
3. `gl_drawbuffer` change applied; `gl_texturemode` or `gl_ext_anisotropic` change applied to all images; swap
   interval change applied.
4. Clearing (below).
5. 2D state: viewport = whole window, orthographic projection with (0, 0) at the top-left, x right, y down, depth
   range ±9999; depth test, culling and blending off; alpha test "greater than 0.05" on; colour white.

**Clearing:**
- `gl_ztrick` = 0: if `gl_clear` ≠ 0 (or at start-up), colour (to black) and depth are cleared, else depth only;
  depth range 0–1, depth function "less or equal".
- `gl_ztrick` ≠ 0: if `gl_clear` ≠ 0 the colour buffer (only) is cleared to (1, 0, 0.5); the depth buffer is never
  cleared; odd frames use depth range 0–0.49999 with "less or equal", even frames range 1–0.5 with "greater or
  equal".

**Render frame** (SPEC-002 slot 0x40), when `r_norefresh` = 0:
1. Copy the frame record, append model lights (SPEC-002), world check.
2. Advance scrolling detail textures (see *Images*, step "detail").
3. Mark dynamic lights on the world; `gl_finish`.
4. View set-up: view leaf and cluster; screen tint (blend from the frame record, replaced by the warning tint below);
   Ghoul statistics reset. With rdflags bit 2 (no world) the view rectangle is cleared to grey (0.3, 0.3, 0.3) and
   depth.
5. Frustum and projection: viewport = view rectangle; aspect = width / height of the view rectangle; vertical field
   of view from the frame record; far = `r_farclipdist`; near = `r_nearclipdist` × f where f = 1 for fov_y ≥ 75,
   f = 1 + (75 − fov_y) / 70 × 2.9 for 5 ≤ fov_y < 75 and f = 3.9 for fov_y < 5 (zoomed views push the near plane
   out). View matrix: Quake II axes (pitch, yaw, roll from the frame record).
6. World (SPEC-410), fog volumes, sorted translucent pass with entities and effects (SPEC-411, SPEC-412), second fog
   pass, full-screen effects (SPEC-412), Ghoul end-of-frame.
7. Statistics print (below).

Then, whenever rdflags bit 2 is clear (also when `r_norefresh` ≠ 0): the light colour at the view origin is
sampled straight down (SPEC-002 slot 0xa4) and the numeric value of `r_lightlevel` := 150 × max(r, g, b) (the text
of the cvar is not updated). Finally the 2D state is set as in Begin frame step 5.

**End frame**: movie frame capture (SPEC-412); buffers are swapped if `gl_drawbuffer` is `GL_BACK` (failure is fatal:
`GLimp_EndFrame() - SwapBuffers() failed!`).

**Menu 3D views** (SPEC-002 slots 0xb4/0xb8): perspective with vertical field of view 33.75084377109428°, aspect
1.3333, near/far from the clip cvars.

### 7. Statistics

| `r_speeds` | Output (once per 3D frame unless noted) |
|---|---|
| 1 | `<wp>:wp <fp>:fp <gp>:gp <tx>:tx <lm>:lm <gt>:gt <tp>:tp <qp>:qp` with widths 4,4,4,3,3,3,3,3: world/brush polygons drawn, fog-volume polygons, Ghoul triangles (SPEC-010 method 43), world textures drawn, lightmap pages blended, Ghoul textures bound (method 40), terrain polygons, alpha quads drawn. Field meanings of tx, lm, tp, qp UNVERIFIED (inferred from where they are counted). |
| 2 | No print; enables the texture-memory part of the warning tint. |
| 3 | At the end of every frame: `GLState: <n> calls saved`, then the counter is reset. |
| 4 | `<n> tex <n> tmem <n> mesh <n> tri <n> vert <n> corn <n> light` (widths 3,7,3,4,4,4,4) from Ghoul statistics methods 40, 41, 42, 43, 45, 46, 47. |

Warning tint (`r_warnings` ≠ 0, only when the view is inside the world with a valid cluster and the map has
visibility data). Components start at 0; each condition adds to the alpha:
- world polygons of the previous frame > 700: red = 1, alpha += (n − 700) / 150 + 0.2;
- Ghoul triangles > 3500: green = 1, alpha += (n − 3500) × 0.001 + 0.2;
- (`r_speeds` = 2 or `r_warnings` = 2) and texture memory > limit (4 MiB with `gl_pictip`, else 16 MiB): blue = 1,
  alpha += (m − limit) / limit × 0.5 + 0.2. In 1.07f the texture-memory counter is never increased, so this part never
  triggers.
Alpha is clamped to 1. When any condition holds, this tint replaces the frame record's blend colour.

### 8. Images

Image types and their letters are those of SPEC-002. All images share one table of 800 slots.

**Lookup by name** (registration calls of SPEC-002 and internal loads):
1. Null name: `GL_FindImage: Invalid null name`; empty name: `GL_FindImage: Name (<name>) too short`; 63 or more
   characters: `GL_FindImage: Name (<name>) too long`. All return null.
2. The name cache is searched with an exact, case-sensitive comparison. A hit marks the image (and its detail and
   damage images) as used in the current registration cycle and returns it. Two spellings that differ only in case
   are two separate images (each loads the file).
3. Then the substitution cache (alternate textures, below) is searched; a hit returns the substitute.
4. Otherwise, if the call may create, the image is loaded.

**Loading** a named image (types other than raw):
1. `<name>.m32` is read. Missing: `GL_FindImage: Can't load (<name>)`, null. Version ≠ 4:
   `GL_FindImage: Invalid version for (<name>)`, null.
2. Type override from `.m32` `kind`: bit 0x2 → detail (5); bit 0x8 → environment map (8); 0x8 wins.
3. *Alternate textures.* If `alt_name` is not empty, this load is not itself a substitution, and either `gl_pictip` ≠ 0
   or (`kind` bit 0x4 and `lock_textures` ≠ 0): the image `textures/<alt_name>` is looked up instead (with the same
   type, without further substitution). If it cannot be loaded, the default `pics/console/notexture` image is used.
   The result is stored in the substitution cache under the requested name and returned. The substitution cache is
   emptied at the start of every registration cycle (SPEC-002 BeginRegistration).
   This resolves SPEC-100: `kind` bit 0x4 marks textures replaced under the parental lock, and `alt_name` is the
   replacement (also used as the low-memory replacement when `gl_pictip` is set).
4. Colour correction: red, green and blue of every pixel of every mip level are replaced through the colour table
   (section 9), except for detail (5) and environment-map (8) images. Alpha is unchanged.
5. A free slot is taken (the first slot not in use, else a new one); exceeding 800 is the drop error of SPEC-002.
   Width and height are those of mip level 0. The image keeps: has-alpha = `.m32` flags bit 0x800; animation byte =
   the low byte of the `.m32` `value` field (used for texture animation, SPEC-410).
6. Upload (below), filtering (below).
7. With `fs_createpak` ≠ 0 and a non-empty `alt_name`, `textures/<alt_name>` is loaded as well.
8. Damage: if `damaged_name` is set, `textures/<damaged_name>` is loaded as a wall image and becomes the damage
   variant (SPEC-002 slot 0xa8). A damage name equal to the image's own name (ignoring case) prints
   `GL_FindImage: Recursvive damage texture (<name>)` (sic) and no damage variant is set.
9. Detail: if `detail_name` is set, `textures/<detail_name>` is loaded as a detail image (same recursion check,
   message `GL_FindImage: Recursvive detail texture (<name>)`); the image keeps `detail_scale_x/y` and
   `detail_shift_x/y`. If a shift is non-zero the image is added to the scrolling list: every 3D frame each scroll
   offset += game-clock delta (SPEC-002 frame record 0x4c) × shift, and when an offset exceeds 1 it is replaced by its
   fractional part (negative offsets are not wrapped). Units: texture repeats per second. How the offset is applied
   is in SPEC-410. This resolves SPEC-100 open question 5 (shift = scroll speed).

Raw images (SPEC-002 slot 0xa0) take a slot without a file; they are uploaded with GL internal format "RGB" (alpha
dropped), one level.

**Mip-level selection and upload:**
1. Start level s = integer part of `gl_picmip`, plus the `.m32` int at offset 0x2D8 when `gl_pictip` ≠ 0 (stock
   values 0/1; 0 on every `pics/` and `ghoul/` file). If s < 0 or s > 14, s = 0. This resolves SPEC-100 field 0x2D8:
   "drop one more level in texture-memory saving mode".
2. Levels whose width or height exceed the driver's maximum texture size are skipped (s increases).
3. If level s does not exist in the file, s steps back to the last level that exists.
4. Levels s, s+1, … up to the last level are uploaded as GL levels 0, 1, …; if `kind` bit 0x1 is set only level s is
   uploaded (no mip-maps).
5. Internal format: detail images → `GL_LUMINANCE8` (the detail texture becomes grey-scale; the GL takes the red
   channel); images with has-alpha → the `r_iaf` format; others → the `r_isf` format. Source data is always 8-bit RGBA.
6. If the GL reports an error for a level, the renderer prints
   `Unsupported format : restoring GL_SOLID_FORMAT` (or `… GL_ALPHA_FORMAT`), resets the corresponding cvar to its
   default and uploads that level again with the default format.

The displayed size of a picture (SPEC-002 drawing calls) is always the level-0 size, whatever level was uploaded.

**Internal formats (`r_isf`, `r_iaf`).** The value must be one of the 43 names under Constants (case-insensitive).
The check runs when the cvar is created, on every change and at start-up. An unknown name prints
`Avaiable formats: ` (sic), an empty line and every valid name on its own line, then the cvar is set back to its
default. New formats apply to images uploaded afterwards (`vid_restart` reloads everything).

**Filtering and wrap** (set at upload and whenever `gl_texturemode`/`gl_ext_anisotropic` change):

| Image type | Minification | Magnification | Anisotropy |
|---|---|---|---|
| pic (3), font (7) | nearest | nearest | — |
| sky (4), raw (6), environment map (8) | the magnification filter of `gl_texturemode` | same | — |
| skin (0), sprite (1), wall (2), detail (5) | `gl_texturemode` | `gl_texturemode` | 2.0 if available and `gl_ext_anisotropic` ≠ 0, else 1.0 |

Texture wrap is "repeat" (default state). Sky clamping is in SPEC-410.

**Freeing.** At the end of a registration cycle (SPEC-002): raw images are always freed; other images not used in the
cycle are freed except pics and fonts; the default notexture image is always kept. Freeing deletes the GL texture,
forgets the name and takes the image off the scrolling list; the slot becomes free.

### 9. Colour correction table

A 256-entry table T maps 8-bit colour values. It is built from four numbers (g, b, c, d):

- T[0] = 0.
- For i = 1 … 255:
  - x = (i + 0.5) / 255.5
  - v = 255 · x^(1 − g) + 0.5
  - k = (1.5 − c)^3 if c < 0.5, else (1.5 − c)^0.5
  - w = 128 · (1 + s · (|v − 128| / 128)^k), where s = +1 if v ≥ 128 else −1
  - T[i] = (w + 160 · b − 80) · d, clamped to 0 … 255, then truncated to an integer.

With the defaults (0, 0.5, 0.5, 1) the table is the identity (within truncation). g > 0 brightens mid-tones; b adds
160·b − 80 levels; c > 0.5 increases contrast, c < 0.5 reduces it; d scales the result.

- The renderer builds it at start-up from (`vid_gamma`, `vid_brightness`, `vid_contrast`, 1.0). Changing these cvars
  has no effect until the renderer restarts or the video menu calls SPEC-002 slot 0xc0.
- SPEC-002 slot 0xc0 takes (g, b, c, d) in this order (resolves the SPEC-002 open question). The video menu's preview
  picture uses (`menu_gamma`, `menu_brightness`, `menu_contrast`, `menu_modulate` × 0.5) and then restores the cvar
  values with d = 1.0 (SPEC-802).
- The table is applied once, when an image is loaded (section 8, step 4). Already loaded images keep their colours.
- The same table is given to the Ghoul system for model textures (SPEC-010 method 37).
- The renderer does not set a hardware gamma ramp; there is no `intensity` cvar (Quake II's is gone).

`gl_modulate` is not part of this table. Its value is copied when the renderer starts and multiplies light values
(SPEC-410); changes need a renderer restart.

### 10. Miscellaneous

- `gl_nobind` ≠ 0: every bind of an image texture binds the console character image instead.
- `gl_gentextures_broken` ≠ 0: special texture names come from a counter starting at 4096 that wraps back to 4096
  after 2^30.
- Messages printed on shutdown failures: `R_Shutdown() - Shutting down`, `ref_gl::R_Shutdown() - wglMakeCurrent
  failed`, `ref_gl::R_Shutdown() - wglDeleteContext failed`, `ref_gl::R_Shutdown() - ReleaseDC failed`. Shutdown also
  restores the desktop display mode if fullscreen was set.
- App activation: SPEC-002 slot 0xc4.

## Constants

**Video modes** (engine table, SPEC-002 import 0x50; only 3–9 are accepted):

| Mode | Size | Mode | Size |
|---|---|---|---|
| 3 | 640 × 480 | 7 | 1152 × 864 |
| 4 | 800 × 600 | 8 | 1280 × 960 |
| 5 | 960 × 720 | 9 | 1600 × 1200 |
| 6 | 1024 × 768 | | |

(The engine table also holds 0 = 320 × 240, 1 = 400 × 300, 2 = 512 × 384, which the renderer never accepts.)

**`gl_texturemode` names** (case-insensitive) with minification / magnification filter:

| Name | Min | Mag |
|---|---|---|
| `GL_NEAREST` | nearest | nearest |
| `GL_LINEAR` | linear | linear |
| `GL_NEAREST_MIPMAP_NEAREST` | nearest, nearest mip | nearest |
| `GL_LINEAR_MIPMAP_NEAREST` (default) | linear, nearest mip | linear |
| `GL_NEAREST_MIPMAP_LINEAR` | nearest, linear mip | nearest |
| `GL_LINEAR_MIPMAP_LINEAR` | linear, linear mip | linear |

**`r_isf` / `r_iaf` names** and the OpenGL internal-format value used:

| Name | Value | Name | Value |
|---|---|---|---|
| `GL_SOLID_FORMAT` | 3 (unsized RGB) | `GL_LUMINANCE12_ALPHA12` | 0x8047 |
| `GL_ALPHA_FORMAT` | 4 (unsized RGBA) | `GL_LUMINANCE16_ALPHA16` | 0x8048 |
| `GL_RGB_S3TC` | 0x83A0 | `GL_INTENSITY` | 0x8049 |
| `GL_RGB4_S3TC` | 0x83A1 | `GL_INTENSITY4` | 0x804A |
| `GL_RGBA_S3TC` | 0x83A2 | `GL_INTENSITY8` | 0x804B |
| `GL_RGBA4_S3TC` | 0x83A3 | `GL_INTENSITY12` | 0x804C |
| `GL_COMPRESSED_RGB_S3TC_DXT1_EXT` | 0x83F0 | `GL_INTENSITY16` | 0x804D |
| `GL_COMPRESSED_RGBA_S3TC_DXT1_EXT` | 0x83F1 | `GL_R3_G3_B2` | 0x2A10 |
| `GL_COMPRESSED_RGBA_S3TC_DXT3_EXT` | 0x83F2 | `GL_RGB4` | 0x804F |
| `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | 0x83F3 | `GL_RGB5` | 0x8050 |
| `GL_ALPHA4` | 0x803B | `GL_RGB8` | 0x8051 |
| `GL_ALPHA8` | 0x803C | `GL_RGB10` | 0x8052 |
| `GL_ALPHA12` | 0x803D | `GL_RGB12` | 0x8053 |
| `GL_ALPHA16` | 0x803E | `GL_RGB16` | 0x8054 |
| `GL_LUMINANCE4` | 0x803F | `GL_RGBA2` | 0x8055 |
| `GL_LUMINANCE8` | 0x8040 | `GL_RGBA4` | 0x8056 |
| `GL_LUMINANCE12` | 0x8041 | `GL_RGB5_A1` | 0x8057 |
| `GL_LUMINANCE16` | 0x8042 | `GL_RGBA8` | 0x8058 |
| `GL_LUMINANCE4_ALPHA4` | 0x8043 | `GL_RGB10_A2` | 0x8059 |
| `GL_LUMINANCE6_ALPHA2` | 0x8044 | `GL_RGBA12` | 0x805A |
| `GL_LUMINANCE8_ALPHA8` | 0x8045 | `GL_RGBA16` | 0x805B |
| `GL_LUMINANCE12_ALPHA4` | 0x8046 | | |

**Other values**

| Name | Value |
|---|---|
| Image slots | 800 |
| Default alpha test | greater than 0.05 |
| Near-plane factor | 1 + (75 − fov_y)/70 × 2.9, limits 1 (fov_y ≥ 75) and 3.9 (fov_y < 5) |
| `r_nearclipdist` forced at start-up | 4 |
| Menu 3D view | fov_y 33.75084377109428°, aspect 1.3333 |
| `r_lightlevel` scale | 150 × max component |
| Warning thresholds | 700 world polygons; 3500 Ghoul triangles; 4 MiB / 16 MiB texture memory |
| Warp sine table peak (file) | 8, halved at every renderer start |
| Highest start mip level | 14 |
| 2D projection depth range | −9999 … 9999 |
| Window class / title | `SoF` |
| Pixel format | 24-bit colour, 32-bit depth, double buffer |
| Safe video mode | 3 |
| Fallback depth | 16 bits |

Mode-setting and window failure messages (each printed on its own line): `GLimp_Init() - GetVersionEx failed`,
`GLimp_Init() - non-NULL DC exists`, `GLimp_Init() - GetDC failed`, `GLimp_Init() - ChoosePixelFormat failed`,
`GLimp_Init() - SetPixelFormat failed`, `GLimp_Init() - wglChoosePixelFormat failed`,
`GLimp_Init() - wglSetPixelFormat failed`, `GLimp_Init() - wglCreateContext failed`,
`GLimp_Init() - wglMakeCurrent failed`, `GLimp_Init() - no hardware acceleration detected`,
`VID_CreateWindow() - GLimp_InitGL failed`; fatal: `Couldn't register window class`, `Couldn't create window`.

## Test vectors / how to verify

1. **Colour table** (truncated values, allow ±1 for floating-point rounding). Entries i = 0, 1, 16, 64, 128, 192,
   250, 255:
   - (0, 0.5, 0.5, 1): 0, 1, 16, 64, 128, 192, 250, 255
   - (0, 0.6, 0.6, 1): 0, 17, 32, 78, 144, 210, 255, 255
   - (0.5, 0.5, 0.5, 1): 0, 20, 65, 128, 181, 221, 252, 255
   - (0, 0.5, 0.5, 0.5): 0, 0, 8, 32, 64, 96, 125, 127
   - (0, 1, 0.5, 1): 0, 81, 96, 144, 208, 255, 255, 255
   - (0, 0.5, 0, 1): 0, 6, 48, 116, 128, 140, 238, 254
   - (0, 0.5, 1, 1): 0, 1, 12, 50, 131, 206, 252, 255
   Check in game: set `vid_brightness 1`, `vid_restart`, `screenshot`; a texel of value 64 on an unlit (fullbright)
   surface reads 144.
2. **Format validation.** `r_isf foo` prints `Avaiable formats: ` and the 43 names; `r_isf` then reads
   `GL_SOLID_FORMAT`. `r_isf gl_rgb5` is accepted.
3. **imagelist** after loading a map: the first two lines are as specified, `pics/console/conchars` appears as
   `F 128 128 RGB: pics/console/conchars`, `pics/console/notexture` as `P   8   8 RGB: pics/console/notexture`.
4. **Mip selection.** `gl_picmip 1`, `vid_restart`, `imagelist`: sizes are unchanged (level-0 size is listed); the
   world looks blurrier. `pics/console/conback` (2 levels, short chain) uploads its 256-wide level; `gl_picmip 3`
   uploads the same level (step back).
5. **Alternate textures.** In a map with graffiti textures (`*_g1`…`*_g6`), `gl_pictip 1`, `vid_restart`: graffiti
   disappears (base texture shown). With the parental lock active (`lock_textures` 1), only textures whose `.m32`
   `kind` has bit 0x4 change.
6. **Driver list.** `gl_driver ""`, `vid_restart` with developer messages on: `Attempting gl_driver : opengl` first;
   then `opengl32` succeeds; `gl_driver` reads `opengl32`.
7. **Card detection.** Delete `vid_card` from `config.cfg`; on start the console shows the executed
   `drivers/*.cfg` scripts and `vid_card` holds the card name matching the GL renderer string (e.g. `geforce` for
   "GeForce …"). A second start without changes executes none of them.
8. **Near plane.** At fov 90 the near plane is 4; zoomed to fov 20 the near plane is 4 × (1 + 55/70 × 2.9) ≈ 13.11 —
   visible as clipping of geometry closer than that.
9. **r_lightlevel.** Standing in a fully lit (1, 1, 1) spot, `r_lightlevel` (numeric value as read by the engine) is
   150; the printed string stays `"0"`.
10. **Warp halving.** Observe water ripple amplitude; after `vid_restart` it is half as large (UNVERIFIED in game; the
    table is halved each start).
11. **Warning tint.** `r_warnings 1` in a map view with more than 700 world polygons: the screen is tinted red with
    alpha (n − 700)/150 + 0.2.
12. **r_speeds 3** prints `GLState: <n> calls saved` once per frame.
13. **gl_drawbuffer GL_FRONT**: the screen stops updating (no swaps) while the 3D view keeps being drawn into the back
    buffer (UNVERIFIED in game; verify by setting it and observing a frozen picture, then `gl_drawbuffer GL_BACK`).

## Open questions

- r_speeds 1 fields tx, lm, tp, qp: meanings inferred from where the counters are increased (UNVERIFIED). Verify by
  comparing counts in a simple map.
- The 3DNow! (`cpu_amd3d`/`use_amd3d`) variant of the box/plane test is assumed to give identical results.
- With `GL_ARB_multitexture` absent but `GL_SGIS_multitexture` present, the SGIS entry points are loaded but the
  renderer uses ARB texture-unit numbers; behaviour on such drivers is UNVERIFIED (no such driver tested).
- Cvars created but unused (`gl_rj`, `gl_ext_swapinterval`, `cpu_mmx`, `use_mmx`, `gl_log`): no effect found;
  `sc_framerate` belongs to the engine.
