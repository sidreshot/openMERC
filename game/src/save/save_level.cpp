// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - the level section of a savegame (exports 0x18 WriteLevel / 0x1C ReadLevel)
// and the game section (0x10 WriteGame / 0x14 ReadGame), the save limit (0x40 SaveAllowed, 0x44 SavesLeft), and a
// memory round-trip self-test.
//
// Level section, in the order of SPEC-752 1.2:
//   1 FTSZ  2 LVLC + STRG  3 NMED, per entity in use EDNM + EDCT + STRG  4-5 script chunks (G6b)
//   6 AI block (G2: Ghoul tables of G9, bodies of G3, vehicle chunks of G4)  7 OINM (G9)  8 PLNM, PLPI x n, PLWA
// followed by OpenMerc's tail: OMCN + OMCL per client slot (the private part of the client records), OMXN and per
// registered extension OMXI + its chunks (save_api.h).
//
// FTSZ: the original writes the byte sizes of its nine function tables, and a module whose tables differ refuses the
// level with `Function index tables have changed size`. OpenMerc saves callbacks as offsets in its own image
// (SPEC-752 2.1 allows own tables for own saves), so its FTSZ holds 'OMG1', a CRC-32 of its code section, the image
// size, the code size, the sizes of the entity, client and level records and of the entity image, and the number of
// extension records: a module with other code - or the original module - stops with that error instead of reading
// foreign offsets (save_image.cpp).
//
// Implements: SPEC-752 1.1, 1.2, 2.3; SPEC-781 "Savegame chunks"; SPEC-303 Behaviour 1 (game module refusal);
//             SPEC-001 exports 0x10-0x1C, 0x40, 0x44

#include "save_local.h"

#include "../ai/ai_hooks.h"
#include "../damage/damage_api.h"
#include "../ghoulglue/gg_ghoul.h"
#include "../scriptglue/g_scriptglue.h"

// G18's item list (SPEC-781 PLNM/PLPI). Requested from G18 (docs/impl/G10.md "Needs from G18"); until they exist
// the fallbacks in save_fallbacks.cpp answer -1 / do nothing and the list is taken from the entity flags.
extern "C" {
int ItemList_Count(void);
edict_t *ItemList_Get(int i);
void ItemList_Clear(void);
void ItemList_Add(edict_t *ent);
}

extern G10Extension *g10_extensions;

namespace g10 {

static cvar_t *g10_savetest;
static bool selftest_running;

// ---------------------------------------------------------------------------------------------------------------
// FTSZ

static void Fingerprint(int32_t out[9])
{
	const ImageInfo &im = Image();
	int nx = 0;
	for (G10Extension *x = g10_extensions; x; x = x->next)
		nx++;
	out[0] = 0x4F4D4731; // 'OMG1'
	out[1] = (int32_t)im.timestamp;
	out[2] = (int32_t)im.size;
	out[3] = (int32_t)im.checksum;
	out[4] = (int32_t)sizeof(edict_t);
	out[5] = (int32_t)sizeof(gclient_t);
	out[6] = (int32_t)sizeof(level_locals_t);
	out[7] = EntityImageSize();
	out[8] = nx;
}

// ---------------------------------------------------------------------------------------------------------------
// items (SPEC-781 "Savegame chunks")

static int CollectItems(edict_t **list, int max)
{
	int n = ItemList_Count();
	if (n >= 0) {
		int k = 0;
		for (int i = 0; i < n && k < max; i++) {
			edict_t *e = ItemList_Get(i);
			if (e)
				list[k++] = e;
		}
		return k;
	}
	// without G18's accessor: the entities flagged as registered in the item list, in entity order
	int k = 0;
	for (int i = 0; i < globals.num_edicts && k < max; i++)
		if (g_edicts[i].inuse && (g_edicts[i].flags & FL_ITEMLIST))
			list[k++] = &g_edicts[i];
	return k;
}

static void WriteItems(void)
{
	static edict_t *list[MAX_EDICTS];
	int n = CollectItems(list, MAX_EDICTS);
	G10_WriteInt("PLNM", n);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		int32_t r[4] = {0, e->item ? ITEM_INDEX(e->item) : 0, G_EdictNum(e), e->itemstate};
		G10_WriteChunk("PLPI", r, 16);
	}
	unsigned char wa = 0; // SPEC-781: meaning UNVERIFIED, never changed in 1.07f play
	G10_WriteChunk("PLWA", &wa, 1);
}

