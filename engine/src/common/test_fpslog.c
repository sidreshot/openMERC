/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Calls the shipped low-fps incident writer (om_fpslog.c) from a cold start.
 * Exit code = number of failed checks.
 *
 * Usage: fpslog_test
 */

#include <stdio.h>
#include <string.h>

#include "header/om_fpslog.h"

static int failed;

static void
Check(int cond, const char *what)
{
	if (cond)
	{
		printf("PASS %s\n", what);
	}
	else
	{
		printf("FAIL %s\n", what);
		failed++;
	}
}

static int
HasPos(const om_fps_log_t *log, float x, float y, float z)
{
	int i;
	int n = OM_FpsLog_Count(log);

	for (i = 0; i < n; i++)
	{
		const om_fps_incident_t *e = OM_FpsLog_At(log, i);

		if (e->pos[0] == x && e->pos[1] == y && e->pos[2] == z)
		{
			return 1;
		}
	}

	return 0;
}

int
main(void)
{
	om_fps_log_t log;
	const om_fps_incident_t *e;
	float pos[3];
	float jitter[3];
	float near_place[3];
	float far_place[3];
	char text[OM_FPS_LOG_SLOTS * OM_FPS_LINE];
	const char *hardware = "cpu AMD Radeon gfx1201 Mesa 26.1.6";
	const char *graphics = "r_mode=-2 vid_fullscreen=1 fov=95 r_om_tess=8 r_om_snow=1";
	int i;
	int wrote;
	int count_before;

	printf("fpslog_test cold start\n");
	Check(OM_FpsLog_EnabledByDefault() == 1, "log is on by default");

	OM_FpsLog_Clear(&log);
	Check(OM_FpsLog_Count(&log) == 0, "cold start holds nothing");

	pos[0] = 10000.0f;
	pos[1] = 20000.0f;
	pos[2] = -3000.0f;

	wrote = OM_FpsLog_Write(&log, 0, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == 0, "zero frame time writes nothing");

	wrote = OM_FpsLog_Write(&log, -1, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == 0, "negative frame time writes nothing");

	/* 60 fps and the last microsecond that is still at or above 30. */
	wrote = OM_FpsLog_Write(&log, 1000000 / 60, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == 0, "60 fps writes nothing");

	wrote = OM_FpsLog_Write(&log, OM_FPS_LOW_US, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == 0, "at 30 fps writes nothing");

	wrote = OM_FpsLog_Write(&log, OM_FPS_LOW_US + 1, pos, hardware, graphics);
	Check(wrote == 1, "under 30 fps writes one incident");
	Check(OM_FpsLog_Count(&log) == 1, "one record after the first drop");

	e = OM_FpsLog_At(&log, 0);
	Check(e != NULL, "the record can be read");
	Check(e && e->pos[0] == pos[0] && e->pos[1] == pos[1] && e->pos[2] == pos[2],
			"record stores the view position");
	Check(e && strcmp(e->hardware, hardware) == 0, "record stores the hardware text");
	Check(e && strcmp(e->graphics, graphics) == 0, "record stores the graphics settings");
	Check(e && e->fps < 30.0f && e->fps > 0.0f, "stored fps is under 30");

	OM_FpsLog_Format(&log, text, (int)sizeof(text));
	Check(strstr(text, hardware) != NULL, "log line contains the hardware text");
	Check(strstr(text, graphics) != NULL, "log line contains the graphics settings");
	Check(strstr(text, "10000.00 20000.00 -3000.00") != NULL, "log line contains the position");

	count_before = OM_FpsLog_Count(&log);
	wrote = OM_FpsLog_Write(&log, 50000, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == count_before,
			"a repeated sample at the same place leaves the count unchanged");

	jitter[0] = pos[0] + 1.0f;
	jitter[1] = pos[1];
	jitter[2] = pos[2];
	wrote = OM_FpsLog_Write(&log, 50000, jitter, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == count_before,
			"a one-unit jitter is the same place");

	near_place[0] = pos[0] + OM_FPS_PLACE_SEPARATION - 1.0f;
	near_place[1] = pos[1];
	near_place[2] = pos[2];
	wrote = OM_FpsLog_Write(&log, 50000, near_place, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == count_before,
			"a move short of the separation is still the same place");

	/* Ten further places, each far from every place already stored. */
	for (i = 1; i <= OM_FPS_LOG_SLOTS; i++)
	{
		float p[3];

		p[0] = 10000.0f + (float)i * 8000.0f;
		p[1] = 20000.0f + (float)i * 9000.0f;
		p[2] = -3000.0f - (float)i * 7000.0f;
		wrote = OM_FpsLog_Write(&log, 40000, p, hardware, graphics);
		Check(wrote == 1, "a distinct place is stored");
	}

	Check(OM_FpsLog_Count(&log) == OM_FPS_LOG_SLOTS, "ten further places leave exactly 10 records");
	Check(!HasPos(&log, pos[0], pos[1], pos[2]), "the oldest record is gone");
	Check(HasPos(&log, 10000.0f + 8000.0f, 20000.0f + 9000.0f, -3000.0f - 7000.0f),
			"the second place is still kept");
	Check(HasPos(&log,
			10000.0f + (float)OM_FPS_LOG_SLOTS * 8000.0f,
			20000.0f + (float)OM_FPS_LOG_SLOTS * 9000.0f,
			-3000.0f - (float)OM_FPS_LOG_SLOTS * 7000.0f),
			"the newest place is kept");

	e = OM_FpsLog_At(&log, 0);
	Check(e && e->pos[0] == 10000.0f + 8000.0f, "age 0 is the oldest survivor");
	e = OM_FpsLog_At(&log, OM_FPS_LOG_SLOTS - 1);
	Check(e && e->pos[0] == 10000.0f + (float)OM_FPS_LOG_SLOTS * 8000.0f,
			"the last age is the newest place");
	Check(e && strstr(e->hardware, "Radeon") != NULL, "overwritten ring still holds hardware");
	Check(e && strstr(e->graphics, "r_om_tess=8") != NULL, "overwritten ring still holds graphics");

	far_place[0] = 10000.0f + (float)OM_FPS_LOG_SLOTS * 8000.0f;
	far_place[1] = 20000.0f + (float)OM_FPS_LOG_SLOTS * 9000.0f;
	far_place[2] = -3000.0f - (float)OM_FPS_LOG_SLOTS * 7000.0f;
	count_before = OM_FpsLog_Count(&log);

	for (i = 0; i < 8; i++)
	{
		wrote = OM_FpsLog_Write(&log, 80000, far_place, hardware, graphics);
		Check(wrote == 0, "burst at one stored place does not add a line");
	}

	Check(OM_FpsLog_Count(&log) == count_before, "the burst leaves the count unchanged");

	OM_FpsLog_Format(&log, text, (int)sizeof(text));
	Check(strstr(text, "10000.00 20000.00 -3000.00") == NULL,
			"formatted log no longer contains the overwritten place");
	Check(strstr(text, hardware) != NULL, "formatted ring still names the hardware");
	Check(strstr(text, graphics) != NULL, "formatted ring still names the graphics settings");

	/* A fast frame at a brand new place must not open a slot. */
	pos[0] = 900000.0f;
	pos[1] = 1.0f;
	pos[2] = 2.0f;
	wrote = OM_FpsLog_Write(&log, OM_FPS_LOW_US, pos, hardware, graphics);
	Check(wrote == 0 && OM_FpsLog_Count(&log) == OM_FPS_LOG_SLOTS,
			"a 30 fps sample at a new place writes nothing");
	Check(!HasPos(&log, 900000.0f, 1.0f, 2.0f), "that place was not stored");

	printf("fpslog_test failures=%d\n", failed);
	return failed;
}
