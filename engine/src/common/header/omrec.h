/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc test aid (not in the original): automatic demo + debug log
 * recording (om_autorecord, client/cl_omrec.c). The server side keeps the
 * last movement result of the local player (client 0) so the client can
 * log what the authoritative pmove did; the common code offers a print and
 * a command hook.
 */

#ifndef COMMON_OMREC_H
#define COMMON_OMREC_H

typedef struct
{
	int count;           /* moves since the server started (wraps) */
	int pm_type;
	float origin[3];     /* units */
	float velocity[3];   /* units/s */
	int pm_flags;
	int pm_time;
	int msec;
	int forwardmove, sidemove, upmove;
	int buttons;
	int onground;        /* the move ended with a ground entity */
	int waterlevel;
	int watertype;
	float probenormal[3];
	float stagger;
} omrec_pm_t;

/* server/sv_pmove.c: last move of the local player (edict 1) */
extern omrec_pm_t omrec_pm;
/* server/sv_pmove.c: running map name, "" without a local server */
const char *SV_OmRec_MapName(void);

/* common/clientserver.c: called with every printed text before the
   console gets it; a non-zero return keeps it off the console (it still
   reaches the terminal and the log file) */
extern int (*com_printhook)(const char *msg);
/* common/cmdparser.c: called with every executed command line */
extern void (*com_cmdhook)(const char *text);

#endif
