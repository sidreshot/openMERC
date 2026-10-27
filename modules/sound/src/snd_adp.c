/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: the `.adp` format (uint32 sample rate + headerless 4-bit IMA ADPCM, high nibble
 * first, decoder starting at predictor 0 / index 0, shift-and-add difference; SPEC-110) and the playback
 * rules of the default backend for it (SPEC-040 1.3): the sound stays at its own rate and is mixed with
 * the ratio rule rate/R = 0.25 or 0.5 (each sample repeated 4 or 2 times) or >= 1 (every k-th sample,
 * k = trunc(rate/R)); other ratios below 1 are silent.
 *
 * The original decodes while mixing with the decoder state reset at the start of every play; the decoder
 * is deterministic, so decoding the playable part once at load time gives the same samples.
 *
 * Implements: SPEC-110, SPEC-040 1.3
 */
#include <string.h>

#include "snd_local.h"

/* tables from the public IMA ADPCM recommended practice (1992) */
static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int ima_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

qboolean ADP_Parse(const byte *data, int len, int *rate, const byte **payload, int *nbytes)
{
    if (!data || len < 4)
        return false;
    *rate = (int)(data[0] | (data[1] << 8) | (data[2] << 16) | ((unsigned)data[3] << 24));
    *payload = data + 4;
    *nbytes = len - 4;
    return true;
}

static int adp_step(int *pred, int *index, int code)
{
    int step = ima_step[*index];
    int d = step >> 3;
    if (code & 4)
        d += step;
    if (code & 2)
        d += step >> 1;
    if (code & 1)
        d += step >> 2;
    if (code & 8)
        *pred -= d;
    else
        *pred += d;
    if (*pred > 32767)
        *pred = 32767;
    else if (*pred < -32768)
        *pred = -32768;
    *index += ima_index[code & 7];
    if (*index < 0)
        *index = 0;
    else if (*index > 88)
        *index = 88;
    return *pred;
}

/* decodes the first nsamples samples of the stream; samples past the stream's end are 0 */
void ADP_DecodeTo(const byte *payload, int nbytes, short *out, int nsamples)
{
    int pred = 0, index = 0, i;
    for (i = 0; i < nsamples; i++) {
        int b = i >> 1;
        if (b >= nbytes) {
            memset(out + i, 0, (size_t)(nsamples - i) * sizeof(short));
            return;
        }
        out[i] = (short)adp_step(&pred, &index, (i & 1) ? (payload[b] & 15) : (payload[b] >> 4));
    }
}

int ADP_Ratio(int rate, int outrate)
{
    if (rate <= 0 || outrate <= 0)
        return 0;
    if (rate * 4 == outrate)
        return -4;
    if (rate * 2 == outrate)
        return -2;
    if (rate >= outrate)
        return rate / outrate;
    return 0;
}

int ADP_OutLength(int srclen, int ratio, int rate, int outrate)
{
    if (srclen <= 0)
        return 0;
    if (ratio > 0)
        return (srclen + ratio - 1) / ratio;
    if (ratio == -2)
        return srclen * 2;
    if (ratio == -4)
        return srclen * 4;
    /* unsupported ratio: silent for the sound's duration */
    return rate > 0 ? (int)((double)srclen * outrate / rate) : 0;
}

int ADP_SourceIndex(int outpos, int ratio)
{
    if (ratio > 0)
        return outpos * ratio;
    if (ratio == -2)
        return outpos >> 1;
    if (ratio == -4)
        return outpos >> 2;
    return 0;
}
