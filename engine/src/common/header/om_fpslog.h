/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Low-fps incident ring. A rendered frame longer than 1/30 s (under 30 fps)
 * stores one record: view position, the hardware description, and the
 * graphics settings. Ten records at most; a newer one overwrites the oldest.
 * A burst at a place already stored is not written again. The place counts
 * as new only when it is at least OM_FPS_PLACE_SEPARATION units from every
 * record still in the ring. The log is on by default (OM_FpsLog_EnabledByDefault).
 *
 * The caller owns the struct. A zeroed struct, or one passed through
 * OM_FpsLog_Clear, is a cold start. This file does no I/O.
 */

#ifndef OM_FPSLOG_H
#define OM_FPSLOG_H

#define OM_FPS_LOG_SLOTS 10
#define OM_FPS_LOW 30
/* SoF units are about an inch. 512 is roughly 13 m: a stutter while
   standing still is one incident, a different spot can be another. */
#define OM_FPS_PLACE_SEPARATION 512.0f
/* Integer microseconds at or faster than 30 fps. A frame of this many
   microseconds is 30.00003 fps and is not an incident. One microsecond
   longer is under 30. */
#define OM_FPS_LOW_US (1000000 / OM_FPS_LOW)
#define OM_FPS_TEXT 384
#define OM_FPS_LINE 960

typedef struct
{
	float fps;
	float pos[3];
	char hardware[OM_FPS_TEXT];
	char graphics[OM_FPS_TEXT];
} om_fps_incident_t;

typedef struct
{
	om_fps_incident_t slot[OM_FPS_LOG_SLOTS];
	int count;
	int next;
} om_fps_log_t;

void OM_FpsLog_Clear(om_fps_log_t *log);

/* 1 when the client should record drops. The cvar default is this value. */
int OM_FpsLog_EnabledByDefault(void);

/* frame_us is the rendered frame time in microseconds (the client passes
   renderdelta). Returns 1 when a new incident was stored, 0 when the frame
   is at or above 30 fps, the place is already stored, or the arguments
   cannot be used. hardware and graphics may be NULL (stored as empty). */
int OM_FpsLog_Write(om_fps_log_t *log, int frame_us, const float pos[3],
		const char *hardware, const char *graphics);

int OM_FpsLog_Count(const om_fps_log_t *log);

/* age 0 is the oldest record still kept. NULL when age is out of range. */
const om_fps_incident_t *OM_FpsLog_At(const om_fps_log_t *log, int age);

/* One log line, oldest first when age grows. Returns the length written,
   not counting the trailing NUL. */
int OM_FpsLog_FormatAt(const om_fps_log_t *log, int age, char *dst, int dstlen);

/* Every kept line, oldest first. The text never holds more than the ring. */
int OM_FpsLog_Format(const om_fps_log_t *log, char *dst, int dstlen);

#endif
