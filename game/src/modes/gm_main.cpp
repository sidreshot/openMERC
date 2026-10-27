// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): mode selection, effective deathmatch flags, teams and the same-team
// rule, level start / setup / frame / exit, the deathmatch end rules and map rotation, rule queries used by
// player, item, weapon and damage code.
// Implements: SPEC-790 1, 2, 3, 4, Behaviour 1, 2, 3 (movement scale, spread, predicates), 5, 11 (movement scale)
#include "gm_internal.h"

gm_client_t gm_cl[MAX_CLIENTS];
gm_level_t gm = {-1};

//-----------------------------------------------------------------------------------------------------------
// mode table (SPEC-790 1 and the flag table of 3)

struct gm_modeinfo {
	const char *name;
	const char *teamname;   // with the mode's 0x80
	unsigned cvarmask;      // bits read from dmflags
	unsigned forceon;       // bits forced on
};

static const gm_modeinfo gm_modes[8] = {
	{"single player", NULL, 0x0000, 0x0000},
	{"DM", "Team DM", 0x3FFF, 0x0000},
	{"Assassin", NULL, 0x3E7F, 0x0000},
	{"Arsenal", NULL, 0x063B, 0x0840},
	{"CTF", NULL, 0x7F7F, 0x0080},
	{"Realistic", "Team Real", 0x36A0, 0x0010},
	{"Control", NULL, 0x7F7F, 0x0080},
	{"CTB", "Team CTB", 0xFFFF, 0x0000},
};

int GM_Mode(void) { return gm.mode; }

static int gm_selectmode(void)
{
	int m = deathmatch ? (int)deathmatch->value : 0;
	if (m < 0 || m > 7)
		m = 1; // values outside 0..7 are not supported by the original (undefined); play deathmatch
	return m;
}

const char *GM_GameTypeName(void)
{
	if (gm.mode < 0)
		return "Unknown";
	const gm_modeinfo &mi = gm_modes[gm.mode];
	if (mi.teamname && (GM_DMFlags() & DF_TEAMPLAY))
		return mi.teamname;
	return mi.name;
}

int GM_DMFlags(void)
{
	if (gm.mode < 0)
		return 0;
	const gm_modeinfo &mi = gm_modes[gm.mode];
	unsigned v = dmflags ? (unsigned)(int)dmflags->value : 0;
	return (int)((v & mi.cvarmask) | mi.forceon);
}

bool GM_Flag(int bit) { return (GM_DMFlags() & bit) != 0; }
bool GM_Deathmatch(void) { return gm.mode > 0; }
bool GM_TeamPlay(void) { return GM_Flag(DF_TEAMPLAY); }
bool GM_TeamGame(void)
{
	switch (gm.mode) {
	case GM_CTF:
	case GM_CONTROL: return true;
	case GM_DM:
	case GM_REALISTIC:
	case GM_CONQUER: return GM_TeamPlay();
	default: return false;
	}
}
bool GM_UsesTeamSpawns(void) { return GM_TeamGame(); }
bool GM_AssignsTeams(void) { return gm.mode == GM_CTF || gm.mode == GM_CONTROL || gm.mode == GM_CONQUER; }
bool GM_ForceJoin(void) { return GM_Flag(DF_FORCE_JOIN); }
bool GM_ModelTeamChangeKills(void)
{
	if (!GM_TeamPlay() || gm.mode == GM_CTF || gm.mode == GM_CONTROL)
		return false;
	return !(gm.mode == GM_CONQUER && GM_Flag(DF_NUMBERED_TEAMS));
}

//-----------------------------------------------------------------------------------------------------------
// client helpers

int GM_MaxClients(void) { return game.maxclients; }

bool GM_IsClient(edict_t *ent)
{
	if (!ent || !ent->client || !g_edicts)
		return false;
	int n = G_EdictNum(ent);
	return n >= 1 && n <= game.maxclients;
}

int GM_Slot(edict_t *ent) { return GM_IsClient(ent) ? G_EdictNum(ent) - 1 : -1; }

gm_client_t *GM_Cl(edict_t *ent)
{
	int s = GM_Slot(ent);
	return (s >= 0 && s < MAX_CLIENTS) ? &gm_cl[s] : NULL;
}

edict_t *GM_ClientEnt(int slot) { return (slot >= 0 && slot < game.maxclients) ? &g_edicts[slot + 1] : NULL; }