static void ReadItems(void)
{
	int n = G10_ReadInt("PLNM");
	ItemList_Clear();
	for (int i = 0; i < n; i++) {
		int32_t r[4] = {0, 0, 0, 0};
		G10_ReadChunk("PLPI", r, 16);
		edict_t *e = G10_DecodeEnt(r[2]);
		if (e && e->inuse) {
			e->itemstate = r[3];
			ItemList_Add(e);
		}
	}
	unsigned char wa = 0;
	G10_ReadChunk("PLWA", &wa, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// write

static void WriteEntities(void)
{
	int n = 0;
	for (int i = 0; i < globals.num_edicts; i++)
		if (g_edicts[i].inuse)
			n++;
	G10_WriteInt("NMED", n);
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse)
			continue;
		G10_WriteInt("EDNM", i);
		// SPEC-752 3.4: the record is copied while the entity is unlinked, and the entity is linked at once - every
		// entity in use, also one that was never linked (the original's saves link them: after a save their bounds
		// and engine link values are those of a linked entity; docs/impl/G10.md). The self-test leaves the world
		// alone so that its two images can be compared.
		if (!selftest_running && e->area_prev)
			gi.unlinkentity(e);
		WriteEntity(e);
		if (!selftest_running)
			gi.linkentity(e);
	}
}

static void WriteTail(void)
{
	G10_WriteInt("OMCN", game.maxclients);
	for (int i = 0; i < game.maxclients; i++)
		WriteClientLevel(&game.clients[i]);
	int nx = 0;
	for (G10Extension *x = g10_extensions; x; x = x->next)
		nx++;
	G10_WriteInt("OMXN", nx);
	for (G10Extension *x = g10_extensions; x; x = x->next) {
		G10_WriteChunk("OMXI", x->id, 4);
		if (x->write)
			x->write();
	}
}

void WriteLevelSection(void)
{
	int32_t ft[9];
	Fingerprint(ft);
	G10_WriteChunk("FTSZ", ft, 36);
	WriteLevelRecord();
	WriteEntities();
	ScriptGlue_WriteLevel();   // SPEC-752 1.2 steps 4-5 (G6b)
	om_ai::WriteLevelAI();     // step 6 (G2; G9 Ghoul tables, G3 bodies, G4 vehicles)
	GG_WriteAttached();        // step 7 (G9)
	WriteItems();              // step 8
	WriteTail();               // OpenMerc
}

// ---------------------------------------------------------------------------------------------------------------
// read

static void ClearLevelObjects(void)
{
	// the objects other packages keep for the entities of the spawned level (SPEC-752 1.2: "all level-tagged memory
	// is freed, the Ghoul id map is cleared, the item system is reset"). Level-tagged memory itself is not freed:
	// package tables made at the level spawn (G7 ambient sets, G4 roster records) stay valid, and the entity strings
	// of the spawn are freed with the level.
	om_ai::LevelStart();  // AI objects released (G2)
	Body_ReleaseAll();    // bodies (G3)
	GG_FreeAll();         // Ghoul glue records, instances, bolt-ons, attached-object records, id map (G9)
	// GG_FreeAll destroys every object of the server Ghoul system, also the player module's cached in-view models
	// (as at the level spawn, g_spawn.cpp): they are forgotten here and precached again after the read
	PlayerModuleForgetViewModels();
	for (int i = 0; i < globals.num_edicts; i++)
		if (g_edicts[i].flags & FL_ITEMLIST)
			ItemRemoveFromList(&g_edicts[i]);
	ItemList_Clear();
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && e->area_prev)
			gi.unlinkentity(e);
	}
	memset(g_edicts, 0, game.maxentities * sizeof(g_edicts[0]));
	globals.num_edicts = game.maxclients + 1;
	for (int i = 0; i < game.maxclients; i++)
		g_edicts[i + 1].client = game.clients + i;
}

