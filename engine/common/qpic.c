/*
Copyright (C) 1996-1997 Id Software, Inc.  Copyright (C) 2013 Kevin
Shanahan

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <assert.h>
#include <stdint.h>

#include "qpic.h"
#include "qtypes.h"
#include "vid.h"
#include "zone.h"

/* --------------------------------------------------------------------------*/
/* Pic Format Transformations                                                */
/* --------------------------------------------------------------------------*/

qpic32_t *
QPic32_Alloc(int width, int height)
{
    const int memsize = offsetof(qpic32_t, pixels[width * height]);
    qpic32_t *pic = Hunk_Alloc(memsize);

    if (pic) {
	pic->width = width;
	pic->height = height;
    }

    return pic;
}

/*
================
QPic32_AlphaFix

Operates in-place on an RGBA pic assumed to have all alpha values
either fully opaque or transparent.  Fully transparent pixels get
their color components set to the average colour of their
non-transparent neighbours to avoid artifacts from blending.

TODO: add an edge clamp mode?
================
*/
static void
QPic32_AlphaFix(qpic32_t *pic)
{
    const int width = pic->width;
    const int height = pic->height;
    qpixel32_t *pixels = pic->pixels;

    int x, y, n, red, green, blue, count;
    int neighbours[8];

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++) {
	    const int current = y * width + x;

	    /* only modify completely transparent pixels */
	    if (pixels[current].alpha)
		continue;

	    /*
	     * Neighbour pixel indexes are left to right:
	     *   1 2 3
	     *   4 * 5
	     *   6 7 8
	     */
	    neighbours[0] = current - width - 1;
	    neighbours[1] = current - width;
	    neighbours[2] = current - width + 1;
	    neighbours[3] = current - 1;
	    neighbours[4] = current + 1;
	    neighbours[5] = current + width - 1;
	    neighbours[6] = current + width;
	    neighbours[7] = current + width + 1;

	    /* handle edge cases (wrap around) */
	    if (!x) {
		neighbours[0] += width;
		neighbours[3] += width;
		neighbours[5] += width;
	    } else if (x == width - 1) {
		neighbours[2] -= width;
		neighbours[4] -= width;
		neighbours[7] -= width;
	    }
	    if (!y) {
		neighbours[0] += width * height;
		neighbours[1] += width * height;
		neighbours[2] += width * height;
	    } else if (y == height - 1) {
		neighbours[5] -= width * height;
		neighbours[6] -= width * height;
		neighbours[7] -= width * height;
	    }

	    /* find the average colour of non-transparent neighbours */
	    red = green = blue = count = 0;
	    for (n = 0; n < 8; n++) {
		if (!pixels[neighbours[n]].alpha)
		    continue;
		red += pixels[neighbours[n]].red;
		green += pixels[neighbours[n]].green;
		blue += pixels[neighbours[n]].blue;
		count++;
	    }

	    /* skip if no non-transparent neighbours */
	    if (!count)
		continue;

	    pixels[current].red = red / count;
	    pixels[current].green = green / count;
	    pixels[current].blue = blue / count;
	}
    }
}

void
QPic_8to32(const qpic8_t *in, qpic32_t *out)
{
    const int width = in->width;
    const int height = in->height;
    const int stride = in->stride;
    const byte *in_p = in->pixels;
    qpixel32_t *out_p = out->pixels;
    int x, y;

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++, in_p++, out_p++)
	    out_p->rgba = d_8to24table[*in_p];
	in_p += stride - width;
    }
}

void
QPic_8to32_Alpha(const qpic8_t *in, qpic32_t *out, byte alpha)
{
    const int width = in->width;
    const int height = in->height;
    const int stride = in->stride;
    const byte *in_p = in->pixels;
    qpixel32_t *out_p = out->pixels;
    int x, y;

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++, in_p++, out_p++)
	    out_p->rgba = (*in_p == alpha) ? 0 : d_8to24table[*in_p];
	in_p += stride - width;
    }
    QPic32_AlphaFix(out);
}

