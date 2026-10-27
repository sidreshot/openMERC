/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I5. Internal declarations of the client effects system.
 *
 * Implements: SPEC-400, SPEC-401
 */

#ifndef OM_FX_LOCAL_H
#define OM_FX_LOCAL_H

#include "cl_fx.h"
#include "fx_eft.h"

/* ---- limits (SPEC-400 Constants) ----------------------------------------- */

#define FX_MAX_DEFS 256       /* configstring effect table */
#define FX_MAX_NAMED 256      /* by-name effect table */
#define FX_MAX_RUNNING 256
#define FX_MAX_PARTICLES 1024
#define FX_MAX_DECALS 1024
#define FX_MAX_DEBRIS 1024

#define FX_CS_EFFECTS 647     /* SPEC-203 */
#define FX_CS_SCREEN5 5       /* style-13 colour (SPEC-400 Light styles) */

/* effect source classes (editor substitution, SPEC-400 Starting item 3) */
enum
{
	FXS_EDITOR = 1,
	FXS_GAME = 2,
	FXS_TRAIL = 3,
	FXS_COLLIDE = 4
};

/* attachment modes */
enum
{
	FXA_POSITION = 0,
	FXA_ENTITY,
	FXA_BOLT,
	FXA_PARTICLE
};

/* trace kinds */
enum
{
	FXTR_WORLD = 0,       /* world brushes only */
	FXTR_BRUSHES,         /* world and brush entities */
	FXTR_SOLID            /* world and every solid entity */
};

/* ---- shared helpers (fx_util.c) ------------------------------------------ */

extern cvar_t *fx_sim_time, *cl_max_quads, *cl_max_debris, *fx_maxdebrisonscreen;
extern cvar_t *cl_freezequads, *freezeworld, *fx_noflash, *fx_edit, *fx_editsegment;
extern cvar_t *fx_editdir, *fx_run, *fx_partadd, *fx_partsub, *fx_partadjust;
extern cvar_t *fx_graphic, *fx_sound, *fx_collide, *fx_trail, *fx_trace;
extern cvar_t *sc_framerate;

extern float fx_now;          /* the effects clock, seconds */

float FX_flrand(float min, float max);
int FX_irand(int min, int max);
int FX_crand(void);           /* C runtime style generator, 0..32767 */
float FX_Range(const fx_range_t *r);

qboolean FX_Frozen(void);
void FX_Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int kind, trace_t *tr, int *entnum);
int FX_TraceMaterial(const trace_t *tr);
void FX_AxesFromDir(const vec3_t dir, vec3_t fwd, vec3_t right, vec3_t up);
void FX_Perpendicular(const vec3_t n, vec3_t out);
int FX_DirIndex(const vec3_t v);
void FX_DirVector(int index, vec3_t out);
void FX_ReadPos(vec3_t out);
void FX_ReadDir(vec3_t out);
void FX_ReadScaledPos(vec3_t out, float scale);

/* client sound list (SPEC-401) */
#define FX_NUM_SOUNDS 114
void FX_ClearSoundList(void);
struct sfx_s *FX_ListSound(int index);
void FX_PlaySound(int index, const vec3_t org, float vol, float atten, int ent,
		int chan, float timeofs);
void FX_PlaySfx(struct sfx_s *sfx, const vec3_t org, float vol, float atten, int ent,
		int chan, float timeofs);

/* client sprite list (SPEC-401) */
#define FX_NUM_SPRITES 57
const char *FX_SpriteName(int index);
void FX_RegisterSprites(void);
void FX_RegisterSpriteName(const char *name);

/* entities */
qboolean FX_EntityInFrame(int num);
const entity_state_t *FX_EntityCurrent(int num);
void FX_EntityLerpOrigin(int num, vec3_t out);
void FX_EntityLerpAngles(int num, vec3_t out);
qboolean FX_IsLocalPlayer(int num);
void FX_TraceLog(const char *fmt, ...) PRINTF_ATTR(1, 2);
/* test captures (cvar om_fxshot) */
void FX_ShotInit(void);
void FX_ShotEvent(char kind, int number); /* 't' temp entity, 'e' svc_effect */
void FX_ShotFrame(void);

/* ---- effect definitions and running effects (fx_system.c) ---------------- */

typedef struct fx_def_s
{
	char name[MAX_QPATH];         /* as requested, without "effects/" and ".eft" */
	qboolean loaded;              /* a file was read and parsed */
	fx_eft_t eft;
	struct sfx_s *sfx[FX_MAX_PRIMS];
	void *model[FX_MAX_PRIMS];    /* Ghoul model of Ghoul particles */
} fx_def_t;

