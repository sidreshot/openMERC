# SPEC-010: Ghoul model system — module interfaces (system, model object, player model, callbacks)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (provides the Ghoul model system), gamex86.dll, player.dll, ref_gl.dll (use it), version 1.07f
Depends on: SPEC-001 (game imports #60–#66), SPEC-002 (renderer `Init` and entity layout), SPEC-003 (player module,
draft), SPEC-005 (module map), SPEC-011 (Ghoul instance interface and the data layouts shared with it).
The network encoding of messages 26/27 (`ghoulreliable` / `ghoulunreliable`) and the `.ghb`/`.ghl` file format are
outside this spec.

## Purpose

"Ghoul" is the hierarchical (skeletal) model system of Soldier of Fortune. It lives inside the engine executable and
is reached by the other modules only through C++ objects with method tables. This spec lists every such object that
crosses a module boundary, how each is obtained and how long it lives, and gives every method in table order with its
arguments, result and observable behaviour. It is the contract a Ghoul reimplementation must offer to the original
game, player and renderer modules, and the contract a reimplemented game, player module or renderer must follow when
running against the original engine. SPEC-011 continues with the largest interface, the model instance.

## Interface / layout

### Conventions (all Ghoul interfaces)
- 32-bit x86, Microsoft Visual C++ 6 object model. An interface pointer points to an object whose **first 4 bytes
  point to its method table**, an array of 4-byte code addresses; method *n* is at byte offset 4·*n*. Other modules
  never touch any other part of the object. A table must contain all listed entries in the listed order, including
  entries no other module calls (the original modules call by fixed offset).
- Methods use the `thiscall` convention: object pointer in register ECX, remaining arguments pushed right to left,
  **the method removes its own arguments** from the stack, integer/pointer results in EAX, `float` results on the x87
  stack. No method is variadic. Argument counts in the tables are exact (they determine the stack clean-up).
- "bool" results are returned in AL; callers test only the low 8 bits. "bool" arguments occupy a 4-byte slot of which
  only the low byte is read.
- "id" = 16-bit unsigned number passed in a 4-byte slot (only the low 16 bits are read) or returned in AX. Ids are
  1-based; **0 always means "none" or "not found"**.