/*
================
QPic32_Stretch
TODO - should probably be doing bilinear filtering or something
================
*/
void
QPic32_Stretch(const qpic32_t *in, qpic32_t *out)
{
    int i, j;
    const qpixel32_t *inrow;
    qpixel32_t *outrow;
    unsigned frac, fracstep;

    assert(!(out->width & 3));

    fracstep = in->width * 0x10000 / out->width;
    outrow = out->pixels;
    for (i = 0; i < out->height; i++, outrow += out->width) {
	inrow = in->pixels + in->width * (i * in->height / out->height);
	frac = fracstep >> 1;
	for (j = 0; j < out->width; j += 4) {
	    outrow[j] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 1] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 2] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 3] = inrow[frac >> 16];
	    frac += fracstep;
	}
    }
}

/* --------------------------------------------------------------------------*/
/* Mipmaps - Handle all variations of even/odd dimensions                    */
/* --------------------------------------------------------------------------*/

static void
QPic32_MipMap_1D_Even(qpixel32_t *pixels, int length)
{
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	out[0] = ((int)in[0] + in[4]) >> 1;
	out[1] = ((int)in[1] + in[5]) >> 1;
	out[2] = ((int)in[2] + in[6]) >> 1;
	out[3] = ((int)in[3] + in[7]) >> 1;
    }
}

static void
QPic32_MipMap_1D_Odd(qpixel32_t *pixels, int length)
{
    const int inlength = length;
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;

    const float w1 = (float)inlength / length;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	const float w0 = (float)(i - length) / inlength;
	const float w2 = (float)(i + 1) / inlength;

	out[0] = w0 * in[0] + w1 * in[4] + w2 * in[8];
	out[1] = w0 * in[1] + w1 * in[5] + w2 * in[9];
	out[2] = w0 * in[2] + w1 * in[6] + w2 * in[10];
	out[3] = w0 * in[3] + w1 * in[7] + w2 * in[11];
    }
}

/*
================
QPic32_MipMap_EvenEven

Simple 2x2 box filter for pics with even width/height
================
*/
static void
QPic32_MipMap_EvenEven(qpixel32_t *pixels, int width, int height)
{
    int i, j;
    byte *in, *out;

    in = out = (byte *)pixels;

    width <<= 2;
    height >>= 1;
    for (i = 0; i < height; i++, in += width) {
	for (j = 0; j < width; j += 8, out += 4, in += 8) {
	    out[0] = ((int)in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
	    out[1] = ((int)in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
	    out[2] = ((int)in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
	    out[3] = ((int)in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
	}
    }
}


/*
================
QPic32_MipMap_OddOdd

With two odd dimensions we have a polyphase box filter in two
dimensions, taking weighted samples from a 3x3 square in the original
pic.
================
*/
static void
QPic32_MipMap_OddOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x3 square on the original pic.
     * Weights for the centre pixel work out to be constant.
     */
    const float wy1 = (float)height / inheight;
    const float wx1 = (float)width / inwidth;

    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] =
		wx0 * wy0 * r0[0] + wx1 * wy0 * r0[4] + wx2 * wy0 * r0[8] +
		wx0 * wy1 * r1[0] + wx1 * wy1 * r1[4] + wx2 * wy1 * r1[8] +
		wx0 * wy2 * r2[0] + wx1 * wy2 * r2[4] + wx2 * wy2 * r2[8];
	    out[1] =
		wx0 * wy0 * r0[1] + wx1 * wy0 * r0[5] + wx2 * wy0 * r0[9] +
		wx0 * wy1 * r1[1] + wx1 * wy1 * r1[5] + wx2 * wy1 * r1[9] +
		wx0 * wy2 * r2[1] + wx1 * wy2 * r2[5] + wx2 * wy2 * r2[9];
	    out[2] =
		wx0 * wy0 * r0[2] + wx1 * wy0 * r0[6] + wx2 * wy0 * r0[10] +
		wx0 * wy1 * r1[2] + wx1 * wy1 * r1[6] + wx2 * wy1 * r1[10] +
		wx0 * wy2 * r2[2] + wx1 * wy2 * r2[6] + wx2 * wy2 * r2[10];
	    out[3] =
		wx0 * wy0 * r0[3] + wx1 * wy0 * r0[7] + wx2 * wy0 * r0[11] +
		wx0 * wy1 * r1[3] + wx1 * wy1 * r1[7] + wx2 * wy1 * r1[11] +
		wx0 * wy2 * r2[3] + wx1 * wy2 * r2[7] + wx2 * wy2 * r2[11];
	}
    }
}