static void ReadEntities(void)
{
	int n = G10_ReadInt("NMED");
	for (int k = 0; k < n; k++) {
		int num = G10_ReadInt("EDNM");
		if (num < 0 || num >= game.maxentities)
			gi.error("ReadLevel: bad entity number %d", num);
		edict_t *e = &g_edicts[num];
		ReadEntity(e);
		if (num + 1 > globals.num_edicts)
			globals.num_edicts = num + 1;
		gi.linkentity(e); // SPEC-752 1.2: the entity is linked into the world
	}
}

static void ReadTail(void)
{
	int nc = G10_ReadInt("OMCN");
	for (int i = 0; i < nc; i++) {
		if (i < game.maxclients)
			ReadClientLevel(&game.clients[i]);
		else
			G10_ReadChunk("OMCL", NULL, 0);
	}
	int nx = G10_ReadInt("OMXN");
	G10Extension *x = g10_extensions;
	for (int i = 0; i < nx; i++, x = x ? x->next : NULL) {
		char id[5] = {0, 0, 0, 0, 0};
		G10_ReadChunk("OMXI", id, 4);
		if (!x || strncmp(x->id, id, 4))
			gi.error("ReadLevel: savegame extension %s not known by this module", id);
		if (x->read)
			x->read();
	}
}

// G4's per-character records are not saved (Needs from G4): a restored character takes the record its slot got
// at the level spawn when that entity is the same map entity (same classname)
struct KeepRoster {
	decltype(((edict_t *)0)->roster) roster;
	char classname[64];
};
static KeepRoster keep[MAX_EDICTS];

static void KeepBefore(void)
{
	for (int i = 0; i < MAX_EDICTS; i++) {
		keep[i].roster = NULL;
		keep[i].classname[0] = 0;
		if (i < globals.num_edicts && g_edicts[i].inuse && g_edicts[i].roster) {
			keep[i].roster = g_edicts[i].roster;
			strncpy(keep[i].classname, g_edicts[i].classname ? g_edicts[i].classname : "",
			        sizeof(keep[i].classname) - 1);
		}
	}
}

static void KeepAfter(void)
{
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && keep[i].roster && e->classname && !strcmp(e->classname, keep[i].classname))
			e->roster = keep[i].roster;
	}
}