- "string" = NUL-terminated 8-bit text. "out buffer" = caller-supplied memory; the methods do not check its size.
- Names (sequence paths, part, note, material and skin names) are compared case-insensitively; the system stores them
  upper-case and returns them upper-case. In paths `\` and `/` are equivalent (stored as `/`).

### Inventory of the interfaces

| # | Interface | Provided by | How another module obtains it | Lifetime / ownership | Methods |
|---|---|---|---|---|---|
| A | Ghoul system | engine | game import #60 (server instance); renderer `Init` third argument (client instance); player module common/client/server imports (see SPEC-003) | static, one per role, exists for the whole process | 49 |
| B | Model object | engine | system method 0 | created by system method 0; destroyed by its own method 27 or when its system instance clears all objects | 29 |
| C | Model instance | engine | model-object method 24; system method 9 (by handle) | see SPEC-011 | 55 |
| D | Player-model description | engine | game import #61 (`.gpm` / `.gbm` file) | new object per call; the caller owns it and deletes it with its method 18 | 19 |
| E | Event callback | **game module / player module** | passed by the caller to model-object method 14 and to instance methods 23 and 25 (SPEC-011) | owned by the module that implements it; must stay valid while registered | 1 |
| F | Render parameters | renderer (or engine menu code) | passed to instance method 41 (SPEC-011) | caller's stack; valid during the call | 1 (plus data) |
| G | GL entry-point table | renderer | passed to system method 1 | plain table of 52 function pointers, must stay valid until system method 3 | — |

Objects of kinds B and C returned by one system instance belong to that instance; ids are per instance.

### Concepts
- **Model object**: one model = a set of registered *sequence files* (`.ghl` paths; each refers into a model data
  file) and four name registries built from those files: *parts*, *notes*, *materials* and per-material *skins*.
  Each registry hands out 1-based ids in registration order.
- **Part**: a named node of the model hierarchy. Type codes: 0 = null node ("bolt": an attachment point with a
  transform but no geometry), 1 = mesh, 2 = camera, 3 = light. Gore zones are ordinary mesh parts whose names begin
  with `GZ_`; there is no separate gore interface (see Behaviour).
- **Sequence**: one animation (one registered `.ghl` path). Playback is per instance (SPEC-011).
- **Note**: a named, timed event inside a sequence with an argument string (e.g. `sound` with a wave-file name).
  Every model has the notes `BOS` and `EOS` (begin / end of sequence).
- **Material / skin**: a material is a named surface group of the meshes (e.g. `a`, `b`, `f`, `h`, `gz`, `c` in the
  shipped player models); a skin is a named texture set for one material. Skin ids are unique across all materials of
  one object.
- **Instance**: one placed, animated copy of a model object (SPEC-011). **Handle** = the id of an object or instance
  as sent over the network.

### A. Ghoul system interface — 49 methods

Three objects of one implementation exist from program start to exit: **server**, **client** and **menu**. The
server one is returned by game import #60 and handed (by the game) to the player module's server side; the client one
is passed as the third argument of the renderer's `Init` and to the player module's client side; the menu one is used
only by the engine's own menu system. They share the model-file and texture caches but have separate object and
instance tables and separate id counters.

Column "Used by" lists the original modules that call the method: G = gamex86.dll, P = player.dll, R = ref_gl.dll,
E = engine only. Entries marked E are called only from inside the engine (network, demo, console); a Ghoul
reimplementation that lives inside a reimplemented engine may implement them freely, but must keep the table slot.

| # | Off | Args (after this) | Returns | Meaning | Used by |
|---|---|---|---|---|---|
| 0 | 0x00 | — | model object | Create an empty model object with a new object id (see Behaviour: ids). On the server, every connected client is told about it | G P E |
| 1 | 0x04 | GL table pointer (layout G) | — | Attach the renderer's OpenGL entry points (shared by all three instances); textures of every model file already loaded are uploaded now | R |
| 2 | 0x08 | bool specular | — | Remember the specular-lighting setting and re-prepare (object method 2) every object of the client and menu instances with it | R |
| 3 | 0x0c | — | — | Release every texture the Ghoul system uploaded and mark the model files as not uploaded (renderer shutdown) | R |
| 4 | 0x10 | — | — | Start of a game frame. Does nothing in 1.07f | G |
| 5 | 0x14 | — | — | End of a game frame. Does nothing in 1.07f | G |
| 6 | 0x18 | — | — | Destroy every model object of this instance, together with all their instances | G E |
| 7 | 0x1c | — | — | Purge: free the prepared-mesh cache, unload model files no object references and textures no client or menu object uses | E |
| 8 | 0x20 | — | — | Reset: forget all network clients, destroy all objects, clear the name tables and stored network data | E |
| 9 | 0x24 | int handle | instance or NULL | Find an instance by its **server handle** (see Behaviour: ids). 0 or unknown → NULL | P R E |
| 10 | 0x28 | int handle | model object or NULL | Find a model object by server handle, same rules. No caller in the original modules | — |
| 11 | 0x2c | function pointer | — | Install the engine file-system callback that turns a game path into an operating-system path (`void (char *out, const char *path)`, writes "" when not found) | E |
| 12 | 0x30 | int client number | — | Network: a client connected; queue every server object for it | E |
| 13 | 0x34 | int client number | — | Network: forget that client | E |
| 14 | 0x38 | — | — | Network: drop and re-add every client (full resend; used when a new map starts with clients connected) | E |
| 15 | 0x3c | — | — | Network: drop every client | E |
| 16 | 0x40 | int client, int frame, engine bit writer | bool | Network: write pending reliable model data for the client; true when nothing is left | E |
| 17 | 0x44 | int client | bool | Network: true while reliable model data is pending for the client | E |
| 18 | 0x48 | int client, int frame, engine bit writer, float time | bool | Network: write instance state changes since the client's last acknowledged frame; true when anything was written | E |
| 19 | 0x4c | int client, int frame | — | Network: client acknowledged `frame`; a negative frame forces full state | E |
| 20 | 0x50 | int client, int frame | — | Network: commit the frame just written | E |
| 21 | 0x54 | int client, int frame | — | Network: begin writing a frame | E |
| 22 | 0x58 | string path | — | Open and immediately close the file through the engine file system so that it is recorded as used (and therefore offered for download). Game: player-model files at level start; engine: console command `adddownload` | G E |
| 23 | 0x5c | — | — | Client: start accepting model data and process everything stored so far | E |
| 24 | 0x60 | int (unused), engine bit reader, bool specular | — | Client: store and (when accepting) apply one reliable chunk | E |
| 25 | 0x64 | int frame, engine bit reader, float time | — | Client: apply instance state changes; unknown instances are created from their object | E |
| 26 | 0x68 | — | int | Client: size in bytes of the stored reliable stream | E |
| 27 | 0x6c | — | pointer | Client: the stored reliable stream (sequence of {32-bit length L, L bytes}); written into demos | E |
| 28 | 0x70 | — | int | Number of entries in the "lights from models" list | R |
| 29 | 0x74 | int index, light record out (layout below) | — | Copy entry `index` of that list (no range check) | R |
| 30 | 0x78 | — | — | Empty the model-light list and the dynamic-light list | R |
| 31 | 0x7c | light record pointer | — | Add a dynamic light that lights Ghoul models this frame | R |
| 32 | 0x80 | — | — | End of the renderer frame. Does nothing in 1.07f | R |
| 33 | 0x84 | — | — | Free the prepared-mesh cache | E |
| 34 | 0x88 | light record pointer | — | Add one static world light | R |
| 35 | 0x8c | — | — | Finish the world-light set (builds a spatial lookup; each light affects the box origin ± radius) | R |
| 36 | 0x90 | — | — | Start a new world-light set (clears it and the prepared-mesh cache) | R |
| 37 | 0x94 | pointer to 256 bytes | — | Gamma ramp applied to texture red, green and blue when Ghoul textures are uploaded (copied; shared by all instances) | R |
| 38 | 0x98 | int level | — | Texture detail reduction used when textures are prepared (0 = full size; shared by all instances; objects of the menu instance always use 0) | G R |
| 39 | 0x9c | float factor, float units | — | Polygon-offset values Ghoul uses for its decal-like passes (shared) | R |
| 40 | 0xa0 | — | int | Statistic: distinct Ghoul textures bound since the last reset | R |
| 41 | 0xa4 | — | int | Statistic: their total size (unit believed bytes, UNVERIFIED) | R |
| 42 | 0xa8 | — | int | Statistic: meshes drawn | R |
| 43 | 0xac | — | int | Statistic: triangles drawn | R |
| 44 | 0xb0 | — | int | Statistic: a second triangle counter that is never reset (UNVERIFIED meaning); no caller | — |
| 45 | 0xb4 | — | int | Statistic: vertices — always 0 in 1.07f | R |
| 46 | 0xb8 | — | int | Statistic: "corners" — always 0 | R |
| 47 | 0xbc | — | int | Statistic: lights — always 0 | R |
| 48 | 0xc0 | — | — | Reset statistics 40–43 and 45–47 and advance the render-frame counter used for "once per frame" texture counting | R |

#### Light record — 28 bytes (methods 29, 31, 34)
| Offset | Type | Meaning |
|---|---|---|
| 0x00 | float[3] | origin |
| 0x0c | float | radius (intensity in the Quake II dynamic-light sense) |
| 0x10 | float[3] | colour r, g, b |

The order differs from the renderer's dynamic-light layout of SPEC-002 (origin, colour, intensity); the renderer
converts in both directions.

#### GL entry-point table — 52 pointers, 208 bytes (method 1)
Every entry uses the `stdcall` convention and the parameter list of the OpenGL 1.1 function of the same name, except
entry 51. The renderer may substitute wrappers (its own state cache, texture-binding cache and filter settings); the
Ghoul system only calls through the table.

| Entries | Functions |
|---|---|
| 0–9 | AlphaFunc, Begin, BindTexture, BlendFunc, Color3f, Color4f, CullFace, DepthFunc, DepthMask, DeleteTextures |
| 10–19 | Disable, Enable, End, FrontFace, GetError, GenTextures, GetFloatv, GetIntegerv, LoadIdentity, LoadMatrixf |
| 20–29 | MatrixMode, MultMatrixf, Normal3f, PixelStorei, PolygonOffset, PopMatrix, PushMatrix, Rotatef, ShadeModel, TexCoord2f |
| 30–38 | TexEnvf, TexImage2D, TexParameteri, TexGeni, Vertex3f, Color3fv, Normal3fv, TexCoord2fv, Vertex3fv |
| 39–50 | LightModelf, LightModelfv, LightModeli, LightModeliv, Lightf, Lightfv, Lighti, Lightiv, Materialf, Materialfv, Materiali, Materialiv |
| 51 | no arguments: apply any state changes the renderer has deferred, before Ghoul issues geometry |

The original renderer defers Enable/Disable for blending, face culling, alpha test, depth test, fog, texture
coordinate generation (S, T) and polygon-offset fill, and AlphaFunc, BlendFunc and DepthFunc, until entry 51 or its
own next draw. Without a table (dedicated server) no textures are uploaded.

### B. Model object interface — 29 methods

| # | Off | Args (after this) | Returns | Meaning | Used by |
|---|---|---|---|---|---|
| 0 | 0x00 | string path, bool alsoOthers | sequence id | Register a sequence file (`.ghl` path). Loads the model data if needed. Returns the new sequence id, 0 when the file cannot be loaded. `alsoOthers` ≠ 0: also register every other sequence stored in the same data file (no original caller passes ≠ 0; UNVERIFIED) | G P E |
| 1 | 0x04 | bool allSkins | — | Finish registration: register every part, every note name (always including `BOS` and `EOS`) and every material found in the loaded sequences; with `allSkins` ≠ 0 also every skin found for each material | G P E |
| 2 | 0x08 | bool specular | — | Prepare for drawing: build render data and load the textures of the registered skins, using the texture detail level of system method 38 (always full detail on the menu instance) and the specular setting | G P E |
| 3 | 0x0c | string path | sequence id | Find a registered sequence by path; 0 when unknown, when its file is not loaded, or for NULL | G P E |
| 4 | 0x10 | — | int (16-bit) | Number of registered sequences | — |
| 5 | 0x14 | id sequence, out buffer | — | Copy the sequence's stored (upper-case, `/`) path | — |
| 6 | 0x18 | string name | part id | Register a part name (normally done by method 1). Prints "name Collision between different object types" when two files disagree on the node type | E |
| 7 | 0x1c | string name | part id | Find a part by name; 0 if none or NULL. Most used lookup (bolts such as `WBOLT_HAND_R`, gore-zone meshes `GZ_*`) | G P E |
| 8 | 0x20 | — | int (16-bit) | Number of parts | G |
| 9 | 0x24 | id part | int | Part type: 0 null/bolt, 1 mesh, 2 camera, 3 light | G |
| 10 | 0x28 | id part, out buffer | — | Copy the part name | G |
| 11 | 0x2c | string name | note id | Register a note name; an existing name returns its id | E |
| 12 | 0x30 | string name | note id | Find a note by name; 0 if none | G P |
| 13 | 0x34 | — | int (16-bit) | Number of note names | — |
| 14 | 0x38 | event callback (E), id note | — | For every occurrence of that note in every loaded sequence, call the callback with (0, 0, 0.0, argument string). With note 0, every occurrence of every note, the string then being `<NOTE NAME> <argument>`. Used to precache the sounds and effects that notes will request | G P |
| 15 | 0x3c | string name | material id | Register a material name (normally done by method 1) | E |
| 16 | 0x40 | string name | material id | Find a material by name; 0 if none | G P |
| 17 | 0x44 | — | int (16-bit) | Number of materials | G |
| 18 | 0x48 | id material, out buffer | — | Copy the material name | G |
| 19 | 0x4c | id material, string skin name, int (ignored, callers pass 0) | skin id | Register a skin (texture set) for a material; returns its id (0 when no loaded file provides it, UNVERIFIED) | G |
| 20 | 0x50 | id material, string skin name, int (ignored) | skin id | Find the skin of that material by name; 0 if none | G P |
| 21 | 0x54 | — | int (16-bit) | Number of skins (all materials) | — |
| 22 | 0x58 | id skin, out buffer | — | Copy the skin name | G |
| 23 | 0x5c | id skin | material id | Material the skin belongs to | — |
| 24 | 0x60 | — | instance | Create a new instance of this model with a new instance id (SPEC-011) | G P E |
| 25 | 0x64 | — | int (16-bit) | This object's id (its network handle) | — |
| 26 | 0x68 | — | system interface | The system instance that owns this object | — |
| 27 | 0x6c | — | — | Destroy: destroy all instances (method 28), release the model files, remove the object from its system instance (the server tells clients), free it. The pointer is invalid afterwards | G E |
| 28 | 0x70 | — | — | Destroy every instance of this object (instance method 53, SPEC-011) | — |

### C. Model instance interface — 55 methods
See SPEC-011.

### D. Player-model description — 19 methods

Created by game import #61 with a file name relative to `Ghoul/pmodels/` (the original game passes `<model>.gpm`
for characters and `boltons/<name>.gbm` for bolt-on pieces). The object is 948 bytes; the creator owns it and deletes
it through method 18 with argument 1. All string results point into the object (valid until it is reloaded or
deleted). Indexes of the three lists are **1-based**.

| # | Off | Args (after this) | Returns | Meaning |
|---|---|---|---|---|
| 0 | 0x00 | string file | — | Clear and (re)load from `Ghoul/pmodels/<file>`; success is reported by method 17 |
| 1 | 0x04 | — | int | A free 32-bit value for the caller (cleared by method 0). Unused by the original game |
| 2 | 0x08 | int value | — | Set that value |
| 3 | 0x0c | — | int | Number of skin entries (list A) |
| 4 | 0x10 | — | int | Number of bolt-on entries (list B) |
| 5 | 0x14 | — | int | Number of part-switch entries (list C) |
| 6 | 0x18 | — | string | Model directory below `Ghoul/` (token 1, e.g. `enemy/meso`; up to 100 bytes) |
| 7 | 0x1c | — | string | Menu sequence path `ghoul/<token 1>/<token 2>.ghl` |
| 8 | 0x20 | — | string | Base model name (token 3, e.g. `meso_player`, `fem_play`) |
| 9 | 0x24 | — | string | Sequence-list (`.gsq`) name (token 4) |
| 10 | 0x28 | — | string | Face-skin prefix (token 7; the game appends its own suffixes); up to 50 bytes |
| 11 | 0x2c | out buffer (≥ 200 bytes) | bool, always true | Copy the final token (always "" in the shipped files; purpose unknown) |
| 12 | 0x30 | out buffer (≥ 50 bytes) | bool, always true | Copy the team name (token 5) |
| 13 | 0x34 | out buffer (≥ 100 bytes) | bool, always true | Copy the sound directory (token 6, e.g. `enemy/dth/russ`; believed: pain/death sounds, UNVERIFIED) |
| 14 | 0x38 | int index, skin entry out (200 bytes) | bool | Copy skin entry `index`; false when index < 1 or > count |
| 15 | 0x3c | int index, bolt-on entry out (204 bytes) | bool | Copy bolt-on entry `index`; same range rule |
| 16 | 0x40 | int index, part-switch entry out (51 bytes) | bool | Copy part-switch entry `index`; same range rule |
| 17 | 0x44 | — | bool | The last load succeeded |
| 18 | 0x48 | int flags | pointer | Destructor; bit 0 set → the memory is freed as well |

Entry layouts (strings NUL-terminated, unused bytes undefined):

| Entry | Offset | Type | Meaning |
|---|---|---|---|
| skin (200 bytes) | 0x00 | char[50] | material name |
| | 0x32 | char[100] | skin name |
| | 0x96 | char[50] | part name ("" = no part: the entry applies to the whole model) |
| bolt-on (204 bytes) | 0x00 | float | scale |
| | 0x04 | char[50] | bolt name on the parent model |
| | 0x36 | char[50] | bolt name on the bolt-on model |
| | 0x68 | char[100] | bolt-on file (another description, relative to `Ghoul/pmodels/`) |
| part switch (51 bytes) | 0x00 | char[50] | part name |
| | 0x32 | byte | 1 = shown, 0 = hidden |

### E. Event callback — implemented by the game and the player module

A one-method interface. The implementing module creates the object (the original modules use static objects); the
Ghoul system only calls it.

| # | Off | Args (after this) | Returns | Meaning |
|---|---|---|---|---|
| 0 | 0x00 | 32-bit value (undefined), pointer userValue, float time, pointer data | bool (ignored) | An event happened. For **note events** `data` is the note key's argument text (never NULL; "" for `EOS`); for **bolt events** (instance method 25) it points to a 68-byte matrix (SPEC-011) that the engine frees right after the call; for **precache enumeration** (object method 14) the call is (0, 0, 0.0, string). `userValue` is the value the owner stored in the instance (SPEC-011 methods 21/22; the original game stores its entity pointer, the player module its inventory object). `time` is the time passed to the instance's event method (SPEC-011 method 45) |

The engine does **not** set the first argument for instance events (its content is undefined); implementations must
ignore it. All original implementations return true. The callback must not keep the `data` pointer after returning,
and should not destroy the calling instance (UNVERIFIED whether that is tolerated). The original game saves its
callbacks in savegames as indexes into a fixed table (SPEC-011, savegames), so callback objects there are static.

The renderer implements one more single-method interface, the **render cull** object that heads the render-parameter
block passed to instance method 41; it is specified with that block in SPEC-011.

### Engine helper imports that operate on these objects (game imports #62–#66)
These entries are listed in SPEC-001; this is their exact behaviour with respect to Ghoul objects.
- **#62** (folder, file, pointer to receive buffer) → int: loads `Ghoul/<folder>/<file>`, or `Ghoul/comskin/<file>`
  when the first does not exist, and returns the length or −1. The buffer belongs to an engine cache (the same buffer
  is returned on later calls for the same path; the caller never frees it) and is **not NUL-terminated**. A missing
  file gives −1 and a NULL buffer.
- **#63** (pointer to remaining count, pointer to cursor, line buffer) → bool: copies bytes up to a line feed. When a
  line feed is found it is consumed, the **last byte copied before it is replaced by NUL** (the function expects
  CR LF line ends), and the result is true. At the end of the data the copied text is terminated and the result is
  false. Vector: "ab\r\ncd" gives "ab" (true), then "cd" (false). A bare LF loses the line's last character.
- **#64** (folder, name): pre-loads `<name>.gsq` from `Ghoul/<folder>/`, else `Ghoul/comskin/`, into the cache.
- **#65** (folder, list name, model object) → bool: the object is an engine model object (B). NULL → false. If the
  list `<name>.gsq` does not exist, the single sequence `Ghoul/<folder>/<name>.GHL` (`Ghoul/<name>.GHL` when folder is
  "") is registered with object method 0 and the result is whether that succeeded. Otherwise each line is processed:
  empty lines are skipped, `#<name>` includes another list from the same folder, any other line registers
  `Ghoul/<folder>/<line>.GHL` (object method 0). The result is false if any registration failed (processing continues).
