// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - Ghoul text files: game imports #62-#66 (.gsq lists) and the player-model
// description (.gpm / .gbm).
// Implements: SPEC-010 D (player-model description, 19 methods), SPEC-010 game imports #62-#66,
//             SPEC-012 section 2 (.gpm/.gbm grammar, field sizes), SPEC-012 caches (text buffers, player-model
//             texts), SPEC-121 (.gsq, .gpm, .gbm)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "ghoul_internal.h"

namespace ghoul {

// ------------------------------------------------------------------ import #62: cached text buffers

static std::string GhoulPath(const std::string &folder, const std::string &file)
{
	return folder.empty() ? "Ghoul/" + file : "Ghoul/" + folder + "/" + file;
}

static bool LoadRaw(const std::string &path, std::vector<char> &out)
{
	if (!Host().load_file)
		return false;
	void *d = nullptr;
	int n = Host().load_file(Host().ctx, path.c_str(), &d);
	if (n < 0 || !d)
	{
		if (d && Host().free_file)
			Host().free_file(Host().ctx, d);
		return false;
	}
	out.assign((const char *)d, (const char *)d + n);
	if (Host().free_file)
		Host().free_file(Host().ctx, d);
	return true;
}

int TextLoad(const char *folder, const char *file, void **buffer)
{
	if (buffer)
		*buffer = nullptr;
	if (!file)
		return -1;
	std::string path = GhoulPath(folder ? folder : "", file);
	std::string key = NormalizeName(path);
	auto it = G().texts.find(key);
	if (it == G().texts.end())
	{
		TextBuffer tb;
		if (LoadRaw(path, tb.data) || LoadRaw(GhoulPath("comskin", file), tb.data))
			tb.length = (int)tb.data.size();
		else
			tb.length = -1; // a missing file is cached too
		it = G().texts.insert(std::make_pair(key, std::move(tb))).first;
	}
	if (it->second.length < 0)
		return -1;
	if (buffer)
		*buffer = it->second.data.empty() ? (void *)"" : (void *)it->second.data.data();
	return it->second.length;
}

// ------------------------------------------------------------------ import #63

int TextGetLine(int *remaining, char **cursor, char *line)
{
	int n = 0;
	while (*remaining > 0)
	{
		char c = **cursor;
		(*cursor)++;
		(*remaining)--;
		if (c == '\n')
		{
			// the last byte copied before the line feed is replaced by NUL (expects CR LF)
			line[n > 0 ? n - 1 : 0] = 0;
			return 1;
		}
		line[n++] = c;
	}
	line[n] = 0;
	return 0;
}

// lines of a .gsq buffer, white space trimmed, empty lines skipped
static void ForEachLine(const char *buf, int len, const std::function<void(const std::string &)> &fn)
{
	std::vector<char> line((size_t)len + 2);
	int remaining = len;
	char *cur = const_cast<char *>(buf);
	for (;;)
	{
		int more = TextGetLine(&remaining, &cur, line.data());
		std::string s(line.data());
		size_t a = s.find_first_not_of(" \t\r\n\f\v");
		size_t b = s.find_last_not_of(" \t\r\n\f\v");
		if (a != std::string::npos)
			fn(s.substr(a, b - a + 1));
		if (!more)
			break;
	}
}

// ------------------------------------------------------------------ imports #64-#66

void TextPreloadList(const char *folder, const char *name)
{
	if (!name)
		return;
	void *b;
	TextLoad(folder, (std::string(name) + ".gsq").c_str(), &b);
}

static bool RegisterListDepth(const std::string &folder, const std::string &name, IGhoulObject *obj, int depth)
{
	if (!obj || depth > 32)
		return false;
	void *buf = nullptr;
	int len = TextLoad(folder.c_str(), (name + ".gsq").c_str(), &buf);
	if (len < 0)
		return obj->RegisterSequence(GhoulPath(folder, name + ".GHL").c_str(), false) != 0;
	bool ok = true;
	ForEachLine((const char *)buf, len, [&](const std::string &l) {
		if (l[0] == '#')
		{
			if (!RegisterListDepth(folder, l.substr(1), obj, depth + 1))
				ok = false;
		}
		else if (!obj->RegisterSequence(GhoulPath(folder, l + ".GHL").c_str(), false))
			ok = false;
	});
	return ok;
}

int TextRegisterList(const char *folder, const char *name, void *object)
{
	if (!object || !name)
		return 0;
	return RegisterListDepth(folder ? folder : "", name, static_cast<IGhoulObject *>(object), 0) ? 1 : 0;
}

static void PartsOffDepth(const std::string &folder, const std::string &name, IGhoulObject *obj, IGhoulInst *inst,
	int depth)
{
	if (depth > 32)
		return;
	void *buf = nullptr;
	int len = TextLoad(folder.c_str(), (name + ".gsq").c_str(), &buf);
	if (len < 0)
		return;
	ForEachLine((const char *)buf, len, [&](const std::string &l) {
		if (l[0] == '#')
			PartsOffDepth(folder, l.substr(1), obj, inst, depth + 1);
		else
		{
			GhoulID p = obj->FindPart(l.c_str());
			if (p)
				inst->SetPartOnOff(p, false);
		}
	});
}

void TextPartsOff(const char *folder, const char *name, void *object, void *instance)
{
	if (!object || !instance || !name)
		return;
	PartsOffDepth(folder ? folder : "", name, static_cast<IGhoulObject *>(object), static_cast<IGhoulInst *>(instance),
		0);
}

// ------------------------------------------------------------------ player-model description (SPEC-010 D)

namespace {

class Lexer
{
public:
	Lexer(const char *b, size_t n) : p_(b), e_(b + n) {}
	static bool Ws(char c) { return c == '\f' || c == '\v' || c == '\t' || c == '\r' || c == '\n' || c == ' '; }
	bool Comment(const char *q) const { return q + 1 < e_ && q[0] == '/' && q[1] == '/'; }
	const char *SkipComment(const char *q) const
	{
		q += 2;
		while (q < e_ && !(*q == '\f' || *q == '\v' || *q == '\r' || *q == '\n'))
			q++;
		return q;
	}
	void SkipWs()
	{
		while (p_ < e_)
		{
			if (Ws(*p_) || *p_ == '{')
				p_++;
			else if (Comment(p_))
				p_ = SkipComment(p_);
			else
				break;
		}
	}
	// false: end of list ('}' consumed) or end of data; the field is then empty
	bool Token(std::string &out)
	{
		out.clear();
		SkipWs();
		if (p_ >= e_)
			return false;
		if (*p_ == '}')
		{
			p_++;
			return false;
		}
		if (*p_ == '"')
		{
			const char *s = ++p_;
			while (p_ < e_ && *p_ != '"')
				p_++;
			out.assign(s, p_);
			if (p_ < e_)
				p_++;
			return true;
		}
		const char *s = p_;
		while (p_ < e_ && !Ws(*p_) && *p_ != '{' && *p_ != '}' && !Comment(p_))
			p_++;
		out.assign(s, p_);
		return true;
	}
	// between header tokens a single '}' met before any other brace is skipped
	void SkipStrayClose()
	{
		const char *q = p_;
		while (q < e_)
		{
			if (Ws(*q))
				q++;
			else if (Comment(q))
				q = SkipComment(q);
			else
				break;
		}
		if (q < e_ && *q == '}')
			p_ = q + 1;
	}

private:
	const char *p_, *e_;
};

void Copy(char *dst, size_t size, const std::string &s)
{
	// SPEC-012: the original does no bounds checks; values are truncated here
	size_t n = s.size() < size - 1 ? s.size() : size - 1;
	std::memcpy(dst, s.data(), n);
	dst[n] = 0;
}

class PlayerModel : public IGhoulPlayerModel
{
public:
	void Load(const char *file) override;
	int GetUserValue() override { return userValue_; }
	void SetUserValue(int v) override { userValue_ = v; }
	int NumSkins() override { return (int)skins_.size(); }
	int NumBoltons() override { return (int)boltons_.size(); }
	int NumPartSwitches() override { return (int)parts_.size(); }
	const char *GetModelDir() override { return dir_; }
	const char *GetMenuSequence() override { return menu_; }
	const char *GetBaseModel() override { return base_; }
	const char *GetSequenceList() override { return set_; }
	const char *GetFacePrefix() override { return face_; }
	bool GetTrailer(char *out) override
	{
		if (out)
			std::strcpy(out, trailer_);
		return true;
	}
	bool GetTeam(char *out) override
	{
		if (out)
			std::strcpy(out, team_);
		return true;
	}
	bool GetSoundDir(char *out) override
	{
		if (out)
			std::strcpy(out, sound_);
		return true;
	}
	bool GetSkinEntry(int index, PlayerSkinEntry *out) override
	{
		if (index < 1 || index > (int)skins_.size() || !out)
			return false;
		*out = skins_[index - 1];
		return true;
	}
	bool GetBoltonEntry(int index, PlayerBoltonEntry *out) override
	{
		if (index < 1 || index > (int)boltons_.size() || !out)
			return false;
		*out = boltons_[index - 1];
		return true;
	}
	bool GetPartEntry(int index, PlayerPartEntry *out) override
	{
		if (index < 1 || index > (int)parts_.size() || !out)
			return false;
		*out = parts_[index - 1];
		return true;
	}
	bool IsLoaded() override { return loaded_; }
	void *Destroy(int flags) override
	{
		if (flags & 1)
		{
			delete this;
			return this;
		}
		Clear();
		return this;
	}

