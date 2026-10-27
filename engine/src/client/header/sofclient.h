/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I6h (client view, HUD, input, demos). Other packages: include, do
 * not edit; ask in docs/impl/I6h.md.
 *
 * Soldier of Fortune 1.07f client: the player-state fields that Quake II's
 * player_state_t has no room for, the HUD / screen interface, the input
 * hooks other client code calls, and the demo recorder / player.
 *
 * Implements: SPEC-310, SPEC-311, SPEC-312 (interfaces)
 */

#ifndef CL_HEADER_SOFCLIENT_H
#define CL_HEADER_SOFCLIENT_H

/* ---- SoF player state (SPEC-202 section 4) ------------------------------
   One record per frame slot of cl.frames (index serverframe & UPDATE_MASK),
   filled by the protocol-33 parser next to the Quake II player_state_t. */
typedef struct
{
	int valid;
	int serverframe;
	int gun_id;               /* field 1: view-weapon ghoul instance id */
	int gun_type;             /* field 1: weapon type */
	int gun_count_a;          /* field 2 */
	int gun_count_b;          /* field 3 */
	int weapon_seq;           /* field 4 */
	int resync;               /* field 5 */
	int inhibit;              /* field 6 */
	int remote_ghoul;         /* field 7 */
	int pm_type;              /* field 8, raw SoF value 0..7 */
	int pm_extra;             /* field 15 */
	short remote_angles[3];   /* field 18, angle16 */
	short remote_origin[3];   /* field 19, 1/8 units */
	int remote_type;          /* field 20 */
	int remote_ent;           /* field 20 (low byte) */
	float weapon_kick;        /* field 22 */
	int fov;                  /* field 24 */
	int rdflags;              /* field 25, all 8 bits */
	short stats[16];          /* field 26 */
	int cinfreeze;            /* field 27 */
	int music;                /* field 28 */
	int ambient;              /* field 29 */
	int rank, rankcount;      /* field 30 */
	int spectated;            /* field 31 */
} sofps_t;

/* the record of frame slot (serverframe & UPDATE_MASK) */
sofps_t *CL_SoF_PlayerExt(int serverframe);
/* called after a player state was parsed (flags = its field word) */
void CL_SoF_PlayerExtParsed(sofps_t *ps, unsigned flags);
/* the record of the frame being shown (cl.frame), never NULL */
const sofps_t *CL_SoF_CurrentPS(void);
/* the record of the frame before it, NULL when not available */
const sofps_t *CL_SoF_PreviousPS(void);
/* demo playback speaks protocol 33 without a connection (cl_sof.c) */
void CL_SoF_SetDemoProtocol(qboolean on);
/* the view for protocol 33 (cl_sofview.c, SPEC-310 sections 12, 13) */
void CL_SoF_CalcViewValues(void);
void CL_SoF_ResetViewWeapon(void);
const float *CL_SoF_ViewWeaponSway(void);  /* model-space translation */
float CL_SoF_GameFov(void);              /* view fov before horplus (ENG2) */
float CL_SoF_EffectDetail(void);           /* min(1, fps / sc_framerate) */
float CL_SoF_SmoothedFps(void);            /* refdef value 0 */
/* deathmatch byte of svc_serverdata (SPEC-203) */
int CL_SoF_ServerDeathmatch(void);
void CL_SoF_SetServerDeathmatch(int dm);

/* ---- input (cl_input.c, cl_keyboard.c; SPEC-312) -------------------------- */
qboolean CL_CinematicFreezeActive(void);
void CL_RequestCinematicSkip(void);
/* toggleconsole policy of SPEC-312 5.1: false = do not toggle */
qboolean Key_ConsoleToggleAllowed(void);
/* zoom factor of SPEC-312 section 8 for the last rendered view */
float CL_MouseZoomFactor(void);
/* force feedback (SPEC-312 section 12, no device support) */
void FF_Init(void);
void FF_Shutdown(void);
void FF_StopAll(void);

