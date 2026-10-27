/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I5 (client effects). Pure reader for Soldier of Fortune effect
 * definitions (.eft): a byte buffer becomes an fx_eft_t. No engine
 * dependencies, so the same file builds into the engine and into the
 * stand-alone checker (test/eft_test.c).
 *
 * Implements: SPEC-142 (layout), SPEC-400 (.eft field meanings and version rules)
 */

#ifndef OM_FX_EFT_H
#define OM_FX_EFT_H

#define FX_MAX_PRIMS 10
#define FX_NAME_LEN 64

/* primitive types (SPEC-400 "Primitive type codes") */
enum
{
	FXP_EMITTER = 0,
	FXP_SOUND = 1,
	FXP_LIGHT = 2,
	FXP_DECAL = 3,
	FXP_FEEDBACK = 4
};

/* emitter flags (+0x20) */
#define FXE_ATTACHED 0x01
#define FXE_SCALE_ELEMENT 0x02
#define FXE_SCALE_TIME 0x04
#define FXE_DIST_LOD 0x10
#define FXE_DIR_FROM_OFFSET 0x20

/* scale targets (+0x24) */
#define FXT_VELOCITY 0x01
#define FXT_ACCEL 0x02
#define FXT_HEIGHT 0x04
#define FXT_WIDTH 0x08
#define FXT_ALPHA 0x10
#define FXT_DURATION 0x20
#define FXT_RADIUS 0x40

/* particle types (+0x94) */
enum
{
	FXPT_SPRITE = 0,
	FXPT_LINE = 1,
	FXPT_ORIENTED = 2,
	FXPT_GHOUL = 3,
	FXPT_DEBRIS = 4
};

/* particle flags (+0x144) */
#define FXF_WIDTH_EQ_HEIGHT 0x00001
#define FXF_LIT 0x00002
#define FXF_ADDITIVE 0x00004
#define FXF_SUBTRACTIVE 0x00008
#define FXF_PHYSICS 0x00010
#define FXF_OLDCOLOR 0x00020
#define FXF_FADE_TRAILS 0x00040
#define FXF_SHRINK_TRAILS 0x00080
#define FXF_ABS_ACCEL 0x00100
#define FXF_FLICKER 0x00200
#define FXF_RANDOM_ADD 0x00400
#define FXF_CHEAP 0x00800
#define FXF_CURVE 0x01000
#define FXF_ONE_FRAME 0x02000
#define FXF_WATER 0x04000
#define FXF_REMOVE_ON_HIT 0x08000
#define FXF_NO_SCALING 0x10000
#define FXF_ENTITIES 0x20000
#define FXF_DECELERATE 0x40000
#define FXF_NO_ZBUFFER 0x80000

typedef struct
{
	float min, max;
} fx_range_t;

/* particle description (SPEC-400 "Particle description") */
typedef struct
{
	int version;
	fx_range_t vel[3];     /* forward, right, up */
	fx_range_t accel[3];
	int ptype;
	fx_range_t size[4];    /* height start, height end, width start, width end */
	int point2;            /* line: point2 source; oriented sprite: orientation */
	fx_range_t endscale;   /* line: PointEndScale */
	char graphic[FX_NAME_LEN];
	fx_range_t life;
	fx_range_t rgba[4];
	float alpha_min_ratio;
	float trail_life;
	fx_range_t start_angle;
	fx_range_t angle_vel;
	int flags;
	float bmin[3], bmax[3];
	float elasticity;
	char collide[FX_NAME_LEN];
	float trail_freq;
	char trail[FX_NAME_LEN];
	fx_range_t gang[3];    /* Ghoul angles: forward (pitch), right (yaw), up (roll) */
	fx_range_t gavel[3];
	int alpha_style;
	int color_style;
} fx_eft_particle_t;

typedef struct
{
	int version;
	fx_range_t delay;
	fx_range_t duration;
	int style;
	int elements;
	int flags;
	int targets;
	float rmin[3], rmax[3];
	float radius;
	float escale[2];
	float tscale[2];
	float dist_per_unit;
	fx_eft_particle_t p;
} fx_eft_emitter_t;

typedef struct
{
	char name[FX_NAME_LEN];
	fx_range_t volume;
	fx_range_t atten;
} fx_eft_sound_t;

typedef struct
{
	fx_range_t radius;
	fx_range_t rgb[3];
	fx_range_t life;
} fx_eft_light_t;

typedef struct
{
	int version;
	char name[FX_NAME_LEN];
	float start_size, end_size;
	float accel;      /* field 0x50 (editor label "Growth Duration") */
	float time;       /* field 0x54 (editor label "Growth Acceleration") */
	int orientation;  /* 0 forward, 1 downward, 2 backward */
} fx_eft_decal_t;

typedef struct
{
	fx_range_t blind, deaf, shake;
	int ff_id, ff_copy;
} fx_eft_feedback_t;

typedef struct
{
	int type;
	fx_range_t trigger;
	int size; /* bytes the record took in the file (checker statistics) */
	union
	{
		fx_eft_emitter_t emitter;
		fx_eft_sound_t sound;
		fx_eft_light_t light;
		fx_eft_decal_t decal;
		fx_eft_feedback_t feedback;
	} u;
} fx_eft_prim_t;

typedef struct
{
	float version;
	int repeat_mode;      /* 0 Instant, 1 Continual */
	fx_range_t repeat;    /* after the trailer limit was applied */
	fx_range_t limit;     /* trailer (version 1: = repeat delay) */
	int count;
	fx_eft_prim_t prims[FX_MAX_PRIMS];
} fx_eft_t;

/* Parses an .eft image. Returns 0 on success; otherwise a negative code and
   a reason in err (if given). Trailing bytes after the trailer are an error
   (*bytes_left reports what was not consumed, for the checker). */
int FX_ParseEft(const unsigned char *buf, int len, fx_eft_t *out, char *err,
		int errsize, int *bytes_left);

/* Writes a definition as a version 3 file (emitter 3, particle 7; SPEC-400
   Editor item 3). Returns the number of bytes written, or -1 if it does not
   fit. */
int FX_WriteEft(const fx_eft_t *in, unsigned char *buf, int size);

#endif
