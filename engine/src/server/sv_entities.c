/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Server side of the frame messages: which entities a client sees, the
 * entity and player-state deltas.
 *
 * OpenMerc: the protocol-33 frame (SPEC-202): 32-bit entity headers,
 * SoF field order and size classes, effect slots, events, packed solid,
 * the 32-bit player-state flag word with the SoF fields, the per-client
 * Ghoul handles in the frame field and the view-weapon field, sound-only
 * entities and culled events.
 *
 * Implements: SPEC-202 s1-s5, SPEC-201 s24 (svc_culledEvent),
 *             SPEC-011 (instance method 52 offers), SPEC-013 s4
 *
 * =======================================================================
 */

#include <stdint.h>

#include "header/server.h"

/* entity header bits (SPEC-202 s2.1) */
#define SU_ORIGIN1 (1u << 0)
#define SU_ORIGIN2 (1u << 1)
#define SU_ANGLE2 (1u << 2)
#define SU_ANGLE3 (1u << 3)
#define SU_ANGLE1 (1u << 4)
#define SU_ORIGIN3 (1u << 5)
#define SU_EVENTS (1u << 6)
#define SU_MORE1 (1u << 7)
#define SU_RENDERFX16 (1u << 8)
#define SU_MODEL2 (1u << 9)
#define SU_RENDERFX8 (1u << 10)
#define SU_REMOVE (1u << 11)
#define SU_FRAME16 (1u << 12)
#define SU_EFFECTSLOTS (1u << 13)
#define SU_EFFECTS16 (1u << 14)
#define SU_MORE2 (1u << 15)
#define SU_EFFECTS8 (1u << 16)
#define SU_FRAME8 (1u << 17)
#define SU_SOLID (1u << 18)
#define SU_SKIN8 (1u << 19)
#define SU_MODEL (1u << 20)
#define SU_SOUND (1u << 21)
#define SU_SOUND256 (1u << 22)
#define SU_MORE3 (1u << 23)
#define SU_NUMBER16 (1u << 24)
#define SU_SKIN16 (1u << 25)
#define SU_YAWOFS (1u << 26)

/* player-state flags (SPEC-202 s4) */
#define SPS_M_TYPE 0x00000001
#define SPS_M_ORIGIN 0x00000002
#define SPS_M_VELOCITY 0x00000004
#define SPS_M_TIME 0x00000008
#define SPS_M_FLAGS 0x00000010
#define SPS_M_GRAVITY 0x00000020
#define SPS_M_DELTA_ANGLES 0x00000040
#define SPS_VIEWOFFSET 0x00000080
#define SPS_VIEWANGLES 0x00000100
#define SPS_KICKANGLES 0x00000200
#define SPS_BLEND 0x00000400
#define SPS_FOV 0x00000800
#define SPS_REMOTE_ORIGIN 0x00001000
#define SPS_REMOTE_VIEW 0x00002000
#define SPS_RDFLAGS 0x00004000
#define SPS_REMOTE_ANGLES 0x00008000
#define SPS_GUN 0x00010000
#define SPS_GUN_COUNT_A 0x00020000
#define SPS_GUN_COUNT_B 0x00040000
#define SPS_WEAPON_KICK 0x00080000
#define SPS_CINFREEZE 0x00400000
#define SPS_MUSIC 0x00800000
#define SPS_AMBIENT 0x01000000
#define SPS_RANK 0x02000000
#define SPS_SPECTATED 0x04000000
#define SPS_PMEXTRA 0x08000000
#define SPS_RESYNC 0x10000000
#define SPS_INHIBIT 0x20000000
#define SPS_WEAPONSEQ 0x40000000

#define RF_SOUNDONLY 0x80
#define RF_GHOUL 0x400000

#define SV_SOUNDONLY_RANGE 1200.0f
#define SV_CULLED_RANGE 1000.0f

static byte fatpvs[65536 / 8];

/* ---------------------------------------------------------------------
 * entity deltas (SPEC-202 s2)
 * --------------------------------------------------------------------- */

static qboolean
SV_SlotsInUse(const sof_entity_state_t *s)
{
	return s->effect_slots[0] || s->effect_slots[3] || s->effect_slots[6] || s->effect_slots[9];
}

