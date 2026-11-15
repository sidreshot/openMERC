// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - pointers into the module saved by name, so a save survives a rebuild.
//
// Callbacks and static tables used to be saved as offsets in the module image, valid only for the very same build:
// every rebuild made all saves refuse to load. Now a save carries the names of the symbols its pointers point at
// (OMSY chunk, right after FTSZ) and each pointer field holds an index into that list; a later build resolves the
// names with its own symbol table.
//
// The symbol table is part of the module: om_save_symtab below is a block reserved in the read-only data, filled
// after linking by tools/om_symtab.py from the linker map (every function and static object, static ones included:
// name, offset from the image base). Format: "OMSYMTB1", u32 count, u32 string bytes, count x {u32 offset, u32
// string offset} sorted by offset, the strings. A name used by more than one object file is "object!name".
//
// A pointer that is not at a symbol is saved as "name+hex" (an element of a table). A name the reading build does
// not have becomes NULL with a console line (a callback that was removed from the code).
//
// Implements: SPEC-752 2.1 (pointer encoding, OpenMerc's own tables for its own saves)

#include "save_local.h"

#include "../damage/damage_api.h"
#include "../props/props.h"

#include <cstring>
#include <cstdlib>
#include <cxxabi.h>
#include <string>
#include <unordered_map>
#include <vector>

/* element sizes of the tables a 32-bit save names with "symbol+hex". The 32-bit sizes are that ABI's sizeof. */
static_assert(sizeof(props::ModelEntry) == 48, "32-bit save element scale");
static_assert(sizeof(props::PartRow) == 24, "32-bit save element scale");
static_assert(sizeof(G3Anim) == 40, "32-bit save element scale");
static_assert(sizeof(gitem_t) == 104, "32-bit save element scale");

#define OM_SYMTAB_BYTES (1024 * 1024)

// the placeholder the patch step finds and overwrites (tools/om_symtab.py: OM_PLACEHOLDER)
extern "C" const unsigned char om_save_symtab[OM_SYMTAB_BYTES] = "OMSYMTAB:PLACEHOLDER:7f3a91c2";

