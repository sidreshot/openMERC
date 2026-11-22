/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Low-fps incident ring. See header/om_fpslog.h. No engine services:
 * the client supplies the frame time, the view position, and the two
 * text fields, and writes the formatted ring to the log itself.
 */

#include <stdio.h>
#include <string.h>

#include "header/om_fpslog.h"

void
OM_FpsLog_Clear(om_fps_log_t *log)
{
	if (!log)
	{
		return;
	}

	memset(log, 0, sizeof(*log));
}

int
OM_FpsLog_EnabledByDefault(void)
{
	return 1;
}

int
OM_FpsLog_Count(const om_fps_log_t *log)
{
	if (!log || log->count < 0)
	{
		return 0;
	}

	if (log->count > OM_FPS_LOG_SLOTS)
	{
		return OM_FPS_LOG_SLOTS;
	}

	return log->count;
}

const om_fps_incident_t *
OM_FpsLog_At(const om_fps_log_t *log, int age)
{
	int n;
	int index;

	n = OM_FpsLog_Count(log);

	if (age < 0 || age >= n)
	{
		return NULL;
	}

	/* While the ring is still filling, slot 0 is the oldest and `next`
	   is the first free slot. Once it is full, `next` is the oldest. */
	if (n < OM_FPS_LOG_SLOTS)
	{
		index = age;
	}
	else
	{
		index = (log->next + age) % OM_FPS_LOG_SLOTS;
	}

	return &log->slot[index];
}

static void
CopyField(char *dst, int dstlen, const char *src)
{
	int i;
	int o;

	if (dstlen < 1)
	{
		return;
	}

	if (!src)
	{
		src = "";
	}

	o = 0;

	for (i = 0; src[i] && o < dstlen - 1; i++)
	{
		char c = src[i];

		if (c == '\n' || c == '\r')
		{
			c = ' ';
		}

		dst[o++] = c;
	}

	dst[o] = 0;
}

static int
PlaceAlreadyStored(const om_fps_log_t *log, const float pos[3])
{
	int i;
	int n;
	float limit2;

	n = OM_FpsLog_Count(log);
	limit2 = OM_FPS_PLACE_SEPARATION * OM_FPS_PLACE_SEPARATION;

	for (i = 0; i < n; i++)
	{
		const om_fps_incident_t *e = OM_FpsLog_At(log, i);
		float dx, dy, dz, dist2;

		dx = pos[0] - e->pos[0];
		dy = pos[1] - e->pos[1];
		dz = pos[2] - e->pos[2];
		dist2 = dx * dx + dy * dy + dz * dz;

		if (dist2 < limit2)
		{
			return 1;
		}
	}

	return 0;
}

int
OM_FpsLog_Write(om_fps_log_t *log, int frame_us, const float pos[3],
		const char *hardware, const char *graphics)
{
	om_fps_incident_t *e;

	if (!log || !pos)
	{
		return 0;
	}

	/* At or above 30 fps, and a non-positive frame time, write nothing. */
	if (frame_us <= OM_FPS_LOW_US)
	{
		return 0;
	}

	if (PlaceAlreadyStored(log, pos))
	{
		return 0;
	}

	e = &log->slot[log->next];
	memset(e, 0, sizeof(*e));
	e->fps = 1000000.0f / (float)frame_us;
	e->pos[0] = pos[0];
	e->pos[1] = pos[1];
	e->pos[2] = pos[2];
	CopyField(e->hardware, OM_FPS_TEXT, hardware);
	CopyField(e->graphics, OM_FPS_TEXT, graphics);

	log->next = (log->next + 1) % OM_FPS_LOG_SLOTS;

	if (log->count < OM_FPS_LOG_SLOTS)
	{
		log->count++;
	}

	return 1;
}

int
OM_FpsLog_FormatAt(const om_fps_log_t *log, int age, char *dst, int dstlen)
{
	const om_fps_incident_t *e;
	int n;

	if (!dst || dstlen < 1)
	{
		return 0;
	}

	dst[0] = 0;
	e = OM_FpsLog_At(log, age);

	if (!e)
	{
		return 0;
	}

	n = snprintf(dst, (size_t)dstlen,
			"fps_incident fps=%.2f pos=%.2f %.2f %.2f hardware=%s graphics=%s\n",
			e->fps, e->pos[0], e->pos[1], e->pos[2], e->hardware, e->graphics);

	if (n < 0)
	{
		dst[0] = 0;
		return 0;
	}

	if (n >= dstlen)
	{
		dst[dstlen - 1] = 0;
		return dstlen - 1;
	}

	return n;
}

int
OM_FpsLog_Format(const om_fps_log_t *log, char *dst, int dstlen)
{
	int age;
	int n;
	int used;

	if (!dst || dstlen < 1)
	{
		return 0;
	}

	dst[0] = 0;
	used = 0;
	n = OM_FpsLog_Count(log);

	for (age = 0; age < n; age++)
	{
		int w = OM_FpsLog_FormatAt(log, age, dst + used, dstlen - used);

		if (w <= 0)
		{
			break;
		}

		used += w;

		if (used >= dstlen - 1)
		{
			break;
		}
	}

	return used;
}
