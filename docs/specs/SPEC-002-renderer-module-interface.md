# SPEC-002: Renderer module interface (engine ↔ refresh DLL)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe and ref_gl.dll, version 1.07f. Also covers how the table is handed to the sound library through
A3Dsnd.dll's `S_A3D_ExportRenderGeom` export.
Depends on: none. It refers forward to specs not yet written: the Ghoul model-system interface, the `.m32` image format,
the `.fnt` font format, the `.trn` terrain format and the SoF BSP lumps.

## Purpose

Soldier of Fortune keeps the Quake II split between the engine executable and a separately loaded renderer (refresh)
library. The engine passes a table of services to the renderer. The renderer returns a table of entry points that the
engine calls to register resources, draw the 3D view and draw 2D graphics.

This spec gives:
- the binary layout of both tables;
- the version constant;
- the meaning and calling contract of every slot;
- the per-frame scene description passed to the renderer;
- how the engine chooses, loads and falls back between renderer libraries.

Quake II 3.19 (`client/ref.h`) is the baseline. Both tables are much larger in SoF.

## Interface / layout

### Binary conventions

- 32-bit x86. All entry points use the C calling convention (`cdecl`): the caller removes arguments.
- `int` and pointers are 4 bytes, `short` is 2 bytes and `float` is IEEE single precision.
- A `vec3` argument is a pointer to 3 consecutive floats.
- "bool" arguments are passed as a 32-bit stack slot. Only the low byte is significant.
- **RGBA by value**: a 32-bit argument whose four bytes, in memory order, are red, green, blue and alpha (0–255 each).
  Read as a little-endian integer, `0xFF00FFFF` is opaque yellow and `0x7F000000` is black at alpha 127.
- **RGBA pointer**: a pointer to four bytes in the same order.
- **Image handle**: an opaque non-null pointer returned by a registration call, or null if the image could not be
  found or loaded. The engine only stores handles: it puts them in scene entities and tests them for null. Drawing
  calls take picture names, not handles.
- **Model handle**: an opaque pointer, used the same way.
- **Font reference**: a pointer to a Microsoft Visual C++ 6.0 `std::string` object holding the font name (see
  *Fonts*). Layout:

  | Offset | Size | Meaning |
  |---|---|---|
  | 0 | 1 (+3 pad) | allocator byte, unused |
  | 4 | 4 | pointer to the NUL-terminated characters; the byte just before them is a share/reference count |
  | 8 | 4 | length in characters |
  | 12 | 4 | capacity |

### Library selection and loading (engine side)

| Item | Value |
|---|---|
| cvar | `vid_ref`, default `gl`, flag archive (1) |
| library file name | `ref_<vid_ref>.dll`, loaded through the normal Windows DLL search |
| entry point | exported symbol `GetRefAPI` |
| interface version | 3 |
| console command | `vid_restart` marks `vid_ref` as modified, so the library is reloaded on the next check |
| related cvars (created together with `vid_ref`) | `vid_xpos` "3", `vid_ypos` "22", `vid_fullscreen` "1", `win_noalttab` "1", `win_noaltenter` "1", `win_broken` "1" (all archive), `r_force_sgi_software` "0" (flags 0x28) |
| other console commands created there | `vid_front`, `vid_modes` |

### Entry point

`GetRefAPI` takes the import table **by value**: 108 bytes copied onto the stack. It returns the export table **by
value**, using the usual MSVC hidden result pointer: the caller passes a pointer to a 220-byte buffer as an extra
first argument, and the function returns that pointer. This is the Quake II convention. The renderer keeps its own
copy of the import table. The engine copies the returned table into its own storage.

### Import table (engine → renderer), 108 bytes = 27 slots

Quake II names are used where the slot clearly corresponds to the Quake II `refimport_t` member.