/*
================
QPic32_MipMap_OddEven

Handle odd width, even height
================
*/
static void
QPic32_MipMap_OddEven(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x2 square on the original pic.
     * Weights for the centre pixels are constant.
     */
    const float wx1 = (float)width / inwidth;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);

	    out[0] = 0.5 * (wx0 * r0[0] + wx1 * r0[4] + wx2 * r0[8] +
			    wx0 * r1[0] + wx1 * r1[4] + wx2 * r1[8]);
	    out[1] = 0.5 * (wx0 * r0[1] + wx1 * r0[5] + wx2 * r0[9] +
			    wx0 * r1[1] + wx1 * r1[5] + wx2 * r1[9]);
	    out[2] = 0.5 * (wx0 * r0[2] + wx1 * r0[6] + wx2 * r0[10] +
			    wx0 * r1[2] + wx1 * r1[6] + wx2 * r1[10]);
	    out[3] = 0.5 * (wx0 * r0[3] + wx1 * r0[7] + wx2 * r0[11] +
			    wx0 * r1[3] + wx1 * r1[7] + wx2 * r1[11]);
	}
    }
}

/*
================
QPic32_MipMap_EvenOdd

Handle even width, odd height
================
*/
static void
QPic32_MipMap_EvenOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 2x3 square on the original pic.
     * Weights for the centre pixels are constant.
     */
    const float wy1 = (float)height / inheight;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x++, in += 8, out += 4) {

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] = 0.5 * (wy0 * ((int)r0[0] + r0[4]) +
			    wy1 * ((int)r1[0] + r1[4]) +
			    wy2 * ((int)r2[0] + r2[4]));
	    out[1] = 0.5 * (wy0 * ((int)r0[1] + r0[5]) +
			    wy1 * ((int)r1[1] + r1[5]) +
			    wy2 * ((int)r2[1] + r2[5]));
	    out[2] = 0.5 * (wy0 * ((int)r0[2] + r0[6]) +
			    wy1 * ((int)r1[2] + r1[6]) +
			    wy2 * ((int)r2[2] + r2[6]));
	    out[3] = 0.5 * (wy0 * ((int)r0[3] + r0[7]) +
			    wy1 * ((int)r1[3] + r1[7]) +
			    wy2 * ((int)r2[3] + r2[7]));
	}
    }
}

/*
================
QPic32_MipMap

Check pic dimensions and call the approriate specialized mipmap function
================
*/
void
QPic32_MipMap(qpic32_t *in)
{
    assert(in->width > 1 || in->height > 1);

    if (in->width == 1) {
	if (in->height & 1)
	    QPic32_MipMap_1D_Odd(in->pixels, in->height);
	else
	    QPic32_MipMap_1D_Even(in->pixels, in->height);

	in->height >>= 1;
	return;
    }

    if (in->height == 1) {
	if (in->width & 1)
	    QPic32_MipMap_1D_Odd(in->pixels, in->width);
	else
	    QPic32_MipMap_1D_Even(in->pixels, in->width);

	in->width >>= 1;
	return;
    }

    if (in->width & 1) {
	if (in->height & 1)
	    QPic32_MipMap_OddOdd(in->pixels, in->width, in->height);
	else
	    QPic32_MipMap_OddEven(in->pixels, in->width, in->height);
    } else if (in->height & 1) {
	QPic32_MipMap_EvenOdd(in->pixels, in->width, in->height);
    } else {
	QPic32_MipMap_EvenEven(in->pixels, in->width, in->height);
    }

    in->width >>= 1;
    in->height >>= 1;

    QPic32_AlphaFix(in);
}