static unsigned
SV_DeltaBits(const sof_entity_state_t *from, const sof_entity_state_t *to, int *slotmask)
{
	unsigned bits = 0;
	int i;

	if (to->origin[0] != from->origin[0])
	{
		bits |= SU_ORIGIN1;
	}

	if (to->origin[1] != from->origin[1])
	{
		bits |= SU_ORIGIN2;
	}

	if (to->origin[2] != from->origin[2])
	{
		bits |= SU_ORIGIN3;
	}

	if (to->angles[0] != from->angles[0])
	{
		bits |= SU_ANGLE1;
	}

	if (to->angles[1] != from->angles[1])
	{
		bits |= SU_ANGLE2;
	}

	if (to->angles[2] != from->angles[2])
	{
		bits |= SU_ANGLE3;
	}

	if (to->angle_diff != from->angle_diff)
	{
		bits |= SU_YAWOFS;
	}

	/* events are never delta-compared */
	if (to->event[0])
	{
		bits |= SU_EVENTS;
	}

	if (to->modelindex != from->modelindex)
	{
		bits |= SU_MODEL;
	}

	if (to->modelindex2 != from->modelindex2)
	{
		bits |= SU_MODEL2;
	}

	if (to->frame != from->frame)
	{
		bits |= (to->frame < 256) ? SU_FRAME8 : SU_FRAME16;
	}

	if ((unsigned)to->skinnum != (unsigned)from->skinnum)
	{
		unsigned v = (unsigned)to->skinnum;

		if (v < 256)
		{
			bits |= SU_SKIN8;
		}
		else if (v < 65536)
		{
			bits |= SU_SKIN16;
		}
		else
		{
			bits |= SU_SKIN8 | SU_SKIN16;
		}
	}

	if (to->effects != from->effects)
	{
		int v = (int)to->effects;

		if (v < 256)
		{
			bits |= SU_EFFECTS8;
		}
		else if (v < 32768)
		{
			bits |= SU_EFFECTS16;
		}
		else
		{
			bits |= SU_EFFECTS8 | SU_EFFECTS16;
		}
	}

	if (to->renderfx != from->renderfx)
	{
		int v = to->renderfx;

		if (v < 256)
		{
			bits |= SU_RENDERFX8;
		}
		else if (v < 32768)
		{
			bits |= SU_RENDERFX16;
		}
		else
		{
			bits |= SU_RENDERFX8 | SU_RENDERFX16;
		}
	}

	if (to->solid != from->solid)
	{
		bits |= SU_SOLID;
	}

	/* the sound data goes only with a changed sound index */
	if (to->sound != from->sound)
	{
		bits |= SU_SOUND;

		if (to->sound & 256)
		{
			bits |= SU_SOUND256;
		}
	}

	*slotmask = 0;

	if (SV_SlotsInUse(from) || SV_SlotsInUse(to))
	{
		for (i = 0; i < 12; i++)
		{
			if (to->effect_slots[i] != from->effect_slots[i])
			{
				*slotmask |= 1 << i;
			}
		}

		if (*slotmask)
		{
			bits |= SU_EFFECTSLOTS;
		}
	}

	if (to->number >= 256)
	{
		bits |= SU_NUMBER16;
	}

	return bits;
}

static void
SV_WriteHeader(sizebuf_t *msg, unsigned bits, int number)
{
	if (bits & 0xFF000000)
	{
		bits |= SU_MORE3 | SU_MORE2 | SU_MORE1;
	}
	else if (bits & 0x00FF0000)
	{
		bits |= SU_MORE2 | SU_MORE1;
	}
	else if (bits & 0x0000FF00)
	{
		bits |= SU_MORE1;
	}

	MSG_WriteByte(msg, bits & 255);

	if (bits & SU_MORE1)
	{
		MSG_WriteByte(msg, (bits >> 8) & 255);
	}

	if (bits & SU_MORE2)
	{
		MSG_WriteByte(msg, (bits >> 16) & 255);
	}

	if (bits & SU_MORE3)
	{
		MSG_WriteByte(msg, (bits >> 24) & 255);
	}

	if (bits & SU_NUMBER16)
	{
		MSG_WriteShort(msg, number);
	}
	else
	{
		MSG_WriteByte(msg, number);
	}
}

/*
 * Writes part of a packetentities message. Can delta from either a
 * baseline or a previous packet_entity.
 */