bool GM_IsSpectator(edict_t *ent) { return GM_IsClient(ent) && ent->client->resp.spectator; }
bool GM_IsBot(edict_t *ent) { return GM_IsClient(ent) && ent->client->pers.isbot; }
bool GM_InGame(edict_t *ent) { return GM_IsClient(ent) && ent->inuse && !ent->client->resp.spectator; }
int GM_EntNum(edict_t *ent) { return ent && g_edicts ? G_EdictNum(ent) : 0; }

int GM_Team(edict_t *ent) { return GM_IsClient(ent) ? ent->client->resp.team : 0; }

const char *GM_TeamName(edict_t *ent)
{
	if (!GM_IsClient(ent))
		return "";
	if (ent->client->resp.team == 1)
		return "blue";
	if (ent->client->resp.team == 2)
		return "red";
	// an unusable request is written back as `*<actual value>` (SPEC-780 1): the actual value counts
	const char *t = Info_ValueForKey(ent->client->pers.userinfo, "teamname");
	return *t == '*' ? t + 1 : t;
}

bool GM_SameTeam(edict_t *a, edict_t *b)
{
	if (!GM_IsClient(a) || !GM_IsClient(b))
		return false;
	if (gm.mode == GM_CTF || gm.mode == GM_CONTROL ||
	    (gm.mode == GM_CONQUER && GM_Flag(DF_NUMBERED_TEAMS) && GM_TeamPlay()))
		return a->client->resp.team == b->client->resp.team;
	if (GM_TeamPlay()) {
		char ta[MAX_INFO_STRING];
		gm_strncpyz(ta, GM_TeamName(a), sizeof(ta));
		return strcmp(ta, GM_TeamName(b)) == 0;
	}
	return false;
}

int GM_GetScore(edict_t *ent) { return GM_IsClient(ent) ? ent->client->resp.score : 0; }

void GM_SetScore(edict_t *ent, int score)
{
	if (!GM_IsClient(ent))
		return;
	ent->client->resp.score = score;
	ent->client->ps.stats[STAT_FRAGS] = (short)score;
}

void GM_AddScore(edict_t *ent, int delta) { GM_SetScore(ent, GM_GetScore(ent) + delta); }

int GM_Rand(void) { return rand() & 0x7fff; }

void GM_Print(edict_t *to, int pkg, int entry, ...)
{
	// the import is variadic; forward up to eight 32-bit arguments (ints, entity numbers, string pointers)
	va_list ap;
	va_start(ap, entry);
	intptr_t a[8];
	for (int i = 0; i < 8; i++)
		a[i] = va_arg(ap, intptr_t);
	va_end(ap);
	gix.SP_Print(to, SPID(pkg, entry), a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}

void GM_LocalSound(edict_t *ent, const char *name, float vol, float att, int chan)
{
	if (!ent)
		return;
	gix.sound(ent, chan, gix.soundindex(name), vol, att, 0, 1);
}

void GM_SoundAll(const char *name, float vol, float att)
{
	int idx = gix.soundindex(name);
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (e->inuse && e->client)
			gix.sound(e, CHAN_VOICE, idx, vol, att, 0, 1);
	}
}

//-----------------------------------------------------------------------------------------------------------
// level start (Behaviour 1)

static void gm_precache_dm(void)
{
	gi.effectindex("environ/irespawn");
	gi.effectindex("environ/prespawn");
	static const char *const snd[] = {"dm/armorpu.wav", "dm/irespawn.wav", "dm/frag.wav",
	                                  "dm/selffrag.wav", "dm/healthpu.wav", "dm/prespawn.wav"};
	for (const char *s : snd)
		gi.soundindex(s);
}

void GM_LevelInit(void)
{
	gm.mode = gm_selectmode();
	gm.logwritten = false;
	GM_InstallHooks();
	gix.configstring(CS_GM_ASSASSIN, gm.mode == GM_ASSASSIN ? "1" : "0");
	gix.configstring(CS_GM_TEAMGAME, GM_TeamGame() ? "1" : "0");
	if (gm.mode == GM_ASSASSIN) {
		gix.configstring(CS_DMICONS + 0, "");
		gix.configstring(CS_DMICONS + 1, "noshoot");
		gix.configstring(CS_DMICONS + 2, "targetyou");
		gix.configstring(CS_DMICONS + 3, "destarget");
	}
	gix.SP_Register("dm_generic");
	if (GM_Deathmatch())
		gm_precache_dm();
	if (gm.mode == GM_REALISTIC)
		gm.nitempos = 0;
	if (gm.mode == GM_CONQUER)
		GM_ConquerLevelInit();
	// per-level client state of the modes (the client records themselves are G18's)
	for (int i = 0; i < MAX_CLIENTS; i++) {
		gm_client_t *c = &gm_cl[i];
		c->target = c->pending = NULL;
		c->flagteam = 0;
		c->hurtcarrier = -100;
		c->fatigue = 0;
		c->exhausted = false;
		c->inzone = c->inzone_prev = false;
		memset(c->icons, 0, sizeof(c->icons));
		memset(c->sent, 0, sizeof(c->sent));
		memset(c->sentvalid, 1, sizeof(c->sentvalid));
	}
}

