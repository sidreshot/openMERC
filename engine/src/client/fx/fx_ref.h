/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I5. The SPEC-002 scene records the effects system hands to the
 * renderer (alpha quads, quad chains), from the renderer interface header
 * of package I4a (common/header/sof/refapi.h), under the names the effects
 * code uses.
 *
 * Implements: SPEC-002 (alpha quad, quad chain, flags), SPEC-412 s.8-s.10 (meaning)
 */

#ifndef OM_FX_REF_H
#define OM_FX_REF_H

#include "../../common/header/sof/refapi.h"

typedef sofalphaquad_t sof_alphaquad_t;
typedef sofquadchain_t sof_quadchain_t;

/* alpha-quad flags (+0x38; SPEC-002, SPEC-AMEND-002 1.12) */
#define AQ_HIDDEN SOF_AQ_HIDDEN       /* set by the renderer on LOD-dropped chain quads */
#define AQ_BILLBOARD SOF_AQ_BILLBOARD
#define AQ_BEAM SOF_AQ_BEAM
#define AQ_LIT SOF_AQ_LIT
#define AQ_ORIENTED SOF_AQ_ORIENTED
#define AQ_NOSORT SOF_AQ_IMMEDIATE
#define AQ_CHEAP SOF_AQ_BATCHED
#define AQ_DECALOFS SOF_AQ_DECAL
#define AQ_ADDITIVE SOF_AQ_ADDITIVE
#define AQ_DARKEN SOF_AQ_DARKEN
#define AQ_NOZ SOF_AQ_NODEPTH
#define AQ_DRAWN SOF_AQ_DRAWN         /* set by the renderer, never cleared */

#define AQ_DIR_DEFAULT 255

#endif