void
SV_WriteDeltaEntity(const sof_entity_state_t *from, const sof_entity_state_t *to,
		sizebuf_t *msg, qboolean force, qboolean newentity)
{
	static const sof_entity_state_t nullstate;
	unsigned bits;
	int slotmask, i;

	(void)newentity;

	if (!from)
	{
		from = &nullstate;
	}

	if ((to->number <= 0) || (to->number >= SOF_MAX_EDICTS))
	{
		Com_Error(ERR_FATAL, "Entity number %i out of range", to->number);
	}

	bits = SV_DeltaBits(from, to, &slotmask);

	if (!bits && !force)
	{
		return; /* nothing to send */
	}

	SV_WriteHeader(msg, bits, to->number);

	if (bits & SU_MODEL)
	{
		MSG_WriteByte(msg, to->modelindex & 255);
	}

	if (bits & SU_MODEL2)
	{
		MSG_WriteByte(msg, to->modelindex2 & 255);
	}

	if (bits & SU_FRAME8)
	{
		MSG_WriteByte(msg, to->frame & 255);
	}
	else if (bits & SU_FRAME16)
	{
		MSG_WriteShort(msg, to->frame);
	}

	if ((bits & SU_SKIN8) && (bits & SU_SKIN16))
	{
		MSG_WriteLong(msg, to->skinnum);
	}
	else if (bits & SU_SKIN8)
	{
		MSG_WriteByte(msg, to->skinnum & 255);
	}
	else if (bits & SU_SKIN16)
	{
		MSG_WriteShort(msg, to->skinnum);
	}

	if ((bits & SU_EFFECTS8) && (bits & SU_EFFECTS16))
	{
		MSG_WriteLong(msg, (int)to->effects);
	}
	else if (bits & SU_EFFECTS8)
	{
		MSG_WriteByte(msg, to->effects & 255);
	}
	else if (bits & SU_EFFECTS16)
	{
		MSG_WriteShort(msg, (int)to->effects);
	}

	if ((bits & SU_RENDERFX8) && (bits & SU_RENDERFX16))
	{
		MSG_WriteLong(msg, to->renderfx);
	}
	else if (bits & SU_RENDERFX8)
	{
		MSG_WriteByte(msg, to->renderfx & 255);
	}
	else if (bits & SU_RENDERFX16)
	{
		MSG_WriteShort(msg, to->renderfx);
	}

	if (bits & SU_ORIGIN1)
	{
		SV_WriteCoord(msg, to->origin[0]);
	}

	if (bits & SU_ORIGIN2)
	{
		SV_WriteCoord(msg, to->origin[1]);
	}

	if (bits & SU_ORIGIN3)
	{
		SV_WriteCoord(msg, to->origin[2]);
	}

	if (bits & SU_ANGLE1)
	{
		SV_WriteAngle(msg, to->angles[0]);
	}

	if (bits & SU_ANGLE2)
	{
		SV_WriteAngle(msg, to->angles[1]);
	}

	if (bits & SU_YAWOFS)
	{
		SV_WriteAngle(msg, to->angle_diff);
	}

	if (bits & SU_ANGLE3)
	{
		SV_WriteAngle(msg, to->angles[2]);
	}

	if (bits & SU_SOUND)
	{
		MSG_WriteByte(msg, to->sound & 255);
		MSG_WriteByte(msg, to->sound_data & 255);
	}

	if (bits & SU_EVENTS)
	{
		/* up to three pairs; bit 7 of an event = another pair follows */
		for (i = 0; i < 3; i++)
		{
			int ev = to->event[i] & 0x7F;
			qboolean more = (i < 2) && to->event[i + 1];

			MSG_WriteByte(msg, ev | (more ? 0x80 : 0));
			MSG_WriteByte(msg, to->eventparm[i] & 255);

			if (!more)
			{
				break;
			}
		}
	}

	if (bits & SU_SOLID)
	{
		MSG_WriteLong(msg, to->solid);
	}

	if (bits & SU_EFFECTSLOTS)
	{
		MSG_WriteShort(msg, slotmask);

		for (i = 0; i < 12; i++)
		{
			if (slotmask & (1 << i))
			{
				MSG_WriteShort(msg, to->effect_slots[i]);
			}
		}
	}
}

int
SV_DeltaEntitySize(const sof_entity_state_t *from, const sof_entity_state_t *to)
{
	byte buf[256];
	sizebuf_t sb;

	SZ_Init(&sb, buf, sizeof(buf));
	SV_WriteDeltaEntity(from, to, &sb, true, true);
	return sb.cursize;
}

/*
 * Writes a delta update of an entity list to the message.
 */
static void
SV_EmitPacketEntities(client_frame_t *from, client_frame_t *to, sizebuf_t *msg)
{
	sof_entity_state_t *oldent = NULL, *newent = NULL;
	int oldindex, newindex;
	int oldnum, newnum;
	int from_num_entities;

	MSG_WriteByte(msg, sofsvc_packetentities);

	from_num_entities = from ? from->num_entities : 0;

	newindex = 0;
	oldindex = 0;

	while ((newindex < to->num_entities) || (oldindex < from_num_entities))
	{
		if (msg->cursize > SV_DATAGRAM_LIMIT - 150)
		{
			break;
		}

		if (newindex >= to->num_entities)
		{
			newnum = 9999;
		}
		else
		{
			newent = &svs.client_entities[(to->first_entity + newindex) %
				svs.num_client_entities];
			newnum = newent->number;
		}

		if (oldindex >= from_num_entities)
		{
			oldnum = 9999;
		}
		else
		{
			oldent = &svs.client_entities[(from->first_entity + oldindex) %
				svs.num_client_entities];
			oldnum = oldent->number;
		}

		if (newnum == oldnum)
		{
			/* delta update from old position; writes nothing when
			   unchanged (entities above 255 always have a header) */
			SV_WriteDeltaEntity(oldent, newent, msg, false, false);
			oldindex++;
			newindex++;
			continue;
		}

		if (newnum < oldnum)
		{
			/* this is a new entity, send it from the baseline */
			SV_WriteDeltaEntity(&sv.baselines[newnum], newent, msg, true, true);
			newindex++;
			continue;
		}

		/* the old entity isn't present in the new message */
		SV_WriteHeader(msg, SU_REMOVE | ((oldnum >= 256) ? SU_NUMBER16 : 0), oldnum);
		oldindex++;
	}

	MSG_WriteShort(msg, 0); /* end of list: 00 00 */
}

