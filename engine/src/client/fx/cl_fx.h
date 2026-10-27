/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I5 (client effects). Public entry points of the Soldier of Fortune
 * client effects system for the rest of the client: network hooks (cl_sof.c),
 * per-frame hooks, view hooks (camera shake, view weapon) and the scene lists
 * handed to the renderer (SPEC-002 alpha quads / quad chains).
 *
 * Implements: SPEC-400, SPEC-401 (interfaces)
 */

#ifndef OM_CL_FX_H
#define OM_CL_FX_H

#include "../header/client.h"
#include "fx_ref.h"

/* life cycle (cl_main.c / cl_effects.c hooks) */
void CLFX_Init(void);                 /* cvars and commands */
qboolean CLFX_Wanted(void);           /* cvar om_fx (default 1): 0 = parse and drop */
void CLFX_Shutdown(void);
void CLFX_ClearState(void);           /* map change, disconnect, video restart: empty everything */
void CLFX_Precache(void);             /* level prepared: reload effect configstrings, sprites, debris */
void CLFX_RegisterSounds(void);       /* after a sound-system restart */

/* network (cl_sof.c) */
void CLFX_EffectConfigString(int slot, const char *name); /* CS_EFFECTS + slot changed */
void CLFX_ParseTempEntity(int type);  /* svc_temp_entity after the type byte */
void CLFX_ParseEffect(void);          /* svc_effect payload */
void CLFX_CulledEvent(int event, int parm, int dist, const vec3_t dir);

/* entity-state data the Quake II entity_state_t has no room for (SPEC-202):
   up to three events and the four effect slots (12 values) */
void CLFX_DeltaBegin(const entity_state_t *from, entity_state_t *to);
void CLFX_DeltaEvent(entity_state_t *to, int i, int event, int parm);
void CLFX_DeltaSlot(entity_state_t *to, int i, int value);
const short *CLFX_StateSlots(const entity_state_t *s); /* 12 values or NULL */
void CLFX_FrameParsed(void);          /* a new valid frame: dispatch entity events */

/* per rendered frame (from CL_AddTEnts): free, submit, simulate */
void CLFX_Frame(void);

/* view (cl_view.c, SPEC-310 s.12 item 3): adds the camera shake */
void CLFX_ApplyCameraShake(vec3_t vieworg, vec3_t viewangles);
void CLFX_AddViewShake(float amount, float decay); /* events 16..20, feedback primitives */

/* view weapon (player-module client hooks, SPEC-003 #2, #3, #6) */
void CLFX_PredictedFire(int code);
void CLFX_ViewWeaponNote(const char *note, qboolean shown, float fov);
void CLFX_ZoomChanged(void);

/* effects started by name from other client code (source class 2) */
int CLFX_StartNamedEffect(const char *name, const vec3_t origin, const vec3_t forward);

/* entity drawing helpers (SPEC-400 "Entity effects bits handled by the effects system") */
void CLFX_EntityAngles(const entity_state_t *s, const vec3_t net_angles, vec3_t out);
qboolean CLFX_EntityTint(const entity_state_t *s, float tint[4]);

/* renders the 3D view: through the SPEC-002 RenderFrame of the OpenMerc GL3
   renderer (with alpha quads and quad chains) when it offers one, else the
   Quake II RenderFrame (fx_scene.c) */
void CLFX_RenderFrame(refdef_t *rd);

/* scene lists of the current frame (SPEC-002 refdef 0x70..0x7c). Valid until
   the next CLFX_Frame. */
int CLFX_SceneAlphaQuads(sof_alphaquad_t ***list);
int CLFX_SceneQuadChains(sof_quadchain_t ***list);

/* provided by cl_sof.c: the SoF direction table (SPEC-201 Appendix A) */
int CL_SoF_DirIndex(const vec3_t v);
void CL_SoF_DirVector(int index, vec3_t out);

#endif