| Off | Quake II name | Prototype (SoF) | Meaning |
|---|---|---|---|
| 0x00 | Sys_Error | void (int err_level, char *fmt, ...) | Raise an engine error. 0 = fatal, 1 = drop to console. Does not return. |
| 0x04 | — | (not set) | Left uninitialised by the engine; the renderer never uses it. |
| 0x08 | — | (not set) | Same as 0x04. |
| 0x0c | Cmd_AddCommand | void (char *name, void (*fn)(void)) | Register a console command. |
| 0x10 | Cmd_RemoveCommand | void (char *name) | Remove a console command. |
| 0x14 | Cmd_Argc | int (void) | Argument count of the command being executed. |
| 0x18 | Cmd_Argv | char *(int i) | Argument string number i. |
| 0x1c | Cmd_ExecuteText | void (int exec_when, char *text) | Queue or execute console text. 0 = now, 1 = insert at front, 2 = append. |
| 0x20 | Con_Printf | void (int level, char *fmt, ...) | Print. Levels: 0 always; 1 developer only; 2 only when the developer value has bit 4 set (UNVERIFIED which cvar); 3 alert — a Windows message box titled `PRINT_ALERT`, also sent to the debugger output. |
| 0x24 | (Z_TagMalloc) | void *(int size, short tag) | Allocate zero-filled memory from the engine zone under a tag. The renderer uses tags 0x2fc and 0x2ff. |
| 0x28 | (Z_Free, exported by SoF.exe) | void (void *p) | Free zone memory. |
| 0x2c | FS_LoadFile | int (char *name, void **buf, int flags) | Load a file from the game file system. Returns its length, or −1 and `*buf` = null. The buffer gets two extra bytes, newline then NUL, after the data. The third argument goes to the engine's file-open routine; the renderer always passes 0 (meaning UNVERIFIED). |
| 0x30 | FS_FreeFile | void (void *buf) | Free a buffer from FS_LoadFile. |
| 0x34 | FS_Gamedir | char *(void) | Current game directory. The renderer does not use it. |
| 0x38 | — | char *(void) | User/output directory. The renderer builds screenshot paths from it (`<dir>/scrnshot/sof00.tga`, `<dir>/rawshot/sof00.raw`); the engine builds save-game paths from the same value. |
| 0x3c | — | bool (char *path) | Find a free numbered file name. The two characters 6 and 5 places before the end of `path` are set to "00" … "99" until no file of that name exists. Returns true if one was found; `path` holds the result. |
| 0x40 | Cvar_Get | cvar * (char *name, char *value, int flags, void (*changed)(cvar *)) | Find or create a cvar. SoF adds a fourth argument: a callback invoked with the cvar when it is created (the renderer always passes null). |
| 0x44 | Cvar_Set | cvar * (char *name, char *value) | |
| 0x48 | Cvar_SetValue | void (char *name, float value) | |
| 0x4c | — | void (bool allow) | While true, cvars with flag 0x80 (hidden/internal) can be looked up and created; while false they are invisible. The renderer sets it true just around its lookup of `lock_textures`. |
| 0x50 | Vid_GetModeInfo | bool (int *width, int *height, int mode) | Mode table lookup. Only modes 3–9 return true. |
| 0x54 | Vid_NewWindow | void (int width, int height) | Report the new window size to the engine. |
| 0x58 | — | lump * (char *mapname, int lump, void *converter) | Fetch a BSP lump through the engine's lump cache, which collision code shares. The renderer reads two words of the returned descriptor: offset 4 = data pointer, offset 8 = element count. Converter contract UNVERIFIED (see open questions). The renderer uses lumps 1–9, 11–13, 19, 20 and 21. |
| 0x5c | — | float (vec3 start, vec3 end) | Ray test against world collision: a point trace with solid contents. Returns the fraction travelled (0…1), or −1.0 if the start point is inside a solid. The renderer passes this function on to the Ghoul model system to cast model shadows. |
| 0x60 | (flrand, exported by SoF.exe) | float (float min, float max) | Random float in [min, max]. |
| 0x64 | (irand, exported by SoF.exe) | int (int min, int max) | Random integer in [min, max]. |
| 0x68 | — | data pointer | The engine's table of unit direction vectors, 12 bytes each, used for byte-encoded directions. The first entry is (0.219625, 0, 0.975584). This is **not** Quake II's normal table. |

#### cvar object (returned by slots 0x40/0x44; fields read directly by the renderer), 32 bytes

| Off | Type | Meaning |
|---|---|---|
| 0x00 | char * | name |
| 0x04 | char * | current string value |
| 0x08 | char * | latched string (pending value) or null |
| 0x0c | int | flags (1 = archive; 0x80 = hidden/internal, see slot 0x4c) |
| 0x10 | function pointer | change callback (SoF addition), or null |
| 0x14 | int | modified flag. Set to 1 when the cvar is created or changed; the reader clears it. |
| 0x18 | float | numeric value |
| 0x1c | cvar * | next cvar in the engine list |

Compared with Quake II, SoF inserts the callback pointer at 0x10. That moves `modified` and `value` 4 bytes later.

### Export table (renderer → engine), 220 bytes = 55 slots

Prototypes are written in C notation. "Engine use" says whether SoF.exe calls the slot (✔), never calls it (—), or
only hands it to another module.