- **#66** (folder, list name, model object, instance): a "parts off" list (the shipped data names them `*_poff.gsq`).
  For each line (with the same `#` include rule) the part is looked up with object method 7 and, if found, hidden with
  instance method 8 (SPEC-011) with argument 0. A missing list does nothing.

So the "objects supplied by the game" of SPEC-001 imports #65/#66 are engine objects handed back by the game, not
game-implemented objects; the only game-implemented Ghoul interface is the event callback (E).

## Behaviour

### Ids and handles
- Each system instance has two independent id counters, one for model objects and one for instances, both starting
  at 1. Freed ids are reused. Asking for an id beyond 32766 is a fatal error "Ghoul :Out of uuids".
- The server instance keys its objects and instances by the ids it allocated. The client and menu instances store
  objects and instances **received from the server** under the negated server id; objects they create themselves get
  positive ids from their own counters. System methods 9 and 10 negate their argument when called on the client or
  menu instance. Consequence for callers: on every instance, method 9 takes the **server's handle** (the value carried
  in entity state and player state — SPEC-002 entity field 0x34 low 16 bits, the player state's view-weapon handle);
  locally created client objects are not reachable by handle.
- Handle 0 → NULL.

### Typical model set-up (used identically by the game, the player module and engine code)
1. `obj = system.method0()`.
2. Optionally `obj.method0("ghoul/<dir>/<base>.ghl", 0)`, then import #65 for the model's `.gsq` list.
3. `obj.method1(allSkins)` — the original game passes its "precache all" setting; the player module and the engine
   pass 1.
