/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-740 (test vector 1: routing rule), SPEC-143 (layout round trip)
 * Owner: package G5.
 *
 * Stand-alone checker for the navigation data code (nav_data.cpp), run against the stock .nvb files:
 *   1. every file parses to its end and writes back byte-identically (layout incl. SPEC-740 amendments);
 *   2. recomputing next_link, rank, tail2 and tail3 with SPEC-740 rule 14.6 from the stored links reproduces
 *      every stored entry exactly;
 *   3. the grid lookup (rule 2) finds, for the centre of every layer just above its floor, that layer or a
 *      higher layer of the same cell (statistics only: the lookup takes the highest floor below the point).
 * usage: navcheck <file.nvb>...      exit code 0 when 1 and 2 hold for every file
 */
#include "../nav_data.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace om_nav;

static uint8_t *ReadFile(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = (uint8_t *)malloc(n > 0 ? n : 1);
	*len = fread(b, 1, n, f);
	fclose(f);
	return b;
}

int main(int argc, char **argv)
{
	int bad_files = 0;
	long long total_entries = 0, total_mismatch = 0, total_tails = 0, tail_mismatch = 0;
	long long lookups = 0, lookup_self = 0, lookup_other = 0, lookup_none = 0;
	for (int a = 1; a < argc; a++) {
		size_t len = 0;
		uint8_t *buf = ReadFile(argv[a], &len);
		if (!buf) {
			printf("%s: cannot read\n", argv[a]);
			bad_files++;
			continue;
		}
		NavData nav;
		char err[128];
		if (!ParseNvb(buf, len, &nav, err, sizeof(err))) {
			printf("%s: parse error: %s\n", argv[a], err);
			bad_files++;
			free(buf);
			continue;
		}
		bool ok = true;
		size_t wlen = WriteNvb(&nav, NULL, 0);
		uint8_t *wbuf = (uint8_t *)malloc(wlen ? wlen : 1);
		WriteNvb(&nav, wbuf, wlen);
		if (wlen != len || memcmp(wbuf, buf, len)) {
			size_t d = 0;
			while (d < len && d < wlen && wbuf[d] == buf[d])
				d++;
			printf("%s: round trip differs (size %zu vs %zu, first difference at %zu)\n", argv[a], wlen, len, d);
			ok = false;
		}
		free(wbuf);

		/* save the stored tables, recompute, compare */
		int n = nav.n;
		uint8_t *nl = (uint8_t *)malloc((size_t)n * n + 1);
		int16_t *rk = (int16_t *)malloc(sizeof(int16_t) * ((size_t)n * n + 1));
		int *t2 = (int *)malloc(sizeof(int) * (n + 1)), *t3 = (int *)malloc(sizeof(int) * (n + 1));
		for (int i = 0; i < n; i++) {
			if (nav.nodes[i].next_link) {
				memcpy(nl + (size_t)i * n, nav.nodes[i].next_link, n);
				memcpy(rk + (size_t)i * n, nav.nodes[i].rank, 2 * (size_t)n);
			}
			t2[i] = nav.nodes[i].tail2;
			t3[i] = nav.nodes[i].tail3;
		}
		ComputeRoutes(nav.nodes, n);
		int mism = 0, tmism = 0;
		for (int i = 0; i < n; i++) {
			if (!nav.nodes[i].next_link)
				continue;
			for (int j = 0; j < n; j++) {
				total_entries += 2;
				if (nav.nodes[i].next_link[j] != nl[(size_t)i * n + j]) {
					if (mism < 5)
						printf("%s: node %d next_link[%d] = %d, stored %d\n", argv[a], i, j,
						       nav.nodes[i].next_link[j], nl[(size_t)i * n + j]);
					mism++;
				}
				if (nav.nodes[i].rank[j] != rk[(size_t)i * n + j]) {
					if (mism < 5)
						printf("%s: node %d rank[%d] = %d, stored %d\n", argv[a], i, j, nav.nodes[i].rank[j],
						       rk[(size_t)i * n + j]);
					mism++;
				}
			}
			total_tails += 2;
			if (nav.nodes[i].tail2 != t2[i] || nav.nodes[i].tail3 != t3[i]) {
				if (tmism < 5)
					printf("%s: node %d tails %d %d, stored %d %d\n", argv[a], i, nav.nodes[i].tail2,
					       nav.nodes[i].tail3, t2[i], t3[i]);
				tmism++;
			}
		}
		total_mismatch += mism;
		tail_mismatch += tmism;
		if (mism || tmism)
			ok = false;
		free(nl);
		free(rk);
		free(t2);
		free(t3);

		/* grid lookup statistics */
		for (int c = 0; c < nav.ncolumns; c++)
			for (int k = 0; k < nav.columns[c].ncells; k++) {
				const NavCell &cell = nav.columns[c].cells[k];
				for (int l = 0; l < cell.nlayers; l++) {
					const NavLayer &ly = cell.layers[l];
					float p[3] = {ly.pos[0] + 24.0f, ly.pos[1] + 24.0f, ly.pos[2] + 1.0f};
					const NavLayer *r = LookupLayer(&nav, p);
					lookups++;
					if (r == &ly)
						lookup_self++;
					else if (r)
						lookup_other++;
					else
						lookup_none++;
				}
			}
		printf("%-40s %s  N=%d M=%d L=%d C=%d  table mismatches %d, tail mismatches %d\n", argv[a],
		       ok ? "OK " : "BAD", nav.n, nav.m, nav.nlayers, nav.ncolumns, mism, tmism);
		if (!ok)
			bad_files++;
		FreeNvb(&nav);
		free(buf);
	}
	printf("files %d, bad %d; routing entries %lld, mismatches %lld; tails %lld, mismatches %lld\n", argc - 1,
	       bad_files, total_entries, total_mismatch, total_tails, tail_mismatch);
	printf("grid lookups %lld: own layer %lld, other layer %lld, none %lld\n", lookups, lookup_self,
	       lookup_other, lookup_none);
	return bad_files ? 1 : 0;
}
