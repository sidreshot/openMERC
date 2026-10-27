/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * The SoF player-movement routine (SPEC-314), shared by the server's
 * game import #81 and the client's movement prediction, so that both run
 * exactly the same code (SPEC-314 section 21, SPEC-AMEND-010 M4/M11).
 *
 * The routine knows nothing about the network protocol, edicts or client
 * entities: the caller fills a sofpm_t with the 28-byte movement state,
 * the user command and callbacks, and copies the results out. Protocol
 * or compatibility variants live in the callers.
 *
 * Implements: SPEC-314 (Behaviour 1-17, section 19 command expansion,
 * section 20 prediction command loop)
 */

#ifndef OM_COMMON_SOF_PMOVE_H
#define OM_COMMON_SOF_PMOVE_H

#include "sof/game_api.h" /* sof_pmove_state_t, sof_usercmd_t */

#ifdef __cplusplus
extern "C" {
#endif

#define SOFPM_MAXTOUCH 32

/* movement flags (SPEC-314 section 2) */
#define SOFPMF_DUCKED 0x01
#define SOFPMF_JUMP_HELD 0x02
#define SOFPMF_ON_GROUND 0x04
#define SOFPMF_TIME_WATERJUMP 0x08
#define SOFPMF_TIME_LAND 0x10
#define SOFPMF_TIME_TELEPORT 0x20
#define SOFPMF_EXHAUSTED 0x40     /* read by SoFPM_RunCommand only (section 19/20) */
#define SOFPMF_NO_PREDICTION 0x80 /* not read by the routine (section 20) */

#define SOFPM_MASK_PLAYERSOLID 0x02010003
#define SOFPM_MASK_DEADSOLID 0x00010003
#define SOFPM_MASK_SPECTATOR 0x02000003 /* world only (type 2) */

#define SOFPM_RUN_BUTTON 0x20

/* the parts of a trace result the routine reads (SPEC-314 section 5) */
typedef struct
{
	int allsolid;
	int startsolid;
	float fraction;
	float endpos[3];
	float normal[3];
	int surfflags;   /* bit 0x2 = slick */
	int contents;
	void *ent;       /* non-null = something was touched */
} sofpm_trace_t;

typedef struct sofpm_s
{
	/* in/out */
	sof_pmove_state_t s;
	sof_usercmd_t cmd;           /* in; moves zeroed for types 3-7 */
	float mins[3], maxs[3];      /* in/out: written only by the duck rules */

	/* in */
	int snapinitial;
	float stagger;               /* remaining knock-back stagger, seconds */
	float sizematters;           /* gserver SIZEMATTERS, 1 = normal */

	/* out */
	int numtouch;
	void *touchents[SOFPM_MAXTOUCH];
	float viewangles[3];
	float viewheight;
	void *groundentity;
	int watertype;
	int waterlevel;
	float probenormal[3];        /* debug: normal of the last ground probe hit (0 = none) */
	int debug;                   /* in: print every ground probe through devprint */

	/* callbacks; mask is the content mask of the move (the caller's
	   player mask, or SOFPM_MASK_SPECTATOR for the world-only spectator
	   slide) */
	void (*trace)(void *ctx, sofpm_trace_t *out, const float *start, const float *mins,
			const float *maxs, const float *end, int mask);
	int (*pointcontents)(void *ctx, const float *point);
	int (*groundisclient)(void *ctx, void *ent);
	void (*devprint)(void *ctx, const char *msg); /* may be NULL */
	void *ctx;
	int mask;                    /* player mask for the normal traces */
} sofpm_t;

/* one move (SPEC-314 Behaviour 1). Outputs are cleared first. */
void SoFPM_Move(sofpm_t *pm);

/* the command expansion of SPEC-314 section 19/20: run button doubling
   (unless flags have 0x40), forward/side scaled by q/255, and command
   splitting (msec > 33 with vertical velocity: msec/10 moves, the first
   (msec mod 10) + 10 ms, the rest 10 ms). Every move runs SoFPM_Move on
   the same block (state, box and inputs carried over). q is the
   movement-speed byte (255 = full speed). Returns the number of moves. */
int SoFPM_RunCommand(sofpm_t *pm, const sof_usercmd_t *cmd, int flags, int q);

#ifdef __cplusplus
}
#endif

#endif
