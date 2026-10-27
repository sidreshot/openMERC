// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 unit tests - stand-ins for the engine import table and the G18 functions the damage package calls.
// Messages written through the import table are decoded into an event log the tests inspect.
// Implements: SPEC-001 (test doubles), SPEC-201 §1/§5 (decoding of the messages G3 sends)
#include "g3_test.h"
#include "../../ghoulglue/gg_ghoul.h"
#include "../../effects/fx.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

using namespace mock;

game_locals_t game;
level_locals_t level;
game_import_t gix;
gi_wrap gi;
game_export_t globals;
edict_t *g_edicts;
edict_t *world;
int meansOfDeath;

// G18's import wrapper and G7's hooks used by effects/fx_emit.cpp (compiled into the tests: G3's temp entities go
// through it)
trace_t gi_wrap::trace(const float *start, const float *mins, const float *maxs, const float *end, edict_t *pass,
                       int mask)
{
	trace_t tr;
	memset(&tr, 0, sizeof(tr));
	gix.trace(start, mins, maxs, end, pass, mask, &tr);
	return tr;
}
namespace fx {
scripted_kill_fn g_arm_scripted_kill;
attack_fn g_fire_attack;
}

namespace test {

std::vector<std::string> log;
std::map<std::string, float> cvars;
std::vector<std::string> effects(1, "");
std::vector<std::string> sounds(1, "");
static std::vector<unsigned char> msg;
std::vector<Model> lib;
static std::vector<Object *> libObjects;
unsigned rng = 12345;
std::vector<int> irandQueue;
int dropArmourCount = -1;

static unsigned Next(void)
{
	rng = rng * 1103515245u + 12345u;
	return (rng >> 8) & 0xffffff;
}

static int Index(std::vector<std::string> &t, const char *n)
{
	for (size_t i = 1; i < t.size(); i++)
		if (Lower(t[i]) == Lower(n))
			return (int)i;
	t.push_back(n);
	return (int)t.size() - 1;
}

static int s_soundindex(const char *n) { return Index(sounds, n); }
static int s_effectindex(const char *n) { return Index(effects, n); }
static int s_modelindex(const char *) { return 1; }
static void s_print(const char *fmt, ...)
{
	char b[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	log.push_back(std::string("print:") + b);
}
static void s_cprintf(edict_t *, int, const char *fmt, ...)
{
	char b[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	log.push_back(std::string("print:") + b);
}
static void s_sound(edict_t *e, int ch, int idx, float vol, float att, float, int)
{
	char b[256];
	snprintf(b, sizeof b, "sound:%s ent=%d ch=%d vol=%.2f att=%.2f", sounds[idx].c_str(), e ? G_EdictNum(e) : -1, ch,
	         vol, att);
	log.push_back(b);
}
static void s_psound(const float *, edict_t *, int ch, int idx, float vol, float att, float, int)
{
	char b[256];
	snprintf(b, sizeof b, "sound:%s pos ch=%d vol=%.2f att=%.2f", sounds[idx].c_str(), ch, vol, att);
	log.push_back(b);
}
static void w_byte(int c) { msg.push_back((unsigned char)c); }
static void w_short(int c)
{
	msg.push_back((unsigned char)(c & 0xff));
	msg.push_back((unsigned char)((c >> 8) & 0xff));
}
static void w_long(int c)
{
	w_short(c & 0xffff);
	w_short((c >> 16) & 0xffff);
}
static void w_float(float f)
{
	int i;
	memcpy(&i, &f, 4);
	w_long(i);
}
static void w_string(const char *s)
{
	while (*s)
		msg.push_back((unsigned char)*s++);
	msg.push_back(0);
}
static void w_pos(const float *p)
{
	for (int i = 0; i < 3; i++)
		w_short((int)(p[i] * 8));
}
static void w_dir(const float *) { msg.push_back(0); }
static void w_angle(float f) { msg.push_back((unsigned char)(int)(f * 256 / 360)); }
static int rdShort(size_t &i)
{
	int v = (short)(msg[i] | (msg[i + 1] << 8));
	i += 2;
	return v;
}
static void s_multicast(const float *, int to)
{
	char b[512];
	size_t i = 0;
	if (msg.empty())
		return;
	int id = msg[i++];
	if (id == svc_effect) {
		int fx = msg[i++];
		int fl = msg[i++];
		std::string where;
		if (fl & 1) {
			i += 6;
			where = "pos";
		} else if (fl & 2) {
			where = "ent" + std::to_string(rdShort(i));
		} else if (fl & 4) {
			int e = rdShort(i);
			if (fl & 8)
				rdShort(i);
			int bolt = rdShort(i);
			where = "ent" + std::to_string(e) + "/bolt" + std::to_string(bolt);
		}
		snprintf(b, sizeof b, "effect:%s %s to=%d", effects[fx].c_str(), where.c_str(), to);
	} else if (id == svc_temp_entity) {
		int te = msg[i++];
		snprintf(b, sizeof b, "te:%d len=%d", te, (int)msg.size());
		if (te == 18 && msg.size() >= 16) {
			// pos(6) dir(1) count size chunk ex ey ez mat
			int c = msg[2 + 6 + 1], sz = msg[2 + 6 + 2], ch = msg[2 + 6 + 3], mat = msg[2 + 6 + 7];
			snprintf(b, sizeof b, "te:18 count=%d size=%d chunk=%d mat=%d", c, sz, ch, mat);
		}
	} else
		snprintf(b, sizeof b, "svc:%d", id);
	log.push_back(b);
	msg.clear();
}
static void s_unicast(edict_t *, qboolean) { s_multicast(nullptr, -1); }
static int s_trace(const float *, const float *, const float *, const float *end, edict_t *, int, trace_t *out)
{
	memset(out, 0, sizeof *out);
	out->fraction = 1.0f;
	VectorCopy(end, out->endpos);
	out->ent = nullptr;
	return 1;
}
edict_t *traceTarget = nullptr;  // entity the filtered sweep touches
static int s_tracefilter(const float *start, const float *mins, const float *maxs, const float *end, edict_t *pass,
                         int mask, trace_t *out, tracefilter_t filter)
{
	s_trace(start, mins, maxs, end, pass, mask, out);
	if (traceTarget) {
		trace_t t = *out;
		t.ent = traceTarget;
		t.fraction = 0.5f;
		if (filter(&t)) {
			*out = t;
			return 0;
		}
	}
	return 1;
}
static int s_pointcontents(const float *) { return 0; }
static int s_inpvs(const float *, const float *) { return 1; }
static void s_link(edict_t *) {}
static int s_boxedicts(const float *mins, const float *maxs, edict_t **list, int maxcount, int area)
{
	int n = 0;
	if (area != 1)
		return 0;
	for (int i = 1; i < globals.num_edicts && n < maxcount; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse)
			continue;
		bool in = true;
		for (int k = 0; k < 3; k++)
			if (e->s.origin[k] + e->maxs[k] < mins[k] || e->s.origin[k] + e->mins[k] > maxs[k])
				in = false;
		if (in)
			list[n++] = e;
	}
	return n;
}
static void *s_tagmalloc(int size, int) { return calloc(1, size); }
static void s_tagfree(void *p) { free(p); }
static void s_freetags(int) {}
static float s_flrand(float lo, float hi) { return lo + (hi - lo) * (Next() / 16777216.0f); }
static int s_irand(int lo, int hi)
{
	if (!irandQueue.empty()) {
		int v = irandQueue.front();
		irandQueue.erase(irandQueue.begin());
		if (v < lo)
			v = lo;
		if (v > hi)
			v = hi;
		return v;
	}
	if (hi <= lo)
		return lo;
	return lo + (int)(Next() % (unsigned)(hi - lo + 1));
}
static float s_cvar_value(const char *n)
{
	auto it = cvars.find(Lower(n));
	return it == cvars.end() ? 0 : it->second;
}
static int s_fileexists(const char *) { return 0; }
// written chunks are also kept for read-back tests (SPEC-001 #85/#86: the id and the length must match)
std::vector<std::pair<int, std::vector<unsigned char>>> chunks;
size_t chunkReadPos;
static int s_writechunk(int id, const void *data, int len)
{
	char b[64];
	snprintf(b, sizeof b, "chunk:%08x len=%d first=%d", id, len, len >= 4 ? *(const int *)data : 0);
	log.push_back(b);
	chunks.push_back({id, std::vector<unsigned char>((const unsigned char *)data, (const unsigned char *)data + len)});
	return 1;
}
static int s_readchunk(int id, void *buffer, int len, void **allocated)
{
	if (chunkReadPos >= chunks.size() || chunks[chunkReadPos].first != id ||
	    (len && (int)chunks[chunkReadPos].second.size() != len)) {
		char b[96];
		snprintf(b, sizeof b, "readchunk:mismatch want %08x len %d", id, len);
		log.push_back(b);
		return 0;
	}
	auto &c = chunks[chunkReadPos++].second;
	if (buffer)
		memcpy(buffer, c.data(), c.size());
	(void)allocated;
	return 1;
}
static void s_spprint(edict_t *, int id, ...)
{
	char b[64];
	snprintf(b, sizeof b, "spprint:%x", id);
	log.push_back(b);
}

class MockSystem : public StubSystem {
public:
	IGhoulObject *CreateObject() override
	{
		Object *o = new Object();
		return o;
	}
};
static MockSystem msys;
static IGhoulSystem *s_getghoul(void) { return &msys; }
static int s_registergsq(const char *dir, const char *list, IGhoulObject *obj)
{
	Object *o = static_cast<Object *>(obj);
	for (auto &m : lib)
		if (Lower(m.name) == Lower(list) && Lower(m.dir) == "enemy/bolt") {
			o->m = m;
			return 1;
		}
	o->m.dir = dir;
	o->m.name = list;
	return 0;
}

// a named function: GCC cannot convert a variadic lambda to a function pointer
static void s_bprintf(int, const char *, ...) {}

void InstallEngine(void)
{
	memset(&gix, 0, sizeof gix);
	gix.soundindex = s_soundindex;
	gix.effectindex = s_effectindex;
	gix.modelindex = s_modelindex;
	gix.imageindex = s_modelindex;
	gix.dprintf = s_print;
	gix.cprintf = s_cprintf;
	gix.bprintf = s_bprintf;
	gix.sound = s_sound;
	gix.positioned_sound = s_psound;
	gix.WriteByte = w_byte;
	gix.WriteChar = w_byte;
	gix.WriteShort = w_short;
	gix.WriteLong = w_long;
	gix.WriteFloat = w_float;
	gix.WriteString = w_string;
	gix.WritePosition = w_pos;
	gix.WriteDir = w_dir;
	gix.WriteAngle = w_angle;
	gix.multicast = s_multicast;
	gix.unicast = s_unicast;
	gix.trace = s_trace;
	gix.tracefilter = s_tracefilter;
	gix.pointcontents = s_pointcontents;
	gix.inPVS = s_inpvs;
	gix.linkentity = s_link;
	gix.unlinkentity = s_link;
	gix.BoxEdicts = s_boxedicts;
	gix.TagMalloc = s_tagmalloc;
	gix.TagFree = s_tagfree;
	gix.FreeTags = s_freetags;
	gix.flrand = s_flrand;
	gix.irand = s_irand;
	gix.cvar_value = s_cvar_value;
	gix.FS_FileExists = s_fileexists;
	gix.WriteChunk = s_writechunk;
	gix.ReadChunk = s_readchunk;
	gix.SP_Print = s_spprint;
	gix.GetGhoul = s_getghoul;
	gix.RegisterGSQSequences = s_registergsq;
}

} // namespace test

// ---------------------------------------------------------------------------------------------------------------
// G18 functions used by G3
// ---------------------------------------------------------------------------------------------------------------
edict_t *G_FirstPlayer(void)
{
	for (int i = 1; i <= game.maxclients; i++)
		if (g_edicts[i].inuse && g_edicts[i].client)
			return &g_edicts[i];
	return nullptr;
}

bool test_sameTeam = false;
qboolean OnSameTeam(edict_t *, edict_t *) { return test_sameTeam; }

edict_t *G_Spawn(void)
{
	for (int i = game.maxclients + 1; i < globals.max_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse) {
			memset(e, 0, sizeof *e);
			e->inuse = true;
			e->s.number = i;
			if (i >= globals.num_edicts)
				globals.num_edicts = i + 1;
			return e;
		}
	}
	return nullptr;
}