	~PlayerModel() {}
	void Clear()
	{
		userValue_ = 0;
		loaded_ = false;
		dir_[0] = menu_[0] = base_[0] = set_[0] = team_[0] = sound_[0] = face_[0] = trailer_[0] = 0;
		skins_.clear();
		boltons_.clear();
		parts_.clear();
	}

private:
	int userValue_ = 0;
	bool loaded_ = false;
	char dir_[100] = {}, menu_[100] = {}, base_[100] = {}, set_[100] = {}, team_[50] = {}, sound_[100] = {},
		 face_[50] = {}, trailer_[200] = {};
	std::vector<PlayerSkinEntry> skins_;
	std::vector<PlayerBoltonEntry> boltons_;
	std::vector<PlayerPartEntry> parts_;
};

void PlayerModel::Load(const char *file)
{
	Clear();
	if (!file)
		return;
	std::string path = std::string("Ghoul/pmodels/") + file;
	std::string key = NormalizeName(path);
	auto it = G().playerTexts.find(key);
	if (it == G().playerTexts.end())
	{
		PlayerModelText t;
		if (!LoadRaw(path, t.data))
			return; // a missing file is not cached (retried every load)
		it = G().playerTexts.insert(std::make_pair(key, std::move(t))).first;
	}
	loaded_ = true; // "loaded" means the file could be read
	const std::vector<char> &d = it->second.data;
	Lexer lx(d.data(), d.size());
	std::string tok[7];
	for (int i = 0; i < 7; i++)
	{
		if (i)
			lx.SkipStrayClose();
		lx.Token(tok[i]);
	}
	Copy(dir_, sizeof(dir_), tok[0]);
	std::snprintf(menu_, sizeof(menu_), "ghoul/%s/%s.ghl", tok[0].c_str(), tok[1].c_str());
	Copy(base_, sizeof(base_), tok[2]);
	Copy(set_, sizeof(set_), tok[3]);
	Copy(team_, sizeof(team_), tok[4]);
	Copy(sound_, sizeof(sound_), tok[5]);
	Copy(face_, sizeof(face_), tok[6]);
	// skin triples (material, skin, part; a part token beginning with '0' = whole model)
	for (;;)
	{
		std::string m, s, p;
		if (!lx.Token(m) || m.empty())
			break;
		bool more = lx.Token(s);
		more = more && lx.Token(p);
		PlayerSkinEntry e;
		std::memset(&e, 0, sizeof(e));
		Copy(e.material, sizeof(e.material), m);
		Copy(e.skin, sizeof(e.skin), s);
		Copy(e.part, sizeof(e.part), (!p.empty() && p[0] == '0') ? std::string() : p);
		skins_.push_back(e);
		if (!more)
			break;
	}
	// bolt-on quadruples (file, parent bolt, bolt-on bolt, scale)
	for (;;)
	{
		std::string f, pb, bb, sc;
		if (!lx.Token(f) || f.empty())
			break;
		bool more = lx.Token(pb);
		more = more && lx.Token(bb);
		more = more && lx.Token(sc);
		PlayerBoltonEntry e;
		std::memset(&e, 0, sizeof(e));
		e.scale = (float)std::atof(sc.c_str());
		Copy(e.bolt, sizeof(e.bolt), pb);
		Copy(e.boltonBolt, sizeof(e.boltonBolt), bb);
		Copy(e.file, sizeof(e.file), f);
		boltons_.push_back(e);
		if (!more)
			break;
	}
	// part switches (part, value): shown iff atoi(value) != 0
	for (;;)
	{
		std::string pn, v;
		if (!lx.Token(pn) || pn.empty())
			break;
		bool more = lx.Token(v);
		PlayerPartEntry e;
		std::memset(&e, 0, sizeof(e));
		Copy(e.part, sizeof(e.part), pn);
		e.shown = std::atoi(v.c_str()) != 0 ? 1 : 0;
		parts_.push_back(e);
		if (!more)
			break;
	}
	std::string tr;
	lx.Token(tr);
	Copy(trailer_, sizeof(trailer_), tr);
}

} // namespace

IGhoulPlayerModel *NewPlayerModel(const char *file)
{
	PlayerModel *p = new PlayerModel();
	p->Load(file);
	return p;
}

} // namespace ghoul
