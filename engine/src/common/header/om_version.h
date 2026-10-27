/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: the one version text of the game (owner decision t90u):
 * "openMERC V<version>" in the menus, the console, the start-up line and the
 * window title. <version> is the release version set in engine/CMakeLists.txt;
 * `git describe` of the build is OM_BUILD, shown only in the build info
 * (start-up banner, --help, cvar om_build). The "version" cvar is not
 * this text: it stays the SoF 1.07f protocol string of the serverinfo.
 */

#ifndef OM_VERSION_H
#define OM_VERSION_H

#ifdef OM_HAVE_VERSION_H
#include "om_version_gen.h"
#endif

#ifndef OM_VERSION
#define OM_VERSION "0.2.2"
#endif

#ifndef OM_BUILD
#define OM_BUILD OM_VERSION
#endif

#define OM_VERSION_STRING "openMERC V" OM_VERSION
#define OM_BUILD_STRING OM_VERSION_STRING " (build " OM_BUILD ")"

#endif
