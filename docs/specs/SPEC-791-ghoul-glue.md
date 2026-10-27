# SPEC-791: Game-side Ghoul glue — model records, object instances, bolt-ons, name helpers, attached objects
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-001 (game imports #60–#66), SPEC-010 (Ghoul system, model object, event callback), SPEC-011
(instance methods), SPEC-014 (Ghoul file semantics), SPEC-202 (entity state, renderfx), SPEC-303 (savegame chunk
format), SPEC-700 (AI object ids and the order of the AI/Ghoul save block), SPEC-752 (level save order, `GHLI`, Ghoul
id remap, entity and function references), SPEC-770 (props: model tables and the prop behaviours built on these
helpers), SPEC-722 (character bodies).

## Purpose

The game module never talks to the Ghoul system directly when it shows a model on an entity. It goes through a small
layer of its own that (1) keeps one *model record* per distinct model (one engine model object each), (2) wraps every
model instance it shows on an entity (*object instance*) and every model hung onto a bolt of another model
(*bolt-on*), (3) offers helpers that work with names (skins, parts, sequences, bolts), (4) keeps, for map objects,
an *attached-object record* with up to six tracked bolt-on parts, and (5) saves and restores all of this in
savegames. This spec gives the observable rules of that layer so that a reimplemented game produces the same models,
skins, parts, sequences and bolt-ons on the same entities, prints the same diagnostics, and reads and writes the same
savegame chunks. The prop behaviours that only use these helpers (box rotation, breaking, pushing, knocking over,
explosions, shootable parts, falling over, riders) are specified in SPEC-770; §12 maps them.

Terms: "object method n" and "instance method n" are the Ghoul interface methods of SPEC-010 B and SPEC-011.
"Developer print" = the game's developer-only console print (SPEC-001 import dprintf); "console print" = always
shown. Time = level time in seconds. Names are compared case-insensitively unless stated.

## Interface / layout

### 1. Model record
One record per distinct key. Key = (directory, list name, skin name, base name); all four compared
case-insensitively; a missing skin or base name equals the empty string. The fields that matter for behaviour and
savegames are listed below.

| Field | Meaning |
|---|---|
| directory | folder below `ghoul/`, e.g. `enemy/meso`, `objects/generic/bench`, `Enemy/bolt` (up to 99 characters) |
| list name | name of the sequence list `<list>.gsq` in that folder (import #65); also the name of the model's *default sequence* `ghoul/<directory>/<list>.ghl` |
| skin name | when non-empty the record was created for that one skin (see Behaviour 2) |
| base name | optional extra sequence `ghoul/<directory>/<base>.ghl` registered before the list |
| all-skins flag | 1: every skin found in the model files is registered; 0: only explicitly registered skins |
| prepared flag | 1 once object method 2 ("prepare for drawing") has been called for the record |
| skin pairs | (material name, skin name) pairs registered while not yet prepared (kept for savegames) |
| availability | 1024 bytes, one per record of the human animation table (SPEC-722, 539 records used): see Behaviour 4 |
| instances | the object instances of this model (map from engine instance to object instance) |

### 2. Object instance
Created for one entity (or for none, see Behaviour 3): engine instance (instance of the record's object), owning
entity, record, current sequence id (the last sequence started through the layer), list of direct bolt-ons. The
entity's Ghoul-instance field points to the engine instance and the engine instance's user value (instance method 21)
is the entity.

### 3. Bolt-on
Engine instance of a *child* record hung on a bolt of a *parent* (either an object instance — "direct" bolt-on — or
another bolt-on): parent bolt id (a null part of the parent model, SPEC-010), child bolt id (the child's own null part
aligned to it), child record, uniform scale, current sequence id, list of its own bolt-ons. Its user value is the
entity that owns the whole tree.

### 4. Attached-object record (map objects)
Created by the standard prop setup (SPEC-770 §3) and by `worldspawn`. Holds: the owning entity; a list of tracked
bolt-on parts (index 1…n, attach order, at most 6 counted, see Behaviour 9.4); a 32-bit "shown" bit mask (bit i−1 =
part i shown); a link to another entity (chain used by props assembled from several entities); and the fall-over state
of SPEC-770 §5.8 (direction code, start time, pivot, radius, start angles, pivot point, direction vector, the think
behaviour to restore). It is freed with its entity.

### 5. Savegame chunks
All integers little-endian 32-bit unless stated; "id" = AI object id from the shared counter of SPEC-700 (0 = none);
"entity reference" and "function index" use the SPEC-752 encodings. "undefined" bytes are written without
meaning and ignored on load. Chunk ids as four letters (SPEC-303 byte order).

`GGOB` — model record, 1432 bytes:

| Offset | Size | Content |
|---|---|---|
| 0 | 4 | 0 |
| 4 | 1 | prepared flag |
| 5 | 1 | all-skins flag |
| 6 | 1024 | availability bytes |
| 1030 | 100 | directory, NUL-terminated |
| 1130 | 100 | list name |
| 1230 | 100 | skin name ("" = none) |
| 1330 | 100 | base name ("" = none) |
| 1430 | 2 | undefined |

When the all-skins flag is 0 it is followed by `GGON` (4 bytes: the number S of strings, always even) and S `STRG`
chunks (SPEC-752 §2.2: text plus NUL; an empty string is written as a single NUL), alternately material name and skin
name of each remembered skin pair, in registration order.

`GGOI` — object instance, 16 bytes: 0 = int 0; 4 = entity reference of the owning entity; 8 = id of its model record;
12 = 16-bit current sequence id; 14 = 2 undefined bytes. Followed by the engine instance image `GHLI` (SPEC-752 §5,
SPEC-011 method 37) when the object instance has an engine instance.

`GGBI` — bolt-on, 32 bytes:

| Offset | Size | Content |
|---|---|---|
| 0 | 1 | 1 = direct (hangs on an object instance), 0 = hangs on another bolt-on |
| 1 | 3 | undefined |
| 4 | 4 | id of the parent bolt-on (0 when direct) |
| 8 | 4 | id of the owning object instance (0 when not direct) |
| 12 | 2 | parent bolt id |
| 14 | 2 | undefined |
| 16 | 4 | 0 |
| 20 | 2 | child bolt id |
| 22 | 2 | current sequence id |
| 24 | 4 | id of the child model record |
| 28 | 4 | float scale |

Followed by `GHLI` of the bolt-on's engine instance.

`EDOI` — attached-object record, 96 bytes (written once per entity that has one, after `OINM`, SPEC-752 §1.2 item 7):

| Offset | Size | Content |
|---|---|---|
| 0 | 4 | entity reference of the owning entity |
| 4 | 4 | undefined |
| 8 | 4 | 1 (part-state kind) |
| 12 | 4 | entity reference of the linked entity |
| 16 | 4 | shown-part bit mask |
| 20 | 4 | tracked part count (0…6) |
| 24 | 4 | undefined |
| 28 | 4 | 2 (fall-state kind) |
| 32 | 4 | entity reference of the fall-state subject (see Open questions) |
| 36 | 4 | fall direction code: 0 none, 1 forward, 2 backward, 3 left, 4 right (SPEC-770 §5.8) |
| 40 | 4 | float fall start time |
| 44 | 4 | float pivot height |
| 48 | 4 | float radius (distance from the pivot edge to the far top edge) |
| 52 | 4 | float angle offset (degrees) |
| 56 | 12 | float[3] angles at the start of the fall |
| 68 | 12 | float[3] pivot point |
| 80 | 12 | float[3] fall direction (unit vector) |
| 92 | 4 | function index of the think behaviour restored after the fall |

Then `EDBN` (4 bytes: number N of entries in the tracked-part list) and, only when N ≠ 0, `EDBL` (N × 4 bytes: the
id of each part's bolt-on, 0 = empty slot).

## Behaviour

### 1. Finding and creating model records
- **Find or create** (directory, list, all-skins, skin, base, find-only): the records are searched in creation order
  for an equal key (Interface 1). Found → that record (its all-skins flag is not changed). Not found and find-only →
  none. Otherwise a new record is created:
  1. a new engine model object (Ghoul system method 0);
  2. base name non-empty → register the sequence `ghoul/<directory>/<base>.ghl` (object method 0);
  3. import #65 (directory, list name, object): registers `<list>.gsq` or, without such a list, the single sequence
     `ghoul/<directory>/<list>.GHL` (SPEC-010);
  4. all-skins = 1 → object method 1 with 1 (all skins), then the record is prepared (Behaviour 3); all-skins = 0 →
     object method 1 with 0 (no skins yet);
  5. if no engine object could be made the record is discarded and the result is none;
  6. availability bytes are computed (Behaviour 4); the record is added to the record list and gets an AI object id
     the next time ids are handed out (SPEC-700).
- Callers pass all-skins = 1 whenever they do not name a skin. Shipped uses with a skin name: prop models with a skin
  key (Behaviour 8), bolt-ons with a skin (Behaviour 9.4), character and player models with face/body skins
  (SPEC-722, SPEC-780).
- **Find by engine object**: the record whose engine object is the given one (used to go from an entity's instance
  back to its record).
- **Find object instance of an engine instance**: instance → its object (instance method 34) → record → the object
  instance registered for that engine instance; none when any step fails.

### 2. Skins
- **Register skin (material, skin)** on a record: material id = object method 16 (material name); 0 → result 0. If
  the record is already prepared the result is object method 20 (find skin) only — no new skin is loaded. Otherwise
  the pair is remembered (for savegames) and object method 19 registers the skin; its id is the result.
- **Register skin by name on any material**: only while not prepared; for material 1, 2, … (object method 17 = count)
  the skin is registered (method 19) and the pair (material name from method 18, skin) remembered; the first non-zero
  id is the result (0 = none provided it).
- **Set skin by names on an instance** (instance, directory, file, material name, skin name — directory and file are
  only used in messages): no instance → console print `ERROR: trying to set skin %s on NULL ghoulInst!\n` (skin).
  Material = object method 16, skin = object method 20 (material, skin); both found → instance method 11 (material,
  skin, whole model). Otherwise console print: material not found → `WARNING: Can't find material:%s\n` (material);
  skin not found → `WARNING: Can't find skin:%s\n` (skin); any other case → `WARNING: unknown error in SetSkin2 on
  object %s/%s.\n` (directory, file).
- **Set skin by names on an entity** (entity, directory, file, material, skin, also-parts): no entity → console print
  `WARNING: Can't set skin for NULL edict\n`; else the rule above on the entity's instance and, when also-parts is set
  and the entity has an attached-object record, on every tracked part's instance (parts 1…count).

### 3. Preparing a record; sequences
- **Prepare** (once per record, only when it has an engine object and is not prepared): object method 2 with
  specular = (`ghl_specular` > 0 and `ghl_light_method` > 0). While doing so the layer creates one temporary object
  instance without an entity and destroys it afterwards (an instance id is used and freed, UNVERIFIED whether this is
  observable). The prepared flag is then 1. Records with all-skins = 1 are prepared at creation; records for one
  skin are prepared by the first user that needs them (Behaviour 8, 9.4).
- **Find sequence (record, name)**: no record → 0. The path is `ghoul/<directory>/<name>.ghl` (object method 3). If
  the record is not prepared yet: developer print `Tried to find sequence %s before registration finished!\n` (name)
  and result 0.

### 4. Sequence availability (human models)
For records whose directory begins with `enemy/meso`, `enemy/ecto` or `enemy/female` (case-sensitive prefix test),
availability byte i = 1 when the model has the sequence `ghoul/<directory>/<name i>.ghl` of human animation record i
(SPEC-722 table, 539 records), else 0; for every other record all 1024 bytes are 1. The bytes are a cache: results equal
checking the sequence files (SPEC-722 "Has animation"). They are saved in `GGOB`.

### 5. Level lifecycle
- Whenever the game frees all entities (level spawn and level load), every object instance of every record is
  destroyed (with its bolt-ons), then every record (destroying its engine model object, object method 27), and
  finally Ghoul system method 6 is called (all engine model objects of the server are destroyed).
- Level spawn, right after that and before map entities spawn (at loading progress 0.05): the following records are
  created with all-skins = 1 in directory `Enemy/bolt` (gore pieces used by SPEC-721): `g_arm_torn_anim1`,
  `g_bone_anim1`, `g_brain`, `g_cap1`, `g_cap2`, `g_cap3`, `g_exit_chunky_anim1`, `guts_bolton`, `g_exit_lrg`,
  `g_exit_smll`, `g_guts`, `g_guts3`, `g_neck_torn_anim2` (13 models).
- At process exit all records are destroyed.

### 6. Object instances
- **New object instance (record, entity)**: a new engine instance (object method 24); if an entity is given, its
  Ghoul-instance field := the instance and the instance's user value := the entity; then import #66 with
  (directory, `<list>_poff`, object, instance) hides the parts listed in `ghoul/<directory>/<list>_poff.gsq` (missing
  list: nothing). Registered in the record and in the AI id registry. No engine instance → nothing is created.
- **Clone (record, entity, source instance)**: as above but the engine instance is a copy of the source instance's
  state without its children (instance method 36 with 0); no parts-off list. Used when a severed body piece becomes
  its own entity (SPEC-721).
- **Move to another entity**: every bolt-on's user value (recursively) becomes the new entity; the old entity's
  Ghoul-instance field is cleared; the new entity's field := the instance and the instance's user value := it. Used
  when a character's model is handed to another entity (SPEC-700 / SPEC-722).
- **Play (object instance, sequence, time, offset, restart, mode, match-phase, reverse)**: remembers the sequence id
  as current and calls instance method 17 with the same arguments (nothing when the id is 0).
- **Remove all object instances of an entity**: for every record, every object instance whose owning entity is the
  entity is destroyed (only when the entity has a Ghoul instance); then the entity's Ghoul-instance field is cleared.
  Used when an entity is freed, a client disconnects, an item is picked up, a body is replaced, and so on.
- **Destroying** an object instance destroys its bolt-ons (Behaviour 7), clears the owning entity's Ghoul-instance
  field and destroys the engine instance (instance method 53).

### 7. Bolt-ons
- **Add bolt-on (parent, parent bolt, child record, child bolt, skin name, scale)** — parent is an object instance or
  a bolt-on:
  1. a new engine instance of the child record's object (object method 24); user value := the entity owning the tree;
  2. the parent's engine instance attaches it: instance method 28 (parent bolt, child, child bolt);
  3. scale outside [0.99999, 1.00001]: the child's transform is multiplied by a uniform scale matrix of that factor;
  4. skin name given: material = the child model's material named like the child record's list name (object method
     16) or, when there is none, the first material (1, 2, …) that has a skin of that name (object method 20); then
     instance method 11 (material, skin, whole model) when both ids are non-zero;
  5. the bolt-on is added to the parent's list and the AI id registry; then, if the child record has an engine object
     and is not prepared, it is prepared (Behaviour 3).
  Missing parent, child record, parent bolt or child bolt (id 0) → no bolt-on.
- **Checked variants** used by character bodies: with no child record or any of parent bolt, child bolt (and, for the
  playing variant, sequence) equal to 0 → developer print `Unable to add bolt on!\n` and none. The playing variant
  then plays the sequence on the new bolt-on: loop mode (0) from the current time, offset 0, restart.
- **Play** on a bolt-on: as for object instances (current sequence remembered, instance method 17).
- **Find** on an object instance: the first direct bolt-on whose parent bolt equals a given bolt id; or the direct
  bolt-on whose engine instance is a given instance.
- **Remove** on an object instance: one given direct bolt-on; all direct bolt-ons on a given parent bolt; all direct
  bolt-ons.
- **Pause all**: instance method 19 at the current time on every direct bolt-on (not on deeper levels).
- **Destroying** a bolt-on destroys its own bolt-ons first, detaches it from its parent's engine instance (instance
  method 31) and destroys its engine instance (instance method 53).
- **Owning entity of a bolt-on**: follow parents up to the direct bolt-on; the owner object instance's entity.

### 8. Simple model on an entity
**Set simple model (entity, directory, file, skin, material)** — the standard way map objects, items and effects get
a model:
1. Nothing happens if the entity already has a Ghoul instance or its renderfx already has bit 0x400000.
2. Record: with both a skin and a material name (non-empty): find-or-create (directory, file, all-skins 0, skin, no
   base) and register skin (material, skin); otherwise find-or-create (directory, file, all-skins 1). None →
   developer print `SetSimpleGhoulModel couldn't create ghoul object %s/%s.\n` (directory, file), stop.
3. The record is prepared if needed.
4. Sequence = find sequence (record, file) (§3). 0 → developer print `SetSimpleGhoulModel couldn't find ghoul
   sequence %s.\n` (file), stop.
5. New object instance for the entity; failure → developer print `SetSimpleGhoulModel couldn't add instance of ghoul
   object %s.\n` (directory), stop.
6. The entity's renderfx is **set to** 0x400000 (all other renderfx bits cleared), its Ghoul-instance field := the
   instance, current sequence := the sequence, and the sequence plays in loop mode (0) from the current time, offset
   0, restart.

### 9. Name helpers
All take an entity (using its Ghoul instance) or an engine instance; they do nothing when there is none.
1. **Hide part by name**: the part id (object method 7) of the instance's model; found → instance method 8 (part, 0);
   not found → developer print `ERROR: SimpleModelRemoveObject - can find part %s!\n` (part name; spelling as
   shipped). **Show part by name**: the same with 1 and no message.
2. **Show only one part**: when the part exists: hide every mesh part (instance method 10 with 0), then show that
   part; otherwise nothing changes.
3. **Show/hide all mesh parts** of one instance (instance method 10). **Hide all, recursively**: instance method 10
   with 0 on the instance and on every attached child at every depth (instance methods 32/33).
4. **Bolt a model-table entry** (SPEC-770 §2 entries: directory, file, …, skin material) onto an instance:
   (instance, parent entry, parent bolt name, child entry, child bolt name, skin name):
   - skin material = the child entry's skin material, else the parent entry's;
   - child record: with a skin name, find-or-create (child directory, child file, all-skins 0, skin) and register
     skin (skin material, skin); otherwise find-or-create with all-skins 1; then prepared;
   - the instance's own record (by its object) and the child record must exist, else developer print
     `ERROR: SimpleModelAddBolt - can't find object %s!\n` with the parent entry's file (own record missing) and/or
     the child entry's file (child record missing), and none;
   - parent bolt = part id of the parent bolt name in the instance's model; child bolt = part id of the child bolt
     name in the child model; sequence = find sequence (child record, child file). Parent bolt not found → developer
     print `ERROR: SimpleModelAddBolt - can't find bolt %s!\n` (parent bolt name); child bolt not found → the same
     message with the child bolt name (both may print); only the sequence missing → none without a message;
   - otherwise a direct bolt-on is added (§7, no skin argument, scale 1) on the instance's object instance; with a
     skin name and a skin material the skin is set on the child (material, skin); the child plays its sequence in
     **static pose** mode (3) at offset 0 from the current time.
   **Tracked prop part**: the same on the entity's instance; on success, when the entity has an attached-object record
   whose part count is below 6, the count is increased and the new part's shown bit is set; the bolt-on is always
   appended to the tracked list (so a seventh part is listed but neither counted nor shown-flagged).
   **Bolt onto a bolt-on** (parent = a bolt-on): the same rules with the parent bolt-on's model; messages
   `ERROR: ComplexModelAddBolt - can't find object %s!\n` (parent entry's file / child entry's file),
   `ERROR: ComplexModelAddBolt - can't find bolt %s!\n` (parent bolt name) and, for a missing child bolt,
   `ERROR: SimpleModelAddBolt - can't find bolt %s!\n` (child bolt name); the new bolt-on is tracked on the attached-object record of the tree's owning entity; no owning entity
   record → developer print `ERROR: ComplexModelAddBolt - can't find root edict for %s!\n` (parent bolt name).
5. **Find bolt-on on a named bolt** (entity, entry, bolt name): the direct bolt-on of the entity's object instance on
   that bolt. Messages (developer): no record → `ERROR: SimpleModelFindBolt - can't find object %s!\n`; bolt name
   not a part → `ERROR: SimpleModelFindBolt - can't find bolt %s!\n` (bolt name); no object instance →
   `ERROR: SimpleModelFindBolt - can't find instance of %s!\n` (the entry's file in the first and third).
6. **Tracked part i** (1-based): the part's engine instance, none when i is outside 1…list size or the slot is empty.
7. **Play sequence by name on an entity** (entity, name, loop): sequence = find sequence (the entity's record, name);
   found → instance method 17 from the current time, offset 0, restart, mode 0 (loop) when `loop` ≠ 0, else mode 1
   (once and hold).
   **Play sequence by name on an instance** (instance, name, code): code 0 → mode 1 (once and hold), 1 → mode 0
   (loop), 2 → mode 3 (static pose); any other code plays nothing. The result is the sequence id (0 = not found).
8. **Scale model** (entity, factor): the entity's instance transform is multiplied by a uniform scale of `factor`
   (prop classes pass values such as 1.1 and 1.3; the collision box is unchanged).

### 10. Placement helpers
- **Place relative** (source, target, forward, right, up): target origin and angles := the source's; target z += up;
  then target origin += forward × F + right × R, where F and R are the forward/right vectors of the angles
  (source pitch, source yaw-offset field, source roll) — the second component is the entity-state yaw-offset field of
  SPEC-770 §5.2, not the yaw (0 for props, so F and R are world-axis aligned; original quirk).
- **In front within a cone** (entity, point, half-angle degrees): with the entity's forward vector and the direction
  to the point both flattened to the horizontal plane and normalised, true when their dot product ≥ cos(half-angle).
  Every call also console-prints `Threshold %f  Dot %f\n` (cosine, dot) — shipped debug output.

### 11. Aim controller
Used by searchlights, turret-like props and vehicle guns to turn a model toward a target point. Parameters: the
entity, the engine instance to turn (a bolt-on, or the entity's own instance), the bolt it hangs on and its parent
instance with the parent's bolt, the target point, a pitch range and a yaw range (radians; ±4000 = unlimited), a
maximum step per update (radians; 4000 = unlimited), flags "yaw enabled", "pitch enabled", "through parents"
(default 1). Defaults: everything 0 except "through parents".
- **Turn the entity**: the desired yaw and pitch are the world direction from the entity origin to the target point
  (yaw from the horizontal components, pitch = elevation). When an enabled axis' desired angle lies outside its range
  (tolerance 0.0005) it is replaced by the nearest limit (result 3). The change of each enabled axis is limited to
  ± max step (result 2). The entity's yaw and/or pitch (degrees) are updated; disabled axes keep their value. Result 1
  when neither limit nor step clamped; result 3 also when the entity or its instance is missing or both axes are
  disabled (nothing changes then).
- **Turn a bolted part**: the same desired angles are computed in the frame of the part's bolt (bolt matrix of the
  parent including the entity transform, mode 5, through parents per flag); the part's own transform is rotated by
  the clamped yaw change about its z axis and the pitch change about its y axis (each change below 0.01 rad in
  magnitude is treated as 0); the resulting angles are kept in the controller for the caller. (UNVERIFIED sign
  conventions of the ranges; shipped searchlights use unlimited ranges.)

### 12. Prop behaviours built on these helpers (specified in SPEC-770)
| Helper | SPEC-770 |
|---|---|
| standard prop setup with a model-table entry; the same steps without a table | §3 |
| right-angle box rotation (also used by effect entities) | §3 step 5 |
| standard break (computed debris), table break, break as death behaviour | §4.1, §4.2 |
| player push and friction restore | §5.1 |
| knock over (k = 6) and tumble | §5.2 |
| throw away, throw away with spin | §5.3 |
| standard prop explosion | §5.4 |
| part table binding, shootable parts, remove part with debris | §2 (bytes 36–39), §5.6, §5.7 |
| fall over (start on the use key, per-frame rotation) | §5.8 (see Open questions) |
| carry riders | §5.9 |

One more shared death behaviour, used by `misc_generic_garbage_bag`, `misc_generic_trashcan_barrel` and
`misc_newyork_trashcan`: a paper-burst effect at the prop origin, then a soda-can debris spray (count 100) from the
origin in the direction normalize(attacker origin − prop origin) (the inflictor when there is no attacker; straight up
(0, 0, 1) when there is neither), then the table break of SPEC-770 §4.2 with the attacker.

### 13. Savegames
- Writing (inside the AI/Ghoul block of the level section, SPEC-700 part 2 order: models, object instances,
  bolt-ons): each model record writes `GGOB` [+ `GGON` + `STRG`…]; each object instance `GGOI` + `GHLI`; each bolt-on
  `GGBI` + `GHLI`. Later (`OINM`, SPEC-752 §1.2 item 7) each attached-object record writes `EDOI`, `EDBN` [+ `EDBL`].
- Reading a model record: the fields of `GGOB` are copied; a new engine object is made; the base sequence and import
  #65 are registered as in Behaviour 1; all-skins = 0 → object method 1 with 0, then `GGON` and the `STRG` pairs are
  read and each pair is registered with "register skin" (the pairs are remembered again); all-skins = 1 → object
  method 1 with 1. The prepared flag is then restored from the image (object method 2 is not called by the record;
  UNVERIFIED whether drawing data is rebuilt elsewhere, see SPEC-010 per-frame preparation).
- Reading an object instance: the entity reference and model id are resolved, the `GHLI` image is read into a new
  instance of the record's object (SPEC-752 §5 id remap), its user value := the entity, the entity's Ghoul-instance
  field := the instance; the current sequence id is restored.
- Reading a bolt-on: ids are resolved, `GHLI` is read into a new instance of the child record's object, and the
  instance is re-attached to the owner's or parent's engine instance with the saved bolts (instance method 28);
  current sequence and scale are restored (the scale is not re-applied: the transform comes from `GHLI`).
- Reading an attached-object record: `EDOI` fields are restored (entity references and the function index decoded)
  and the record is attached to its owning entity; `EDBN`/`EDBL` rebuild the tracked list from bolt-on ids (unknown
  id → empty slot).

### 14. Test entity `ghoul_model`
- Spawn: movetype 5, solid 2 (box), renderfx 0x400000; models `enemy/ecto` / `officer` and `enemy/bolt` /
  `w_briefcase` (all skins); box (−16, −16, −32)…(16, 16, 16); a new object instance for the entity plays
  `std_ilooking_n_a_a` once and hold; an end-of-sequence callback is registered on the model's `EOS` note;
  `w_briefcase` is bolted from its `to_wbolt_hand_l` to the officer's `wbolt_hand_l` and posed statically; think every
  0.1 s.
- Callback: sets a flag. Think: when the flag is set — developer print `my sequence has ended! hooha!\n`; while fewer
  than 7 sequences were played: play `std_istretch_n_a_a` (even count) or `std_istand_n_a_n` (odd count) once and
  hold, count + 1; after 7 (or when the sequence is missing): the callback is removed (developer print `getting rid
  of seq-end callback\n`) and `std_ilooking_n_a_a` loops. The flag and the count are shared by all `ghoul_model`
  entities (reset at each spawn).

## Constants
| Name | Value |
|---|---|
| Ghoul renderfx bit | 0x400000 |
| Tracked prop parts | at most 6 counted (shown bits 0–5) |
| Availability table | 1024 bytes, 539 used (human animation table) |
| Human model directory prefixes | `enemy/meso`, `enemy/ecto`, `enemy/female` |
| Sequence path | `ghoul/<directory>/<name>.ghl` |
| Parts-off list | `<list>_poff` (`.gsq`) |
| Scale tolerance | 0.99999 … 1.00001 = no scaling |
| Aim controller tolerances | limit 0.0005 rad, dead zone 0.01 rad, "unlimited" 4000 |
| Chunk sizes | `GGOB` 1432, `GGON` 4, `GGOI` 16, `GGBI` 32, `EDOI` 96, `EDBN` 4, `EDBL` 4 × N |
| Level-spawn gore models | 13 (Behaviour 5) |

## Test vectors / how to verify
1. Spawn `misc_generic_bench` in a map with `developer 1`: no `SetSimpleGhoulModel` message; the entity is sent with
   renderfx exactly 0x400000 (net capture, SPEC-202) and its default sequence loops.
2. Give a prop a `skin` key that names a missing skin: the console (not only developer output) shows
   `WARNING: Can't find skin:<name>`.
3. Two props of the same model and skin share one engine model object (Ghoul debug `gserver STATS`; UNVERIFIED output
   format); a third with a different skin adds another object.
4. Savegame of a single-player level with a character holding a weapon bolt-on: the AI block contains `GGOB` (1432)
   records, `GGOI` (16) + `GHLI` per object instance and `GGBI` (32) + `GHLI` per bolt-on; `GGOB` byte 1030 onward
   holds the model directory; records without all skins are followed by `GGON` with an even count.
5. A save with props (e.g. `misc_castle_chair`, five bolt-on parts) has after `OINM` one `EDOI` (96) per prop and, for
   the chair, `EDBN` = 5 and an `EDBL` of 20 bytes.
6. `ghoul_model` (cheat `gimme ghoul_model`, SPEC-006): the officer plays looking, then alternates stretch/stand seven
   times with `my sequence has ended! hooha!` before each change, then prints `getting rid of seq-end callback` and
   keeps looking.
7. A security camera prop (it uses the cone helper) prints `Threshold … Dot …` lines to the console while active
   (UNVERIFIED which camera state calls it).

## Open questions
- Fall over (SPEC-770 §5.8): the fall state's subject entity is never set to the prop in 1.07f (record creation stores
  "none" and the start does not set it), so the per-frame rotation finds no subject on its first step, ends at once
  and restores the saved think; the start has already removed the prop's use-key behaviour. Expected observable
  result: a `misc_generic_table1` with spawnflag 4 does not tip over when used and ignores further use presses.
  Verify in game before implementing SPEC-770 §5.8 as written.
- `worldspawn` also creates an attached-object record and stores −1 or −2 in the fall-state radius field; meaning
  unknown.
- Whether the temporary instance made while preparing a record (Behaviour 3) can be observed (instance ids).
- Whether a record loaded with the prepared flag set but without object method 2 draws and traces correctly on the
  server before the per-frame preparation of SPEC-010 runs.
- Exact sign conventions of the aim-controller ranges (only unlimited ranges seen in props; vehicle guns use their own
  values, SPEC-730).
