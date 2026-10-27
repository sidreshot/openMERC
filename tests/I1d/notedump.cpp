// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc I1d test aid: lists the note events of every sequence of the in-view weapon models as the Ghoul
// library delivers them (each sequence played once from t = 0, events processed in 1 ms steps), from game data
// extracted to a directory (tools/pak.py x <pak0> <root> "ghoul/weapon/inview/*").
//   notedump <root> [weapon dir ...]      e.g. notedump C:\...\I1d\extract Pistol1 Knife
// Output: "<dir> <sequence> <ms> <NOTE> <argument>" per event, and "<dir> <sequence> end <duration ms>".
// Implements: SPEC-600 (notes of the in-view models), SPEC-010/011 (note callbacks, method 45)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../engine/src/ghoul/ghoul_api.h"
#include "../../engine/src/ghoul/ghoul_host.h"

using namespace ghoul;

static std::string g_root;

static int LoadFile(void *, const char *path, void **data)
{
	std::string p = g_root + "/" + path;
	for (auto &c : p)
		if (c == '\\')
			c = '/';
	FILE *f = std::fopen(p.c_str(), "rb");
	if (!f) {
		// the extract keeps the pak's lower-case names
		for (auto &c : p)
			c = (char)tolower((unsigned char)c);
		std::string q = p;
		// restore the root's case
		q.replace(0, g_root.size(), g_root);
		f = std::fopen(q.c_str(), "rb");
	}
	if (!f) {
		*data = nullptr;
		return -1;
	}
	std::fseek(f, 0, SEEK_END);
	long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	void *b = std::malloc(n + 1);
	std::fread(b, 1, n, f);
	std::fclose(f);
	*data = b;
	return (int)n;
}
static void FreeFile(void *, void *d) { std::free(d); }
static void Print(void *, const char *t) { std::fputs(t, stderr); }
static void Fatal(void *, const char *t)
{
	std::fprintf(stderr, "%s\n", t);
	std::exit(2);
}

struct Logger : public IGhoulCallback {
	std::string dir, seq, note;
	float start = 0;
	bool Event(uint32_t, void *, float time, const void *data) override
	{
		std::printf("%s %s %d %s %s\n", dir.c_str(), seq.c_str(), (int)((time - start) * 1000.0f + 0.5f), note.c_str(),
					data ? (const char *)data : "");
		return true;
	}
};

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: notedump <root> [dir ...]\n");
		return 1;
	}
	g_root = argv[1];
	om_ghoul_host_t host = {};
	host.load_file = LoadFile;
	host.free_file = FreeFile;
	host.print = Print;
	host.fatal = Fatal;
	OMGhoul_Init(&host);
	IGhoulSystem *sys = static_cast<IGhoulSystem *>(OMGhoul_GetSystem(OM_GHOUL_CLIENT));
	std::vector<std::string> dirs;
	for (int i = 2; i < argc; i++)
		dirs.push_back(argv[i]);
	if (dirs.empty())
		dirs = {"Knife", "Pistol2", "Pistol1", "MPistol", "Assaultrifle", "SniperRifle", "Autoshotgun", "Shotgun",
				"Machinegun", "Rocket", "MPG", "Flamegun", "Hurthand", "Throwhand"};
	for (auto &d : dirs) {
		IGhoulObject *obj = sys->CreateObject();
		std::string folder = "Weapon/Inview/" + d;
		if (!OMGhoul_RegisterList(folder.c_str(), d.c_str(), obj)) {
			std::printf("%s: no sequence list\n", d.c_str());
			continue;
		}
		obj->FinishRegistration(true);
		int nn = obj->NumNotes();
		std::vector<Logger> logs(nn + 1);
		IGhoulInst *inst = obj->CreateInstance();
		for (int n = 1; n <= nn; n++) {
			char name[256] = "";
			// note names are not exposed by a getter: find each known key
			(void)name;
		}
		static const char *keys[] = {"sound", "fire", "altfire", "EOS", "cleanblood", "shellbullets", "effect", "BOS"};
		for (auto *k : keys) {
			GhoulID id = obj->FindNote(k);
			if (id) {
				logs[id].dir = d;
				logs[id].note = k;
				inst->AddNoteCallback(&logs[id], id);
			}
		}
		int ns = obj->NumSequences();
		for (int s = 1; s <= ns; s++) {
			char path[512] = "";
			obj->GetSequencePath((GhoulID)s, path);
			std::string seq = path;
			size_t sl = seq.find_last_of("/\\");
			if (sl != std::string::npos)
				seq = seq.substr(sl + 1);
			float ft = 0;
			float dur = inst->SequenceDuration((GhoulID)s, 1, &ft);
			float t0 = 100.0f * s;
			for (auto &l : logs) {
				l.seq = seq;
				l.start = t0;
			}
			inst->Play((GhoulID)s, t0, 0.0f, true, 1, false, false);
			for (int ms = 0; ms <= (int)(dur * 1000.0f) + 50; ms++)
				inst->ProcessEvents(t0 + ms * 0.001f);
			std::printf("%s %s end %d\n", d.c_str(), seq.c_str(), (int)(dur * 1000.0f + 0.5f));
		}
		inst->Destroy();
		obj->Destroy();
	}
	OMGhoul_Shutdown();
	return 0;
}