typedef struct
{
	vec3_t origin, fwd, right, up;
	float scale;                  /* 0 = 1 */
	int count;                    /* elements per burst override, 0 = none */
	qboolean has_pos2;
	vec3_t pos2;
	qboolean has_rmin, has_rmax;
	vec3_t rmin, rmax;
	int mode;                     /* FXA_* */
	int ent;
	int ghoul_inst;
	int bolt;
	qboolean bolt_reversed;
	qboolean trace_pos2;          /* svc_effect flag 0x20 */
	int part, part_serial;        /* FXA_PARTICLE: followed particle */
} fx_params_t;

typedef struct
{
	float trigger;                /* one-shot: fire time; emitter: start of emitting */
	float end;                    /* emitter end of emitting */
	float next;                   /* emitter next burst */
	qboolean done;                /* one-shot fired/skipped; emitter stopped */
	int live;                     /* emitter: living particles */
	int head;                     /* emitter: first particle (index) or -1 */
	sof_quadchain_t chain;
} fx_slot_t;

typedef struct
{
	qboolean inuse;
	fx_def_t *def;
	int source;
	fx_params_t p;
	float start;
	qboolean detached;
	qboolean refresh;
	qboolean priority;            /* OpenMerc: the local player's own effect (view weapon, own entity) */
	unsigned serial;              /* OpenMerc: start counter, identifies this instance */
	fx_slot_t slot[FX_MAX_PRIMS];
} fx_run_t;

extern fx_run_t fx_running[FX_MAX_RUNNING];

void FX_DefaultParams(fx_params_t *p);
fx_def_t *FX_FindEffect(const char *name);        /* by-name lookup (loads on demand) */
fx_def_t *FX_ConfigEffect(int slot);
int FX_StartEffect(fx_def_t *def, const fx_params_t *p, int source);
int FX_StartNamed(const char *name, const fx_params_t *p, int source);
void FX_ClearRunning(void);
/* OpenMerc pool statistics since the last level start (om_fxpool) */
extern int fx_pool_drop_world, fx_pool_drop_own, fx_pool_evicted, fx_pool_slot_capped;
void FX_UpdateRunning(void);

/* ---- emitters and particles (fx_emitter.c, fx_particle.c) ---------------- */

enum
{
	FXK_FREE = 0,
	FXK_BILLBOARD,
	FXK_LINE,
	FXK_GHOUL,
	FXK_TIMER
};

struct fx_part_s;
typedef void (*fx_timer_cb_t)(struct fx_part_s *p, float f);
typedef void (*fx_trail_cb_t)(struct fx_part_s *p);

typedef struct fx_part_s
{
	sof_alphaquad_t q;            /* first member: the renderer's record */
	int kind;
	int serial;
	float spawn, die;
	qboolean oneframe;
	qboolean noimage;             /* no sprite: freed unless timer or Ghoul */

	vec3_t pos, vel, accel;
	qboolean attached;
	vec3_t local;                 /* attached: offset in the effect frame */
	vec3_t end, endlocal;
	vec3_t endvel, endaccel;
	float lookahead;              /* line Point2 source 0: T, 0 = no re-placing */
	qboolean decel;               /* "Decelerate to a stop" */
	qboolean stopped;             /* frozen in place by the decelerate rule */

	float d1, d2, r1, r2;         /* dimensions and their rates per second */
	float angle, avel;            /* billboard rotation (radians) */
	vec3_t gangles, gavel;        /* Ghoul: degrees, degrees per second */
	float gscale, gscalerate;

	int alpha;
	float arate;                  /* styles 0/1: per second; styles 2/3: peak */
	float a0;
	int astyle, cstyle;
	byte rgb[3];

	int fx, prim;                 /* owning emitter, -1 */
	int next;                     /* next particle of that emitter */
	const fx_eft_particle_t *desc;
	int pflags;

	float trailtime;
	float trailint;
	fx_trail_cb_t trailcb;

	qboolean physics;             /* collide with the particle box */
	vec3_t bmin, bmax;
	float elasticity;
	qboolean hitents;

	fx_timer_cb_t timercb;
	float tinterval, tjitter, tlife, tnext;

	void *model;                  /* Ghoul particles: model record (fx_ghoulmodel.cpp) */
	void *ginst;                  /* Ghoul particles: client instance, created when first drawn */
} fx_part_t;

extern fx_part_t fx_parts[FX_MAX_PARTICLES];

fx_part_t *FX_AllocParticle(void);
void FX_FreeParticle(fx_part_t *p);
void FX_ClearParticles(void);
void FX_SetLife(fx_part_t *p, float seconds);
void FX_RunParticles(float dt);
void FX_FreeDeadParticles(void);
void FX_RefreshScene(void);
extern int fx_stat_freed[3], fx_stat_chains, fx_stat_chainquads; /* om_fxtrace 2 */
void FX_EmitterBurst(fx_run_t *run, int prim, float t);
void FX_DetachParticles(int fx, int prim);

