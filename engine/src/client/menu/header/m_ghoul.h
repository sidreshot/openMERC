/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: C interface of the menu runtime to the engine's Ghoul model
 * system (the menu system object, SPEC-010 A role 2), m_ghoul.cpp.
 *
 * Implements: SPEC-803 (section 1), SPEC-010, SPEC-011 (methods used by menus)
 */

#ifndef OM_M_GHOUL_H
#define OM_M_GHOUL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mgmodel_s mgmodel_t;

/* note kinds reported to the note handler */
#define MG_NOTE_EOS 1
#define MG_NOTE_SOUND 2

/* ghoul/menus/<name>.ghl: object, sequence, registration, preparation, one
   instance; NULL when a step fails. *noeos is set when the model has no
   EOS note. */
mgmodel_t *MG_Load(const char *name, int *noeos);
void MG_Free(mgmodel_t *m);
/* part id (1-based), 0 = none */
int MG_FindPart(mgmodel_t *m, const char *name);
/* child placed on the parent's bolt (instance method 28) */
void MG_Attach(mgmodel_t *parent, int parentbolt, mgmodel_t *child, int childbolt);
/* playback modes of SPEC-011: 0 loop, 1 once, 3 static pose at offset */
void MG_Play(mgmodel_t *m, float time, int mode, float offset);
/* instance transform: rotation from three angles (radians), uniform scale */
void MG_SetTransform(mgmodel_t *m, float scale, const float angles[3]);
/* bounds of all parts at the given time in model space x scale transform */
void MG_Bounds(mgmodel_t *m, float time, float mins[3], float maxs[3]);
/* bolt translation through all parents (matrix mode 5) */
void MG_BoltPos(mgmodel_t *m, int bolt, float time, float out[3]);
/* note events up to time; the handler gets the kind and the argument */
void MG_SetNoteHandler(mgmodel_t *m, void (*fn)(void *ctx, int kind, const char *arg), void *ctx);
void MG_ProcessEvents(mgmodel_t *m, float time);
/* sequence duration in seconds for a playback mode (instance method 27) */
float MG_Duration(mgmodel_t *m, int mode);
/* player-model description ghoul/pmodels/<file> (game import #61): 1 when it loads; its team name */
int MG_PlayerTeam(const char *file, char *team, int size);
/* I1c: the model's instance (ghoul::IGhoulInst *) for drawing (cl_ghoul.c, SPEC-803 section 1 Rendering) */
void *MG_Instance(mgmodel_t *m);
/* I1c: SPEC-803 section 2 - player model from ghoul/pmodels/<file> (".gpm" included) with skins, parts and
   bolt-ons, its menu sequence looping from `time`; NULL when it cannot be built. Freed with MG_Free. */
mgmodel_t *MG_LoadPlayer(const char *file, float time);

#ifdef __cplusplus
}
#endif

#endif