| Off | Quake II name | Prototype | Engine use | Meaning |
|---|---|---|---|---|
| 0x00 | api_version | int | ✔ | Must be 3. |
| 0x04 | Init | int (void *hinstance, void *wndproc, void *ghoul) | ✔ | Creates the window and GL context, registers renderer cvars and commands, and loads the default images. Returns 1 on success and −1 on failure; the engine tests only for −1. The **third argument is SoF-specific**: a pointer to the engine's Ghoul model-system object, a C++ object whose first word points to its method table. The renderer keeps it and calls it to draw Ghoul models, collect model lights and so on (separate spec). |
| 0x08 | Shutdown | void (void) | ✔ | Releases everything and removes the renderer's console commands. |
| 0x0c | — | void (void) | ✔ | Makes every existing texture resident by binding each one once. The engine calls it together with the sound system's "touch" operation. |
| 0x10 | BeginRegistration | void (char *mapname) | ✔ | Starts a registration cycle and loads world `maps/<mapname>.bsp`. If the same map is already loaded and cvar `flushmap` is 0, the world is kept. |
| 0x14 | RegisterModel | model * (char *name) | ✔ | `*N` returns inline brush model N. N must be 1–256 and less than the world's submodel count; otherwise drop error "Bad inline model number". Any other name: if a world is loaded, its model is returned **whatever the name**. If no world is loaded, a name ending in `bsp` is loaded as the world; anything else returns null. Empty name: drop error "Mod_ForName: NULL name". Ghoul models are **not** registered here. |
| 0x18 | RegisterSkin | image * (char *name) | — | Finds or loads an image of type *skin*. |
| 0x1c | — | void (char *name) | — | Frees the named image if it is loaded as a skin. |
| 0x20 | RegisterPic | image * (char *name) | ✔ | Finds or loads an image of type *pic*. |
| 0x24 | — | void (char *name) | ✔ | Frees the named pic. The menus use it before reloading a picture after video settings change. |
| 0x28 | — | image * (char *name) | ✔ | Finds or loads an image of type *sprite*, e.g. effect textures such as `textures/sprites/smoke2`. |
| 0x2c | — | void (char *name) | — | Frees the named sprite image. |
| 0x30 | SetSky | void (char *name, float rotate, vec3 axis) | ✔ | Loads sky definition `pics/sky/<name>.txt` (see *Behaviour*). |
| 0x34 | — | void (float type, float density, float angle, float speed, float red, float green, float blue) | ✔ (console `cloud`) | Cloud-layer parameters. **Does nothing in 1.07f.** |
| 0x38 | — | void (char *name) | ✔ | Selects the terrain set `env/<name>.trn`. `(null)` or an empty name means none. |
| 0x3c | EndRegistration | void (void) | ✔ | Ends the cycle and frees unused images (see *Behaviour*). |
| 0x40 | RenderFrame | void (refdef *fd) | ✔ | Renders the 3D view described by `fd` (see *Scene description*). |
| 0x44 | DrawGetPicSize | void (int *w, int *h, char *name) | ✔ | Pixel size of a pic. Both are set to −1 if it is missing. |
| 0x48 | DrawPic | void (int x, int y, char *name, RGBA by value) | ✔ | Draws the pic at its own size, tinted by the colour. |
| 0x4c | — | void (int x, int y, float sx, float sy, RGBA pointer, char *name) | ✔ | Draws the pic scaled: size = pic size × (sx, sy). |
| 0x50 | — | void (int x, int y, int left, int top, int right, int bottom, RGBA pointer, char *name) | ✔ | Draws the sub-rectangle left..right × top..bottom of the pic (in pic pixels) at (x, y), size \|right−left\| × \|bottom−top\|. |
| 0x54 | DrawStretchPic | void (int x, int y, int w, int h, RGBA pointer, char *name, int rotation) | ✔ | Draws the pic stretched to w × h. `rotation` 0–3 turns the texture by quarter turns (see Constants). A negative w or h mirrors the pic. Compared with Quake II this adds the colour and rotation. |
| 0x58 | DrawChar | void (int x, int y, int c, RGBA pointer) | ✔ | Draws an 8×8 glyph from `pics/console/conchars` (16 × 16 grid). Nothing is drawn for c = 32 or y ≤ −8. |
| 0x5c | — | void (int x, int y, char *s, RGBA by value, int maxlen) | — | Draws up to maxlen characters at 8-pixel steps in one colour. |
| 0x60 | — | void (int x, int y, char *s, int maxlen) | ✔ | Console-style string with colour codes (see *Text colour codes*). Starts in colour 1. maxlen counts drawn characters. |
| 0x64 | — | void (int x, int y, char *s, int maxlen, RGBA pointer) | ✔ | Same as 0x60, but starts in the given colour. |
| 0x68 | — | (not set) | — | Left uninitialised by the renderer. Must never be called. |
| 0x6c | — | void (float x, float y, float scale, RGBA pointer, int c) | ✔ | One 8×8 glyph at a fractional position. Used by the engine's "typewriter" text, which prints one character at a time with sound `misc/type.wav` and a trailing `_` cursor. The engine always passes scale 1.0; whether it has any effect is UNVERIFIED. |
| 0x70 | DrawTileClear | void (int x, int y, int w, int h, char *name) | ✔ | Fills the rectangle with the pic repeated every 64 pixels, aligned to the screen. |
| 0x74 | DrawFill | void (int x, int y, int w, int h, RGBA pointer) | ✔ | Alpha-blended solid rectangle. Quake II took a palette index; SoF takes RGBA. |
| 0x78 | — | void (int x, int y, int w, int h, RGBA pointer) | ✔ | Blended solid rectangle with a one-line border drawn in lighter and darker shades of the colour. Used for progress bars and menu boxes. Shading formula UNVERIFIED. |
| 0x7c | — | void (float x1, float y1, float x2, float y2, float width, RGBA pointer) | ✔ | Thick 2D line from (x1, y1) to (x2, y2). |
| 0x80 | — | (not set) | — | Left uninitialised by the renderer. Must never be called. |
| 0x84 | DrawFadeScreen | void (RGBA pointer) | ✔ | Full-screen blended rectangle. The pause screen uses (0, 0, 0, 127). Quake II took no argument. |
| 0x88 | — | void (int unused1, int unused2, int n, vec3 *verts, RGBA pointer) | — | Filled polygon through n vertices (12-byte stride). Nothing is drawn when n < 3. The first two arguments are ignored (UNVERIFIED). |
| 0x8c | — | void (int x, int y, int w, int h) | ✔ | Sets and remembers a 2D clip rectangle (top-left origin) and enables clipping. Ignored when w or h is negative. A renderer option can disable it (UNVERIFIED which cvar). |
| 0x90 | — | void (void) | ✔ | Clip rectangle := whole screen; clipping disabled. |
| 0x94 | — | void (int x) | ✔ | Narrows clipping to the remembered rectangle from its left edge up to column x. Nothing happens if x is left of that edge. The remembered rectangle is not changed. |
| 0x98 | — | void (int x) | ✔ | Narrows clipping to the remembered rectangle from column x to its right edge (same condition as 0x94). |
| 0x9c | — | void (void) | ✔ | Re-applies the remembered rectangle. |
| 0xa0 | — | void (char *name, int w, int h, byte *rgba) | ✔ | Creates or replaces an image from raw 32-bit RGBA pixels under `name`. It can then be drawn by name. Used for save-game thumbnails (128 × 128). The renderer keeps width and height as 16-bit values. |
| 0xa4 | — | void (vec3 point, vec3 color_out, int dir) | ✔ | Light colour at a world point. With dir 255 the sample is taken straight down. Other values pick entry `dir` of the direction table (import 0x68) and sample along it. Writes (1, 1, 1) if the world has no light data. The value is the Quake II `R_LightPoint` output, exported. |
| 0xa8 | — | void (short texinfo, int unused) | ✔ | Switches every world surface using texture-info entry `texinfo` to that texture's "damaged" variant, if it has one. Indices ≥ the world's texture-info count are ignored; negative indices are not checked (undefined). Driven by a server message (short + byte); the byte is ignored. |
| 0xac | — | void (vec3 origin, char *text, char *pic, RGBA by value) | ✔ | Projects the world point into the current 3D view. Draws `text` (font `small`) and/or `pic` centred near it; either may be null. Nothing is drawn when the point is behind the viewer. Used for player names and team icons above heads. Exact offsets UNVERIFIED. |
| 0xb0 | BeginFrame | void (void) | ✔ | Starts a frame and applies pending video changes. Quake II took a float camera separation; **SoF takes nothing**. |
| 0xb4 | — | void (bool restore) | ✔ | Switches to 2D drawing state after 3D rendering. The engine calls it with 0 right after RenderFrame. With a non-zero argument it also restores the projection saved by slot 0xb8 and re-enables clipping if it was on. |
| 0xb8 | — | void (bool flag) | ✔ | Saves 2D state and sets a 3D perspective (4:3 aspect) so the menus can draw a Ghoul model inside the 2D screen. Paired with 0xb4(1). How `flag` is used is UNVERIFIED. |
| 0xbc | EndFrame | void (void) | ✔ | Ends the frame and presents it (swaps buffers). |
| 0xc0 | — | void (float, float, float, float) | ✔ | Rebuilds the colour-correction table that is applied to true-colour images **when they are loaded**. The video menu calls it with three settings and 1.0, then frees and re-registers its preview picture. Parameter meanings UNVERIFIED. |
| 0xc4 | AppActivate | void (int active) | ✔ | Non-zero brings the window to the front. Zero minimises it in fullscreen mode. |
| 0xc8 | — | void (int x, int y, char *text, RGBA by value, font ref *font, int keep) | ✔ | Draws text in a proportional font (see *Fonts*). If `keep` is non-zero, the colour argument is ignored and the colour current at the end of the previous call is used (UNVERIFIED). |
| 0xcc | — | int (char *text, font ref *font) | ✔ | Pixel width of `text` in that font, using the same rules as 0xc8. |
| 0xd0 | — | int (font ref *font) | ✔ | Line height of the font in pixels. |
| 0xd4 | — | void (byte *out, int w, int h) | ✔ | Reads the current screen and point-samples it into a w × h RGBA image (alpha 255) at `out`. The engine passes 128 × 128 and a 64 KiB buffer for save-game thumbnails. |
| 0xd8 | — | void (void *a3d, void *arg2, void *arg3, int *control) | handed to sound | Sends world geometry to the Aureal A3D sound renderer (see *Behaviour*). The engine never calls it; only the sound library does. |