/* temp-entity style "puff" (SPEC-401 notation) */
typedef struct
{
	int sprite;                   /* client sprite list index, -1 = none */
	float w, h;                   /* first dimension (h) along up, second (w) along right */
	int r, g, b, a;
	float life_ms;
	int blend;                    /* 0 normal, 1 additive, 2 darkening */
	qboolean lit;
	int lightdir;
	vec3_t vel, accel;
	float rot, avel;
	float grow;
	float arate;
	qboolean flat;                /* oriented to (0, 0, 1) */
	const float *normal;          /* oriented to this normal */
} fx_puff_t;

void FX_PuffInit(fx_puff_t *pf);
fx_part_t *FX_Puff(const vec3_t org, const fx_puff_t *pf);
fx_part_t *FX_Line(const vec3_t start, const vec3_t end, int sprite, float halfwidth,
		int r, int g, int b, int a, float life_ms, int blend, qboolean lit);
fx_part_t *FX_Timer(const vec3_t org, fx_timer_cb_t cb, float interval_ms, float jitter,
		float life_ms);

/* ---- decals (fx_decal.c) ------------------------------------------------ */

/* impact description (SPEC-201 impact, SPEC-400 Decals, SPEC-401 Impact handling) */
typedef struct
{
	int kind;                     /* 0 world, 1 world point, 2 brush entity, 3 other entity */
	vec3_t point, normal;
	int ent;                      /* entity number, -1 = none */
	int material;                 /* 0..42, -1 = none */
	int surfflags;
	int leaf;
	/* added by the consumer */
	int markmode;                 /* 4 bullet, 5 explosion, other footprint/dent */
	float power;
	int hole;                     /* sprite index, -1 = none */
	float alphafactor;
	float yaw;
} fx_impact_t;

void FX_ClearDecals(void);
void FX_AddDecal(const fx_impact_t *im, const char *sprite, int r, int g, int b, float alpha,
		float size, float endsize, float accel, float time, float rotation);
void FX_RunDecals(float dt);
int FX_SubmitDecals(sof_alphaquad_t **list, int n, int max);

/* ---- debris (fx_debris.c) ------------------------------------------------ */

#define FX_NUM_DEBRIS_MODELS 24

typedef struct
{
	qboolean alive;
	int model;
	int part;
	const char *skin;
	vec3_t pos, vel, angles, avel;
	float gravity;
	int cclass;
	qboolean settle;              /* temp-entity chunks: lie flat when slow */
	qboolean leaf;                /* blowing paper */
	qboolean swingup;             /* blowing paper: the "swing up" flag (SPEC-AMEND-005 I5.14) */
	qboolean rest;
	float scale;
	float spawn;
	void *inst;                   /* model record */
	void *ginst;                  /* client instance, created when first drawn */
} fx_debris_t;

void FX_ClearDebris(void);
void FX_PreloadDebris(void);
fx_debris_t *FX_AddDebris(int model, const vec3_t org, const vec3_t angles, const vec3_t vel,
		const vec3_t avel, float gravity, int cclass);
void FX_RunDebris(float dt);
qboolean FX_DebrisSlotAlive(int slot);
void FX_SubmitDebris(void);
void FX_TraceDebris(void);
int FX_DebrisParts(int model);

/* ---- lights, shake (cl_lights.c, fx_system.c) ---------------------------- */

extern int cl_sof_lights_added;
void CL_SoF_HoldNewLights(qboolean hold);
int FX_ParticleSerial(void);
void FX_TraceCreated(int since, int lights_since);
void CL_SoF_AddLight(int key, const vec3_t org, int r, int g, int b, float radius,
		float life_ms, float decay);
void CL_SoF_LightningFlash(float r, float g, float b, float ms);
void CL_SoF_ClearLights(void);

/* ---- temp entities, impacts, events (fx_tent.c, fx_impact.c) ------------- */

void FX_ClearTempState(void);
void FX_Impact(fx_impact_t *im, const vec3_t dir, int power);
void FX_MarkAndSound(fx_impact_t *im, float volume_override);
void FX_MaterialEffect(const fx_impact_t *im, const vec3_t dir, int power);
qboolean FX_ReadImpact(fx_impact_t *im);
void FX_EntityEvent(int ent, int event, int parm);
void FX_ImpactChunks(const fx_impact_t *im, int material, int count, float scale, int model_override);
void FX_GlassShards(const fx_impact_t *im, float scale, int count);
void FX_SparkEmitter(const vec3_t org, const vec3_t n, float life_ms);
void FX_Sparks(const vec3_t org, const vec3_t dir, int kind);
void FX_ElectricArc(const vec3_t start, const vec3_t end, float seglen, qboolean branches);
void FX_RecursiveBolt(const vec3_t a, const vec3_t b, int depth, float prob, float width);