int test_freed = 0;
void G_FreeEdict(edict_t *e)
{
	test_freed++;
	e->inuse = false;
}

void G_UseTargets(edict_t *, edict_t *) { test::log.push_back("usetargets"); }

edict_t *DropWeaponItem(edict_t *ent, int weapon, int)
{
	test::log.push_back("dropweapon:" + std::to_string(weapon));
	edict_t *e = G_Spawn();
	if (e)
		VectorCopy(ent->s.origin, e->s.origin);
	return e;
}

edict_t *SpawnKnifePickup(const vec3_t origin, const vec3_t, float)
{
	test::log.push_back("knifepickup");
	edict_t *e = G_Spawn();
	if (e)
		VectorCopy(origin, e->s.origin);
	return e;
}

void WeaponPrecacheAttack(int) {}

float vectoyaw(const vec3_t vec)
{
	if (vec[1] == 0 && vec[0] == 0)
		return 0;
	float yaw = (float)(atan2(vec[1], vec[0]) * 180 / M_PI);
	if (yaw < 0)
		yaw += 360;
	return yaw;
}

char *vtos(const vec3_t v)
{
	static char s[64];
	snprintf(s, sizeof s, "(%i %i %i)", (int)v[0], (int)v[1], (int)v[2]);
	return s;
}

