// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - the records of the game and level sections: GAME, CLNT, LVLC, EDCT and
// OpenMerc's OMCL (the private part of a client record).
//
// Layouts:
// - GAME (1688): the original layout of SPEC-752 3.1 for every specified field. The 92 bytes at 0x63C, left open by
//   SPEC-752 (open question 3), hold the difficulty record of SPEC-760 B9.2; the positions below were read from the
//   original's own automatic saves at skills 0, 1, 2 and 4 (black box, docs/impl/G10.md): 0x640 T-a ... 0x658 T-g,
//   0x65C encumbrance value, 0x660 S-a, 0x664 T-h, 0x668 cheats (SPEC-752), 0x66C T-i, 0x670-0x684 the six spawner
//   values (SPEC-752: "level-spawner state 0x670-0x687"), 0x688 saves allowed, 0x68C S, 0x690 T, 0x694 difficulty
//   score. 0x618-0x63B hold the mission statistics totals and played time (SPEC-AMEND-007 G18.41).
// - CLNT (1536): the interface part of the client record (player state 0-199, ping 200; SPEC-780 1) and the
//   persistent data the next level needs, at the offsets the original uses (userinfo 204, name 716, best-weapon
//   string 736, remembered weapon id 752, field of view 756 as int, connected 760 = 0x2F8 (SPEC-752 1.2), health
//   764, maximum health 768: read from the original's saves), inventory presence 844 and second-object presence 852
//   (SPEC-752 3.2). The
//   view-weapon instance (player state 0x6C, SPEC-752 "108 cleared") and the body instance (0x7C) are 0. Everything
//   else of OpenMerc's client is in OMCL.
// - OMCL (OpenMerc, level section): OpenMerc's whole client record, pointer fields encoded.
// - LVLC (1440): OpenMerc's level record from offset 0 (frame 0, time 4, level name 8 and map name 72 are at the
//   original offsets), pointer fields encoded; the rest 0.
// - EDCT (1124): OpenMerc's entity record. The engine prefix and the string fields 420-500 lie at the original
//   offsets (SPEC-760 1, SPEC-752 3.4); the game-private part is OpenMerc's (SPEC-760 1: not an interface). The world
//   link (0x80-0x117) is not saved (the entity is unlinked while it is written and linked again on load,
//   SPEC-752 3.4); OpenMerc's record is longer than 1124 bytes, and its last bytes are stored in that link area.
// Implements: SPEC-752 1.1, 2.1, 2.2, 3.1-3.4; SPEC-760 1, B9.2; SPEC-780 1

#include "save_local.h"

#include <stddef.h>

