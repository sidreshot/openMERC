/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: GameSpy lobby chat (an IRC client) - the gsc_* commands and
 * cvars of Soldier of Fortune 1.07f and the data it hands to the menus'
 * chat, users and rooms widgets.
 *
 * Implements: SPEC-204 (section 5), SPEC-302 (gsc_* cvars and commands),
 *             SPEC-803 (section 3, data of the lobby widgets),
 *             SPEC-AMEND-006 (I2d.7)
 */

#ifndef CO_GAMESPY_CHAT_H
#define CO_GAMESPY_CHAT_H

#include "shared.h"

/* GameSpy's own chat host (default of gsc_server in 1.07f) is gone; OpenSpy
   runs an IRC-compatible replacement ("Peerchat") on the same port */
#define GSC_ORIGINAL_SERVER "peerchat.gamespy.com"
#define GSC_DEFAULT_SERVER "peerchat.openspy.net"

void GSC_Init(void);      /* cvars and commands; after the SoF engine cvars */
void GSC_Frame(void);     /* every engine frame */
void GSC_Shutdown(void);  /* sends QUIT and closes the connection */

/* menu side (client/menu/m_data.c, SPEC-803 section 3); not in a
   dedicated-server build. The lobby data belongs to the menu system: while no
   menu is open the line, room and user operations are ignored, and closing
   the last menu clears it all (SPEC-AMEND-006 I2d.7). */
void MC_PublicLine(const char *text);
void MC_PrivateLine(const char *text);
void MC_ClearRooms(void);
void MC_AddRoom(const char *room);
void MC_ClearUsers(void);
void MC_AddUser(const char *entry);
const char *MC_SelectedUser(void);
void MC_ClearLobby(void);

#endif