/* ---------------------------------------------------------------------
 * player state (SPEC-202 s4)
 * --------------------------------------------------------------------- */

static qboolean
VecDiff(const float *a, const float *b)
{
	return (a[0] != b[0]) || (a[1] != b[1]) || (a[2] != b[2]);
}

static void
SV_WritePlayerstateToClient(client_frame_t *from, client_frame_t *to, sizebuf_t *msg)
{
	static const sof_player_state_t nullps;
	const sof_player_state_t *ps, *ops;
	int ogun;
	unsigned pflags, statbits;
	int i;

	ps = &to->ps;
	ops = from ? &from->ps : &nullps;
	ogun = from ? from->gunhandle : 0;

	pflags = 0;

	/* I1d: field 1 is the pair (handle, weapon type); a change of the type alone is sent too (the original
	   sends (31, 6), (30, 8), (31, 1) ... at weapon switches, tests/I1d/vwstates.py o_a3) */
	if ((to->gunhandle != ogun) || ((ps->gun_type & 255) != (ops->gun_type & 255)))
	{
		pflags |= SPS_GUN;
	}

	if (ps->gun_count_a != ops->gun_count_a)
	{
		pflags |= SPS_GUN_COUNT_A;
	}

	if (ps->gun_count_b != ops->gun_count_b)
	{
		pflags |= SPS_GUN_COUNT_B;
	}

	if (ps->weapon_seq != ops->weapon_seq)
	{
		pflags |= SPS_WEAPONSEQ;
	}

	if (ps->resync != ops->resync)
	{
		pflags |= SPS_RESYNC;
	}

	if (ps->inhibit != ops->inhibit)
	{
		pflags |= SPS_INHIBIT;
	}

	if (ps->pmove.pm_type != ops->pmove.pm_type)
	{
		pflags |= SPS_M_TYPE;
	}

	if ((ps->pmove.origin[0] != ops->pmove.origin[0]) ||
		(ps->pmove.origin[1] != ops->pmove.origin[1]) ||
		(ps->pmove.origin[2] != ops->pmove.origin[2]))
	{
		pflags |= SPS_M_ORIGIN;
	}

	if ((ps->pmove.velocity[0] != ops->pmove.velocity[0]) ||
		(ps->pmove.velocity[1] != ops->pmove.velocity[1]) ||
		(ps->pmove.velocity[2] != ops->pmove.velocity[2]))
	{
		pflags |= SPS_M_VELOCITY;
	}

	if (ps->pmove.pm_time != ops->pmove.pm_time)
	{
		pflags |= SPS_M_TIME;
	}

	if (ps->pmove.pm_flags != ops->pmove.pm_flags)
	{
		pflags |= SPS_M_FLAGS;
	}

	if (ps->pmove.gravity != ops->pmove.gravity)
	{
		pflags |= SPS_M_GRAVITY;
	}

	if ((ps->pmove.delta_angles[0] != ops->pmove.delta_angles[0]) ||
		(ps->pmove.delta_angles[1] != ops->pmove.delta_angles[1]) ||
		(ps->pmove.delta_angles[2] != ops->pmove.delta_angles[2]))
	{
		pflags |= SPS_M_DELTA_ANGLES;
	}

	if (ps->pmove.move_scale != ops->pmove.move_scale)
	{
		pflags |= SPS_PMEXTRA;
	}

	if (VecDiff(ps->viewoffset, ops->viewoffset))
	{
		pflags |= SPS_VIEWOFFSET;
	}

	if (VecDiff(ps->viewangles, ops->viewangles))
	{
		pflags |= SPS_VIEWANGLES;
	}

	if (VecDiff(ps->remote_angles, ops->remote_angles))
	{
		pflags |= SPS_REMOTE_ANGLES;
	}

	if (VecDiff(ps->remote_origin, ops->remote_origin))
	{
		pflags |= SPS_REMOTE_ORIGIN;
	}

	if ((ps->remote_type != ops->remote_type) || (ps->remote_entity != ops->remote_entity))
	{
		pflags |= SPS_REMOTE_VIEW;
	}

	if (VecDiff(ps->kick_angles, ops->kick_angles))
	{
		pflags |= SPS_KICKANGLES;
	}

	if (VecDiff(ps->weapon_kick, ops->weapon_kick))
	{
		pflags |= SPS_WEAPON_KICK;
	}

	if ((ps->blend[0] != ops->blend[0]) || (ps->blend[1] != ops->blend[1]) ||
		(ps->blend[2] != ops->blend[2]) || (ps->blend[3] != ops->blend[3]))
	{
		pflags |= SPS_BLEND;
	}

	if (ps->fov != ops->fov)
	{
		pflags |= SPS_FOV;
	}

	if (ps->rdflags != ops->rdflags)
	{
		pflags |= SPS_RDFLAGS;
	}

	if (ps->cinematic_freeze != ops->cinematic_freeze)
	{
		pflags |= SPS_CINFREEZE;
	}

	if (ps->music != ops->music)
	{
		pflags |= SPS_MUSIC;
	}

	if (ps->ambient != ops->ambient)
	{
		pflags |= SPS_AMBIENT;
	}

	if ((ps->dm_rank != ops->dm_rank) || (ps->dm_ranked != ops->dm_ranked))
	{
		pflags |= SPS_RANK;
	}

	if (ps->spectated != ops->spectated)
	{
		pflags |= SPS_SPECTATED;
	}

	/* write it */
	MSG_WriteByte(msg, sofsvc_playerinfo);
	MSG_WriteLong(msg, (int)pflags);

	if (pflags & SPS_GUN)
	{
		MSG_WriteShort(msg, to->gunhandle);
		MSG_WriteByte(msg, ps->gun_type & 255);
	}

	if (pflags & SPS_GUN_COUNT_A)
	{
		MSG_WriteByte(msg, ps->gun_count_a);
	}

	if (pflags & SPS_GUN_COUNT_B)
	{
		MSG_WriteShort(msg, ps->gun_count_b);
	}

	if (pflags & SPS_WEAPONSEQ)
	{
		MSG_WriteByte(msg, ps->weapon_seq);
	}

	if (pflags & SPS_RESYNC)
	{
		MSG_WriteByte(msg, ps->resync);
	}

	if (pflags & SPS_INHIBIT)
	{
		MSG_WriteByte(msg, ps->inhibit);
	}

	if (pflags & SPS_M_TYPE)
	{
		MSG_WriteByte(msg, ps->pmove.pm_type & 255);
	}

	if (pflags & SPS_M_ORIGIN)
	{
		MSG_WriteShort(msg, ps->pmove.origin[0]);
		MSG_WriteShort(msg, ps->pmove.origin[1]);
		MSG_WriteShort(msg, ps->pmove.origin[2]);
	}

	if (pflags & SPS_M_VELOCITY)
	{
		MSG_WriteShort(msg, ps->pmove.velocity[0]);
		MSG_WriteShort(msg, ps->pmove.velocity[1]);
		MSG_WriteShort(msg, ps->pmove.velocity[2]);
	}

	if (pflags & SPS_M_TIME)
	{
		MSG_WriteByte(msg, ps->pmove.pm_time);
	}

	if (pflags & SPS_M_FLAGS)
	{
		MSG_WriteByte(msg, ps->pmove.pm_flags);
	}

	if (pflags & SPS_M_GRAVITY)
	{
		MSG_WriteShort(msg, ps->pmove.gravity);
	}

	if (pflags & SPS_M_DELTA_ANGLES)
	{
		MSG_WriteShort(msg, ps->pmove.delta_angles[0]);
		MSG_WriteShort(msg, ps->pmove.delta_angles[1]);
		MSG_WriteShort(msg, ps->pmove.delta_angles[2]);
	}

	if (pflags & SPS_PMEXTRA)
	{
		MSG_WriteByte(msg, ps->pmove.move_scale);
	}

	if (pflags & SPS_VIEWOFFSET)
	{
		MSG_WriteChar(msg, (int)(ps->viewoffset[0] * 4));
		MSG_WriteChar(msg, (int)(ps->viewoffset[1] * 4));
		MSG_WriteChar(msg, (int)(((double)ps->viewoffset[2] - 14.6) * 5.0));
	}

	if (pflags & SPS_VIEWANGLES)
	{
		SV_WriteAngle16(msg, ps->viewangles[0]);
		SV_WriteAngle16(msg, ps->viewangles[1]);
		SV_WriteAngle16(msg, ps->viewangles[2]);
	}

	if (pflags & SPS_REMOTE_ANGLES)
	{
		SV_WriteAngle16(msg, ps->remote_angles[0]);
		SV_WriteAngle16(msg, ps->remote_angles[1]);
		SV_WriteAngle16(msg, ps->remote_angles[2]);
	}

	if (pflags & SPS_REMOTE_ORIGIN)
	{
		MSG_WriteShort(msg, (int)ps->remote_origin[0]);
		MSG_WriteShort(msg, (int)ps->remote_origin[1]);
		MSG_WriteShort(msg, (int)ps->remote_origin[2]);
	}

	if (pflags & SPS_REMOTE_VIEW)
	{
		MSG_WriteByte(msg, ps->remote_type & 255);
		MSG_WriteByte(msg, ps->remote_entity & 255);
	}

	if (pflags & SPS_KICKANGLES)
	{
		MSG_WriteChar(msg, (int)(ps->kick_angles[0] * 4));
		MSG_WriteChar(msg, (int)(ps->kick_angles[1] * 4));
		MSG_WriteChar(msg, (int)(ps->kick_angles[2] * 4));
	}

	if (pflags & SPS_WEAPON_KICK)
	{
		MSG_WriteChar(msg, (int)(ps->weapon_kick[0] * 4));
	}

	if (pflags & SPS_BLEND)
	{
		MSG_WriteByte(msg, (int)(ps->blend[0] * 255));
		MSG_WriteByte(msg, (int)(ps->blend[1] * 255));
		MSG_WriteByte(msg, (int)(ps->blend[2] * 255));
		MSG_WriteByte(msg, (int)(ps->blend[3] * 255));
	}

	if (pflags & SPS_FOV)
	{
		MSG_WriteByte(msg, (int)ps->fov);
	}

	if (pflags & SPS_RDFLAGS)
	{
		MSG_WriteByte(msg, ps->rdflags & 255);
	}

	/* send stats */
	statbits = 0;

	for (i = 0; i < SOF_MAX_STATS; i++)
	{
		if (ps->stats[i] != ops->stats[i])
		{
			statbits |= 1u << i;
		}
	}

	MSG_WriteLong(msg, (int)statbits);

	for (i = 0; i < SOF_MAX_STATS; i++)
	{
		if (statbits & (1u << i))
		{
			MSG_WriteShort(msg, ps->stats[i]);
		}
	}

	if (pflags & SPS_CINFREEZE)
	{
		MSG_WriteByte(msg, ps->cinematic_freeze);
	}

	if (pflags & SPS_MUSIC)
	{
		MSG_WriteByte(msg, ps->music);
	}

	if (pflags & SPS_AMBIENT)
	{
		MSG_WriteShort(msg, ps->ambient);
	}

	if (pflags & SPS_RANK)
	{
		MSG_WriteByte(msg, ps->dm_rank);
		MSG_WriteByte(msg, ps->dm_ranked);
	}

	if (pflags & SPS_SPECTATED)
	{
		MSG_WriteByte(msg, ps->spectated);
	}
}

