// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — ambient sound sets (sound/sound.ams), func_ambientset, environ_soundgen,
// func_musicControl and the music value rules.
// Implements: SPEC-771 B.1-B.5, SPEC-112 (layout read by the game)

#include "ambient.h"
#include "../props/props.h"

namespace ambient {

angry_count_fn g_angry_count = NULL;

struct Set {
	char name[64];
	int loop;      // wave index or -1
	int nwaves;
	int waves[14];
};

static const int MAXSETS = 256;
static Set s_sets[3][MAXSETS];
static int s_nsets[3];
static char (*s_waves)[64];
static int s_nwaves;
static float s_musictime[MAX_CLIENTS + 1];

void LevelReset(void)
{
	s_nsets[0] = s_nsets[1] = s_nsets[2] = 0;
	s_nwaves = 0;
	s_waves = NULL; // TAG_LEVEL memory, freed by the level spawn
	memset(s_musictime, 0, sizeof(s_musictime));
}

static void Load(void)
{
	LevelReset();
	void *buf = NULL;
	int len = gix.FS_LoadFile("sound/sound.ams", &buf, 0);
	if (len <= 0 || !buf) {
		gi.cprintf(NULL, PRINT_HIGH, "Unable to locate sound/sound.ams - all ambient sounds will be disabled\n");
		return;
	}
	const unsigned char *p = (const unsigned char *)buf, *end = p + len;
	if (p[0] != 11) {
		gi.cprintf(NULL, PRINT_HIGH, "Invalid version (%d) for Ambient Sound Set (Should be %d)\n", p[0], 11);
		gix.FS_FreeFile(buf);
		return;
	}
	if (len < 17) {
		gix.FS_FreeFile(buf);
		return;
	}
	unsigned counts[4];
	memcpy(counts, p + 1, 16);
	p += 17;
	int nw = (int)counts[0];
	if (nw > 1024)
		nw = 1024;
	s_waves = (char(*)[64])gi.TagMalloc(64 * (nw > 0 ? nw : 1), TAG_LEVEL);
	for (unsigned i = 0; i < counts[0] && p < end; i++) {
		const unsigned char *s = p;
		while (p < end && *p)
			p++;
		if ((int)i < nw) {
			size_t l = (size_t)(p - s);
			if (l > 63)
				l = 63;
			memcpy(s_waves[i], s, l);
			s_waves[i][l] = 0;
		}
		p++;
	}
	s_nwaves = nw;
	for (int g = 0; g < 3; g++) {
		for (unsigned i = 0; i < counts[1 + g] && p < end; i++) {
			const unsigned char *s = p;
			while (p < end && *p)
				p++;
			size_t l = (size_t)(p - s);
			p++;
			if (p + 14 > end)
				break;
			short loop;
			unsigned short n;
			memcpy(&loop, p + 10, 2);
			memcpy(&n, p + 12, 2);
			p += 14;
			const unsigned char *w = p;
			p += 2 * n + 6; // waves, distance, volume min/max
			if (s_nsets[g] >= MAXSETS)
				continue;
			Set &set = s_sets[g][s_nsets[g]++];
			if (l > 63)
				l = 63;
			memcpy(set.name, s, l);
			set.name[l] = 0;
			set.loop = loop;
			set.nwaves = n > 14 ? 14 : n;
			for (int k = 0; k < set.nwaves; k++) {
				unsigned short v;
				memcpy(&v, w + 2 * k, 2);
				set.waves[k] = v;
			}
		}
	}
	gix.FS_FreeFile(buf);
}

static int Find(int g, const char *name)
{
	if (!name || !name[0])
		return -1;
	for (int i = 0; i < s_nsets[g]; i++)
		if (!Q_stricmp(s_sets[g][i].name, name))
			return i;
	return -1;
}

static void PrecacheWave(int w)
{
	if (w >= 0 && w < s_nwaves && s_waves[w][0])
		gi.soundindex(s_waves[w]);
}

void LevelSpawned(void)
{
	Load();
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = g_edicts + i;
		if (!e->inuse || !e->soundname || !e->soundname[0])
			continue;
		for (int g = 0; g < 3; g++) {
			int k = Find(g, e->soundname);
			if (k < 0)
				continue;
			const Set &s = s_sets[g][k];
			if (s.loop >= 0)
				PrecacheWave(s.loop);
			for (int j = 0; j < s.nwaves; j++)
				PrecacheWave(s.waves[j]);
			break;
		}
	}
}

int AreaSetIndex(const char *name) { return Find(0, name); }
int PointSetIndex(const char *name) { return Find(1, name); }