void ReadLevelSection(void)
{
	int32_t ft[9], mine[9];
	G10_ReadChunk("FTSZ", ft, 36);
	Fingerprint(mine);
	if (memcmp(ft, mine, sizeof(ft)))
		gi.error("Function index tables have changed size"); // SPEC-752 1.2 (a save of another module or build)
	KeepBefore();
	ClearLevelObjects();
	ReadLevelRecord();
	ReadEntities();
	ScriptGlue_ReadLevel();
	om_ai::ReadLevelAI();
	GG_ReadAttached();
	ReadItems();
	ReadTail();
	KeepAfter();
	// SPEC-752 1.2, after the item chunks: client entities re-attached to their records, the client field 0x2F8
	// (persistent "connected") cleared, the Ghoul id remap of SPEC-752 5
	for (int i = 0; i < game.maxclients; i++) {
		g_edicts[i + 1].client = game.clients + i;
		game.clients[i].pers.connected = false;
	}
	GG_RemapEntityIds();
	// G3 bodies look their hand bolts up again lazily after a load (g3_human.cpp SaveFields), but Fire uses the right
	// hand bolt without the lookup, so a loaded character never fired (kos1: "fire ... -> 0" every think). The
	// public query does the lookup (Needs from G3 in docs/impl/G10.md)
	for (int i = 0; i < globals.num_edicts; i++)
		if (g_edicts[i].inuse && g_edicts[i].g3body)
			g_edicts[i].g3body->HandBolt(0);
	// the player module's in-view models are precached again (SPEC-760 B1.4 level start, g_player.cpp), and the
	// inventories get their owner context again (they were made by ReadGame before the entities existed)
	PlayerModuleLevelStart();
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (e->inuse && e->client && e->client->inv)
			PlayerSetIdleContext(e);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// self-test: write the level into memory, read it back into the running game, write it again and compare. Run by
// `set g10_savetest 1` before a `save` (after the real level chunks are written, so the save file is complete).
// The server stops afterwards (see the end of SelfTest); continuing after a real load is tested with cont.cfg.

static int ChunkCount(const std::vector<unsigned char> &v)
{
	int n = 0;
	for (size_t p = 0; p + 8 <= v.size(); n++) {
		int32_t h[2];
		memcpy(h, &v[p], 8);
		p += 8 + h[1];
	}
	return n;
}

void SelfTest(void)
{
	std::vector<unsigned char> a, b;
	selftest_running = true;
	MemoryBegin(&a, NULL);
	WriteLevelSection();
	MemoryEnd();
	int ents_before = 0;
	for (int i = 0; i < globals.num_edicts; i++)
		ents_before += g_edicts[i].inuse ? 1 : 0;
	g_loading = 1;
	MemoryBegin(NULL, &a);
	ReadLevelSection();
	MemoryEnd();
	g_loading = 0;
	MemoryBegin(&b, NULL);
	WriteLevelSection();
	MemoryEnd();
	selftest_running = false;
	int ents_after = 0;
	for (int i = 0; i < globals.num_edicts; i++)
		ents_after += g_edicts[i].inuse ? 1 : 0;
	// chunk-by-chunk comparison; the AI ids come from a counter that never goes back (SPEC-700), so the AI block
	// is compared by chunk ids and sizes only
	size_t pa = 0, pb = 0;
	int nchunks = 0, diffs = 0, sizediffs = 0, aidiffs = 0;
	char first[64] = "";
	bool inai = false;
	while (pa + 8 <= a.size() && pb + 8 <= b.size()) {
		int32_t ha[2], hb[2];
		memcpy(ha, &a[pa], 8);
		memcpy(hb, &b[pb], 8);
		char ida[5] = {(char)(ha[0] >> 24), (char)(ha[0] >> 16), (char)(ha[0] >> 8), (char)ha[0], 0};
		if (!strcmp(ida, "AICT"))
			inai = true;
		if (!strcmp(ida, "OINM"))
			inai = false;
		nchunks++;
		if (ha[0] != hb[0] || ha[1] != hb[1]) {
			sizediffs++;
			if (!first[0])
				Com_sprintf(first, sizeof(first), "%s #%d (size %d/%d)", ida, nchunks, ha[1], hb[1]);
			break;
		}
		bool differ = memcmp(&a[pa + 8], &b[pb + 8], ha[1]) != 0;
		if (differ && !strcmp(ida, "EDCT")) {
			// the read links every entity (SPEC-752 1.2): link count, bounds and the engine's link values change;
			// the rest of the record must be equal
			std::vector<unsigned char> x(&a[pa + 8], &a[pa + 8] + ha[1]), y(&b[pb + 8], &b[pb + 8] + hb[1]);
			int lc = (int)offsetof(edict_t, linkcount), b0 = (int)offsetof(edict_t, absmin),
			    b1 = (int)offsetof(edict_t, solid), e0 = (int)offsetof(edict_t, engine_a),
			    e1 = (int)offsetof(edict_t, movetype);
			int ranges[3][2] = {{lc, lc + 4}, {b0, b1}, {e0, e1}};
			for (auto &r : ranges)
				for (int o = r[0]; o < r[1] && o < (int)x.size(); o++)
					x[o] = y[o] = 0;
			differ = x != y;
		}
		if (differ) {
			if (inai)
				aidiffs++;
			else {
				diffs++;
				if (!first[0])
					Com_sprintf(first, sizeof(first), "%s #%d", ida, nchunks);
			}
		}
		pa += 8 + ha[1];
		pb += 8 + hb[1];
	}
	gi.cprintf(NULL, PRINT_HIGH,
	           "g10 savetest: %d entities saved, %d restored; image %d / %d bytes, %d / %d chunks; %d chunks differ "
	           "(%d in the AI block), sequence %s%s\n",
	           ents_before, ents_after, (int)a.size(), (int)b.size(), ChunkCount(a), ChunkCount(b), diffs + aidiffs,
	           aidiffs, sizediffs ? "DIFFERS at " : "equal", first);
	gi.dprintf("g10 savetest: first content difference %s\n", first[0] ? first : "none");
	// The read destroyed every object of the server Ghoul system, also the player module's view-weapon objects that
	// the clients' live inventories still use (a real load makes new inventories before the level is spawned). The
	// level is not continued after the test: the server is stopped once the save command has finished.
	gi.cprintf(NULL, PRINT_HIGH, "g10 savetest: the level cannot continue after the test, stopping the server\n");
	for (int i = 0; i < game.maxclients; i++)
		game.clients[i].inv = NULL; // the inventories are left alone (their Ghoul objects are gone) until then
	gi.AddCommandString("killserver\n");
}

} // namespace g10