/* ---------------------------------------------------------------------
 * frames (SPEC-202 s1, s5)
 * --------------------------------------------------------------------- */

void
SV_WriteFrameToClient(client_t *client, sizebuf_t *msg)
{
	client_frame_t *frame, *oldframe;
	int lastframe, i;

	/* this is the frame we are creating */
	frame = &client->frames[sv.framenum & UPDATE_MASK];

	if (client->lastframe <= 0)
	{
		/* client is asking for a retransmit */
		oldframe = NULL;
		lastframe = -1;
	}
	else if (sv.framenum - client->lastframe >= (UPDATE_BACKUP - 3))
	{
		/* client hasn't gotten a good message through in a long time */
		oldframe = NULL;
		lastframe = -1;
	}
	else
	{
		/* we have a valid message to delta from */
		oldframe = &client->frames[client->lastframe & UPDATE_MASK];
		lastframe = client->lastframe;
	}

	/* events of entities not in the frame come first (SPEC-201 order) */
	for (i = 0; i < client->numculled; i++)
	{
		sv_culledevent_t *ce = &client->culled[i];

		MSG_WriteByte(msg, sofsvc_culledevent);
		MSG_WriteByte(msg, ce->event);
		MSG_WriteByte(msg, ce->parm);
		MSG_WriteByte(msg, ce->distance);
		SV_WriteDir(msg, ce->dir);
	}

	MSG_WriteByte(msg, sofsvc_frame);
	MSG_WriteLong(msg, sv.framenum);
	MSG_WriteLong(msg, lastframe); /* what we are delta'ing from */
	MSG_WriteFloat(msg, sv.gametime);

	/* send over the areabits */
	MSG_WriteByte(msg, frame->areabytes);
	SZ_Write(msg, frame->areabits, frame->areabytes);

	/* delta encode the playerstate */
	SV_WritePlayerstateToClient(oldframe, frame, msg);

	/* delta encode the entities */
	SV_EmitPacketEntities(oldframe, frame, msg);
}