4. Optionally explicit skins with method 19.
5. `obj.method2(specular)` — the game passes true when cvar `ghl_specular` > 0 and its lighting-method cvar
   `ghl_light_method` > 0; the player module passes `ghl_specular` > 0.
6. Lookups (methods 3, 7, 12, 16, 20), then instances with method 24.
Lookup tables are built on the first Find call and are not rebuilt while non-empty; names registered after the first
lookup may not be found (UNVERIFIED).

### Lifetimes and resets
| Event | What happens to Ghoul state |
|---|---|
| Process start / exit | The three system instances are constructed / destroyed; their pointers never change |
| Game: level spawn, level load, game shutdown | The game destroys all its objects (server method 6) |
| Server shutdown | server methods 15 (and 8 unless the server is only reconnecting) |
| `map` while clients are connected | server method 14 |
| Dedicated server spawn | server method 7 |
| Client disconnect / state clear | client method 8 |
| Client finished loading | client methods 23, then 7 |
| Renderer shutdown (`vid_restart`, mode change) | client method 3; objects and instances survive and are re-uploaded by method 1 at the next renderer `Init` |
| Renderer loads a world | methods 36, 34 for each world light, 35 |

### Per-frame protocols seen by other modules
- **Game frame**: method 4, then each entity thinks (the original game runs each entity instance's event processing,
  instance method 45, with the level time unless cvar `ghl_no_server_update` ≥ 0.5), then method 5. In the first frames of a
  level the game prepares every instance's object (object method 2), controlled by cvars `ghl_precache_verts` and
  `ghl_precache_texture`.
