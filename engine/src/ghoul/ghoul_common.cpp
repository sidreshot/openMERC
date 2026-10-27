// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - host access, error reporting, name rules.
// Implements: SPEC-010 (conventions), SPEC-012 (limits)

#include "ghoul_common.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ghoul {

static om_ghoul_host_t g_host;
static bool g_hostSet = false;

void SetHost(const om_ghoul_host_t *host)
{
	if (host)
	{
		g_host = *host;
		g_hostSet = true;
	}
	else
	{
		std::memset(&g_host, 0, sizeof(g_host));
		g_hostSet = false;
	}
}

const om_ghoul_host_t &Host() { return g_host; }

static std::string VFormat(const char *fmt, va_list ap)
{
	char small[1024];
	va_list ap2;
	va_copy(ap2, ap);
	int n = std::vsnprintf(small, sizeof(small), fmt, ap2);
	va_end(ap2);
	if (n < 0)
		return std::string();
	if (n < (int)sizeof(small))
		return std::string(small, (size_t)n);
	std::string s((size_t)n + 1, '\0');
	std::vsnprintf(&s[0], s.size(), fmt, ap);
	s.resize((size_t)n);
	return s;
}

std::string StrFormat(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::string s = VFormat(fmt, ap);
	va_end(ap);
	return s;
}

void Print(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::string s = VFormat(fmt, ap);
	va_end(ap);
	if (g_host.print)
		g_host.print(g_host.ctx, s.c_str());
	else
		std::fputs(s.c_str(), stdout);
}

void Fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::string s = "Ghoul :" + VFormat(fmt, ap);
	va_end(ap);
	if (g_host.fatal)
		g_host.fatal(g_host.ctx, s.c_str());
	std::fprintf(stderr, "%s\n", s.c_str());
	std::abort();
}

double Milliseconds()
{
	if (g_host.milliseconds)
		return g_host.milliseconds(g_host.ctx);
	return 0.0;
}

void *MemAlloc(size_t size)
{
	if (g_host.alloc)
		return g_host.alloc(g_host.ctx, size);
	return std::malloc(size);
}

void MemFree(void *p)
{
	if (!p)
		return;
	if (g_host.release)
		g_host.release(g_host.ctx, p);
	else
		std::free(p);
}

FileBuffer::~FileBuffer() { Reset(); }

void FileBuffer::Reset()
{
	if (data_ && g_host.free_file)
		g_host.free_file(g_host.ctx, data_);
	data_ = nullptr;
	size_ = 0;
}

bool FileBuffer::Load(const std::string &path)
{
	Reset();
	if (!g_host.load_file)
		return false;
	void *d = nullptr;
	int len = g_host.load_file(g_host.ctx, path.c_str(), &d);
	if (len < 0 || !d)
	{
		if (d && g_host.free_file)
			g_host.free_file(g_host.ctx, d);
		return false;
	}
	data_ = (uint8_t *)d;
	size_ = (size_t)len;
	return true;
}

bool FileExists(const std::string &path)
{
	FileBuffer b;
	return b.Load(path);
}

std::string NormalizeName(const char *s)
{
	std::string r;
	if (!s)
		return r;
	for (; *s; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (c == '\\')
			c = '/';
		r.push_back((char)std::toupper(c));
	}
	return r;
}

std::string NormalizeName(const std::string &s) { return NormalizeName(s.c_str()); }

std::string Upper(const std::string &s)
{
	std::string r(s);
	for (auto &c : r)
		c = (char)std::toupper((unsigned char)c);
	return r;
}

bool IEquals(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int ca = std::toupper((unsigned char)*a), cb = std::toupper((unsigned char)*b);
		if (ca != cb)
			return false;
		if (!ca)
			return true;
	}
}

std::string StripExtension(const std::string &s)
{
	if (s.size() >= 4 && s[s.size() - 4] == '.')
		return s.substr(0, s.size() - 4);
	return s;
}

std::string DirectoryOf(const std::string &path)
{
	size_t p = path.find_last_of("/\\");
	if (p == std::string::npos)
		return std::string();
	return path.substr(0, p + 1);
}

} // namespace ghoul
