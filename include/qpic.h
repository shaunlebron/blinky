/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2013 Kevin Shanahan and others

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

#ifndef QPIC_H
#define QPIC_H

#include <stdint.h>

#include "qtypes.h"

typedef struct {
    int width;
    int height;
    int stride;
    const byte *pixels;
} qpic8_t;

typedef union {
    uint32_t rgba;
    struct {
	byte red;
	byte green;
	byte blue;
	byte alpha;
    };
} qpixel32_t;

typedef struct {
    int width;
    int height;
    qpixel32_t pixels[];
} qpic32_t;

/* Allocate hunk space for a texture */
qpic32_t *QPic32_Alloc(int width, int height);

/* Create 32 bit texture from 8 bit source, alpha is palette index to mask */
void QPic_8to32(const qpic8_t *in, qpic32_t *out);
void QPic_8to32_Alpha(const qpic8_t *in, qpic32_t *out, byte alpha);

/* Stretch from in size to out size */
void QPic32_Stretch(const qpic32_t *in, qpic32_t *out);

/* Shrink texture in place to next mipmap level */
void QPic32_MipMap(qpic32_t *pic);

#endif /* QPIC_H */