/*
 * The client will interpolate the view position,
 * so we can't use a single PVS point
 */
static void
SV_FatPVS(vec3_t org)
{
	int leafs[64];
	int i, j, count;
	int longs;
	byte *src;
	vec3_t mins, maxs;

	for (i = 0; i < 3; i++)
	{
		mins[i] = org[i] - 8;
		maxs[i] = org[i] + 8;
	}

	count = CM_BoxLeafnums(mins, maxs, leafs, 64, NULL);

	if (count < 1)
	{
		Com_Error(ERR_FATAL, "SV_FatPVS: count < 1");
	}

	longs = (CM_NumClusters() + 31) >> 5;

	/* convert leafs to clusters */
	for (i = 0; i < count; i++)
	{
		leafs[i] = CM_LeafCluster(leafs[i]);
	}

	memcpy(fatpvs, CM_ClusterPVS(leafs[0]), longs << 2);

	/* or in all the other leaf bits */
	for (i = 1; i < count; i++)
	{
		for (j = 0; j < i; j++)
		{
			if (leafs[i] == leafs[j])
			{
				break;
			}
		}

		if (j != i)
		{
			continue; /* already have the cluster we want */
		}

		src = CM_ClusterPVS(leafs[i]);

		for (j = 0; j < (longs << 2); j++)
		{
			fatpvs[j] |= src[j];
		}
	}
}

