// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - C entry points for the engine (ghoul_host.h).
// Implements: SPEC-010 A (the three system objects), SPEC-010 game imports #61-#66 (SPEC-001 #60-#66),
//             SPEC-012 section 10 (gserver)

#include "ghoul_host.h"
#include "ghoul_internal.h"

namespace ghoul {
int TextLoad(const char *folder, const char *file, void **buffer);
int TextGetLine(int *remaining, char **cursor, char *line);
void TextPreloadList(const char *folder, const char *name);
int TextRegisterList(const char *folder, const char *name, void *object);
void TextPartsOff(const char *folder, const char *name, void *object, void *instance);
IGhoulPlayerModel *NewPlayerModel(const char *file);
void GserverCommand(int argc, const char *const *argv);
void DestroySystems();
void ForgetShadowTextures();
} // namespace ghoul

using namespace ghoul;

extern "C" {

void OMGhoul_Init(const om_ghoul_host_t *host)
{
	SetHost(host);
	for (int r = 0; r < 3; r++)
		GetSystemObject(r);
}

void OMGhoul_Shutdown(void)
{
	DestroySystems();
	ReleaseAllTextures();
	ForgetShadowTextures();
	FlushChunkCache();
	G().files.PurgeUnreferenced();
	G().imageLists.clear();
	G().textures.clear();
	G().texts.clear();
	G().playerTexts.clear();
}

void *OMGhoul_GetSystem(int role) { return static_cast<IGhoulSystem *>(GetSystemObject(role)); }

void *OMGhoul_NewPlayerModel(const char *file) { return NewPlayerModel(file); }

int OMGhoul_LoadFile(const char *folder, const char *file, void **buffer) { return TextLoad(folder, file, buffer); }

int OMGhoul_GetLine(int *remaining, char **cursor, char *line) { return TextGetLine(remaining, cursor, line); }

void OMGhoul_PreloadList(const char *folder, const char *name) { TextPreloadList(folder, name); }

int OMGhoul_RegisterList(const char *folder, const char *name, void *object)
{
	return TextRegisterList(folder, name, object);
}

void OMGhoul_PartsOff(const char *folder, const char *name, void *object, void *instance)
{
	TextPartsOff(folder, name, object, instance);
}

void OMGhoul_Command(int argc, const char *const *argv) { GserverCommand(argc, argv); }
float OMGhoul_SizeMatters(void) { return G().gs.sizeMatters; }

} // extern "C"