### Scene description passed to RenderFrame

The engine builds the structures below every frame and passes a pointer to the first one. The renderer copies the
top-level record (132 bytes) but reads the lists through the pointers in it. The alpha-quad and quad-chain lists
point at **live engine objects that the renderer writes to** (marked "renderer writes").

#### Frame record (`refdef`), 132 bytes (Quake II: 108)

| Off | Type | Quake II field | Meaning |
|---|---|---|---|
| 0x00 | float | — | Smoothed frame rate. The engine never lets it fall below `cl_maxfps`: new = 0.1·fps + 0.9·old. No renderer use found (UNVERIFIED). |
| 0x04 | int | x | View rectangle, screen pixels. |
| 0x08 | int | y | |
| 0x0c | int | width | |
| 0x10 | int | height | |
| 0x14 | float | fov_x | Degrees. The engine refuses values < 1 or > 179. |
| 0x18 | float | fov_y | Engine computes atan(height / (width / tan(fov_x/2)))·2. |
| 0x1c | vec3 | vieworg | |
| 0x28 | vec3 | viewangles | Pitch, yaw, roll. |
| 0x34 | float[4] | blend | Full-screen tint, RGBA 0–1. |
| 0x44 | float | time | Client time in seconds. The renderer uses it for glow pulsing. |
| 0x48 | float | — | Game clock from the previous frame. |
| 0x4c | float | — | Game-clock delta: offset 0x50 minus offset 0x48. Used for texture scrolling and fades. |
| 0x50 | float | — | Game clock in seconds. It stops while the game is paused. It is the renderer's main animation clock: warping, sky, animated world textures (frame = clock × 2) and Ghoul animation. |
| 0x54 | int | rdflags | See flag table. |
| 0x58 | byte * | areabits | Area visibility bits. |
| 0x5c | lightstyle * | lightstyles | 256 entries. |
| 0x60 | int | num_entities | ≤ 512. |
| 0x64 | entity ** | entities | **An array of pointers to entities.** Quake II used an array of structures. |
| 0x68 | int | num_dlights | ≤ 32. |
| 0x6c | dlight * | dlights | The array **must have room for 32 entries**: the renderer appends extra lights after the engine's (see *Behaviour*). |
| 0x70 | int | — | Number of quad chains (≤ 256). Takes the place of Quake II `num_particles`. |
| 0x74 | quadchain ** | — | Array of pointers to quad chains. |
| 0x78 | int | — | Number of alpha quads (≤ 1024). |
| 0x7c | alphaquad ** | — | Array of pointers to alpha quads. |
| 0x80 | pointer | — | Renderer scratch. The engine must leave it 0; the renderer resets it to 0 at the end of each frame. |

