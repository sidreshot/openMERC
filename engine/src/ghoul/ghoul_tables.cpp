// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 OpenMerc contributors
// OpenMerc Ghoul model system - fixed tables, computed at run time from common/header/om_geodesic.h.
// Implements: SPEC-014 (section 13: six axes followed by the 250 directions), SPEC-201 Appendix B (weights)

#include "ghoul_tables.h"

#include "../common/header/om_geodesic.h"

namespace ghoul {

const float *DirTable()
{
	static float table[256 * 3];
	static bool built = false;
	if (!built)
	{
		static const float axes[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		float dirs[OM_NUMDIRS][3];
		OM_BuildDirs(dirs);
		for (int i = 0; i < 6; i++)
			for (int c = 0; c < 3; c++)
				table[i * 3 + c] = axes[i][c];
		for (int i = 0; i < OM_NUMDIRS; i++)
			for (int c = 0; c < 3; c++)
				table[(6 + i) * 3 + c] = dirs[i][c];
		built = true;
	}
	return table;
}

float HuffWeight(int c)
{
	return OM_HuffWeight(c & 255);
}

} // namespace ghoul