/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1c (engine client: the Ghoul client system). Other packages: include, do not edit; ask in
 * docs/impl/I1c.md.
 *
 * The client owns the engine's client Ghoul system (SPEC-010 A, role "client"): it feeds it the
 * svc_ghoulreliable / svc_ghoulunreliable payloads of the server (SPEC-013), keeps the game clock the
 * renderer and the Ghoul instances run on (SPEC-202 section 5), hands the system to the renderer through
 * the SPEC-002 Init slot, turns Ghoul entities of the frame into renderer entities (SPEC-002) and adds the
 * view weapon (SPEC-310 section 13). It also draws the menu models of the menu system (SPEC-803 section 1).
 *
 * Implements: SPEC-010, SPEC-011, SPEC-013 (client side), SPEC-002 (Init slot, entities), SPEC-202 (section 5,
 * game clock), SPEC-310 (section 13), SPEC-803 (section 1, rendering)
 */

#ifndef CL_HEADER_CL_GHOUL_H
#define CL_HEADER_CL_GHOUL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- cl_ghoul.c: engine side -------------------------------------------- */

/* CL_Init / CL_Shutdown */
void CL_Ghoul_Init(void);
void CL_Ghoul_Shutdown(void);

/* the client Ghoul system (ghoul::IGhoulSystem *, SPEC-010 A); never NULL once the library is up. The
   player module's client side gets this pointer (SPEC-003 client import 0). */
void *CL_Ghoul_System(void);
/* true when Ghoul entities are drawn: the running renderer accepted the system (SPEC-002 Init slot) */
int CL_Ghoul_Active(void);

/* renderer loading (vid.c): after a renderer started / before it goes away */
void CL_Ghoul_RendererStarted(void);
void CL_Ghoul_RendererStopping(void);

/* protocol 33 parser (cl_sof.c) */
void CL_Ghoul_FrameParsed(int serverframe, float gametime);  /* svc_frame: number and game time */
void CL_Ghoul_ParseReliable(const unsigned char *data, int len);   /* svc 26 payload */
void CL_Ghoul_ParseUnreliable(const unsigned char *data, int len); /* svc 27 payload */
void CL_Ghoul_LoadingDone(void);   /* precache finished: system methods 23 then 7 */
void CL_Ghoul_ClearState(void);    /* CL_ClearState (disconnect, new serverdata): system method 8 */

/* once per rendered client frame (cl_main.c), before the screen is drawn, with the unclamped frame time */
void CL_Ghoul_RunFrame(float frametime);
/* the view is rebuilt (cl_entities.c CL_AddEntities, before the view is computed): frame-record clock
   values and the previous frame's view origin (SPEC-AMEND-006 I1c.1 rule 4, I1c.2 step 8) */
void CL_Ghoul_ViewRebuild(void);
/* game clock (SPEC-202 section 5): the estimate now, seconds */
float CL_Ghoul_GameClock(void);
/* SPEC-002 frame record 0x48 / 0x4c / 0x50 as the last view rebuild set them */
void CL_Ghoul_FrameRecordClock(float *old, float *delta, float *now);
/* auto-rotate yaw of effect 0x2000 (SPEC-AMEND-006 I1c.7: real-time clock), degrees */
float CL_Ghoul_AutoRotateYaw(void);

/* entities (cl_sof.c, cl_entities.c): CL_Ghoul_EntityValues fills angles, frame, old frame, back-lerp and
   alpha of any packet entity (SPEC-AMEND-006 I1c.2); then CL_Ghoul_AddEntity adds a Ghoul one (own = the
   viewer's own entity). It returns false when no Ghoul drawing is available (the caller keeps its
   placeholder) */
struct entity_s;
struct entity_state_s;
void CL_Ghoul_EntityValues(struct entity_s *ent, const struct entity_state_s *s);
int CL_Ghoul_AddEntity(struct entity_s *ent, const struct entity_state_s *s, int own);
void CL_Ghoul_AddViewWeapon(void);

/* menus (m_model.c): draws one root menu model at screen point (x, y) with the menu clock; camerapart =
   the model's `forced_camera` part or 0. The caller has entered the renderer's menu-model mode. */
void CL_Ghoul_DrawMenuModel(void *instance, float x, float y, float time, int camerapart);
/* MENU-SCRATCH: a menu model without light nodes of its own (the player preview without the stock panel) */
void CL_Ghoul_DrawMenuModelLit(void *instance, float x, float y, float time, float light);

/* ---- cl_ghoulsys.cpp: thin C layer over the C++ Ghoul interfaces (ghoul_api.h) ---- */
void *CLG_System(void);
void CLG_ReadReliable(const unsigned char *data, int len, int specular);
void CLG_ReadUnreliable(int frame, const unsigned char *data, int len, float time);
void CLG_StartAccepting(void);
void CLG_Purge(void);
void CLG_Reset(void);
void *CLG_FindInstance(int handle);
void CLG_GetTransform(void *instance, float m16[16]);
void CLG_SetTransform(void *instance, const float m16[16]);
void CLG_DrawMenu(void *instance, float x, float y, float time, int camerapart, int notexturestack);
void CLG_DrawMenuLit(void *instance, float x, float y, float time, int notexturestack, float light);

#ifdef __cplusplus
}
#endif

#endif
