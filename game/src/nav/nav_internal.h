/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-740 ("Level-wide navigation state")
 * Owner: package G5 (game/src/nav). Private to nav_*.cpp.
 */
#ifndef OM_NAV_INTERNAL_H
#define OM_NAV_INTERNAL_H

#include "nav_data.h"

struct NavLevel {
	bool loaded;
	om_nav::NavData data;
	int player_node;
	int player_layer;
	int around[4];                   /* the four layers around the player (written, never read) */
	uint8_t seen[om_nav::NAV_SEEN_MAX];
	float *stamp;                    /* per node */
	int search_counter;
	uint8_t *marks;                  /* 2-bit occupancy marks per layer (written, never read) */
};

extern NavLevel g_nav;

const om_nav::NavLayer *Nav_Lookup(const float p[3]);
void Nav_FreeData(void);
void Nav_DebugLine(const vec3_t a, const vec3_t b, unsigned color, int duration);
void Nav_EditorShutdown(void);

#endif