int MoverSound(const char *name, int k)
{
	int i = Find(2, name);
	if (i < 0)
		return -1;
	const Set &s = s_sets[2][i];
	// a missing entry reads as wave 0 of the file (SPEC-771 B.2, amending SPEC-760 B5.1)
	int w = (k >= 0 && k < s.nwaves) ? s.waves[k] : 0;
	if (w < 0 || w >= s_nwaves)
		return -1;
	return gi.soundindex(s_waves[w]);
}

// ---------------------------------------------------------------------------------------------------------------
// B.5 music

static int ClientIndex(edict_t *p)
{
	int n = G_EdictNum(p);
	return (n >= 1 && n <= MAX_CLIENTS) ? n : 0;
}

void MusicSet(edict_t *player, int value)
{
	if (!player || !player->client)
		return;
	player->client->ps.musicvalue = (uint8_t)value;
	s_musictime[ClientIndex(player)] = (float)(int)level.time;
}

void MusicAuto(edict_t *player)
{
	if (!player || !player->client)
		return;
	int v = player->client->ps.musicvalue;
	if (v != 0 && v != 2 && v != 3)
		return;
	if (v == 3 && level.time < s_musictime[ClientIndex(player)] + 4)
		return;
	int angry = g_angry_count ? g_angry_count(player) : 0;
	int score = 4 * angry;
	if (player->health < 50)
		score += 2;
	if (player->health < 25)
		score += 2;
	int r = (score >= 9 && angry > 0) ? 3 : 2;
	if (r != v)
		MusicSet(player, r);
}

void MusicDanger(edict_t *player)
{
	if (!player || !player->client)
		return;
	int v = player->client->ps.musicvalue;
	if (v == 0 || v == 2 || v == 3)
		MusicSet(player, 3);
}

// ---------------------------------------------------------------------------------------------------------------
// B.3 func_ambientset, B.4 environ_soundgen, B.5 func_musicControl

static void PointHelper(edict_t *ent, bool rotate)
{
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NONE;
	ent->s.modelindex = 0;
	props::SetBox(ent, -5, -5, -7, 5, 5, 12);
	if (rotate)
		props::RotateBox(ent);
	ent->clipmask = 0x02030003;
	gi.linkentity(ent);
}

static void ambientset_use(edict_t *self, edict_t *, edict_t *activator)
{
	int i = AreaSetIndex(self->soundname);
	if (i < 0 || !activator || !activator->inuse || !activator->client)
		return;
	activator->client->ps.ambientset = (short)i;
}

static void SP_func_ambientset(edict_t *ent)
{
	PointHelper(ent, false);
	ent->use = ambientset_use;
}

static void soundgen_on(edict_t *self)
{
	self->s.sound = PointSetIndex(self->soundname);
	self->s.sounddata = 0xF8;
	self->s.effects |= EF_SOUNDISSET;
}

static void soundgen_think(edict_t *self)
{
	int i = PointSetIndex(self->soundname);
	if (i < 0) {
		gi.cprintf(NULL, PRINT_HIGH, "Soundgen with invalid name!\n");
		G_FreeEdict(self);
		return;
	}
	soundgen_on(self);
}

static void soundgen_use(edict_t *self, edict_t *, edict_t *)
{
	if (self->s.effects & EF_SOUNDISSET) {
		self->s.sound = 0;
		self->s.effects &= ~EF_SOUNDISSET;
	} else
		soundgen_on(self);
}

static void SP_environ_soundgen(edict_t *ent)
{
	PointHelper(ent, true);
	ent->use = soundgen_use;
	if (ent->spawnflags == 0) {
		ent->think = soundgen_think;
		ent->nextthink = level.time + 0.3f;
	}
}

static void musiccontrol_use(edict_t *self, edict_t *, edict_t *activator)
{
	if (!activator || !activator->client)
		return; // the original assumes a player (no check)
	if (self->health == -2)
		MusicSet(activator, 1); // the original writes the controller's own (missing) client and crashes
	else if (self->health == -1)
		MusicSet(activator, 2);
	else
		MusicSet(activator, self->sounds + 3);
}

static void SP_func_musicControl(edict_t *ent)
{
	PointHelper(ent, false);
	ent->use = musiccontrol_use;
}

static const props::SpawnEntry s_spawns[] = {
	{"func_ambientset", SP_func_ambientset},
	{"environ_soundgen", SP_environ_soundgen},
	{"func_musicControl", SP_func_musicControl},
	{NULL, NULL}};
static props::SpawnTable s_reg(s_spawns);

} // namespace ambient