namespace g10 {

#define EF(f, k) {(int)offsetof(edict_t, f), k, #f}
#define CF(f, k) {(int)offsetof(gclient_t, f), k, #f}
#define LF(f, k) {(int)offsetof(level_locals_t, f), k, #f}

// ---------------------------------------------------------------------------------------------------------------
// field tables. Every pointer of edict_t, gclient_t and level_locals_t must be listed (any other field is copied
// as it is). The string order is the STRG order (SPEC-752 2.2).

static const Field ent_fields[] = {
	EF(client, FK_CLIENT),
	EF(area_prev, FK_CLEAR),
	EF(area_next, FK_CLEAR),
	EF(owner, FK_ENTITY),
	EF(ghoulInst, FK_CLEAR), // restored by the Ghoul glue's GGOI chunks (G9, SPEC-791 B13)
	EF(model, FK_STRING),
	EF(message, FK_STRING),
	EF(classname, FK_STRING),
	EF(target, FK_STRING),
	EF(targetname, FK_STRING),
	EF(scripttarget, FK_STRING),
	EF(killtarget, FK_STRING),
	EF(killfacing, FK_STRING),
	EF(team, FK_STRING),
	EF(pathtarget, FK_STRING),
	EF(deathtarget, FK_STRING),
	EF(combattarget, FK_STRING),
	EF(soundname, FK_STRING),
	EF(spawn1, FK_STRING),
	EF(spawn2, FK_STRING),
	EF(spawn3, FK_STRING),
	EF(target_ent, FK_ENTITY),
	EF(goalentity, FK_ENTITY),
	EF(movetarget, FK_ENTITY),
	EF(prethink, FK_PTR),
	EF(think, FK_PTR),
	EF(blocked, FK_PTR),
	EF(touch, FK_PTR),
	EF(use, FK_PTR),
	EF(plUse, FK_PTR),
	EF(pain, FK_PTR),
	EF(die, FK_PTR),
	EF(map, FK_STRING),
	EF(chain, FK_ENTITY),
	EF(enemy, FK_ENTITY),
	EF(oldenemy, FK_ENTITY),
	EF(activator, FK_ENTITY),
	EF(groundentity, FK_ENTITY),
	EF(teamchain, FK_ENTITY),
	EF(teammaster, FK_ENTITY),
	EF(mynoise, FK_ENTITY),
	EF(mynoise2, FK_ENTITY),
	EF(item, FK_PTR),
	EF(moveinfo.endfunc, FK_PTR),
	EF(attachedto, FK_ENTITY),
	EF(lasthit, FK_ENTITY),
	EF(burn_attacker, FK_ENTITY),
	EF(ghoulmodel, FK_STRING),
	EF(helperlink, FK_ENTITY), /* SPEC-AMEND-005 G2.8: was raw-copied; stale abs ptr crashed G_HelperUnlink on load */
	EF(g3body, FK_PRESENCE),   // G3: re-created by the AI block (Body_ReadData)
	EF(g3damageowner, FK_ENTITY),
	EF(ai, FK_PRESENCE),       // G2: re-created by the AI block (ReadLevelAI)
	EF(ai_waitaction[0], FK_PTR),
	EF(ai_waitaction[1], FK_PTR),
	EF(ai_waitaction[2], FK_PTR),
	EF(ai_waitaction[3], FK_PTR),
	EF(ai_waitaction[4], FK_PTR),
	EF(ai_waitaction[5], FK_PTR),
	EF(ai_moveaction, FK_PTR),
	EF(roster, FK_CLEAR),      // G4 record: see save_level.cpp (kept from the level spawn for the same map entity)
};

static const Field client_fields[] = {
	{(int)(offsetof(gclient_t, ps) + offsetof(player_state_t, weaponinst)), FK_CLEAR, "ps.weaponinst"}, // rebuilt by the player module (export 11)
	{(int)(offsetof(gclient_t, ps) + offsetof(player_state_t, bodyinst)), FK_CLEAR, "ps.bodyinst"},
	CF(inv, FK_PRESENCE), // the game section's INVN restores it (kept on load)
	CF(chase_target, FK_ENTITY),
	CF(camera, FK_ENTITY),
	CF(pm_flagbolt, FK_CLEAR),
	CF(helper_lean, FK_ENTITY),
	CF(helper_crouch, FK_ENTITY),
	CF(helper_gun, FK_ENTITY),
	CF(burn_attacker, FK_ENTITY),
};

static const Field level_fields[] = {
	LF(changemap, FK_STRING),
	LF(sight_client, FK_ENTITY),
	LF(sight_entity, FK_ENTITY),
	LF(sound_entity, FK_ENTITY),
	LF(sound2_entity, FK_ENTITY),
	LF(current_entity, FK_ENTITY),
	LF(trigger_sound_list[0], FK_ENTITY),
	LF(trigger_sound_list[1], FK_ENTITY),
	LF(trigger_sound_list[2], FK_ENTITY),
	LF(trigger_sound_list[3], FK_ENTITY),
	LF(trigger_sound_list[4], FK_ENTITY),
	LF(trigger_sound_list[5], FK_ENTITY),
	LF(trigger_sound_list[6], FK_ENTITY),
	LF(trigger_sound_list[7], FK_ENTITY),
	LF(trigger_sound_list[8], FK_ENTITY),
	LF(trigger_sound_list[9], FK_ENTITY),
	LF(trigger_sound_list[10], FK_ENTITY),
	LF(trigger_sound_list[11], FK_ENTITY),
	LF(trigger_sound_list[12], FK_ENTITY),
	LF(trigger_sound_list[13], FK_ENTITY),
	LF(trigger_sound_list[14], FK_ENTITY),
	LF(trigger_sound_list[15], FK_ENTITY),
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

const Field *EntityFields(int *n)
{
	*n = COUNT(ent_fields);
	return ent_fields;
}

const Field *ClientFields(int *n)
{
	*n = COUNT(client_fields);
	return client_fields;
}

// ---------------------------------------------------------------------------------------------------------------
// generic encode / decode of a record copy

static void PutI(unsigned char *p, int ofs, int v)
{
	int32_t w = v;
	memset(p + ofs, 0, sizeof(void *));
	memcpy(p + ofs, &w, 4);
}

static int GetI(const unsigned char *p, int ofs)
{
	int32_t w;
	memcpy(&w, p + ofs, 4);
	return w;
}

static void *GetP(const unsigned char *p, int ofs)
{
	void *v;
	memcpy(&v, p + ofs, sizeof(v));
	return v;
}

static void SetP(unsigned char *p, int ofs, const void *v) { memcpy(p + ofs, &v, sizeof(v)); }

// rec = a copy of the live record: pointer fields are replaced by their codes
static void EncodeFields(unsigned char *rec, const Field *f, int n, const char *what)
{
	for (int i = 0; i < n; i++) {
		void *v = GetP(rec, f[i].ofs);
		int code = 0;
		switch (f[i].kind) {
		case FK_STRING: code = G10_StringMark((const char *)v); break;
		case FK_ENTITY: code = G10_EncodeEnt((const edict_t *)v); break;
		case FK_CLIENT:
			code = (v && game.clients) ? (int)((gclient_t *)v - game.clients) : -1;
			if (code >= game.maxclients)
				code = -1;
			break;
		case FK_PTR:
			code = -1;
			if (v) {
				code = G10_EncodePtr(v);
				if (code < 0) {
					g_warnings++;
					gi.dprintf("Unable to enumerate %s function (%s)\n", f[i].name, what);
				}
			}
			break;
		case FK_CLEAR: code = 0; break;
		case FK_PRESENCE: code = v ? 1 : 0; break;
		}
		PutI(rec, f[i].ofs, code);
	}
}

static void WriteStrings(const unsigned char *live, const Field *f, int n)
{
	for (int i = 0; i < n; i++)
		if (f[i].kind == FK_STRING)
			G10_WriteString((const char *)GetP(live, f[i].ofs));
}

// rec = the record as read: codes in the pointer fields are replaced by pointers; strings are read from their STRG
static void DecodeFields(unsigned char *rec, const Field *f, int n)
{
	for (int i = 0; i < n; i++) {
		int code = GetI(rec, f[i].ofs);
		void *v = NULL;
		switch (f[i].kind) {
		case FK_STRING: v = G10_ReadString(code); break;
		case FK_ENTITY: v = G10_DecodeEnt(code); break;
		case FK_CLIENT: v = (code >= 0 && code < game.maxclients && game.clients) ? game.clients + code : NULL; break;
		case FK_PTR: v = G10_DecodePtr(code); break;
		case FK_CLEAR:
		case FK_PRESENCE: v = NULL; break;
		}
		SetP(rec, f[i].ofs, v);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// EDCT

static const int ORIG_EDCT = 1124;

static int LinkStart(void) { return (int)offsetof(edict_t, area_prev); }
static int LinkEnd(void) { return (int)offsetof(edict_t, svflags); }
// bytes of the link area that can carry the tail: after the two list pointers
static int TailRoom(void) { return LinkEnd() - LinkStart() - 2 * (int)sizeof(void *); }
static bool Packed(void) { return (int)sizeof(edict_t) > ORIG_EDCT && (int)sizeof(edict_t) - ORIG_EDCT <= TailRoom(); }

int EntityImageSize(void) { return Packed() ? ORIG_EDCT : (int)sizeof(edict_t); }

void WriteEntity(edict_t *e)
{
	static unsigned char rec[sizeof(edict_t)], img[sizeof(edict_t) + ORIG_EDCT];
	memcpy(rec, e, sizeof(edict_t));
	EncodeFields(rec, ent_fields, COUNT(ent_fields), "entity");
	// the world link is not saved (unlinked state)
	memset(rec + LinkStart(), 0, LinkEnd() - LinkStart());
	int size = EntityImageSize();
	if (Packed()) {
		int tail = (int)sizeof(edict_t) - ORIG_EDCT;
		memcpy(img, rec, ORIG_EDCT);
		memcpy(img + LinkStart() + 2 * sizeof(void *), rec + ORIG_EDCT, tail);
	} else
		memcpy(img, rec, size);
	G10_WriteChunk("EDCT", img, size);
	WriteStrings((const unsigned char *)e, ent_fields, COUNT(ent_fields));
}

void ReadEntity(edict_t *e)
{
	static unsigned char rec[sizeof(edict_t)], img[sizeof(edict_t) + ORIG_EDCT];
	int size = EntityImageSize();
	memset(img, 0, sizeof(img));
	G10_ReadChunk("EDCT", img, size);
	if (Packed()) {
		int tail = (int)sizeof(edict_t) - ORIG_EDCT;
		memcpy(rec, img, ORIG_EDCT);
		memcpy(rec + ORIG_EDCT, img + LinkStart() + 2 * sizeof(void *), tail);
	} else
		memcpy(rec, img, size);
	memset(rec + LinkStart(), 0, LinkEnd() - LinkStart());
	DecodeFields(rec, ent_fields, COUNT(ent_fields));
	memcpy(e, rec, sizeof(edict_t));
}

// ---------------------------------------------------------------------------------------------------------------
// client records

static const int ORIG_CLNT = 1536;
enum {
	CL_PS = 0,         // player state, 200 bytes (SPEC-780 1)
	CL_PING = 200,
	CL_USERINFO = 204, // 512
	CL_NAME = 716,     // 16
	CL_BESTWEAP = 736, // 8
	CL_WEAPON = 752,   // remembered weapon id (0x2F0)
	CL_FOV = 756,      // int
	CL_CONNECTED = 760, // 0x2F8 (SPEC-752 1.2)
	CL_HEALTH = 764,
	CL_MAXHEALTH = 768,
	CL_INVENTORY = 844, // presence (SPEC-752 3.2)
	CL_HELPER = 852,    // second per-client object presence (OpenMerc has none)
};

void WriteClientGame(gclient_t *c)
{
	unsigned char img[ORIG_CLNT];
	memset(img, 0, sizeof(img));
	memcpy(img + CL_PS, &c->ps, 200);
	memset(img + CL_PS + 0x6C, 0, 4); // view-weapon instance: cleared (SPEC-752 3.2 "108 cleared")
	memset(img + CL_PS + 0x7C, 0, 4); // the game's own body instance pointer
	PutI(img, CL_PING, c->ping);
	memcpy(img + CL_USERINFO, c->pers.userinfo, 512);
	memcpy(img + CL_NAME, c->pers.netname, 16);
	memcpy(img + CL_BESTWEAP, c->pers.bestweap, 8);
	PutI(img, CL_WEAPON, c->pers.weapon);
	PutI(img, CL_FOV, (int)c->pers.fov);
	PutI(img, CL_CONNECTED, c->pers.connected ? 1 : 0);
	PutI(img, CL_HEALTH, c->pers.health);
	PutI(img, CL_MAXHEALTH, c->pers.max_health);
	PutI(img, CL_INVENTORY, c->inv ? 1 : 0);
	PutI(img, CL_HELPER, 0);
	G10_WriteChunk("CLNT", img, ORIG_CLNT);
	// the record has no string fields (no STRG); the player module writes its INVN (SPEC-003 slot 75)
	if (c->inv)
		c->inv->WriteSave();
}

void ReadClientGame(gclient_t *c)
{
	unsigned char img[ORIG_CLNT];
	memset(img, 0, sizeof(img));
	G10_ReadChunk("CLNT", img, ORIG_CLNT);
	if (c->inv) {
		PlayerDeleteInventory(c->inv);
		c->inv = NULL;
	}
	memset(c, 0, sizeof(*c));
	memcpy(&c->ps, img + CL_PS, 200);
	memset((unsigned char *)&c->ps + 0x6C, 0, sizeof(void *));
	memset((unsigned char *)&c->ps + 0x7C, 0, sizeof(void *));
	c->ping = GetI(img, CL_PING);
	memcpy(c->pers.userinfo, img + CL_USERINFO, 512);
	c->pers.userinfo[511] = 0;
	memcpy(c->pers.netname, img + CL_NAME, 16);
	c->pers.netname[15] = 0;
	memcpy(c->pers.bestweap, img + CL_BESTWEAP, 8);
	c->pers.bestweap[7] = 0;
	c->pers.weapon = GetI(img, CL_WEAPON);
	c->pers.fov = (float)GetI(img, CL_FOV);
	c->pers.connected = GetI(img, CL_CONNECTED) != 0;
	c->pers.health = GetI(img, CL_HEALTH);
	c->pers.max_health = GetI(img, CL_MAXHEALTH);
	// the userinfo flags OpenMerc keeps in the persistent data (SPEC-780 1, ClientUserinfoChanged)
	const char *s = Info_ValueForKey(c->pers.userinfo, "predicting");
	c->pers.predicting = *s && strcmp(s, "0") != 0;
	// SPEC-752 1.1: a client whose record had an inventory object gets a new one, which reads INVN
	if (GetI(img, CL_INVENTORY)) {
		c->inv = PlayerNewInventory();
		if (c->inv)
			c->inv->ReadSave();
		else
			G10_ReadChunk("INVN", NULL, 0);
	}
}

void WriteClientLevel(gclient_t *c)
{
	static unsigned char rec[sizeof(gclient_t)];
	memcpy(rec, c, sizeof(gclient_t));
	EncodeFields(rec, client_fields, COUNT(client_fields), "client");
	G10_WriteChunk("OMCL", rec, (int)sizeof(gclient_t));
}

void ReadClientLevel(gclient_t *c)
{
	static unsigned char rec[sizeof(gclient_t)];
	G10_ReadChunk("OMCL", rec, (int)sizeof(gclient_t));
	PlayerInventory *inv = c->inv; // from the game section (INVN)
	DecodeFields(rec, client_fields, COUNT(client_fields));
	memcpy(c, rec, sizeof(gclient_t));
	c->inv = inv;
}

// ---------------------------------------------------------------------------------------------------------------
// GAME (SPEC-752 3.1)

static const int ORIG_GAME = 1688;

static void PutF(unsigned char *p, int ofs, float v) { memcpy(p + ofs, &v, 4); }
static float GetF(const unsigned char *p, int ofs)
{
	float v;
	memcpy(&v, p + ofs, 4);
	return v;
}

void WriteGameRecord(bool autosave)
{
	unsigned char img[ORIG_GAME];
	memset(img, 0, sizeof(img));
	memcpy(img + 0x000, game.helpmessage1, 512);
	memcpy(img + 0x200, game.helpmessage2, 512);
	PutI(img, 0x400, game.helpchanged);
	PutI(img, 0x404, 0); // clients link: written as 0
	memcpy(img + 0x408, game.spawnpoint, 512);
	PutI(img, 0x608, game.maxclients);
	PutI(img, 0x60C, game.maxentities);
	PutI(img, 0x610, game.serverflags);
	img[0x614] = autosave ? 1 : 0;
	img[0x615] = (unsigned char)game.cinematic;
	// 0x618 mission statistics (8 int32 totals), 0x638 mission played time (SPEC-AMEND-007 G18.41)
	for (int i = 0; i < 8; i++)
		PutI(img, 0x618 + 4 * i, game.mstat_total[i]);
	PutF(img, 0x638, game.mstat_time);
	// difficulty record (SPEC-760 B9.2; positions measured on the original's saves)
	for (int i = 0; i < 7; i++)
		PutF(img, 0x640 + 4 * i, game.tparm[i]);
	PutI(img, 0x65C, game.encumbrance);
	PutF(img, 0x660, game.sparm);
	PutF(img, 0x664, game.tparm[7]);
	PutI(img, 0x668, game.cheats);
	PutI(img, 0x66C, (int)game.tparm[8]);
	PutF(img, 0x670, game.spawn_alertgain);
	PutF(img, 0x674, game.spawn_alertfade);
	PutF(img, 0x678, game.spawn_maxalert);
	PutF(img, 0x67C, game.spawn_maxinterval);
	PutF(img, 0x680, game.spawn_maxnear);
	PutF(img, 0x684, game.spawn_interval);
	PutI(img, 0x688, game.saves_allowed);
	PutI(img, 0x68C, game.skill_spawning);
	PutI(img, 0x690, game.skill_toughness);
	PutF(img, 0x694, game.difficulty_score);
	G10_WriteChunk("GAME", img, ORIG_GAME);
}

bool ReadGameRecord(void)
{
	unsigned char img[ORIG_GAME];
	memset(img, 0, sizeof(img));
	G10_ReadChunk("GAME", img, ORIG_GAME);
	memcpy(game.helpmessage1, img + 0x000, 512);
	memcpy(game.helpmessage2, img + 0x200, 512);
	game.helpmessage1[511] = game.helpmessage2[511] = 0;
	game.helpchanged = GetI(img, 0x400);
	memcpy(game.spawnpoint, img + 0x408, 512);
	game.spawnpoint[511] = 0;
	// the client array and the entity array were sized by InitGame from the cvars the save restored
	int maxclients = GetI(img, 0x608), maxentities = GetI(img, 0x60C);
	if (maxclients != game.maxclients || maxentities != game.maxentities)
		gi.dprintf("G10: save made with maxclients %d / maxentities %d, running %d / %d\n", maxclients, maxentities,
		           game.maxclients, game.maxentities);
	game.serverflags = GetI(img, 0x610);
	game.autosaved = img[0x614] != 0;
	game.cinematic = img[0x615];
	for (int i = 0; i < 8; i++)
		game.mstat_total[i] = GetI(img, 0x618 + 4 * i);
	game.mstat_time = GetF(img, 0x638);
	for (int i = 0; i < 7; i++)
		game.tparm[i] = GetF(img, 0x640 + 4 * i);
	game.encumbrance = GetI(img, 0x65C);
	game.sparm = GetF(img, 0x660);
	game.tparm[7] = GetF(img, 0x664);
	game.cheats = GetI(img, 0x668);
	game.tparm[8] = (float)GetI(img, 0x66C);
	game.spawn_alertgain = GetF(img, 0x670);
	game.spawn_alertfade = GetF(img, 0x674);
	game.spawn_maxalert = GetF(img, 0x678);
	game.spawn_maxinterval = GetF(img, 0x67C);
	game.spawn_maxnear = GetF(img, 0x680);
	game.spawn_interval = GetF(img, 0x684);
	game.saves_allowed = GetI(img, 0x688);
	game.skill_spawning = GetI(img, 0x68C);
	game.skill_toughness = GetI(img, 0x690);
	game.difficulty_score = GetF(img, 0x694);
	return img[0x614] != 0;
}

// ---------------------------------------------------------------------------------------------------------------
// LVLC (SPEC-752 3.3)

static const int ORIG_LVLC = 1440;

void WriteLevelRecord(void)
{
	static unsigned char img[ORIG_LVLC > sizeof(level_locals_t) ? ORIG_LVLC : sizeof(level_locals_t)];
	memset(img, 0, sizeof(img));
	memcpy(img, &level, sizeof(level_locals_t));
	EncodeFields(img, level_fields, COUNT(level_fields), "level");
	int size = sizeof(level_locals_t) <= ORIG_LVLC ? ORIG_LVLC : (int)sizeof(level_locals_t);
	G10_WriteChunk("LVLC", img, size);
	WriteStrings((const unsigned char *)&level, level_fields, COUNT(level_fields));
}

void ReadLevelRecord(void)
{
	static unsigned char img[ORIG_LVLC > sizeof(level_locals_t) ? ORIG_LVLC : sizeof(level_locals_t)];
	memset(img, 0, sizeof(img));
	int size = sizeof(level_locals_t) <= ORIG_LVLC ? ORIG_LVLC : (int)sizeof(level_locals_t);
	G10_ReadChunk("LVLC", img, size);
	DecodeFields(img, level_fields, COUNT(level_fields));
	memcpy(&level, img, sizeof(level_locals_t));
}

} // namespace g10
