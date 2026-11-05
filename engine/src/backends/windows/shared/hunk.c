/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 * Memory handling functions.
 *
 * =======================================================================
 */

#include <windows.h>

#include "../../../common/header/common.h"

byte *membase;
int hunkcount;
size_t hunkmaxsize;
size_t cursize;
static size_t committed; /* OpenMerc: bytes of the hunk committed so far */

void *
Hunk_Begin(int maxsize)
{
	/* reserve a huge chunk of memory, but don't commit any yet */
	/* plus 32 bytes for cacheline */
	hunkmaxsize = maxsize + sizeof(size_t) + 32;
	cursize = 0;
	committed = 0;

	membase = VirtualAlloc(NULL, hunkmaxsize, MEM_RESERVE, PAGE_NOACCESS);

	if (!membase)
	{
		Sys_Error("VirtualAlloc reserve failed");
	}

	return (void *)membase;
}

void *
Hunk_Alloc(int size)
{
	/* round to cacheline */
	size = (size + 31) & ~31;

	/* commit pages as needed. OpenMerc: only the pages beyond those already committed, 1 MB at a time - committing
	   the whole range again at every allocation made the kernel walk all its pages each time (the 12 000 face
	   polygons of a map took 0.4 s) */
	if (cursize + size > committed)
	{
		size_t want = cursize + size + 1024 * 1024;

		if (want > hunkmaxsize)
		{
			want = hunkmaxsize;
		}

		if (!VirtualAlloc(membase + committed, want - committed, MEM_COMMIT, PAGE_READWRITE))
		{
			/* near the end of the reservation: only what is needed */
			want = cursize + size;

			if (!VirtualAlloc(membase + committed, want - committed, MEM_COMMIT, PAGE_READWRITE))
			{
				Sys_Error("VirtualAlloc commit failed.\n");
			}
		}

		committed = want;
	}

	cursize += size;

	if (cursize > hunkmaxsize)
	{
		Sys_Error("Hunk_Alloc overflow");
	}

	return (void *)(membase + cursize - size);
}

int
Hunk_End(void)
{
	hunkcount++;

	return cursize;
}

void
Hunk_Free(void *base)
{
	if (base)
	{
		VirtualFree(base, 0, MEM_RELEASE);
	}

	hunkcount--;
}
