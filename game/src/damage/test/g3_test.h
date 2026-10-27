// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 unit tests - shared declarations of the test harness.
// Implements: SPEC-720, SPEC-721, SPEC-722 (test vectors)
#pragma once
#include "g3_mock.h"

#include <map>
#include <string>
#include <vector>

namespace test {
extern std::vector<std::string> log;
extern std::map<std::string, float> cvars;
extern std::vector<mock::Model> lib;
extern std::vector<int> irandQueue;
extern edict_t *traceTarget;
extern std::vector<std::pair<int, std::vector<unsigned char>>> chunks; // written savegame chunks
extern size_t chunkReadPos;                                              // next chunk ReadChunk returns
void InstallEngine(void);
}
extern bool test_sameTeam;
extern int test_freed;