- **Renderer frame**: statistics reset (48) at frame setup after reading 43; when cvar `gl_dlightintensity` ≠ 0 the
  renderer appends the model lights (28, 29; radius × `gl_dlightintensity`) to its dynamic-light array (up to 32
  entries, SPEC-002), calls 30, and when `ghl_light_method` > 0.5 passes every dynamic light to 31 (radius × 0.5);
  each Ghoul entity is drawn by finding its instance with method 9 and calling the instance's bounds and draw methods
  (SPEC-011); method 32 after the frame; `r_speeds` prints 40–43 and 45–47.
- **Renderer start-up**: method 38 (`ghl_mip` rounded), 39 (`ghl_poly_fact`, `ghl_poly_unit`), 37 (the renderer's
  image gamma table), 1 (GL table), 2 (`ghl_specular` > 0.5 and `ghl_light_method` > 0.5). Order between the first
  two and the last three UNVERIFIED.

### Errors
Fatal Ghoul errors terminate the program through the engine's system-error path with the text "Ghoul :<message>"
(they are not level drops). Messages: "Out of uuids", "Reliable underflowed", "Delta underflowed",
"StringTable underflowed", "Null object to create remote instance from.", "No object found to create remote instance
from.", "Ghoul reliable overflow", "Ghoul unreliable overflow". Object methods report non-fatal problems with console
prints (e.g. "name Collision between different object types").