static void gm_spawnfree_weapon_pickup(int weapon)
{
	gitem_t *it = FindItemForWeapon(weapon);
	if (!it)
		return;
	edict_t *e = G_Spawn();
	e->classname = (char *)it->classname;
	SpawnItem(e, it);
	G_FreeEdict(e);
}

static void gm_precache_inview(unsigned mask)
{
	if (pe && pe->PrecacheViewModels)
		pe->PrecacheViewModels(mask);
}

static void gm_set_touchdelay(void)
{
	float d = ctf_control_delay_time ? ctf_control_delay_time->value : gix.cvar_value("ctf_control_delay_time");
	gm.touchdelay = level.time + d;
}

bool GM_TouchAllowed(void) { return level.time > gm.touchdelay; }

void GM_LevelSetup(void)
{
	if (gm.mode < 0)
		gm.mode = gm_selectmode();
	if (gm.mode == GM_SP) {
		gix.SP_Register("singleplr");
		return;
	}
	if (gm.mode != GM_ARSENAL) {
		gm_precache_inview((1u << WP_KNIFE) | (1u << WP_PISTOL1) | (1u << WP_HURTHAND) | (1u << WP_THROWHAND));
		gm_spawnfree_weapon_pickup(WP_PISTOL1);
	}
	switch (gm.mode) {
	case GM_ASSASSIN:
		gix.SP_Register("dm_assassin");
		gi.soundindex("dm/assassin/killtarget.wav");
		gi.soundindex("dm/assassin/badkill.wav");
		gix.configstring(CS_GM_ASSASSIN2, "1");
		break;
	case GM_ARSENAL: {
		gix.SP_Register("dm_arsenal");
		gi.soundindex("dm/arsenal/bigwinner.wav");
		gi.soundindex("dm/arsenal/lastweapon.wav");
		unsigned mask = 0;
		for (int w = 1; w <= 14; w++)
			mask |= 1u << w;
		gm_precache_inview(mask);
		for (int w = 1; w <= 14; w++)
			gm_spawnfree_weapon_pickup(w);
		GM_ArsenalDrawSet();
		gm.arsenal_announced = false;
		gm.arsenal_newround = false;
		break;
	}
	case GM_CTF:
		gix.SP_Register("dm_ctf");
		GM_CTFLevelSetup();
		GM_FlagsSetup();
		gm_set_touchdelay();
		break;
	case GM_CONTROL:
		gix.SP_Register("dm_control");
		GM_ControlLevelSetup();
		GM_FlagsSetup();
		gm_set_touchdelay();
		break;
	case GM_CONQUER:
		gi.soundindex("dm/conquer/beep.wav");
		gi.soundindex("player/conquerLP.wav");
		gi.soundindex("null.wav");
		gm.cs33[0] = gm.cs10[0] = 0;
		gm_set_touchdelay();
		break;
	default:
		break;
	}
}

//-----------------------------------------------------------------------------------------------------------
// every frame, end rules and the end of a level (Behaviour 2)

static int gm_teamsums(int *sums, edict_t **first, int max)
{
	int n = 0;
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (!e->inuse || !e->client)
			continue;
		int k = 0;
		for (; k < n; k++)
			if (GM_SameTeam(first[k], e))
				break;
		if (k == n) {
			if (n >= max)
				continue;
			first[n] = e;
			sums[n] = 0;
			n++;
		}
		sums[k] += e->client->resp.score;
	}
	return n;
}