void Com_Printf(char *msg, ...) { (void)msg; }
void Sys_Error(char *error, ...)
{
	(void)error;
	abort();
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-791 glue (G9) as seen by G3: in the unit tests no instance is glue-managed, so G3 takes its direct paths
// ---------------------------------------------------------------------------------------------------------------
GGModel *GG_FindModel(const char *dir, const char *list, bool allskins, const char *skin, const char *, bool)
{
	test::log.push_back(std::string("ggfind:") + (dir ? dir : "") + "/" + (list ? list : "") + "/" + (skin ? skin : "") +
	                    (allskins ? "/all" : ""));
	return nullptr;
}
bool GG_Prepared(GGModel *) { return false; }
GhoulID GG_RegisterSkin(GGModel *, const char *, const char *) { return 0; }
void GG_Prepare(GGModel *) {}
GGModel *GG_ModelOfObject(ghoul::IGhoulObject *) { return nullptr; }
GGModel *GG_ModelOfInst(ghoul::IGhoulInst *) { return nullptr; }
GGInst *GG_ObjInstOf(ghoul::IGhoulInst *) { return nullptr; }
ghoul::IGhoulObject *GG_Object(GGModel *) { return nullptr; }
const char *GG_ModelDir(GGModel *) { return nullptr; }
const char *GG_ModelList(GGModel *) { return nullptr; }
GhoulID GG_FindSequence(GGModel *, const char *) { return 0; }
GGInst *GG_NewInst(GGModel *, edict_t *) { return nullptr; }
GGInst *GG_CloneInst(GGModel *, edict_t *, ghoul::IGhoulInst *) { return nullptr; }
ghoul::IGhoulInst *GG_EngineInst(GGInst *) { return nullptr; }
GGBolt *GG_AddBolt(GGInst *, GhoulID, GGModel *, GhoulID, const char *, float) { return nullptr; }
GGBolt *GG_AddBoltToBolt(GGBolt *, GhoulID, GGModel *, GhoulID, const char *, float) { return nullptr; }
void GG_PlayBolt(GGBolt *, GhoulID, float, float, bool, int, bool, bool) {}
GGBolt *GG_FindBoltByInst(GGInst *, ghoul::IGhoulInst *) { return nullptr; }
void GG_RemoveBolt(GGInst *, GGBolt *) {}
void GG_RemoveBoltsOn(GGInst *, GhoulID) {}
void GG_RemoveAllBolts(GGInst *) {}
ghoul::IGhoulInst *GG_BoltEngineInst(GGBolt *) { return nullptr; }
GGModel *GG_BoltModel(GGBolt *) { return nullptr; }
GhoulID GG_BoltSequence(GGBolt *) { return 0; }
GhoulID GG_BoltChildBolt(GGBolt *) { return 0; }
float GG_BoltScale(GGBolt *) { return 1; }
GGBolt *GG_FirstBolt(GGInst *) { return nullptr; }
GGBolt *GG_FirstChildBolt(GGBolt *) { return nullptr; }
GGBolt *GG_NextBolt(GGBolt *) { return nullptr; }

// G5 / G9 notifications
int test_gmMode = -1;
void Bot_Damaged(edict_t *, edict_t *) {}
void GM_ClientDamaged(edict_t *, edict_t *) {}
int GM_Mode(void) { return test_gmMode; }
int GM_DMFlags(void) { return 0; }

// G6b
int test_clientDamaged = 0;
void ScriptGlue_ClientDamaged(edict_t *) { test_clientDamaged++; }
int ScriptGlue_CinematicState(void) { return 0; }
void GG_RegisterCallback(ghoul::IGhoulCallback *) {}