static qboolean
SV_EntityVisible(const edict_t *ent, int clientarea)
{
	int i, l;

	/* check area */
	if (!CM_AreasConnected(clientarea, ent->areanum))
	{
		/* doors can legally straddle two areas,
		   so we may need to check another one */
		if (!ent->areanum2 || !CM_AreasConnected(clientarea, ent->areanum2))
		{
			return false; /* blocked by a door */
		}
	}

	if (ent->num_clusters == -1)
	{
		/* too many leafs for individual check, go by headnode */
		return CM_HeadnodeVisible(ent->headnode, fatpvs);
	}

	/* check individual leafs */
	for (i = 0; i < ent->num_clusters; i++)
	{
		l = ent->clusternums[i];

		if (fatpvs[l >> 3] & (1 << (l & 7)))
		{
			return true;
		}
	}

	return false;
}

/* the point a client sees and hears from */
static void
SV_ClientViewOrigin(const edict_t *clent, vec3_t org)
{
	const sof_player_state_t *ps = &clent->client->ps;
	int i;

	if (ps->remote_type > 0)
	{
		for (i = 0; i < 3; i++)
		{
			org[i] = ps->remote_origin[i] * 0.125f;
		}

		return;
	}

	for (i = 0; i < 3; i++)
	{
		org[i] = ps->pmove.origin[i] * 0.125f + ps->viewoffset[i];
	}
}

/*
 * Decides which entities are going to be visible to the client, and
 * copies off the playerstat and areabits.
 */