static bool gm_endrules(void)
{
	if (gm.mode == GM_CONTROL && GM_ControlEndRule())
		return true;
	if (gm.mode == GM_CTF && gix.cvar_value("ctf_flag_captured") != 0) {
		GM_Print(NULL, SP_DM_CTF, CTF_END_GAME);
		return true;
	}
	if (timelimit && timelimit->value) {
		for (int i = 0; i < game.maxclients; i++) {
			edict_t *e = &g_edicts[i + 1];
			if (!e->inuse || !e->client)
				continue;
			if (level.framenum - e->client->resp.enterframe >= timelimit->value * 600) {
				GM_Print(NULL, SP_DM_GENERIC, DMG_TIME_LIMIT_HIT, (int)(short)(int)timelimit->value);
				return true;
			}
		}
	}
	if (fraglimit && fraglimit->value) {
		if (!GM_TeamPlay()) {
			for (int i = 0; i < game.maxclients; i++) {
				edict_t *e = &g_edicts[i + 1];
				if (!e->inuse || !e->client)
					continue;
				if (e->client->resp.score >= fraglimit->value) {
					GM_Print(NULL, SP_DM_GENERIC, DMG_FRAG_LIMIT_HIT, (int)(short)(int)fraglimit->value);
					return true;
				}
			}
		} else {
			int sums[MAX_CLIENTS];
			edict_t *first[MAX_CLIENTS];
			int n = gm_teamsums(sums, first, MAX_CLIENTS);
			for (int k = 0; k < n; k++) {
				if (sums[k] >= fraglimit->value) {
					GM_Print(NULL, SP_DM_GENERIC, DMG_FRAG_LIMIT_HIT, (int)(short)(int)fraglimit->value);
					return true;
				}
			}
		}
	}
	return false;
}

void GM_RunFrame(void)
{
	if (gm.mode < 0)
		return;
	if (gm.mode == GM_ARSENAL)
		GM_ArsenalFrame();
	else if (gm.mode == GM_CONQUER)
		GM_ConquerFrame();
	else if (gm.mode == GM_CTF)
		GM_FlagsFrame();
	if (!GM_Deathmatch() || level.intermissiontime)
		return;
	if (gm_endrules())
		GM_EndDMLevel();
}

static char gm_nextmap[MAX_QPATH];

static edict_t *gm_changelevel(const char *map)
{
	edict_t *ent = G_Spawn();
	ent->classname = (char *)"target_changelevel";
	gm_strncpyz(gm_nextmap, map, sizeof(gm_nextmap));
	ent->map = gm_nextmap;
	return ent;
}

void GM_EndDMLevel(void)
{
	if (GM_Flag(DF_SAME_LEVEL)) {
		BeginIntermission(gm_changelevel(level.mapname));
		return;
	}
	if (sv_maplist && sv_maplist->string && *sv_maplist->string) {
		static char list[1024];
		gm_strncpyz(list, sv_maplist->string, sizeof(list));
		static const char *seps = " ,\n\r";
		char *first = NULL;
		char *t = strtok(list, seps);
		while (t) {
			if (!first)
				first = t;
			if (Q_stricmp(t, level.mapname) == 0) {
				char *next = strtok(NULL, seps);
				BeginIntermission(gm_changelevel(next ? next : first));
				return;
			}
			t = strtok(NULL, seps);
		}
	}
	if (level.nextmap[0]) {
		BeginIntermission(gm_changelevel(level.nextmap));
		return;
	}
	edict_t *ent = G_Find(NULL, FOFS(classname), "target_changelevel");
	if (ent) {
		BeginIntermission(ent);
		return;
	}
	BeginIntermission(gm_changelevel(level.mapname));
}

void GM_LevelExit(void)
{
	switch (gm.mode) {
	case GM_CTF: GM_CTFLevelExit(); break;
	case GM_CONTROL: GM_ControlLevelExit(); break;
	case GM_CONQUER: GM_ConquerLevelExit(); break;
	default: break;
	}
}

//-----------------------------------------------------------------------------------------------------------
// rules consulted by other code

float GM_MoveScale(edict_t *ent)
{
	float slow = (ent && ent->client) ? ent->client->legslow : 0;
	if (slow < 0)
		slow = 0;
	if (slow > 0.5f)
		slow = 0.5f;
	switch (gm.mode) {
	case GM_SP: return game.skill_spawning == 4 ? 1.0f : 0.5625f;
	case GM_REALISTIC: return 0.5625f * (1.0f - slow);
	default: return (g_movescale ? g_movescale->value : 1.0f) * (1.0f - slow);
	}
}