There is no Quake II particle list. The console statistics line printed with `cl_stats` names the two SoF lists:
`ent:%i  lt:%i  alphaquads: %i      quadchains: %i(%i)`.

#### Entity, 96 bytes (Quake II: 72)

| Off | Type | Quake II field | Meaning |
|---|---|---|---|
| 0x00 | model * | model | Null: the renderer draws a small placeholder shape. |
| 0x04 | vec3 | angles | |
| 0x10 | vec3 | — | Bounding-box minimum, decoded by the engine from the packed `solid` of the entity state. No renderer use found (UNVERIFIED). |
| 0x1c | vec3 | — | Bounding-box maximum, same source (UNVERIFIED). |
| 0x28 | vec3 | origin | Already interpolated by the engine. |
| 0x34 | int | frame | Brush models: texture-animation frame. Ghoul models (flag 0x400000): the low 16 bits are the Ghoul instance handle. |
| 0x38 | int | oldframe? | Written by the engine; meaning UNVERIFIED. |
| 0x3c | float | backlerp | 1 − interpolation fraction. |
| 0x40 | int | skinnum | |
| 0x44 | int | — | 0 in every engine-built entity (UNVERIFIED). |
| 0x48 | float | alpha? | 1.0 in packet entities (UNVERIFIED). |
| 0x4c | int | — | 0 (UNVERIFIED). |
| 0x50 | image * | skin | Player skin image, otherwise null. |
| 0x54 | int | flags | RF_* bits (table below). |
| 0x58 | float * | — | If non-null, the renderer **writes** the entity's computed light colour (3 floats) here after drawing a Ghoul entity. |
| 0x5c | byte (+3 pad) | — | 255 in packet entities (UNVERIFIED). |

Only three entity kinds are drawn: brush models, Ghoul models and the null placeholder. All entities are sorted back to
front together with alpha quads, quad chains and translucent world surfaces, by squared distance from the view origin.
Weapon-model entities sort as distance 0 and so draw last.

#### Dynamic light (`dlight`), 28 bytes — identical to Quake II

| Off | Type | Meaning |
|---|---|---|
| 0x00 | vec3 | origin |
| 0x0c | vec3 | colour r, g, b |
| 0x18 | float | intensity (radius). Contribution = (intensity − distance) / 256 × colour. |

#### Light style (`lightstyle`), 16 bytes — identical to Quake II

| Off | Type | Meaning |
|---|---|---|
| 0x00 | float[3] | rgb |
| 0x0c | float | white = r + g + b |

#### Alpha quad (SoF), 100 bytes read by the renderer

| Off | Type | Meaning |
|---|---|---|
| 0x00 | vec3 | Origin; start point for beams. |
| 0x0c | vec3 | Beam (flag 0x4): end point. Billboard (flag 0x2): +0x0c = width, +0x10 = height. |
| 0x18 | float | Beam: half width. Billboard: cosine of the rotation (1.0 = none). |
| 0x1c | float | Billboard: sine of the rotation. |
| 0x20 | vec3 | Normal used instead of the view axes when flag 0x10 is set. |
| 0x2c | byte[4] | Colour RGBA. |
| 0x30 | image * | Image cache. The renderer fills it from +0x34 on first use (renderer writes). |
| 0x34 | char * | Texture name, registered as a sprite image. |
| 0x38 | uint16 | Flags (table below). |
| 0x3a | byte | Direction argument for the light probe (see export 0xa4); 255 = default. |
| 0x3c | int | Fog cache: < 0 = not yet computed (renderer writes). |
| 0x40 | int | Leaf index for visibility testing; < 0 = always visible. |
| 0x44 | 28 bytes | Engine-only data. |
| 0x60 | alphaquad * | Next quad in a chain. |

#### Quad chain (SoF), 40 bytes

| Off | Type | Meaning |
|---|---|---|
| 0x00 | alphaquad * | First quad; the rest are linked through +0x60. |
| 0x04 | vec3 | Bounding-box minimum (used for culling). |
| 0x10 | vec3 | Bounding-box maximum. |
| 0x1c | float | Screen-coverage weight. The renderer divides it by distance and stores the result back (renderer writes). |
| 0x20 | int | Quad count, used when dropping quads for level of detail. |
| 0x24 | pointer | Engine-only owner. |

## Behaviour

### Loading, version check and fallback

1. The engine (re)loads the renderer at start-up and whenever `vid_ref` is modified, for example by `vid_restart`.
   While loading, screen updates are suspended and the 3D view is marked "not prepared", so all registrations are
   redone afterwards.
2. If a renderer is already loaded, the engine calls Shutdown and unloads the library. If unloading fails, it raises a
   fatal error, "Reflib FreeLibrary failed".
3. The console prints `------- Loading ref_<x>.dll -------`.
4. **The library cannot be loaded.** The console prints `LoadLibrary("ref_<x>.dll") failed`, and the load counts as
   failed.