using namespace g10;

// ---------------------------------------------------------------------------------------------------------------
// exports

void G10_WriteGame(qboolean autosave)
{
	g_warnings = 0;
	// SPEC-752 1.1 step 1: a normal save refreshes the persistent data from the entities first
	if (!autosave)
		SaveClientData();
	WriteGameRecord(autosave != 0);
	for (int i = 0; i < game.maxclients; i++)
		WriteClientGame(&game.clients[i]);
}

qboolean G10_ReadGame(int unused)
{
	(void)unused;
	g_loading = 1;
	// SPEC-752 1.1: the value designated through import 67 (the player module's context flag) is set to 0
	if (gix.playercontext && *gix.playercontext)
		*(int *)*gix.playercontext = 0;
	bool autosave = ReadGameRecord();
	for (int i = 0; i < game.maxclients; i++)
		ReadClientGame(&game.clients[i]);
	g_loading = 0;
	return autosave;
}

void G10_WriteLevel(void)
{
	if (!g10_savetest)
		g10_savetest = gi.cvar("g10_savetest", "0", 0);
	WriteLevelSection();
	if (g_warnings)
		gi.dprintf("G10: %d pointers could not be encoded\n", g_warnings);
	if (g10_savetest && g10_savetest->value)
		SelfTest();
}

void G10_ReadLevel(void)
{
	g_loading = 1;
	ReadLevelSection();
	g_loading = 0;
}

// The save limit is per level (black box, docs/impl/G10.md): every level starts with the game's "saves allowed"
// (SPEC-760 B9.2, = sk_saving, -1 unlimited; GAME 0x688 keeps that value), and the count left in the level is part of
// the level record (the original's LVLC 0x4EC goes 1, 0 over two saves at skill 3 while GAME 0x688 stays 2; loading
// the first save gives the count 1 back).
int G10_SavesLeft(void)
{
	if (!level.g10_savesinit) {
		level.g10_savesinit = 1;
		level.g10_savesleft = game.saves_allowed;
	}
	return level.g10_savesleft;
}

// SPEC-001 export 0x40: a manual save consumes one of the level's saves. Messages (`singleplr` string package,
// matching the original's black-box output at skills 2 and 3): n >= 2 left after this save SAVES_N_LEFT with n,
// 1 left SAVES_ONE_LEFT, 0 left SAVES_USED_UP; none left before the save SAVES_NO_MORE and the save is refused.
qboolean G10_SaveAllowed(void)
{
	edict_t *p = G_FirstPlayer();
	int left = G10_SavesLeft();
	if (left < 0)
		return true;
	if (left == 0) {
		SP_PrintTo(p, "singleplr", "SAVES_NO_MORE");
		return false;
	}
	left = --level.g10_savesleft;
	Stats_Count(MSTAT_SAVES, 1); // SPEC-AMEND-007 G18.41: each save the limit allows
	if (left == 0)
		SP_PrintTo(p, "singleplr", "SAVES_USED_UP");
	else if (left == 1)
		SP_PrintTo(p, "singleplr", "SAVES_ONE_LEFT");
	else {
		int id = SP_Id("singleplr", "SAVES_N_LEFT");
		if (id && p)
			gix.SP_Print(p, id, left);
	}
	return true;
}
