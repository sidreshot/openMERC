/*
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
 * OpenMerc: Internet server list (GameSpy masters + server queries).
 * The console command "serverlist" fills and prints it; a menu can start a
 * refresh with CL_ServerList_Refresh and read the entries while and after
 * it runs.
 *
 * Implements: SPEC-204 (section 5)
 *
 * =======================================================================
 */

#ifndef CL_SERVERLIST_H
#define CL_SERVERLIST_H

#include "../../common/header/gamespy.h"

typedef enum
{
	SL_IDLE,
	SL_TRANSFERRING,    /* talking to the masters */
	SL_QUERYING         /* lists received, querying servers */
} serverlist_state_t;

/* one answered server, as handed to the menu (SPEC-204 section 5) */
typedef struct
{
	gsaddr_t query;         /* address from the master list (query port) */
	gsaddr_t game;          /* same IP, port = hostport (28910 if missing); for "connect" */
	char hostname[64];
	char mapname[64];
	char gametype[32];
	char gamever[16];
	char violence[16];
	int numplayers;
	int numbots;
	int maxplayers;
	int ping;               /* ms from the query to the first reply datagram */
	char text[256];         /* <hostname>+<mapname>+<n>/<bots>/<max>++<gametype>+<violence>+<ping>+ */
} serverlist_entry_t;

void CL_ServerList_Init(void);
void CL_ServerList_Frame(void);
void CL_ServerList_Refresh(qboolean print);
serverlist_state_t CL_ServerList_State(void);
int CL_ServerList_Count(void);
const serverlist_entry_t *CL_ServerList_Entry(int index);
const char *SL_StatusText(const char *ref, const char *fallback); /* menu_servername text */

#endif
