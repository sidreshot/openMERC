// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - Ghoul bit streams: LSB-first bits, variable-length ids / times / frame offsets
// and Ghoul strings (dictionary, Huffman and raw literal blocks).
// Implements: SPEC-201 Appendix B.1-B.3 (bit order, integers, strings), Appendix C (dictionary),
//             Appendix D (Huffman weights); SPEC-013 (string coding of savegame images)

#include <cstring>
#include <vector>

#include "ghoul_internal.h"
#include "ghoul_tables.h"

namespace ghoul {

void Bits::Write(uint32_t v, int n)
{
	if (n <= 0)
		return;
	if (b_->bit + n > b_->maxbits)
	{
		b_->overflowed = 1;
		b_->bit += n; // position advances so callers can measure; nothing is stored
		return;
	}
	for (int i = 0; i < n; i++)
	{
		int p = b_->bit + i;
		uint8_t &byte = b_->data[p >> 3];
		if ((v >> i) & 1u)
			byte |= (uint8_t)(1u << (p & 7));
		else
			byte &= (uint8_t)~(1u << (p & 7));
	}
	b_->bit += n;
}

uint32_t Bits::Read(int n)
{
	uint32_t v = 0;
	for (int i = 0; i < n; i++)
	{
		int p = b_->bit + i;
		if (p >= b_->maxbits)
		{
			b_->overflowed = 1; // the rest reads as zero
			break;
		}
		if ((b_->data[p >> 3] >> (p & 7)) & 1u)
			v |= 1u << i;
	}
	b_->bit += n;
	return v;
}

void Bits::WriteId(uint32_t v)
{
	v &= 0xFFFFu;
	if (!v)
	{
		Write(0, 1);
		return;
	}
	Write(1, 1);
	uint32_t n = 0;
	while (n < 3 && v >= (1u << (4 * n + 4)))
		n++;
	Write(n, 2);
	Write(v, (int)(4 * n + 4));
}

uint32_t Bits::ReadId()
{
	if (!Read(1))
		return 0;
	uint32_t n = Read(2);
	return Read((int)(4 * n + 4));
}

void Bits::WriteTime(int32_t v)
{
	if (!v)
	{
		Write(0, 1);
		return;
	}
	Write(1, 1);
	uint32_t m = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
	Write(v < 0 ? 1 : 0, 1);
	uint32_t n = 0;
	while (n < 3 && (uint64_t)m >= (1ull << (8 * n + 8)))
		n++;
	Write(n, 2);
	Write(m, (int)(8 * n + 8));
}

int32_t Bits::ReadTime()
{
	if (!Read(1))
		return 0;
	uint32_t sign = Read(1);
	uint32_t n = Read(2);
	uint32_t m = Read((int)(8 * n + 8));
	return sign ? -(int32_t)m : (int32_t)m;
}

void Bits::WriteFrameOffset(uint32_t v)
{
	if (!v)
	{
		Write(0, 1);
		return;
	}
	Write(1, 1);
	uint32_t n = 0;
	while (n < 3 && (uint64_t)v >= (1ull << (8 * n + 8)))
		n++;
	Write(n, 2);
	Write(v, (int)(8 * n + 8));
}

uint32_t Bits::ReadFrameOffset()
{
	if (!Read(1))
		return 0;
	uint32_t n = Read(2);
	return Read((int)(8 * n + 8));
}

void Bits::WriteFloat(float f)
{
	uint32_t u;
	std::memcpy(&u, &f, 4);
	Write(u, 32);
}

float Bits::ReadFloat()
{
	uint32_t u = Read(32);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// ------------------------------------------------------------------ Huffman code (Appendix B.3 / D)

namespace {
struct Huffman
{
	struct Node
	{
		int child[2] = {-1, -1};
		int sym = -1;
		float w = 0;
	};
	std::vector<Node> nodes;
	int root = -1;
	std::vector<uint8_t> codeBits[256]; // branch sequence from the root

	Huffman()
	{
		int slot[256];
		for (int i = 0; i < 256; i++)
		{
			Node n;
			n.sym = i;
			n.w = HuffWeight(i);
			nodes.push_back(n);
			slot[i] = i;
		}
		for (int k = 0; k < 255; k++)
		{
			int s1 = -1, s2 = -1;
			for (int i = 0; i < 256; i++)
				if (slot[i] >= 0 && (s1 < 0 || nodes[slot[i]].w < nodes[slot[s1]].w))
					s1 = i;
			for (int i = 0; i < 256; i++)
				if (i != s1 && slot[i] >= 0 && (s2 < 0 || nodes[slot[i]].w < nodes[slot[s2]].w))
					s2 = i;
			Node n;
			n.child[0] = slot[s2];
			n.child[1] = slot[s1];
			n.w = nodes[slot[s1]].w + nodes[slot[s2]].w; // single precision sum
			nodes.push_back(n);
			slot[s1] = (int)nodes.size() - 1;
			slot[s2] = -1;
		}
		for (int i = 0; i < 256; i++)
			if (slot[i] >= 0)
				root = slot[i];
		std::vector<uint8_t> path;
		Walk(root, path);
	}
	void Walk(int n, std::vector<uint8_t> &path)
	{
		if (nodes[n].sym >= 0)
		{
			codeBits[nodes[n].sym] = path;
			return;
		}
		for (int b = 0; b < 2; b++)
		{
			path.push_back((uint8_t)b);
			Walk(nodes[n].child[b], path);
			path.pop_back();
		}
	}
};
const Huffman &Huff()
{
	static Huffman h;
	return h;
}

// no dictionary (docs/DEVIATIONS.md "Fixed tables"): strings are always coded
int DictFind(const std::string &)
{
	return -1;
}
} // namespace

void Bits::WriteString(const std::string &in)
{
	std::string s = in.size() > 250 ? in.substr(0, 250) : in;
	int d = DictFind(s);
	if (d >= 0)
	{
		Write(1, 1);
		WriteId((uint32_t)d);
		return;
	}
	Write(0, 1);
	// Huffman block
	const Huffman &h = Huff();
	size_t nbits = 0;
	for (unsigned char c : s)
		nbits += h.codeBits[c].size();
	size_t huffL = 1 + (nbits + 7) / 8;
	std::vector<uint8_t> block;
	if (huffL >= s.size() + 1)
	{
		block.push_back(0xFF);
		block.insert(block.end(), s.begin(), s.end());
	}
	else
	{
		block.assign(huffL, 0);
		block[0] = (uint8_t)(8 * (huffL - 1) - nbits);
		size_t p = 8;
		for (unsigned char c : s)
			for (uint8_t b : h.codeBits[c])
			{
				if (b)
					block[p >> 3] |= (uint8_t)(1u << (p & 7));
				p++;
			}
	}
	uint32_t L = (uint32_t)block.size();
	if (!L)
	{
		Write(0, 1);
		return;
	}
	Write(1, 1);
	uint32_t m = 0;
	while (m < 3 && L >= (1u << (2 * (m + 1))))
		m++;
	Write(m, 2);
	Write(L, (int)(2 * m + 2));
	for (uint8_t b : block)
		Write(b, 8);
}

std::string Bits::ReadString()
{
	if (Read(1))
	{
		uint32_t d = ReadId();
		(void)d; // a dictionary code: OpenMerc has no dictionary
		return std::string();
	}
	if (!Read(1))
		return std::string();
	uint32_t m = Read(2);
	uint32_t L = Read((int)(2 * m + 2));
	std::vector<uint8_t> block(L);
	for (auto &b : block)
		b = (uint8_t)Read(8);
	if (L == 0)
		return std::string();
	if (block[0] == 0xFF)
		return std::string(block.begin() + 1, block.end());
	const Huffman &h = Huff();
	int nbits = (int)(8 * L) - 8 - block[0];
	std::string out;
	int n = h.root;
	for (int i = 0; i < nbits; i++)
	{
		int p = 8 + i;
		int bit = (block[p >> 3] >> (p & 7)) & 1;
		n = h.nodes[n].child[bit];
		if (n < 0)
			break;
		if (h.nodes[n].sym >= 0)
		{
			out.push_back((char)h.nodes[n].sym);
			n = h.root;
		}
	}
	return out;
}

} // namespace ghoul