float GM_SpreadAddon(edict_t *ent, int weapon, bool airbornelike)
{
	if (!ent || !ent->client)
		return 0;
	if (weapon == WP_SNIPER)
		return ent->client->ps.fov > 60 ? 4.5f : 0.0f;
	float k;
	switch (gm.mode) {
	case GM_ASSASSIN:
	case GM_ARSENAL:
	case GM_CTF:
	case GM_CONTROL: k = 3.75e-5f; break;
	case GM_REALISTIC: k = 0.5625f * 3.75e-5f; break;
	default: return 0;
	}
	float v2 = DotProduct(ent->velocity, ent->velocity);
	return v2 * k + (airbornelike ? 3.0f : 0.0f);
}

int GM_WeightLimit(void)
{
	if (gm.mode == GM_SP)
		return game.encumbrance;
	if (gm.mode == GM_REALISTIC)
		return 4;
	return 0;
}

bool GM_AutoSwitch(void) { return gm.mode != GM_REALISTIC; }
bool GM_WeaponsStay(void) { return GM_Flag(DF_WEAPONS_STAY); }
bool GM_NoDrops(void) { return gm.mode == GM_ARSENAL; }
float GM_DroppedLifetime(void) { return GM_Deathmatch() ? 20.0f : 0.0f; }

int GM_MedkitLimit(void)
{
	if (gm.mode == GM_REALISTIC || GM_Flag(DF_REALISTIC_DAMAGE))
		return 1;
	return 2;
}

// SPEC-781 1 filter table; dmflags as the mode sees them
int GM_ItemFilter(int itemindex, int kind, int standard)
{
	if (gm.mode <= GM_SP)
		return 0;
	unsigned f = (unsigned)GM_DMFlags();
	unsigned raw = dmflags ? (unsigned)(int)dmflags->value : 0;
	const int CTF_FLAG = 36, CONTROL_FLAG = 39;
	if (itemindex == CTF_FLAG && gm.mode != GM_CTF && gm.mode != GM_CONTROL)
		return 1;
	if (gm.mode == GM_REALISTIC) {
		if (kind == IK_ARMOR && (raw & DF_NO_ARMOR))
			return 1;
		if ((raw & DF_BULLET_ONLY) && (kind == IK_WEAPON || kind == IK_AMMO) && !standard)
			return 1;
		if (itemindex == 37)
			return 1;
		if (itemindex == 38)
			return 34;
		return 0;
	}
	if (kind == IK_HEALTH && (f & DF_NO_HEALTH))
		return 1;
	if (kind == IK_EQUIP && (f & DF_NO_ITEMS) && !(gm.mode == GM_CTF && itemindex == CTF_FLAG) &&
	    !(gm.mode == GM_CONTROL && itemindex == CONTROL_FLAG))
		return 1;
	if (kind == IK_ARMOR && (f & DF_NO_ARMOR))
		return 1;
	if (gm.mode == GM_ARSENAL) {
		if (kind == IK_AMMO || kind == IK_WEAPON)
			return 1;
		return 0;
	}
	if (kind == IK_AMMO && (f & DF_INFINITE_AMMO))
		return 1;
	if ((f & DF_BULLET_ONLY) && (kind == IK_WEAPON || kind == IK_AMMO) && !standard)
		return 1;
	return 0;
}

bool GM_ZoneArmourCovered(int zone) { return (zone >= 3 && zone <= 8) || zone == 24 || zone == 25; }
bool GM_ZoneIsLimb(int zone) { return (zone >= 9 && zone <= 12) || (zone >= 16 && zone <= 23); }
bool GM_BoltOnKnockable(const char *name)
{
	if (!name)
		return true;
	return Q_stricmp(name, "w_pistol1") != 0 && Q_stricmp(name, "w_knife") != 0;
}

void GM_RememberItem(int itemindex, const vec3_t origin)
{
	if (gm.mode != GM_REALISTIC || gm.nitempos >= GM_MAXITEMPOS || !origin)
		return;
	gm.itempos[gm.nitempos].item = itemindex;
	VectorCopy(origin, gm.itempos[gm.nitempos].origin);
	gm.nitempos++;
}

int GM_RememberedItems(int itemindex, vec3_t *out, int max)
{
	int n = 0;
	for (int i = 0; i < gm.nitempos && n < max; i++)
		if (gm.itempos[i].item == itemindex)
			VectorCopy(gm.itempos[i].origin, out[n++]);
	return n;
}