void
SV_BuildClientFrame(client_t *client)
{
	int e, clientnum;
	vec3_t org, delta;
	edict_t *ent;
	edict_t *clent;
	client_frame_t *frame;
	sof_entity_state_t *state;
	int leafnum, clientarea;
	float dist;
	qboolean visible;

	clent = CL_EDICT(client);
	client->numculled = 0;

	if (!clent->client)
	{
		return; /* not in game yet */
	}

	clientnum = (int)(client - svs.clients);

	/* this is the frame we are creating */
	frame = &client->frames[sv.framenum & UPDATE_MASK];

	frame->senttime = svs.realtime; /* save it for ping calc later */

	/* find the client's PVS */
	SV_ClientViewOrigin(clent, org);

	leafnum = CM_PointLeafnum(org);
	clientarea = CM_LeafArea(leafnum);

	/* calculate the visible areas */
	frame->areabytes = CM_WriteAreaBits(frame->areabits, clientarea);

	if (frame->areabytes > (int)sizeof(frame->areabits))
	{
		frame->areabytes = sizeof(frame->areabits);
	}

	/* grab the current player state */
	frame->ps = clent->client->ps;

	if (sv_hostdebug && ((int)sv_hostdebug->value & 32) && !(sv.framenum % 50))
	{
		const unsigned *w = (const unsigned *)clent->client;
		int k;

		Com_Printf("ps of client %i, frame %i:", clientnum, sv.framenum);

		for (k = 0; k < 64; k++)
		{
			Com_Printf("%s%08x", (k & 7) ? " " : va("\n  %03x: ", k * 4), w[k]);
		}

		Com_Printf("\n");
	}

	SV_FatPVS(org);

	/* the Ghoul system builds this client's instance update */
	SV_Ghoul_BeginFrame(clientnum, sv.framenum);
	frame->gunhandle = frame->ps.gun ? SV_Ghoul_Offer(frame->ps.gun, -1.0f) : 0;

	/* build up the list of visible entities */
	frame->num_entities = 0;
	frame->first_entity = svs.next_client_entities;

	for (e = 1; e < ge->num_edicts && e < SOF_MAX_EDICTS; e++)
	{
		ent = EDICT_NUM(e);

		if (!ent->inuse)
		{
			continue;
		}

		/* ignore ents without visible models */
		if (ent->svflags & SOF_SVF_NOCLIENT)
		{
			continue;
		}

		/* candidates: a model, effects, a sound, an effect slot, an event or
		   the Ghoul bit (SPEC-202 s3.1) */
		if (!ent->s.modelindex && !ent->s.effects && !ent->s.sound &&
			!SV_SlotsInUse(&ent->s) && !ent->s.event[0] &&
			!(ent->s.renderfx & RF_GHOUL))
		{
			continue;
		}

		visible = (ent == clent) || SV_EntityVisible(ent, clientarea);

		VectorSubtract(org, ent->s.origin, delta);
		dist = VectorLength(delta);

		if (!visible)
		{
			/* SPEC-202 s3.1 drops sound-only entities beyond 1200 units; the
			   original 1.07f sends those with attenuation class 0 (sound data
			   bits 0..2, "no attenuation") at any distance: kos1 sends 39 such
			   looping sounds up to 4678 units away (client demo, 2026-10-25,
			   OPEN_QUESTIONS I1b) */
			if (!ent->s.sound || ((dist > SV_SOUNDONLY_RANGE) && (ent->s.sound_data & 7)))
			{
				/* its event still reaches the client (SPEC-202 s3.1) */
				if (ent->s.event[0] && ((ent->s.event[0] & 0x7F) < 24) &&
					(dist < SV_CULLED_RANGE) && (client->numculled < SV_MAX_CULLED))
				{
					sv_culledevent_t *ce = &client->culled[client->numculled++];

					ce->event = ent->s.event[0] & 0x7F;
					ce->parm = ent->s.eventparm[0];
					ce->distance = (int)(dist * 0.25f);
					VectorCopy(delta, ce->dir);
					VectorNormalize(ce->dir);
				}

				continue;
			}
		}

		/* add it to the circular client_entities array */
		state = &svs.client_entities[svs.next_client_entities % svs.num_client_entities];

		if (ent->s.number != e)
		{
			Com_DPrintf("FIXING ENT->S.NUMBER!!!\n");
			ent->s.number = e;
		}

		*state = ent->s;

		if (!visible)
		{
			state->renderfx |= RF_SOUNDONLY; /* sent only for its sound */
		}

		/* the frame field carries this client's Ghoul handle */
		if (ent->ghoulInst)
		{
			state->frame = SV_Ghoul_Offer(ent->ghoulInst, dist);
		}

		/* don't mark players missiles as solid */
		if (ent->owner == clent)
		{
			state->solid = 0;
		}

		svs.next_client_entities++;
		frame->num_entities++;
	}
}

/* SPEC-202 s3.1: after every server frame the events of all entities are cleared */
void
SV_ClearEntityEvents(void)
{
	int i;
	edict_t *ent;

	if (!ge || (sv.state != ss_game))
	{
		return;
	}

	for (i = 0; i < ge->num_edicts; i++)
	{
		ent = EDICT_NUM(i);
		memset(ent->s.event, 0, sizeof(ent->s.event));
		memset(ent->s.eventparm, 0, sizeof(ent->s.eventparm));
	}
}

/*
 * Save everything in the world out without deltas.
 * Used for recording footage for merged or assembled demos
 */
void
SV_RecordDemoMessage(void)
{
	int e;
	edict_t *ent;
	sizebuf_t buf;
	byte buf_data[32768];
	int len;

	if (!svs.demofile)
	{
		return;
	}

	SZ_Init(&buf, buf_data, sizeof(buf_data));

	/* write a frame message that doesn't
	   contain a player_state_t */
	MSG_WriteByte(&buf, sofsvc_frame);
	MSG_WriteLong(&buf, sv.framenum);

	MSG_WriteByte(&buf, sofsvc_packetentities);

	for (e = 1; e < ge->num_edicts && e < SOF_MAX_EDICTS; e++)
	{
		ent = EDICT_NUM(e);

		/* ignore ents without visible models unless they have an effect */
		if (ent->inuse && ent->s.number &&
			(ent->s.modelindex || ent->s.effects || ent->s.sound ||
			 ent->s.event[0]) && !(ent->svflags & SOF_SVF_NOCLIENT))
		{
			SV_WriteDeltaEntity(NULL, &ent->s, &buf, false, true);
		}
	}

	MSG_WriteShort(&buf, 0); /* end of packetentities */

	/* now add the accumulated multicast information */
	SZ_Write(&buf, svs.demo_multicast.data, svs.demo_multicast.cursize);
	SZ_Clear(&svs.demo_multicast);

	/* now write the entire message to the file, prefixed by the length */
	len = LittleLong(buf.cursize);
	fwrite(&len, 4, 1, svs.demofile);
	fwrite(buf.data, buf.cursize, 1, svs.demofile);
}