/* material table (SPEC-401 Surface materials) */
enum
{
	FXM_DUST = 0, FXM_CONCRETE, FXM_METAL, FXM_WATER, FXM_SPLAT, FXM_PIPE, FXM_VENT,
	FXM_COMPUTER, FXM_FLESH, FXM_GLASS, FXM_LITPANEL, FXM_VENDING, FXM_PAPER, FXM_PAPERBURST
};

typedef struct
{
	byte rgba[4];
	int markmode;                 /* 0 none, 1 hard, 2 soft */
	int footstep;                 /* first of 4 sound-list entries */
	int impact;                   /* sound-list entry, 44 = metal (random 79..81) */
	int landing;                  /* sound-list entry */
	int hole;                     /* sprite-list entry */
	int effect;                   /* FXM_* */
	const char *arg;              /* splat colour / spurt effect */
} fx_material_t;

#define FX_NUM_MATERIALS 43
extern const fx_material_t fx_materials[FX_NUM_MATERIALS];

/* debris models (SPEC-400 Debris item 1) */
typedef struct
{
	const char *dir, *file;
	int parts; /* chunk models: number of the model's parts (alphabetical) the creator chooses from; 1 = whole model */
} fx_debris_model_t;

extern const fx_debris_model_t fx_debris_models[FX_NUM_DEBRIS_MODELS];

/* ---- Ghoul shim (fx_ghoul.c) --------------------------------------------- */

void *FXG_RegisterModel(const char *dir, const char *file);
qboolean FXG_Available(void);
qboolean FXG_MuzzlePoint(int shooter, vec3_t out);
qboolean FXG_InstanceHasPart(int handle, const char *name);
void FX_FleshDrops(const vec3_t org, const vec3_t n, int r, int g, int b, int a, float size);
qboolean FXG_BoltFrame(int ent, int ghoul_inst, int bolt, qboolean random, vec3_t org,
		vec3_t fwd, vec3_t right, vec3_t up, qboolean reversed);

/* ---- rendering hand-off (fx_particle.c) ---------------------------------- */

void FX_BeginScene(void);
qboolean FX_SceneAddQuad(sof_alphaquad_t *q);
qboolean FX_SceneAddChain(sof_quadchain_t *c);
void FX_SubmitParticles(void);
void FX_SubmitChains(void);

/* ---- camera shake --------------------------------------------------------- */

extern float fx_shake, fx_shake_decay;

/* ---- misc ------------------------------------------------------------------ */

extern cvar_t *fx_interim;

float FX_DetailFactor(void);
void FX_VecToAngles(const vec3_t v, vec3_t angles);

/* "random rotation" of SPEC-401: rand(0, 6.28) (SPEC-AMEND-005 I5.15) */
#define FX_RandRot() FX_flrand(0, 6.28f)
#define FX_DEG2RAD(a) ((a) * ((float)M_PI / 180.0f))
#define FX_RAD2DEG(a) ((a) * (180.0f / (float)M_PI))
float FX_FrameRate(void);
const sofrefexport_t *FX_Ref(void);
qboolean FX_SceneActive(void);
void FX_RegisterSpriteImage(const char *name);
qboolean FX_LightPoint(const vec3_t p, vec3_t out);
int FX_AdvanceClock(float *now, float ctime, float dt, void (*step)(float));
int FX_LodCount(int N, float D);
float FX_RingAngle(int k, int N, float phi);
void FX_Bounce(vec3_t vel, const vec3_t n, float e);
float FX_StyleAlpha(int style, float t, float a0, float peak);
float FX_DecalGrowSize(float start, float end, float accel, float time, float tau);
float FX_ImpactVolume(float power);
float FX_FootVolume(float volume, float dist);
void FX_SelfTest_f(void);
void FX_MeatDemo_f(void);
void FX_InterimDraw(void);
void FXG_AddDebrisEntity(fx_debris_t *d);
void FXG_AddParticleEntity(fx_part_t *p);
void FXG_FreeInstance(void *ginst);
void FX_AddEmitterDebris(void *model, const fx_eft_particle_t *desc, const vec3_t org,
		const vec3_t angles, const vec3_t vel, const vec3_t avel, const vec3_t accel, float scale);
fx_part_t *FX_SparkParticle(const vec3_t org, const vec3_t n, int smin, int smax, float size,
		int alpha, float gravity, int sprite, int blend, int cmin, int cmax);
void FX_SodaCans(const vec3_t org, const vec3_t dir, int speed, int count);
void FX_SparkShower(const vec3_t org, const vec3_t dir);
void FX_BlowingPaper(const vec3_t org, int count);
qboolean FX_LimitPaper(void);

#endif