5. **`GetRefAPI` is missing.** Fatal error: `GetProcAddress failed on ref_<x>.dll`.
6. **api_version is not 3.** The library is unloaded, then fatal error: `ref_<x>.dll has incompatible api_version`.
7. **Init returns −1.** Shutdown is called, the library is unloaded, and the load counts as failed.
8. **Success.** If the current sound library exports `S_A3D_ExportRenderGeom`, the engine calls it with a pointer to
   the engine's copy of the export table. The renderer is then marked active.
9. **Fallback after a failed load.** If `vid_ref` is `gl`, the engine raises a fatal error with a localized message:
   there is nothing else to fall back to. Otherwise it sets `vid_ref` to `gl`, opens the console unless it is already
   open, and tries again on the next check.
10. **Sound library without `S_A3D_ExportRenderGeom`.** Each time the sound library is loaded, the engine checks for
    this export. If it is missing, the engine sets slot 0xd8 of its export-table copy to null. If it is present and
    slot 0xd8 is non-null, the engine passes the table to it.
11. Screen updates resume after loading, unless the client is in the full "active" connection state. Exact condition
    UNVERIFIED.

### Registration cycle (per map)

When a map is entered, the engine:
1. calls BeginRegistration(map name);
2. registers pics named by configstrings;
3. registers models — inline `*N` and the world;
4. registers pics again for the image precache list;
5. calls SetSky(name, rotate, axis) from the sky configstrings (`%f %f %f` axis format);
6. calls slot 0x38 with the terrain configstring;
7. calls EndRegistration.

Each registration marks the image or model as used in the current cycle. **EndRegistration** then frees:
- every image not used in this cycle, **except** pics and font images, which survive across maps;
- **all** raw-pixel images (slot 0xa0), whether used or not.

The renderer can hold at most 800 images. Exceeding that is a drop error, "Max number of allowed textures exceeded".

Image names are given without extension. The renderer loads `<name>.m32` through FS_LoadFile and accepts only
format version 4; the rest of the format is in a separate spec. Other name limits:
- an empty name prints "GL_FindImage: Name (…) too short";
- a name of 63 or more characters prints "… too long" and returns null;
- a missing file prints `GL_FindImage: Can't load (<name>)` and returns null.

Default images loaded by Init: `pics/console/notexture` (pic) and `pics/console/conchars` (font). If either is missing
the error is fatal.

### SetSky

SetSky reads the text file `pics/sky/<name>.txt`. Each line names one sky-face image, up to six; line breaks may be
CR, LF or both. The face order is UNVERIFIED; Quake II used rt, bk, lf, ft, up, dn.
- A line equal to `none` (any case) leaves that face as the default "notexture" image.
- `rotate` (degrees per second, UNVERIFIED unit) and `axis` are stored for sky rotation.

Console command `sky <basename> <rotate> <axis x y z>` calls SetSky directly:
- if all five arguments are not given, the axis is (0, 0, 1);
- if only the base name is given, rotate is 0.

### Terrain, clouds

- Slot 0x38 loads terrain set `env/<name>.trn`. An unreadable file prints `<name> isn't a valid terrain set name`; a
  bad version prints `Invalid version for Terrain Set <name>`. Console command `terrain <terrainFileName>` calls it
  directly.
- Console command `cloud <type> <density> <angle> <speed> <red> <green> <blue>` calls slot 0x34, which has no
  effect in 1.07f.

### Frame sequence

A normal screen update is:
1. BeginFrame;
2. RenderFrame, if a 3D view is shown;
3. slot 0xb4 with argument 0;
4. all 2D drawing;
5. EndFrame.

- Console command `timerefresh` renders 128 frames. Each frame adds 2.8125° (= 360/128) to the view yaw. Then it
  prints `<seconds> seconds (<fps> fps)`.
- With an argument, `timerefresh` wraps all 128 RenderFrame calls in a single BeginFrame/EndFrame pair.
- RenderFrame does nothing when the renderer cvar `r_norefresh` is set.
- If the frame record does not set rdflags bit 2 and no world is loaded, RenderFrame raises a drop error:
  `R_RenderView: NULL worldmodel`.

**Dynamic-light write-back.** When cvar `gl_dlightintensity` (default 2.0, clamped to 4) is non-zero, RenderFrame
appends lights reported by the Ghoul system to the engine's dlight array. They go after the engine's own entries, up
to entry 31, with intensity multiplied by that cvar.

### Alpha-quad and quad-chain rendering

**Level of detail.** When the summed coverage weight of all quad chains exceeds cvar `gl_effectfill` (default 100),
the renderer drops quads from chains for that frame. It marks them hidden and zeroes their size. Exact formula
UNVERIFIED.

**Render flags (`entity.flags`)**

| Bit | Quake II name | SoF effect |
|---|---|---|
| 0x00000001 | RF_MINLIGHT | Ghoul light raised to at least 0.1 per component. |
| 0x00000002 | RF_VIEWERMODEL | Not drawn in the normal pass. |
| 0x00000004 | RF_WEAPONMODEL | Sorted last; separate parameters. |
| 0x00000008 | RF_FULLBRIGHT | Light = (1, 1, 1). |
| 0x00000010 | RF_DEPTHHACK | Depth range compressed to 30 %. |
| 0x00000200 | RF_GLOW | Light += 0.1·sin(time·7), and never below 0.8 × the original. |
| 0x00001000 | (RF_SHELL_BLUE) | Passed to the Ghoul system as a boolean. Meaning UNVERIFIED. |
| 0x00400000 | — | The entity is a Ghoul model. |
| 0x10000000 | — | Ghoul: use plain point lighting, without directional probing (UNVERIFIED). |

