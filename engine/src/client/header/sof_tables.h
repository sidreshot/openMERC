/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc - fixed tables of the network protocol. No data is stored here (user decision 2026-10-26,
 * docs/DEVIATIONS.md "Fixed tables"): the direction set is computed at run time (common/header/om_geodesic.h)
 * and the configstring dictionary is empty, so every configstring travels as a literal string (SPEC-203
 * code -1).
 *
 * Implements: SPEC-201 ('dir'), SPEC-203 (svc_configstring coding, dictionary not used)
 */

#ifndef CL_SOF_TABLES_H
#define CL_SOF_TABLES_H

#include "../../common/header/om_geodesic.h"

#define SOF_CS_DICT_SIZE 0
#define SOF_NUMDIRS OM_NUMDIRS
#define sof_dir_bytes (OM_DirBytes())

#endif