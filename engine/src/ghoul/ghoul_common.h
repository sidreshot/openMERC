// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - host access, error reporting, name rules.
// Implements: SPEC-010 (conventions: upper-case names, '/' paths, fatal "Ghoul :" errors),
//             SPEC-012 (limits)
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "ghoul_host.h"

namespace ghoul {

void SetHost(const om_ghoul_host_t *host);
const om_ghoul_host_t &Host();

void Print(const char *fmt, ...);
// "Ghoul :<message>" through the host's fatal path; never returns.
[[noreturn]] void Fatal(const char *fmt, ...);
double Milliseconds();

void *MemAlloc(size_t size);
void MemFree(void *p);

// A file loaded through the host; releases itself.
class FileBuffer
{
public:
	FileBuffer() = default;
	~FileBuffer();
	FileBuffer(const FileBuffer &) = delete;
	FileBuffer &operator=(const FileBuffer &) = delete;
	bool Load(const std::string &path); // false when missing
	const uint8_t *Data() const { return data_; }
	size_t Size() const { return size_; }
	void Reset();

private:
	uint8_t *data_ = nullptr;
	size_t size_ = 0;
};

bool FileExists(const std::string &path);

// Upper-case, '\' -> '/' (SPEC-010 name rule)
std::string NormalizeName(const char *s);
std::string NormalizeName(const std::string &s);
std::string Upper(const std::string &s);
bool IEquals(const char *a, const char *b);

// strip a trailing ".ext" (4 characters) when present
std::string StripExtension(const std::string &s);

// directory part of a path including the trailing '/', "" when none
std::string DirectoryOf(const std::string &path);

std::string StrFormat(const char *fmt, ...);

} // namespace ghoul
