/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1d (engine client: the client side of the player module). Other packages: include, do not edit;
 * ask in docs/impl/I1d.md.
 *
 * The client hosts the client side of the player module (player.dll, SPEC-003): it loads it for every
 * connection (GetPlayerClientAPI with the engine's common and client import tables), keeps a prediction
 * inventory fed by svc 28 / 29 / 30, writes the shared context flag at the start of every client frame, and
 * plays what the view weapon's animation notes ask for: `sound` notes (view-weapon sounds), `effect` notes
 * (effects on the view model's bolts, SPEC-400 "View weapon") and the predicted fire effects of `fire` /
 * `altfire` notes. When the client does not predict (single player, listen server, `cl_predict 0`) the engine
 * registers its own note handlers on the view-weapon instance the server replicates; when it predicts, the
 * module's own callbacks on its predicted instance call the client imports.
 *
 * Implements: SPEC-003 (client side), SPEC-602 (client side), SPEC-400 ("View weapon"), SPEC-310 section 13
 */

#ifndef CL_HEADER_CL_PLAYER_H
#define CL_HEADER_CL_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- cl_player.c ------------------------------------------------------------------------------------- */

void CL_Player_Init(void);        /* CL_Init: cvars, commands */
void CL_Player_Shutdown(void);    /* CL_Shutdown: the client side is released */

/* start of every client frame (CL_Frame, before packets are read): context flag, the session of a demo */
void CL_Player_FrameStart(void);
/* every client frame after the packets were read: the view weapon's notes (engine handlers while not
   predicting) */
void CL_Player_Frame(void);
/* CL_SendConnectPacket: a connection starts - `predicting` is set and the client side loaded before the
   connect's userinfo is sent */
void CL_Player_ConnectionStarting(void);
/* CL_Disconnect: the prediction inventory goes and the client side is unloaded */
void CL_Player_Disconnect(void);

/* protocol 33 parser (cl_sof.c): svc 28 (inventory events), svc 29 (restart prediction), svc 30 (full
   inventory state); each reads its payload from net_message */
void CL_Player_ParseEvents(void);
void CL_Player_ParseRestart(void);
void CL_Player_ParseFullState(void);

/* user command (cl_input.c CL_SendCmd, after the command was built): the predicted fire times
   (SPEC-202 section 6.1); 0 when nothing was fired */
void CL_Player_CommandFireTimes(float *primary, float *alternate);

/* true while the client predicts its weapon (SPEC-602 section 1) */
int CL_Player_Predicting(void);

/* the view-weapon Ghoul instance the client draws: the predicted one while predicting, else NULL (the
   caller uses the instance of player-state field 1) */
void *CL_Player_PredictedViewInstance(void);

/* client import 4 / 6 / 2-3 behaviour, also used by the engine's own note handlers */
void CL_Player_NoteSound(const char *note);
void CL_Player_NoteEffect(const char *note);
void CL_Player_FireEffects(int weapon, int alternate);

/* ---- cl_playerghl.cpp: C layer over the C++ objects (inventory method table, Ghoul callbacks) -------- */

void CLP_InvSetOwner(void *inv, void *ctx);              /* method 61 */
void CLP_InvThink(void *inv);                            /* method 3 */
int CLP_InvCommand(void *inv, const char *cmd, int force); /* method 4 */
int CLP_InvCurrentWeapon(void *inv);                     /* method 23 */
void CLP_InvClearEvents(void *inv);                      /* method 68 */
void CLP_InvReadEvents(void *inv);                       /* method 69 */
void CLP_InvApplyEvents(void *inv);                      /* method 70 */
void CLP_InvReadFullState(void *inv, void *sizebuf);     /* method 77 */

/* the engine's note handlers on a (replicated) view-weapon instance: registered once per instance, then
   its events processed up to `time` */
void CLP_ViewNotesAttach(void *instance);
void CLP_ViewNotesProcess(void *instance, float time);

#ifdef __cplusplus
}
#endif

#endif
