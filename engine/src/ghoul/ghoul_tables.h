// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - fixed tables, computed at run time (no stored data; docs/DEVIATIONS.md
// "Fixed tables", docs/impl/LEGAL-tables.md).
// Implements: SPEC-014 (section 13 direction table), SPEC-201 Appendix B (string coding)
#pragma once

#include <cstdint>

namespace ghoul {

// OpenMerc keeps no ghoul string dictionary: every string is sent Huffman-coded or literal.
constexpr int kGhoulDictSize = 0;

// The 256-entry direction table as floats (normals and packed rotations): the six axes, then the
// 250 directions of the network table.
const float *DirTable();   // 256 * 3 floats

// OpenMerc's own Huffman weight of a character code (0..255), > 0.
float HuffWeight(int c);

} // namespace ghoul