**Frame flags (`refdef.rdflags`)**

| Bit | Quake II name | SoF effect |
|---|---|---|
| 0x1 | RDF_UNDERWATER | No renderer effect found (UNVERIFIED). |
| 0x2 | RDF_NOWORLDMODEL | The world is neither required nor drawn. |
| 0x4 | (RDF_IRGOGGLES) | Night-vision view: the world is drawn without lightmaps, Ghoul models fullbright, plus a full-screen green noise overlay that fades in. |

**Alpha-quad flags (+0x38)**

| Bit | Meaning |
|---|---|
| 0x0001 | Hidden. The renderer also sets it on quads dropped for level of detail. |
| 0x0002 | Billboard: faces the camera, optional rotation. |
| 0x0004 | Beam from origin to end point. |
| 0x0008 | Lit: colour modulated by the light at the origin. |
| 0x0010 | Oriented by the +0x20 normal. |
| 0x0040 | Drawn immediately, without depth sorting. |
| 0x0080 | Chain head: the whole chain is drawn as one batch (UNVERIFIED). |
| 0x0100 | Depth offset, decal style (when enabled by renderer options; UNVERIFIED which). |
| 0x0200 | Additive blend; colour premultiplied by alpha. |
| 0x0400 | Darkening blend (destination × (1 − source colour)). |
| 0x0800 | Chain head: drawn without depth test, sorted as farthest (UNVERIFIED). |
| 0x1000 | Set by the renderer: drawn this frame. |

### Text colour codes (slots 0x60, 0x64, 0xc8)

In coloured strings, a byte 1–31 is not drawn. It selects the colour for the following characters from the 32-entry
table under *Constants*.
- Slots 0x60/0x64: every byte 1–31 is a colour code.
- Proportional-font text (slot 0xc8): byte 10 starts a new line and byte 13 is ignored. Neither changes the colour;
  table entries 10 and 13 are therefore never selected there.

### Fonts (slots 0xc8, 0xcc, 0xd0)

Fonts are selected by name:

| Font name | Source |
|---|---|
| `small` | `pics/menus/fonts/font1` (metrics `pics/menus/fonts/font1.fnt` + image) |
| `medium` | `pics/menus/fonts/font4` |
| `title` | `pics/menus/fonts/font2` |
| `interface` | `pics/menus/fonts/inttext`, a fixed-cell font. Its grid parameters 32, 64, 32, 24 have UNVERIFIED meaning (probably first character, character count, cell width, cell height). |

A missing `.fnt` file prints `Unable to find : <file>`.

Text layout rules:
- Glyphs are advanced by their own width.
- A space advances by half the line height.
- A character outside the font's range draws nothing and has width 0.
- A character inside the range whose glyph has zero width is drawn and measured as `.`.
- A newline moves down one line height and returns to the start x.

### A3D geometry export (slot 0xd8)

The Aureal A3D sound library calls slot 0xd8 periodically while its geometry option is enabled. The renderer then
submits nearby visible world polygons to the A3D geometry interface, for reflection and occlusion. If the word at
`control[0]` is non-zero, or the world changed since the last call, the renderer drops its cached geometry first.
Exact argument types belong to the A3D sound spec (UNVERIFIED).

### Renderer console commands and notable cvars

| Kind | Names |
|---|---|
| Commands added by the renderer | `screenshot`, `rawshot`, `imagelist`, `gl_strings` (removed again by Shutdown) |
| Renderer cvars named in this spec | `flushmap`, `lock_textures`, `r_norefresh`, `r_drawentities`, `r_fullbright`, `gl_dlightintensity`, `gl_effectfill`, `s3tc_avail`, `vid_card`, and the `ghl_*` group (Ghoul rendering: `ghl_light_method`, `ghl_specular`, `ghl_shadows`, `ghl_mip`, …) |
| Driver configuration | During Init the renderer runs console text `exec drivers/alldefs.cfg`, `exec drivers/<card>.cfg`, `exec drivers/memory<N>.cfg` and `exec drivers/cpu<N>.cfg` when the hardware is recognised. The detection details belong to a separate spec. |

## Constants

| Name | Value |
|---|---|
| Interface version | 3 |
| Import table size | 108 bytes (27 slots) |
| Export table size | 220 bytes (55 slots) |
| Frame record size | 132 bytes |
| Entity size | 96 bytes |
| Dynamic light size | 28 bytes |
| Light style size | 16 bytes |
| Alpha quad, renderer-visible size | 100 bytes |
| Quad chain size | 40 bytes |
| Max entities / dlights / quad chains / alpha quads / light styles | 512 / 32 / 256 / 1024 / 256 |
| Max images in renderer | 800 |
| Max inline model number | 256 |
| Image name length | 1–62 characters |
| Save-game thumbnail | 128 × 128 RGBA |
| `gl_dlightintensity` default / clamp | 2.0 / 4.0 |
| `gl_effectfill` default | 100.0 |

**Image types** (the `imagelist` legend is "M = Skin, S = Sprite, W = wall, P = Pic, D = Detail. F = Font"):

| Type | Meaning |
|---|---|
| 0 | skin |
| 1 | sprite |
| 2 | wall |
| 3 | pic |
| 4 | sky |
| 5 | detail |
| 6 | raw RGBA (slot 0xa0) |
| 7 | font |
| 8 | UNVERIFIED |

**Rotation table for slot 0x54.** Texture coordinates for the four quad corners, taken in order: (x, y), (x+w, y),
(x+w, y+h), (x, y+h).

