// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the common body: family factory, animation records and playback, finished
// state, pause, expression state, turning and root motion, model event registration and dispatch, stuck knives,
// bolt-ons and skins, weapon identification, corpse rules.
// Implements: SPEC-722 common part (Body families, Animation records, Playback, Finished state, Pause, Expression
//             state, Turning and root motion, Model events, Bolt-ons and skins, Weapon identification, Corpses)
#include "g3_local.h"

#include <stdlib.h>

using namespace ghoul;

#define G3A(n, c, k, yl, yr, f, p, s, g) {n, ANIMCLS_##c, k, yl, yr, (uint32_t)(f), POSTURE_##p, s, g},
const G3Anim g3_humanAnims[] = {
#include "g3_humananims.inc"
};
#include "g3_animalanims.inc"
const G3Anim g3_dogAnims[] = {G3_DOG_ANIMS};
const G3Anim g3_cowAnims[] = {G3_COW_ANIMS};
#undef G3A
const int g3_numHumanAnims = (int)(sizeof(g3_humanAnims) / sizeof(g3_humanAnims[0]));
const int g3_numDogAnims = (int)(sizeof(g3_dogAnims) / sizeof(g3_dogAnims[0]));
const int g3_numCowAnims = (int)(sizeof(g3_cowAnims) / sizeof(g3_cowAnims[0]));

const G3Anim *Anim_Find(const G3Anim *table, int count, const char *name)
{
	if (!name)
		return NULL;
	for (int i = 0; i < count; i++)
		if (!Q_stricmp(table[i].name, name))
			return &table[i];
	return NULL;
}

const G3Anim *Anim_Human(const char *name)
{
	return Anim_Find(g3_humanAnims, g3_numHumanAnims, name);
}

int Anim_HumanIndex(const G3Anim *a)
{
	if (a < g3_humanAnims || a >= g3_humanAnims + g3_numHumanAnims)
		return -1;
	return (int)(a - g3_humanAnims);
}

bool G3_IsHumanFamily(int f)
{
	return f == BODY_MESO || f == BODY_ECTO || f == BODY_FEMALE || f == BODY_MESO_ARMOURED || f == BODY_HUMAN;
}

// ---------------------------------------------------------------------------------------------------------------
// weapon identification (common part)
// ---------------------------------------------------------------------------------------------------------------
struct WeaponId {
	const char *model;
	int weapon, attack;
};
static const WeaponId weaponIds[] = {{"w_knife", 1, 1}, {"w_bat", 1, 1}, {"w_kantana", 1, 1}, {"w_pistol2", 2, 4},
	{"w_pistol1", 3, 3}, {"w_machinepistol", 4, 5}, {"w_assault_rifle", 5, 6}, {"w_sniperrifle", 6, 7},
	{"w_autoshotgun", 7, 9}, {"w_shotgun", 8, 11}, {"w_machinegun", 9, 12}, {"w_rocket", 10, 14}, {"w_mpg", 11, 16},
	{"w_flamethrower", 12, 18}, {NULL, 0, 0}};

int Body_WeaponNumber(const char *model)
{
	for (int i = 0; model && weaponIds[i].model; i++)
		if (!Q_stricmp(weaponIds[i].model, model))
			return weaponIds[i].weapon;
	return 0;
}

int Body_AttackTypeOfModel(const char *model)
{
	for (int i = 0; model && weaponIds[i].model; i++)
		if (!Q_stricmp(weaponIds[i].model, model))
			return weaponIds[i].attack;
	return 0;
}

float Body_RefireDelay(int at, int charRef)
{
	switch (at) {
	case 1: return G3_flrand(1.35f, 1.65f);
	case 4: return 1.5f;
	case 3: return 0.8f;
	case 5: return 0.01f;
	case 6: return G3_flrand(0.1f, 0.5f);
	case 7: return 2.5f;
	case 9: return (charRef % 3) == 1 ? 2.0f : 0.6f;
	case 11: return 4.0f;
	case 12: return G3_flrand(0.5f, 1.0f);
	case 14: return 4.0f;
	case 16: return 6.0f;
	case 18: return 0.01f;
	case 17: return 6.0f;
	case 29: return 2.5f;
	}
	return 0;
}

