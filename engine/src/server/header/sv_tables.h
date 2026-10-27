/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc - fixed tables of the network protocol, server copy. No data is stored here (see
 * client/header/sof_tables.h and docs/DEVIATIONS.md "Fixed tables").
 *
 * Implements: SPEC-201 ('dir'), SPEC-203 (svc_configstring coding, dictionary not used)
 */

#ifndef SV_SOF_TABLES_H
#define SV_SOF_TABLES_H

#include "../../common/header/om_geodesic.h"

#define SV_CS_DICT_SIZE 0
#define SV_NUMDIRS OM_NUMDIRS
#define sv_dir_bytes (OM_DirBytes())

#endif