namespace g10 {

struct SymEntry {
	uint32_t off, str;
};

static const unsigned char *s_tab;
static uint32_t s_count;
static const SymEntry *s_ent;
static const char *s_str;
static bool s_tried;
static std::unordered_map<std::string, uint32_t> s_byname;

// the table as patched into the file (read through a volatile pointer: the compiler knows only the placeholder)
static bool Table(void)
{
	if (s_tried)
		return s_tab != nullptr;
	s_tried = true;
	const unsigned char *volatile p = om_save_symtab;
	const unsigned char *t = p;
	if (memcmp(t, "OMSYMTB1", 8)) {
		gi.dprintf("savegame: the module has no symbol table (tools/om_symtab.py did not run) - callbacks cannot be "
		           "saved\n");
		return false;
	}
	uint32_t n, sb;
	memcpy(&n, t + 8, 4);
	memcpy(&sb, t + 12, 4);
	if (16 + (uint64_t)n * 8 + sb > OM_SYMTAB_BYTES)
		return false;
	s_tab = t;
	s_count = n;
	s_ent = (const SymEntry *)(t + 16);
	s_str = (const char *)(t + 16 + n * 8);
	return true;
}

// "name" or "name+hex" for a pointer into the module, false if it is not ours or there is no table
static bool SymName(const void *p, std::string &out)
{
	const ImageInfo &im = Image();
	const unsigned char *q = (const unsigned char *)p;
	if (!Table() || !im.base || q < im.base || (uint64_t)(q - im.base) >= im.size)
		return false;
	uint32_t off = (uint32_t)(q - im.base);
	uint32_t lo = 0, hi = s_count;
	while (lo < hi) { // first entry with offset > off
		uint32_t mid = (lo + hi) / 2;
		if (s_ent[mid].off <= off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo == 0)
		return false;
	const SymEntry &e = s_ent[lo - 1];
	out = s_str + e.str;
	if (off != e.off) {
		char d[16];
		snprintf(d, sizeof(d), "+%x", off - e.off);
		out += d;
	}
	return true;
}

// Itanium name to the qualified name a Windows map uses ("ns::func", no arguments).
static std::string QualOf(const char *mangled)
{
	std::string s = mangled ? mangled : "";
	size_t bang = s.rfind('!');
	if (bang != std::string::npos)
		s = s.substr(bang + 1);
	if (s.size() < 2 || s[0] != '_' || s[1] != 'Z')
		return s;
	int status = -1;
	char *d = abi::__cxa_demangle(s.c_str(), nullptr, nullptr, &status);
	if (!d || status != 0) {
		std::free(d);
		return s;
	}
	std::string out(d);
	std::free(d);
	size_t paren = out.find('(');
	if (paren != std::string::npos)
		out.resize(paren);
	const char *anon = "(anonymous namespace)::";
	for (;;) {
		size_t a = out.find(anon);
		if (a == std::string::npos)
			break;
		size_t n = strlen(anon);
		if (a > 0 && out[a - 1] == ' ') {
			a--;
			n++;
		}
		out.erase(a, n);
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

static std::unordered_map<std::string, uint32_t> s_byqual;
static bool s_qual_ready;

static void BuildQual(void)
{
	if (s_qual_ready)
		return;
	s_qual_ready = true;
	if (!Table())
		return;
	for (uint32_t i = 0; i < s_count; i++)
		s_byqual.emplace(QualOf(s_str + s_ent[i].str), s_ent[i].off);
}

static int Elem32(const std::string &type)
{
	if (type.find("ModelEntry") != std::string::npos)
		return 36;
	if (type.find("PartRow") != std::string::npos)
		return 12;
	if (type.find("G3Anim") != std::string::npos)
		return 36;
	if (type.find("gitem_s") != std::string::npos)
		return 64;
	return 0;
}

static int Elem64(const std::string &type)
{
	if (type.find("ModelEntry") != std::string::npos)
		return (int)sizeof(props::ModelEntry);
	if (type.find("PartRow") != std::string::npos)
		return (int)sizeof(props::PartRow);
	if (type.find("G3Anim") != std::string::npos)
		return (int)sizeof(G3Anim);
	if (type.find("gitem_s") != std::string::npos)
		return (int)sizeof(gitem_t);
	return 0;
}

// "?name@ns@@..." from a Windows link map, plus an optional "+hex" byte offset into a table.
static void *MsvcPtr(const std::string &name)
{
	std::string s = name;
	uint32_t delta = 0;
	size_t plus = s.rfind('+');
	if (plus != std::string::npos && plus + 1 < s.size()) {
		char *end = nullptr;
		unsigned long v = strtoul(s.c_str() + plus + 1, &end, 16);
		if (end && *end == 0 && end != s.c_str() + plus + 1) {
			delta = (uint32_t)v;
			s.resize(plus);
		}
	}
	size_t bang = s.rfind('!');
	if (bang != std::string::npos)
		s = s.substr(bang + 1);
	if (s.empty() || s[0] != '?')
		return nullptr;
	size_t cut = s.find("@@");
	if (cut == std::string::npos || cut < 2)
		return nullptr;
	std::string type = s.substr(cut + 2);
	std::string body = s.substr(1, cut - 1);
	std::vector<std::string> parts;
	size_t at = 0;
	while (at <= body.size()) {
		size_t n = body.find('@', at);
		if (n == std::string::npos) {
			if (at < body.size())
				parts.push_back(body.substr(at));
			break;
		}
		parts.push_back(body.substr(at, n - at));
		at = n + 1;
	}
	if (parts.empty() || parts[0].empty())
		return nullptr;
	std::string qual;
	for (int k = (int)parts.size() - 1; k >= 1; k--) {
		if (!qual.empty())
			qual += "::";
		qual += parts[k];
	}
	if (!qual.empty())
		qual += "::";
	qual += parts[0];
	BuildQual();
	auto it = s_byqual.find(qual);
	if (it == s_byqual.end())
		return nullptr;
	uint32_t off = it->second;
	if (delta) {
		int e32 = Elem32(type);
		int e64 = Elem64(type);
		if (e32 <= 0 || e64 <= 0 || (delta % (uint32_t)e32) != 0)
			return nullptr;
		off += (delta / (uint32_t)e32) * (uint32_t)e64;
	}
	const ImageInfo &im = Image();
	if (!im.base)
		return nullptr;
	return (void *)(im.base + off);
}

static void *SymPtr(const std::string &name)
{
	if (!Table())
		return nullptr;
	if (s_byname.empty())
		for (uint32_t i = 0; i < s_count; i++)
			s_byname.emplace(s_str + s_ent[i].str, s_ent[i].off);
	std::string base = name;
	uint32_t delta = 0;
	size_t plus = name.rfind('+');
	if (plus != std::string::npos && plus > 0) {
		base = name.substr(0, plus);
		delta = (uint32_t)strtoul(name.c_str() + plus + 1, nullptr, 16);
	}
	auto it = s_byname.find(base);
	if (it != s_byname.end())
		return (void *)(Image().base + it->second + delta);
	return MsvcPtr(name);
}

// ---------------------------------------------------------------------------------------------------------------
// the names of one save (level section)

static std::vector<std::string> s_names;
static std::unordered_map<std::string, int> s_index;
static std::vector<void *> s_ptrs;
static size_t s_written;

void SymReset(void)
{
	s_names.clear();
	s_index.clear();
	s_ptrs.clear();
	s_written = 0;
}

int SymEncode(const void *p)
{
	if (!p)
		return -1;
	std::string nm;
	if (!SymName(p, nm))
		return -1;
	auto it = s_index.find(nm);
	if (it != s_index.end())
		return it->second;
	int k = (int)s_names.size();
	s_names.push_back(nm);
	s_index.emplace(nm, k);
	return k;
}

void *SymDecode(int code)
{
	if (code < 0 || code >= (int)s_ptrs.size())
		return nullptr;
	return s_ptrs[code];
}

// OMSY: u32 count, then the names, each with its terminating zero
void SymWriteTable(void)
{
	std::vector<char> blob(4);
	uint32_t n = (uint32_t)s_names.size();
	memcpy(blob.data(), &n, 4);
	for (const std::string &s : s_names)
		blob.insert(blob.end(), s.c_str(), s.c_str() + s.size() + 1);
	G10_WriteChunk("OMSY", blob.data(), (int)blob.size());
	s_written = s_names.size();
}

// a pointer encoded after the table was written could not be read back: a write path without a collect step
void SymCheckComplete(void)
{
	if (s_names.size() > s_written)
		gi.dprintf("savegame: %d pointer(s) after the symbol list, e.g. %s - not restorable\n",
		           (int)(s_names.size() - s_written), s_names[s_written].c_str());
}

bool SymReadTable(void)
{
	SymReset();
	void *d = nullptr;
	int len = G10_ReadChunkAny("OMSY", &d);
	if (len < 4 || !d) {
		G10_Free(d);
		return false;
	}
	const char *b = (const char *)d;
	uint32_t n;
	memcpy(&n, b, 4);
	int pos = 4, missing = 0;
	for (uint32_t i = 0; i < n && pos < len; i++) {
		std::string nm(b + pos);
		pos += (int)nm.size() + 1;
		void *p = SymPtr(nm);
		if (!p && missing++ < 8)
			gi.dprintf("savegame: %s is not in this build - restored as none\n", nm.c_str());
		s_ptrs.push_back(p);
	}
	if (missing > 8)
		gi.dprintf("savegame: %d names not in this build\n", missing);
	G10_Free(d);
	return true;
}

} // namespace g10