| Rotation | Texture coordinates |
|---|---|
| 0 | (0,0) (1,0) (1,1) (0,1) |
| 1 | (1,0) (1,1) (0,1) (0,0) |
| 2 | (1,1) (0,1) (0,0) (1,0) |
| 3 | (0,1) (0,0) (1,0) (1,1) |

**Text colour table** (index: R, G, B, A, hexadecimal):

| Index | RGBA | Index | RGBA | Index | RGBA | Index | RGBA |
|---|---|---|---|---|---|---|---|
| 0 | 00 00 00 00 | 8 | 00 00 00 FF | 16 | 56 4D 28 7F | 24 | 98 00 53 FF |
| 1 | FF FF FF FF | 9 | 7F 7F 7F 7F | 17 | 4C 5E 36 7F | 25 | 96 00 18 FF |
| 2 | FF 00 00 FF | 10 | 00 00 00 00 | 18 | 37 6F 65 7F | 26 | 70 2D 07 FF |
| 3 | 00 FF 00 FF | 11 | 7F 00 00 7F | 19 | 00 55 72 FF | 27 | 54 49 2A FF |
| 4 | FF FF 00 FF | 12 | 00 7F 00 7F | 20 | 54 64 7E FF | 28 | 61 A9 97 FF |
| 5 | 00 00 FF FF | 13 | 00 00 00 00 | 21 | 1E 2A 63 BF | 29 | CB 8F 39 FF |
| 6 | FF 00 FF FF | 14 | 7F 7F 00 7F | 22 | 66 09 7B FF | 30 | CF 83 16 FF |
| 7 | 00 FF FF FF | 15 | 00 00 7F 7F | 23 | 70 5E 61 FF | 31 | FF 80 20 FF |

**Error levels used across the interface:** 0 fatal, 1 drop, 2 disconnect (the engine uses 2 for "Failed to open map
file" during lump loading).

## Test vectors / how to verify

1. **Version and table sizes.** Write a probe `ref_probe.dll` that exports `GetRefAPI`. Have it record the 108-byte
   import block it receives and return a 220-byte table with version 3 and logging stubs. Set `vid_ref probe` and run
   `vid_restart`.
   - Expected: GetRefAPI is called once, then Init with three arguments.
   - Import slots 0x04 and 0x08 contain arbitrary values; all others are non-null.
   - Slot 0x68 points to floats starting 0.219625, 0, 0.975584.
2. **Version mismatch.** Same probe, returning version 2. Expected: fatal error `ref_probe.dll has incompatible
   api_version`.
3. **Missing library.** `vid_ref nosuch`, then `vid_restart`. Expected:
   - console shows `LoadLibrary("ref_nosuch.dll") failed`;
   - `vid_ref` reads `gl` afterwards;
   - the console is open;
   - the GL renderer comes back.
4. **Init failure.** Probe Init returns −1. Expected: Shutdown is called, then the fallback to `gl` from test 3.
   Init returning 0 must count as **success**.
5. **Frame order.** Probe logging during gameplay shows, per frame: BeginFrame, RenderFrame(ptr), slot 0xb4(0),
   2D calls, EndFrame.
   - At RenderFrame, check the record's num_entities ≤ 512 and that entities is an array of pointers.
   - The dlights array has 32 entries' worth of writable memory.
6. **Pause fade.** Pause a single-player game. The probe records slot 0x84 called with bytes (0, 0, 0, 0x7F).
7. **timerefresh.** In a map, `timerefresh` produces 128 RenderFrame calls whose yaw goes up in 2.8125° steps.
8. **cloud.** `cloud 1 1 0 0 1 1 1` with the real renderer produces no visible change.
9. **Thumbnails.** Saving a single-player game produces a 128 × 128 RGBA thumbnail through slot 0xd4. Loading the
   save menu passes the stored thumbnail through slot 0xa0 under the save's name.
10. **Colour codes.** Through the probe, call slot 0x60 with the bytes `02 'A' 03 'B'`. The real renderer draws a red
    "A" and a green "B". Compare screenshots with the real ref_gl.dll by calling it from the probe harness.
11. **Fonts.** Slot 0xd0 with font `small` returns the line height stored in `pics/menus/fonts/font1.fnt`. Check it
    against the data file once the `.fnt` spec exists.

## Open questions

- Method table of the Ghoul object passed to Init. The renderer uses methods at byte offsets 0x0c, 0x24, 0x70–0x80,
  0x88–0xc0. To be specified with the Ghoul system.
- Slot 0x58: layout of the 44-byte lump descriptor and the converter callback contract. Verify by logging calls from
  a probe renderer.
- Slot 0xc0: which video settings map to which parameter, and the correction formula. Verify by sweeping the menu
  sliders with a probe.
- Slot 0x6c: is `scale` honoured? Probe with scale 2.
- Slot 0xb8: meaning of its argument.
- Slot 0x78: border shading formula.
- Entity fields 0x10–0x24, 0x38, 0x44–0x4c and 0x5c: no renderer reader was found. Possibly the Ghoul system reads
  them through the entity pointer.
- Frame-record field 0x00: consumer unknown.
- Con_Printf level 2: which cvar/bit gates it.
- FS_LoadFile third argument meaning.
- rdflags bit 0x1 handling.
- Image type 8 and `.m32` flag bits; deferred to the `.m32` spec.
- A3D export argument types (slot 0xd8); deferred to the A3D sound spec.