// entity events (SPEC-771 C.4)
void G3_AddEvent(edict_t *ent, int ev, int parm)
{
	entity_state_t &s = ent->s;
	if ((s.event[0] & 0x7f) == 0) {
		s.event[0] = ev;
		s.eventparm[0] = parm;
	} else if ((s.event[1] & 0x7f) == 0) {
		s.event[0] |= 0x80;
		s.event[1] = ev;
		s.eventparm[1] = parm;
	} else {
		s.event[1] |= 0x80;
		s.event[2] = ev;
		s.eventparm[2] = parm;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------------------------
// Memory: bodies and their gore records live on the module's own heap (zero-filled), never in tagged engine memory.
// The level spawn frees level-tagged memory before it frees the entities (SPEC-760 B1.3 step 1), and corpses and
// AI-owned bodies are released at various later points, so a level-tagged body could be freed twice (G9's crash
// report 2026-10-25). Every body is on a list; Body_ReleaseAll() destroys them at the level spawn's "reset bodies"
// step (Gore_LevelStart) and at ShutdownGame.
static G3Body *g3bodyList;

void *G3Body::operator new(size_t size)
{
	return calloc(1, size);
}

void G3Body::operator delete(void *p)
{
	free(p);
}

void Body_ReleaseAll(void)
{
	while (g3bodyList) {
		G3Body *b = g3bodyList;
		if (b->ent && b->ent->g3body == b)
			b->ent->g3body = NULL;
		b->ent = NULL;
		delete b; // unlinks itself
	}
}

int Body_Count(void)
{
	int n = 0;
	for (G3Body *b = g3bodyList; b; b = b->listNext)
		n++;
	return n;
}

G3Body::G3Body(edict_t *e, int fam)
{
	listPrev = NULL;
	listNext = g3bodyList;
	if (g3bodyList)
		g3bodyList->listPrev = this;
	g3bodyList = this;
	family = fam;
	ent = e;
	owner = NULL;
	modelDir = "";
	cur = NULL;
	finished = true;
	pauseKind = 0;
	expr = EXPR_NEUTRAL;
	exprExpire = 0;
	exprLock = false;
	eventFlags = 0;
	pendingRestart = false;
	playStart = 0;
	deathTime = firstDeathTime = 0;
	VectorClear(deathPoint);
	lastHitTime = -999;
	lastHitFlags = 0;
	VectorClear(rootVel);
	rootYawRate = 0;
	readyToRemove = false;
	gibbed = false;
	gore = NULL;
	muted = false;
	voiceEnabled = true;
	gutsInst = NULL;
	frozenAt = 0;
	numKnives = 0;
	if (e)
		e->g3body = this;
}

G3Body::~G3Body()
{
	if (gore) {
		gore->~G3Gore();
		free(gore);
		gore = NULL;
	}
	if (ent && ent->g3body == this)
		ent->g3body = NULL;
	if (listPrev)
		listPrev->listNext = listNext;
	else if (g3bodyList == this)
		g3bodyList = listNext;
	if (listNext)
		listNext->listPrev = listPrev;
	listPrev = listNext = NULL;
}

G3Gore *G3_NewGore(G3Body *b, int zones)
{
	void *p = calloc(1, sizeof(G3Gore)); // module heap, released by ~G3Body (see operator new above)
	if (!p)
		return NULL;
	return new (p) G3Gore(b, zones);
}

class G3BaseBody : public G3Body {
public:
	G3BaseBody(edict_t *e, int f) : G3Body(e, f) {}
};

G3Body *G3_NewBaseBody(edict_t *ent, int family)
{
	return new G3BaseBody(ent, family);
}

G3Body *Body_Create(edict_t *ent, int family)
{
	if (!ent)
		return NULL;
	if (ent->g3body)
		Body_Destroy(ent);
	switch (family) {
	case BODY_MESO:
	case BODY_ECTO:
	case BODY_FEMALE:
	case BODY_MESO_ARMOURED:
		return G3_NewHumanBody(ent, family);
	case BODY_HUSKY:
	case BODY_ROTTWEILER:
		return G3_NewDogBody(ent, family);
	case BODY_COW:
		return G3_NewCowBody(ent);
	case BODY_HELI:
		return G3_NewHeliBody(ent);
	case BODY_TANK:
		return G3_NewTankBody(ent, false);
	case BODY_SNOWCAT:
		return G3_NewTankBody(ent, true);
	default:
		G3_Printf("ERROR: invalid body class code: %d\n", family);
		return G3_NewHumanBody(ent, BODY_MESO);
	}
}

void Body_Destroy(edict_t *ent)
{
	if (!ent || !ent->g3body)
		return;
	G3Body *b = ent->g3body;
	ent->g3body = NULL;
	b->ent = NULL;
	delete b;
}

G3Body *Body_CreateForPlayer(edict_t *player, const char *baseModel)
{
	int fam = (baseModel && !Q_stricmp(baseModel, "fem_play")) ? BODY_FEMALE : BODY_MESO;
	G3Body *b = Body_Create(player, fam);
	if (b) {
		b->owner = NULL;
		b->Precache();
	}
	return b;
}

// ---------------------------------------------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------------------------------------------
bool G3Body::HasAnim(const G3Anim *a)
{
	if (!a)
		return false;
	return G3G_Sequence(Inst(), modelDir, a->name) != 0;
}

static void PrintCantPlay(edict_t *ent, const char *name)
{
	if (!ent || !ent->classname)
		G3_DPrintf("Unknown monster couldn't play animation %s!\n", name);
	else if (ent->targetname)
		G3_DPrintf("%s %s couldn't play animation %s!\n", ent->classname, ent->targetname, name);
	else
		G3_DPrintf("Some %s couldn't play animation %s!\n", ent->classname, name);
}

bool G3Body::SetAnim(const G3Anim *a, bool force)
{
	if (!a)
		return false;
	IGhoulInst *inst = Inst();
	bool restart = force;
	if (pendingRestart) {
		restart = true;
		pendingRestart = false;
	}
	bool matchPhase = (a->flags & ANIMF_MATCH_PHASE) != 0;
	// 1
	if (a == cur && !restart && !finished) {
		if (pauseKind != 0 && pauseKind != 2)
			Resume();
		return true;
	}
	// 2
	if (cur && !(cur->flags & ANIMF_LOOP) && finished && matchPhase) {
		restart = true;
		matchPhase = false;
	}
	// 3 lean exit
	float start = G3_Time();
	if (!(a->flags & ANIMF_LEAN) && cur && (cur->flags & ANIMF_LEAN) && (a->cls == ANIMCLS_HIT ||
	    a->cls == ANIMCLS_DEATH) && inst) {
		Matrix4 m;
		if (G3G_BoltMatrix(inst, G3G_Part(inst, "sbolt_mouth"), &m, 5, true)) {
			float yaw = atan2f(m.m[0][1], m.m[0][0]) * (float)(180.0 / M_PI);
			float pitch = -atan2f(m.m[0][2], sqrtf(m.m[0][0] * m.m[0][0] + m.m[0][1] * m.m[0][1])) *
			              (float)(180.0 / M_PI);
			if (family == BODY_HUSKY || family == BODY_ROTTWEILER)
				yaw -= 90;
			ent->s.angles[YAW] = anglemod(ent->s.angles[YAW] + yaw);
			ent->s.angles[PITCH] = anglemod(ent->s.angles[PITCH] + pitch);
			for (int i = 0; i < 2; i++)
				ent->s.angles[i] = (float)((int)(ent->s.angles[i] * 65536.0f / 360.0f) & 65535) * 360.0f / 65536.0f;
		}
	}
	if (a->flags & ANIMF_LEAN)
		start += 0.1f;
	// 4
	if (pauseKind == 2)
		return false;
	// 5
	GhoulID seq = G3G_Sequence(inst, modelDir, a->name);
	if (!inst || !seq) {
		PrintCantPlay(ent, a->name);
		return false;
	}
	PrePlay(a, restart || a != cur);
	// 6
	if ((a->flags & ANIMF_KEEP_START) && cur && cur != a && cur->cls == a->cls)
		start = playStart;
	// 7
	bool reversed = (a->flags & ANIMF_REVERSED) != 0;
	GhoulID before = inst->GetPlayingSequence();
	inst->Play(seq, start, reversed ? 1.0f : a->startOffset, restart, (a->flags & ANIMF_LOOP) ? kPlayLoop : kPlayOnce,
	           matchPhase, reversed);
	playStart = start;
	// 8
	if (before != seq || restart) {
		finished = false;
		if (owner) {
			Matrix4 m;
			if (G3G_BoltMatrix(inst, G3G_Part(inst, "quake_ground"), &m, 5, false) && owner->FeetFollowFloor())
				owner->OnGroundOffset(m.m[3][2]);
		}
		if (frozenAt > 0 && G3_Time() - frozenAt < 0.1f)
			frozenAt = 0; // a new animation within 0.1 s re-arms the corpse freeze
	}
	// 9
	cur = a;
	if (pauseKind != 0 && pauseKind != 2)
		Resume();
	return true;
}

bool G3Body::AnimDone(void)
{
	if (!cur)
		return true;
	if ((cur->flags & ANIMF_LOOP) && !(cur->flags & ANIMF_FINISH_CYCLE))
		return true;
	return finished;
}

void G3Body::Pause(int kind, float time)
{
	IGhoulInst *inst = Inst();
	if (inst)
		inst->Pause(time);
	pauseKind = kind;
}

void G3Body::Resume(void)
{
	if (pauseKind == 0 || pauseKind == 2)
		return;
	IGhoulInst *inst = Inst();
	if (inst)
		inst->Resume(G3_Time());
	pauseKind = 0;
}

void G3Body::SetExpression(int state, float duration, bool lock)
{
	if (!exprLock && !lock) {
		if ((expr == EXPR_PAIN || expr == EXPR_DEAD) && state != EXPR_DEAD)
			return;
	} else if (exprLock && !lock) {
		if (state != EXPR_DEAD)
			return;
	}
	expr = state;
	exprExpire = G3_Time() + duration;
	exprLock = lock;
}

// ---------------------------------------------------------------------------------------------------------------
// turning and root motion
// ---------------------------------------------------------------------------------------------------------------
void G3Body::YawLimits(float L, float d, bool targetIsClient, float *left, float *right)
{
	if (!cur) {
		*left = L;
		*right = -L;
		return;
	}
	float y = rootYawRate * 0.1f * (float)(180.0 / M_PI);
	float f = 1.0f;
	bool hasTarget = owner && owner->Target();
	if (hasTarget && ent->health > 0) {
		if (!targetIsClient) {
			if (d < 64)
				f = 3;
			else if (d <= 192)
				f = 3 - 2 * (d - 64) / 128;
			else
				f = 1;
		} else {
			float g;
			if (d < 64)
				g = 2;
			else if (d <= 192)
				g = 2 - (d - 64) / 128;
			else
				g = 1;
			f = g * owner->TurnFactor();
		}
	}
	*left = cur->yawLeft * 1.5f + f * y;
	*right = cur->yawRight * 1.5f + f * y;
	rootYawRate = 0;
}

void G3Body::MoveDelta(float step, vec3_t out)
{
	vec3_t d;
	for (int i = 0; i < 3; i++) {
		d[i] = rootVel[i] * 0.1f;
		if (d[i] > 50)
			d[i] = 50;
		if (d[i] < -50)
			d[i] = -50;
		d[i] *= step;
	}
	vec3_t f, r, u;
	AngleVectors(ent->s.angles, f, r, u);
	for (int i = 0; i < 3; i++)
		out[i] = d[0] * f[i] + d[1] * r[i] + d[2] * u[i];
}

// ---------------------------------------------------------------------------------------------------------------
// model events
// ---------------------------------------------------------------------------------------------------------------
enum {
	NOTE_BOS, NOTE_EOS, NOTE_FIRE, NOTE_JUMP, NOTE_THUD, NOTE_KNEE, NOTE_INAIR, NOTE_THROW, NOTE_DROPWEAPON,
	NOTE_SOUND, NOTE_RSTEP, NOTE_LSTEP, NOTE_OOZE, NOTE_COUNT
};
enum { BOLT_ORIGIN, BOLT_GROUND, BOLT_MOUTH, BOLT_HEEL_R, BOLT_HEEL_L, BOLT_COUNT };
static const char *const noteNames[NOTE_COUNT] = {"BOS", "EOS", "fire", "jump", "thud", "knee", "inair", "throw",
	"dropweapon", "sound", "rstep", "lstep", "ooze"};

class G3NoteCB : public IGhoulCallback {
public:
	int note;
	bool Event(uint32_t, void *user, float, const void *data) override
	{
		edict_t *e = (edict_t *)user;
		if (e && e->inuse && e->g3body)
			e->g3body->OnNote(note, (const char *)data);
		return true;
	}
};
class G3BoltCB : public IGhoulCallback {
public:
	int which;
	bool Event(uint32_t, void *user, float, const void *data) override
	{
		edict_t *e = (edict_t *)user;
		if (e && e->inuse && e->g3body && data)
			e->g3body->OnBolt(which, (const Matrix4 *)data);
		return true;
	}
};
static G3NoteCB noteCBs[NOTE_COUNT];
static G3BoltCB boltCBs[BOLT_COUNT];
static bool cbInit;

IGhoulCallback *G3_NoteCallback(int note)
{
	if (!cbInit) {
		for (int i = 0; i < NOTE_COUNT; i++)
			noteCBs[i].note = i;
		for (int i = 0; i < BOLT_COUNT; i++)
			boltCBs[i].which = i;
		cbInit = true;
	}
	return &noteCBs[note];
}

IGhoulCallback *G3_BoltCallback(int which)
{
	G3_NoteCallback(0);
	return &boltCBs[which];
}

void G3Body::RegisterEvents(void)
{
	IGhoulInst *inst = Inst();
	IGhoulObject *o = G3G_Object(inst);
	if (!inst || !o)
		return;
	bool player = ent && ent->client;
	for (int i = 0; i < NOTE_COUNT; i++) {
		if (player && i != NOTE_EOS)
			continue;
		GhoulID n = o->FindNote(noteNames[i]);
		if (n)
			inst->AddNoteCallback(G3_NoteCallback(i), n);
	}
	if (player)
		return;
	struct { const char *name; int which, mode; const char *warn; } bolts[] = {
		{"quake_origin", BOLT_ORIGIN, 1, "WARNING: monster origin not found--wacky!!\n"},
		{"quake_ground", BOLT_GROUND, 5, NULL},
		{G3_IsHumanFamily(family) ? "sbolt_mouth" : "GBOLT_HEAD", BOLT_MOUTH, 5,
			"WARNING: monster mouth not found--sight will be defective!!\n"},
		{G3_IsHumanFamily(family) ? "sbolt_heel_r" : NULL, BOLT_HEEL_R, 5, NULL},
		{G3_IsHumanFamily(family) ? "sbolt_heel_l" : NULL, BOLT_HEEL_L, 5, NULL}};
	for (auto &b : bolts) {
		if (!b.name)
			continue;
		GhoulID id = o->FindPart(b.name);
		if (!id) {
			if (b.warn)
				G3_Printf("%s", b.warn);
			continue;
		}
		inst->AddMatrixCallback(G3_BoltCallback(b.which), id, b.mode);
	}
}

void G3Body::OnNote(int note, const char *arg)
{
	switch (note) {
	case NOTE_BOS:
		if (cur && (cur->flags & ANIMF_REVERSED)) {
			finished = true;
			eventFlags |= BODYEV_EOS;
			if (owner)
				owner->OnSequenceEnd();
		}
		break;
	case NOTE_EOS:
		finished = true;
		eventFlags |= BODYEV_EOS;
		if (owner)
			owner->OnSequenceEnd();
		break;
	case NOTE_FIRE: eventFlags |= BODYEV_FIRE; break;
	case NOTE_JUMP: eventFlags |= BODYEV_JUMP; break;
	case NOTE_THUD:
	case NOTE_KNEE: eventFlags |= BODYEV_THUD; break;
	case NOTE_INAIR:
		eventFlags |= BODYEV_INAIR;
		Pause(1, G3_Time());
		break;
	case NOTE_THROW: eventFlags |= BODYEV_THROW; break;
	case NOTE_DROPWEAPON:
		if (ent && !(ent->spawnflags & 0x20000)) {
			ent->spawnflags &= ~0x40000;
			if (owner)
				owner->OnDropWeaponNote();
		}
		break;
	case NOTE_SOUND:
		if (arg && *arg) {
			char s[128];
			Com_sprintf(s, sizeof s, "%s.wav", arg);
			G3_Sound(ent, CHAN_AUTO, s, 0.6f, ATTN_NORM);
		}
		break;
	case NOTE_RSTEP: eventFlags |= BODYEV_RSTEP; break;
	case NOTE_LSTEP: eventFlags |= BODYEV_LSTEP; break;
	case NOTE_OOZE: eventFlags |= BODYEV_OOZE; break;
	}
}

// Animation-driven yaw rate (radians per second) from the `quake_origin` rate matrix (SPEC-012 §6 mode 1):
// M = Δ·XF·s with Δ the bolt's model-space displacement over 0.001 of the cycle, XF the instance transform (no
// attach bolt on a character's own instance) and s = 1/(0.001·D). The yaw is that of Δ's rotation:
// Δ = (M/s)·XF⁻¹ (rotation parts, rows normalised), rate = atan2(Δ01, Δ00)·s; s = |row 0 of M| / |row 0 of XF|.
// (Reading M[0][1] directly gives XF's own −90° rotation × s: G2's report, docs/impl/G2.md.)
float G3Body::RootYawRate(const Matrix4 *m)
{
	IGhoulInst *inst = Inst();
	if (!inst)
		return 0;
	Matrix4 xf;
	inst->GetTransform(&xf);
	float ms = sqrtf(m->m[0][0] * m->m[0][0] + m->m[0][1] * m->m[0][1] + m->m[0][2] * m->m[0][2]);
	float xs = sqrtf(xf.m[0][0] * xf.m[0][0] + xf.m[0][1] * xf.m[0][1] + xf.m[0][2] * xf.m[0][2]);
	if (ms < 1e-6f || xs < 1e-6f)
		return 0;
	// rotation of XF is orthogonal after normalisation: XF⁻¹ = transpose; only row 0 of Δ is needed
	float d00 = 0, d01 = 0;
	for (int k = 0; k < 3; k++) {
		d00 += (m->m[0][k] / ms) * (xf.m[0][k] / xs);
		d01 += (m->m[0][k] / ms) * (xf.m[1][k] / xs);
	}
	return atan2f(d01, d00) * (ms / xs);
}

void G3Body::OnBolt(int which, const Matrix4 *m)
{
	switch (which) {
	case BOLT_ORIGIN:
		rootVel[0] = m->m[3][0];
		rootVel[1] = -m->m[3][1];
		rootVel[2] = m->m[3][2];
		rootYawRate = RootYawRate(m);
		// the four corner-peek fire records: no vertical motion, and once the record is done the velocity is
		// reversed (the character steps back out of the peek; SPEC-722 root motion, SPEC-711 A9)
		if (cur && G3_IsHumanFamily(family) && !Q_strncasecmp(cur->name, "std_fcornerpeak_", 16)) {
			rootVel[2] = 0;
			if (finished)
				VectorNegate(rootVel, rootVel);
		}
		break;
	case BOLT_GROUND:
		if (owner && owner->FeetFollowFloor())
			owner->OnGroundOffset(m->m[3][2]);
		break;
	case BOLT_MOUTH: {
		vec3_t ang;
		VectorCopy(ent->s.angles, ang);
		float yaw = atan2f(m->m[0][1], m->m[0][0]) * (float)(180.0 / M_PI);
		if (!G3_IsHumanFamily(family))
			yaw -= 90;
		ang[YAW] = anglemod(ang[YAW] + yaw);
		ent->viewheight = (int)m->m[3][2];
		if (owner)
			owner->OnLook(ang, m->m[3][2]);
		break;
	}
	case BOLT_HEEL_R:
	case BOLT_HEEL_L:
		OnHeel(which == BOLT_HEEL_R ? 0 : 1, m->m[3][2]);
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// per think
// ---------------------------------------------------------------------------------------------------------------
void G3Body::Think(void)
{
	IGhoulInst *inst = Inst();
	if (inst)
		inst->ProcessEvents(G3_Time());
	// deathmatch player: when the hit reaction's record is done, the record that played before returns (stand-in for
	// the player code's next movement choice, SPEC-AMEND-006 G3.19; g3_damage.cpp)
	if (painUntil > 0 && (!cur || cur->cls != ANIMCLS_HIT || finished)) {
		const G3Anim *back = painPrev;
		bool reaction = cur && cur->cls == ANIMCLS_HIT;
		painUntil = 0;
		painPrev = NULL;
		if (reaction && ent && ent->client && ent->health > 0 && back)
			SetAnim(back, false);
	}
	if (exprExpire > 0 && G3_Time() > exprExpire) {
		expr = EXPR_NEUTRAL;
		exprLock = false;
		exprExpire = 0;
	}
	if (gutsInst && !G3G_IsDescendant(inst, gutsInst))
		gutsInst = NULL;
	if (gutsInst) {
		// g_exit_guts_anim1 once, then g_exit_guts_anim2 in a loop
		GhoulID s1 = 0;
		IGhoulObject *o = gutsInst->GetGhoulObject();
		if (o)
			s1 = o->FindSequence("ghoul/Enemy/bolt/g_exit_guts_anim1.ghl");
		float dur = s1 ? gutsInst->SequenceDuration(s1, kPlayOnce, NULL) : 0;
		if (gutsStart == 0)
			gutsStart = G3_Time();
		if (G3_Time() - gutsStart >= dur) {
			GhoulID s2 = o ? o->FindSequence("ghoul/Enemy/bolt/g_exit_guts_anim2.ghl") : 0;
			if (s2)
				gutsInst->Play(s2, G3_Time(), 0, true, kPlayLoop, false, false);
			gutsInst = NULL;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// bolt-ons and skins
// ---------------------------------------------------------------------------------------------------------------
IGhoulInst *G3Body::AddBoltOn(const char *dir, const char *model, const char *skin, const char *bolt,
                              const char *toBolt, float scale)
{
	return G3G_AttachByName(Inst(), bolt, dir, model, toBolt, scale, skin);
}

IGhoulInst *G3Body::AddRandomBoltOn(float p, const char *dir, const char *model, const char *skin, const char *bolt,
                                    const char *toBolt, float scale)
{
	if (G3_CreatePak()) {
		AddBoltOn(dir, model, skin, bolt, toBolt, scale);
		return NULL; // "not attached" while a pak list is built
	}
	if (G3_flrand(0, 1) >= p)
		return NULL;
	return AddBoltOn(dir, model, skin, bolt, toBolt, scale);
}

bool G3Body::SetSkin(const char *material, const char *skin)
{
	return G3G_SetSkinByName(Inst(), material, skin, 0);
}

bool G3Body::ShowPart(const char *part, bool shown)
{
	GhoulID p = G3G_Part(Inst(), part);
	if (!p)
		return false;
	Inst()->SetPartOnOff(p, shown);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// stuck knives (SPEC-721 §10)
// ---------------------------------------------------------------------------------------------------------------
bool G3Body::IsStuckKnife(IGhoulInst *k)
{
	for (int i = 0; i < numKnives; i++)
		if (knives[i] == k)
			return true;
	return false;
}

void G3Body::AddStuckKnife(IGhoulInst *k)
{
	if (numKnives < 8)
		knives[numKnives++] = k;
}

void G3Body::ReleaseStuckKnives(IGhoulInst *only)
{
	IGhoulInst *inst = Inst();
	for (int i = 0; i < numKnives;) {
		IGhoulInst *k = knives[i];
		if (only && k != only) {
			i++;
			continue;
		}
		if (!G3G_IsDescendant(inst, k)) {
			knives[i] = knives[--numKnives]; // destroyed with its piece or a gib
			continue;
		}
		// the knife becomes a pickup at its position (model Enemy/Bolt/w_knife)
		vec3_t pos;
		VectorCopy(ent->s.origin, pos);
		Matrix4 m;
		IGhoulInst *parent = k->GetParent();
		GhoulID kb = k->GetAttachBolt();
		(void)kb;
		if (parent) {
			GhoulID bolt = 0;
			int n = parent->NumChildren();
			for (int c = 0; c < n; c++) {
				GhoulID b;
				if (parent->GetChild(c, &b) == k)
					bolt = b;
			}
			if (G3G_BoltMatrix(parent, bolt, &m, 5, true)) {
				vec3_t local = {m.m[3][0], m.m[3][1], m.m[3][2]};
				G3_EntityToWorld(ent, local, pos);
			}
		}
		edict_t *p = SpawnKnifePickup(pos, ent->s.angles, 0);
		if (p)
			p->g3flags |= G3F_KNIFE_PICKUP;
		if (parent)
			G3G_RemoveChild(parent, k);
		else if (inst)
			G3G_RemoveChild(inst, k);
		knives[i] = knives[--numKnives];
	}
}

// ---------------------------------------------------------------------------------------------------------------
// corpses (common part "Corpses")
// ---------------------------------------------------------------------------------------------------------------
static int corpseCount;

void Body_CorpseFrameBegin(void)
{
	corpseCount = 0;
}

static bool ClientSees(const vec3_t pos)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client)
			continue;
		vec3_t d, f;
		VectorSubtract(pos, p->s.origin, d);
		if (VectorLength(d) > 800)
			continue;
		VectorNormalize(d);
		AngleVectors(p->client->v_angle, f, NULL, NULL);
		if (DotProduct(f, d) > -0.35f)
			return true;
	}
	return false;
}

bool Body_CorpseCheck(edict_t *c, bool isArmourTarget)
{
	G3Body *b = Body_Of(c);
	if (!b)
		return true;
	if (isArmourTarget)
		return false;
	if (b->readyToRemove)
		return true;
	float now = G3_Time();
	if (b->fadeEnd > 0) {
		if (now >= b->fadeEnd)
			return true;
		return false;
	}
	int maxc = (int)G3_Cvar("ai_maxcorpses");
	edict_t *sc = G3_SightClient();
	bool sameTeamAsPlayer = false; // UNVERIFIED which entity is compared (SPEC-722)
	(void)sc;
	if (corpseCount < maxc || G3_Cinematic() || sameTeamAsPlayer) {
		corpseCount++;
		if (now - b->deathTime >= maxc * 15.0f) {
			if (ClientSees(c->s.origin)) {
				b->deathTime = now - 25.0f;
				return false;
			}
			return true;
		}
		return false;
	}
	if (now - b->deathTime > 3.0f) {
		if (!c->ghoulInst)
			return true;
		b->fadeEnd = now + 3.0f;
		// fade-out: entity effects bit 0x8000 with start/duration (SPEC-771 C.4)
		memset(c->s.fx, 0, sizeof c->s.fx);
		c->s.effects &= ~(0x0001 | 0x0800);
		c->s.effects |= 0x8000;
		int start = (int)(now * 1000.0f);
		c->s.fx[0][0] = (short)(start & 0xffff);
		c->s.fx[0][1] = (short)((start >> 16) & 0xffff);
		c->s.fx[0][2] = 3000;
	}
	return false;
}

void Body_CopyToCorpse(edict_t *player, edict_t *corpse)
{
	G3Body *pb = Body_Of(player);
	int fam = pb ? pb->family : BODY_MESO;
	G3Body *b = Body_Create(corpse, fam);
	if (b && pb) {
		b->modelDir = pb->modelDir;
		b->deathTime = b->firstDeathTime = G3_Time();
		if (b->gore && pb->gore) {
			for (int i = 0; i < b->gore->numZones && i < pb->gore->numZones; i++) {
				b->gore->zones[i] = pb->gore->zones[i];
			}
		}
	}
}