/* ---- demos (cl_demo.c; SPEC-311) ---------------------------------------- */
void CL_Demo_Init(void);
qboolean CL_Demo_Playing(void);
/* called once per client frame before packets are read */
void CL_Demo_RunFrame(void);
/* the playback ends (disconnect, end of file); true when one was running */
qboolean CL_Demo_StopPlayback(void);
/* parser hooks for recording */
void CL_Demo_ClearLevel(void);
void CL_Demo_StoreBaseline(int number, const byte *data, int len);
void CL_Demo_StoreGhoulReliable(const byte *data, int len);
void CL_Demo_UncompressedFrame(void);
void CL_Demo_AfterMessage(void);

/* ---- screen / HUD (cl_screen.c, cl_hud.c; SPEC-310) ---------------------- */
void HUD_Init(void);
void HUD_RegisterMedia(void);         /* at level registration */
void HUD_DrawInterface(void);          /* section 2 */
void HUD_DrawCrosshair(void);          /* section 3 */
void HUD_DrawLabels(void);             /* section 4 */
void HUD_DrawLayout(void);             /* section 5 */
void HUD_DrawRemoteOverlay(void);      /* section 8 */
void HUD_SetCountdown(int seconds);    /* svc_countdown */
void HUD_ParseLayout(const char *s);   /* svc_layout */
void HUD_Ignore_Init(void);
qboolean HUD_IsIgnored(int slot);
void HUD_SetNameCols(int first, int last, int value); /* svc_playernamecols */

/* screen prints (SPEC-310 section 6) */
void SCR_CaptionPrint(const char *str);
void SCR_TypewriterPrint(int x, int y, int speed, const char *str);
void SCR_CreditPrint(const char *str);
void SCR_ClearPrints(void);
/* SoF screen parts (cl_sofscreen.c, cl_screen.c) */
void SCR_SoF_Init(void);
void SCR_SoF_CenterPrint(const char *str);
void SCR_CancelTypewriter(void);
void SCR_SoF_AddNetgraph(void);
void SCR_SoF_LoadingProgress(float f);   /* menu_loading + one frame */
void SCR_SoF_DrawOverlays(void);
void SCR_SoF_DrawNet(void);
void SCR_SoF_EndFrame(void);
void SCR_UpdateScreenForce(void);        /* draws even while loading */
qboolean SCR_SoFMode(void);
void HUD_Set2D(void);
/* OpenMerc V1 HUD scaling (scr_hudscale): the HUD lays out in a virtual window of
   HUD_W() x HUD_H() pixels; between HUD_BeginScaled/HUD_EndScaled the renderer's
   SoF 2D slots multiply every coordinate and size by HUD_Scale() */
float HUD_Scale(void);
int HUD_W(void);
int HUD_H(void);
void HUD_BeginScaled(void);
void HUD_EndScaled(void);
/* OpenMerc (text size): centre prints and subtitles, HUD factor x scr_textscale */
float HUD_TextFactor(void);
int HUD_TextW(void);
int HUD_TextH(void);
void HUD_BeginText(void);
void HUD_EndText(void);
/* string-package print (SPEC-201 section 34) */
void CL_SP_Print(int id, const byte *data, int len, qboolean obit);

/* string-package text by id (package << 8 | entry); "" when unknown.
   Local stand-in until the engine string-package runtime (I1a) lands. */
const char *CL_StringById(int id);
const char *CL_StringByName(const char *ref);
int CL_StringFlags(int id);
void CL_Strings_LevelChanged(void);
void CL_Strings_RegisterMedia(void);  /* client packages at level registration */
/* string-package text with the layout flag (SPEC-201 section 34) */
void HUD_LayoutText(const char *s);
/* 2D helpers of cl_hud.c for the screen code (font 0 small, 1 medium, 2 title) */
void HUD_DrawFontText(int x, int y, const char *s, const unsigned char *rgba, int font, int keep);
int HUD_FontHeight(int font);
int HUD_TextWidth(const char *s, int font);
void HUD_DrawColouredString(int x, int y, const char *s, const unsigned char *rgba);
void HUD_DrawCharFloat(float x, float y, int ch, const unsigned char *rgba);
void HUD_DrawFade(const unsigned char *rgba);
void HUD_DrawFill(int x, int y, int w, int h, const unsigned char *rgba);
void HUD_DrawLine(float x1, float y1, float x2, float y2, float width, const unsigned char *rgba);
void HUD_DrawPic(int x, int y, const char *name, const unsigned char *rgba);
void HUD_GetPicSize(int *w, int *h, const char *name);

#endif
