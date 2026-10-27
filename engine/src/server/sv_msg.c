/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc server: the primitive encodings of protocol 33 that differ from
 * Quake II (coord/angle truncation constants, the 250-entry direction
 * table, the configstring dictionary, the move checksum).
 *
 * Implements: SPEC-201 (primitive encodings, Appendix A),
 *             SPEC-203 (svc_configstring dictionary coding),
 *             SPEC-202 s6.3 (move checksum)
 */

#include "header/server.h"
#include "header/sv_tables.h"

static float sv_dirs[SV_NUMDIRS][3];
static qboolean sv_tables_ready;

void
SV_InitTables(void)
{
	int i, j;

	if (sv_tables_ready)
	{
		return;
	}

	for (i = 0; i < SV_NUMDIRS; i++)
	{
		for (j = 0; j < 3; j++)
		{
			const byte *b = sv_dir_bytes + i * 12 + j * 4;
			unsigned u = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);

			memcpy(&sv_dirs[i][j], &u, 4);
		}
	}

	sv_tables_ready = true;
}

/* coord: short = value x 8 truncated toward zero */
void
SV_WriteCoord(sizebuf_t *sb, float f)
{
	MSG_WriteShort(sb, (int)(f * 8.0f));
}

void
SV_WritePos(sizebuf_t *sb, const vec3_t pos)
{
	SV_WriteCoord(sb, pos[0]);
	SV_WriteCoord(sb, pos[1]);
	SV_WriteCoord(sb, pos[2]);
}

/* angle: low 8 bits of degrees x 256/360 truncated toward zero */
void
SV_WriteAngle(sizebuf_t *sb, float f)
{
	MSG_WriteByte(sb, ((int)(f * 0.71111113f)) & 255);
}

void
SV_WriteAngle16(sizebuf_t *sb, float f)
{
	MSG_WriteShort(sb, ((int)(f * 182.04445f)) & 0xFFFF);
}

/* dir: the entry with the largest dot product; ties keep the lower index;
   a zero vector or one with no positive dot product gives 0 */
int
SV_DirToByte(const vec3_t dir)
{
	int i, best = 0;
	float d, bestd = 0;

	SV_InitTables();

	if (!dir)
	{
		return 0;
	}

	for (i = 0; i < SV_NUMDIRS; i++)
	{
		d = dir[0] * sv_dirs[i][0] + dir[1] * sv_dirs[i][1] + dir[2] * sv_dirs[i][2];

		if (d > bestd)
		{
			bestd = d;
			best = i;
		}
	}

	return best;
}

void
SV_WriteDir(sizebuf_t *sb, const vec3_t dir)
{
	MSG_WriteByte(sb, SV_DirToByte(dir));
}

/* OpenMerc keeps no configstring dictionary (docs/DEVIATIONS.md "Fixed tables"): every text is sent as a
   literal (code -1, SPEC-203) */
int
SV_ConfigstringDictCode(const char *s)
{
	(void)s;
	return -1;
}

/* svc_configstring payload after the id: short index, short code [, string] */
void
SV_WriteConfigstringMsg(sizebuf_t *sb, int index, const char *s)
{
	int code = SV_ConfigstringDictCode(s);

	MSG_WriteByte(sb, sofsvc_configstring);
	MSG_WriteShort(sb, index);
	MSG_WriteShort(sb, code);

	if (code < 0)
	{
		MSG_WriteString(sb, s);
	}
}

int
SV_ConfigstringMsgSize(const char *s)
{
	return (SV_ConfigstringDictCode(s) < 0) ? 5 + (int)strlen(s) + 1 : 5;
}

/* SPEC-202 s6.3 */
int
SV_MoveChecksum(const byte *region, int len, unsigned sequence)
{
	byte tmp[64 + 4];
	unsigned p;

	if (len > 60)
	{
		len = 60;
	}

	if (len < 0)
	{
		len = 0;
	}

	memcpy(tmp, region, len);
	p = sequence % 2996;
	tmp[len + 0] = sv_dir_bytes[p] ^ (sequence & 0xFF);
	tmp[len + 1] = sv_dir_bytes[p + 1];
	tmp[len + 2] = sv_dir_bytes[p + 2] ^ ((sequence >> 8) & 0xFF);
	tmp[len + 3] = sv_dir_bytes[p + 3];

	return Com_BlockChecksum(tmp, len + 4) & 0xFF;
}