### Gore zones and damage caps
They are data conventions, not interface features: gore-zone meshes are parts named `GZ_*`, shown and hidden like any
part; their look is chosen by giving material `gz` a skin (e.g. `gz_armor`) on that part; cap material `c` with skins
such as `c_blood`, and cap bolts named like `g_cap1`. The game finds all of them by name through methods 7, 16 and 20.

## Constants

| Name | Value |
|---|---|
| Method counts | system 49, model object 29, instance 55, player model 19, event callback 1 |
| Ids | 1 … 32766; 0 = none |
| Part types | 0 null/bolt, 1 mesh, 2 camera, 3 light |
| Always-present notes | `BOS`, `EOS` |
| Light record | 28 bytes |
| GL table | 52 entries, 208 bytes |
| Gamma table | 256 bytes |
| Player-model object size | 948 bytes |
| Player-model entries | skin 200, bolt-on 204, part switch 51 bytes |
| Player-model file folder | `Ghoul/pmodels/` |
| Ghoul file fallback folder | `Ghoul/comskin/` |

Ghoul cvars (name, default, archive flag 1 = saved): renderer — `ghl_light_method` "2" 1, `ghl_specular` "1" 1,
`ghl_wireframe` "0" 0, `ghl_shadows` "2" 1, `ghl_player_shadow` "1" 1, `ghl_shadow_darkness` ".75" 1,
`ghl_shadow_tilt` ".25" 1, `ghl_shadow_dist` "25" 1, `ghl_fast_lights` "0" 1, `ghl_max_lights` "3" 1,
`ghl_no_render` "0" 0, `ghl_poly_unit` "-.1" 1, `ghl_poly_fact` "-.1" 1, `ghl_obj_fog` "1" 1,
`ghl_gllight_local_viewer` "1" 1, `ghl_gllight_directional` "0" 1, `ghl_gllight_attenuation` "1" 1,
`ghl_gllight_max_lights` "6" 1, `ghl_no_cull` "0" 0, `ghl_pre_cull` "1" 0, `ghl_no_texture_stack` "0" 1,
`ghl_light_multiply` "1.3" 1, `ghl_precache_verts` "0" 0, `ghl_precache_texture` "1" 0, `ghl_mip` "0" 1;
game — also `ghl_no_server_update` "0" 0 (and the same defaults for those it shares). The game clamps `ghl_mip` to
0–2 before calling method 38; the renderer only rounds it.

Debug console command (engine): `gserver <subcommand> [argument]` with subcommands RELOAD, SIZEMATTERS, IMAGELIST,
USEAGE/USAGE, STATS, LEAKAGE, MEM_START, MEM_END, TEST, LIGHTSMOOTH/LIGHT_SMOOTH, LIGHTAMBIENT/LIGHT_AMBIENT,
PRECACHEALL/PRECACHE_ALL, AUTOPREP/AUTO_PREP, CACHEGHB/CACHE_GHB, FLUSHMESHCACHE/FLUSH_MESH_CACHE (prints
"Mesh Cache Flushed"), SPEED, LISTFILES/LIST_FILES (prints "%d total ghoul files have been loaded."), SUMMARY. Not
part of the module contract.

## Test vectors / how to verify
1. Stub host for gamex86.dll (SPEC-001 test 1) extended with a logging Ghoul system object of 49 methods: during
   `GetGameAPI` only method 38 is called, with 0 when `ghl_mip` = 0 and 2 when `ghl_mip` = 7.
2. Same host over one level: the game calls only system methods 0, 4, 5, 6, 22, 38; method 4 and 5 alternate once
   per `RunFrame`.
3. Renderer: load the original `ref_gl.dll` with a logging system object: `Init` calls 38, 39, 37, 1, 2 (each once);
   method 1 receives 52 non-NULL pointers; `vid_restart` produces method 3 before the next `Init`.
4. Ids: create three objects, destroy the second, create one more → its id equals the destroyed one (method 25).
5. Names: register `ghoul/x/y.ghl` and look up `GHOUL\X\Y.GHL` with object method 3 → same id; method 5 returns
   `GHOUL/X/Y.GHL`.
6. Import #63 with the vector above.
7. Player model: load a shipped `.gpm` (e.g. a Meso player model) and compare methods 3–16 against the file's tokens;
   method 14 with index 0 and with index count+1 returns false.
8. Precache enumeration: object method 14 with the note id of `sound` on a weapon model calls the callback once per
   `sound` note key with arguments (0, 0, 0.0, "<wave name>").

## Open questions
- Object method 0, second argument: effect not observed (no caller sets it).
- Whether register methods (6, 15, 19) that return 0 still consume an id.
- Lazily built lookup tables: confirm whether a name registered after the first lookup is findable.
- What fills the "lights from models" list read by methods 28/29 (no writer found statically). Verify by loading a
  model that contains light nodes and watching the renderer's dynamic lights.
- Statistic 44 meaning, statistic 41 unit.
- Exact effect of the specular flag (object method 2, system method 2) on drawing.
- Player-model final token and user value (methods 1, 2, 11): no user found.
