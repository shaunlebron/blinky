/*
Copyright (C) 1996-1997 Id Software, Inc.

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
// common.c -- misc functions used in client and server

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef NQ_HACK
#include "quakedef.h"
#include "host.h"
#endif
#ifdef QW_HACK
#ifdef SERVERONLY
#include "qwsvdef.h"
#include "server.h"
#else
#include "quakedef.h"
#endif
#include "protocol.h"
#endif

#include "cmd.h"
#include "common.h"
#include "console.h"
#include "crc.h"
#include "draw.h"
#include "net.h"
#include "shell.h"
#include "sys.h"
#include "zone.h"

#define NUM_SAFE_ARGVS 7

static const char *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static const char *argvdummy = " ";

static const char *safeargvs[NUM_SAFE_ARGVS] = {
  "-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse", "-dibonly"
};

cvar_t registered = { "registered", "0" };
#ifdef NQ_HACK
static cvar_t cmdline = { "cmdline", "0", false, true };
#endif

static qboolean com_modified;		// set true if using non-id files
static int static_registered = 1;	// only for startup check, then set

qboolean msg_suppress_1 = 0;

static void COM_InitFilesystem(void);
static void COM_Path_f(void);
static void *SZ_GetSpace(sizebuf_t *buf, int length);

/* Checksums for the original id pak directory structures */
#define ID1_PAK0_COUNT        339 /* id1/pak0.pak - v1.0x */
#define ID1_PAK0_CRC_V100   13900 /* id1/pak0.pak - v1.00 */
#define ID1_PAK0_CRC_V101   62751 /* id1/pak0.pak - v1.01 */
#define ID1_PAK0_CRC_V106   32981 /* id1/pak0.pak - v1.06 */
#define ID1_PAK0_COUNT_V091   308 /* id1/pak0.pak - v0.91/0.92, not supported */
#define ID1_PAK0_CRC_V091   28804 /* id1/pak0.pak - v0.91/0.92, not supported */
#define QW_PAK0_CRC         52883

#ifdef NQ_HACK
#define CMDLINE_LENGTH	256
static char com_cmdline[CMDLINE_LENGTH];
#endif

qboolean standard_quake = true, rogue, hipnotic;

#ifdef QW_HACK
char gamedirfile[MAX_OSPATH];
#endif

/* FIXME - remove this and checks like it; no need to be registered... */
// this graphic needs to be in the pak file to use registered features
static unsigned short pop[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x6600, 0x0000, 0x0000, 0x0000, 0x6600, 0x0000,
    0x0000, 0x0066, 0x0000, 0x0000, 0x0000, 0x0000, 0x0067, 0x0000,
    0x0000, 0x6665, 0x0000, 0x0000, 0x0000, 0x0000, 0x0065, 0x6600,
    0x0063, 0x6561, 0x0000, 0x0000, 0x0000, 0x0000, 0x0061, 0x6563,
    0x0064, 0x6561, 0x0000, 0x0000, 0x0000, 0x0000, 0x0061, 0x6564,
    0x0064, 0x6564, 0x0000, 0x6469, 0x6969, 0x6400, 0x0064, 0x6564,
    0x0063, 0x6568, 0x6200, 0x0064, 0x6864, 0x0000, 0x6268, 0x6563,
    0x0000, 0x6567, 0x6963, 0x0064, 0x6764, 0x0063, 0x6967, 0x6500,
    0x0000, 0x6266, 0x6769, 0x6a68, 0x6768, 0x6a69, 0x6766, 0x6200,
    0x0000, 0x0062, 0x6566, 0x6666, 0x6666, 0x6666, 0x6562, 0x0000,
    0x0000, 0x0000, 0x0062, 0x6364, 0x6664, 0x6362, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0062, 0x6662, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0061, 0x6661, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x6500, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x6400, 0x0000, 0x0000, 0x0000
};

/*

All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and
all game directories.  The sys_* files pass this to host_init in
quakeparms_t->basedir.  This can be overridden with the "-basedir" command
line parm to allow code debugging in a different directory.  The base
directory is only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that
all generated files (savegames, screenshots, demos, config files) will be
saved to.  This can be overridden with the "-game" command line parameter.
The game directory can never be changed while quake is executing.  This is a
precacution against having a malicious server instruct clients to write files
over areas they shouldn't.

*/

//============================================================================


// ClearLink is used for new headnodes
void
ClearLink(link_t *l)
{
    l->prev = l->next = l;
}

void
RemoveLink(link_t *l)
{
    l->next->prev = l->prev;
    l->prev->next = l->next;
}

void
InsertLinkBefore(link_t *l, link_t *before)
{
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}

/* Unused */
#if 0
void
InsertLinkAfter(link_t *l, link_t *after)
{
    l->next = after->next;
    l->prev = after;
    l->prev->next = l;
    l->next->prev = l;
}
#endif

/*
============================================================================

			LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

/*
 * Use this function to share static string buffers
 * between different text processing functions.
 * Try to avoid fixed-size intermediate buffers like this if possible
 */
#define COM_STRBUF_LEN 2048
static char *
COM_GetStrBuf(void)
{
    static char buffers[4][COM_STRBUF_LEN];
    static int index;
    return buffers[3 & ++index];
}



int
Q_atoi(const char *str)
{
    int val;
    int sign;
    int c;

    if (*str == '-') {
	sign = -1;
	str++;
    } else
	sign = 1;

    val = 0;

//
// check for hex
//
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
	str += 2;
	while (1) {
	    c = *str++;
	    if (c >= '0' && c <= '9')
		val = (val << 4) + c - '0';
	    else if (c >= 'a' && c <= 'f')
		val = (val << 4) + c - 'a' + 10;
	    else if (c >= 'A' && c <= 'F')
		val = (val << 4) + c - 'A' + 10;
	    else
		return val * sign;
	}
    }
//
// check for character
//
    if (str[0] == '\'') {
	return sign * str[1];
    }
//
// assume decimal
//
    while (1) {
	c = *str++;
	if (c < '0' || c > '9')
	    return val * sign;
	val = val * 10 + c - '0';
    }

    return 0;
}


float
Q_atof(const char *str)
{
    double val;
    int sign;
    int c;
    int decimal, total;

    if (*str == '-') {
	sign = -1;
	str++;
    } else
	sign = 1;

    val = 0;

//
// check for hex
//
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
	str += 2;
	while (1) {
	    c = *str++;
	    if (c >= '0' && c <= '9')
		val = (val * 16) + c - '0';
	    else if (c >= 'a' && c <= 'f')
		val = (val * 16) + c - 'a' + 10;
	    else if (c >= 'A' && c <= 'F')
		val = (val * 16) + c - 'A' + 10;
	    else
		return val * sign;
	}
    }
//
// check for character
//
    if (str[0] == '\'') {
	return sign * str[1];
    }
//
// assume decimal
//
    decimal = -1;
    total = 0;
    while (1) {
	c = *str++;
	if (c == '.') {
	    decimal = total;
	    continue;
	}
	if (c < '0' || c > '9')
	    break;
	val = val * 10 + c - '0';
	total++;
    }

    if (decimal == -1)
	return val * sign;
    while (total > decimal) {
	val /= 10;
	total--;
    }

    return val * sign;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

qboolean bigendien;

short (*BigShort) (short l);
short (*LittleShort) (short l);
int (*BigLong) (int l);
int (*LittleLong) (int l);
float (*BigFloat) (float l);
float (*LittleFloat) (float l);

short
ShortSwap(short l)
{
    byte b1, b2;

    b1 = l & 255;
    b2 = (l >> 8) & 255;

    return (b1 << 8) + b2;
}

short
ShortNoSwap(short l)
{
    return l;
}

int
LongSwap(int l)
{
    byte b1, b2, b3, b4;

    b1 = l & 255;
    b2 = (l >> 8) & 255;
    b3 = (l >> 16) & 255;
    b4 = (l >> 24) & 255;

    return ((int)b1 << 24) + ((int)b2 << 16) + ((int)b3 << 8) + b4;
}

int
LongNoSwap(int l)
{
    return l;
}

float
FloatSwap(float f)
{
    union {
	float f;
	byte b[4];
    } dat1, dat2;

    dat1.f = f;
    dat2.b[0] = dat1.b[3];
    dat2.b[1] = dat1.b[2];
    dat2.b[2] = dat1.b[1];
    dat2.b[3] = dat1.b[0];
    return dat2.f;
}

float
FloatNoSwap(float f)
{
    return f;
}

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

#ifdef QW_HACK
usercmd_t nullcmd;		// guarenteed to be zero
#endif

//
// writing functions
//

void
MSG_WriteChar(sizebuf_t *sb, int c)
{
    byte *buf;

#ifdef PARANOID
    if (c < -128 || c > 127)
	Sys_Error("%s: range error", __func__);
#endif

    buf = SZ_GetSpace(sb, 1);
    buf[0] = c;
}

void
MSG_WriteByte(sizebuf_t *sb, int c)
{
    byte *buf;

#ifdef PARANOID
    if (c < 0 || c > 255)
	Sys_Error("%s: range error", __func__);
#endif

    buf = SZ_GetSpace(sb, 1);
    buf[0] = c;
}

void
MSG_WriteShort(sizebuf_t *sb, int c)
{
    byte *buf;

#ifdef PARANOID
    if (c < ((short)0x8000) || c > (short)0x7fff)
	Sys_Error("%s: range error", __func__);
#endif

    buf = SZ_GetSpace(sb, 2);
    buf[0] = c & 0xff;
    buf[1] = c >> 8;
}

void
MSG_WriteLong(sizebuf_t *sb, int c)
{
    byte *buf;

    buf = SZ_GetSpace(sb, 4);
    buf[0] = c & 0xff;
    buf[1] = (c >> 8) & 0xff;
    buf[2] = (c >> 16) & 0xff;
    buf[3] = c >> 24;
}

void
MSG_WriteFloat(sizebuf_t *sb, float f)
{
    union {
	float f;
	int l;
    } dat;

    dat.f = f;
    dat.l = LittleLong(dat.l);

    SZ_Write(sb, &dat.l, 4);
}

void
MSG_WriteString(sizebuf_t *sb, const char *s)
{
    if (!s)
	SZ_Write(sb, "", 1);
    else
	SZ_Write(sb, s, strlen(s) + 1);
}

void
MSG_WriteStringvf(sizebuf_t *sb, const char *fmt, va_list ap)
{
    int maxlen, len;

    /*
     * FIXME - Kind of ugly to check space first then do getspace
     * afterwards, but we don't know how much we'll need before
     * hand. Update the SZ interface?
     */
    maxlen = sb->maxsize - sb->cursize;
    len = vsnprintf((char *)sb->data + sb->cursize, maxlen, fmt, ap);

    /* Use SZ_GetSpace to check for overflow */
    SZ_GetSpace(sb, len + 1);
}

void
MSG_WriteStringf(sizebuf_t *sb, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    MSG_WriteStringvf(sb, fmt, ap);
    va_end(ap);
}

void
MSG_WriteCoord(sizebuf_t *sb, float f)
{
    /*
     * Co-ords are send as shorts, with the low 3 bits being the fractional
     * component
     */
    MSG_WriteShort(sb, (int)(f * (1 << 3)));
}

void
MSG_WriteAngle(sizebuf_t *sb, float f)
{
    MSG_WriteByte(sb, (int)floorf((f * 256 / 360) + 0.5f) & 255);
}

void
MSG_WriteAngle16(sizebuf_t *sb, float f)
{
    MSG_WriteShort(sb, (int)floorf((f * 65536 / 360) + 0.5f) & 65535);
}

#ifdef QW_HACK
void
MSG_WriteDeltaUsercmd(sizebuf_t *buf, const usercmd_t *from,
		      const usercmd_t *cmd)
{
    int bits;

//
// send the movement message
//
    bits = 0;
    if (cmd->angles[0] != from->angles[0])
	bits |= CM_ANGLE1;
    if (cmd->angles[1] != from->angles[1])
	bits |= CM_ANGLE2;
    if (cmd->angles[2] != from->angles[2])
	bits |= CM_ANGLE3;
    if (cmd->forwardmove != from->forwardmove)
	bits |= CM_FORWARD;
    if (cmd->sidemove != from->sidemove)
	bits |= CM_SIDE;
    if (cmd->upmove != from->upmove)
	bits |= CM_UP;
    if (cmd->buttons != from->buttons)
	bits |= CM_BUTTONS;
    if (cmd->impulse != from->impulse)
	bits |= CM_IMPULSE;

    MSG_WriteByte(buf, bits);

    if (bits & CM_ANGLE1)
	MSG_WriteAngle16(buf, cmd->angles[0]);
    if (bits & CM_ANGLE2)
	MSG_WriteAngle16(buf, cmd->angles[1]);
    if (bits & CM_ANGLE3)
	MSG_WriteAngle16(buf, cmd->angles[2]);

    if (bits & CM_FORWARD)
	MSG_WriteShort(buf, cmd->forwardmove);
    if (bits & CM_SIDE)
	MSG_WriteShort(buf, cmd->sidemove);
    if (bits & CM_UP)
	MSG_WriteShort(buf, cmd->upmove);

    if (bits & CM_BUTTONS)
	MSG_WriteByte(buf, cmd->buttons);
    if (bits & CM_IMPULSE)
	MSG_WriteByte(buf, cmd->impulse);
    MSG_WriteByte(buf, cmd->msec);
}
#endif /* QW_HACK */

#ifdef NQ_HACK
/*
 * Write the current message length to the start of the buffer (in big
 * endian format) with the control flag set.
 */
void
MSG_WriteControlHeader(sizebuf_t *sb)
{
    int c = NETFLAG_CTL | (sb->cursize & NETFLAG_LENGTH_MASK);

    sb->data[0] = c >> 24;
    sb->data[1] = (c >> 16) & 0xff;
    sb->data[2] = (c >> 8) & 0xff;
    sb->data[3] = c & 0xff;
}
#endif

//
// reading functions
//
int msg_readcount;
qboolean msg_badread;

void
MSG_BeginReading(void)
{
    msg_readcount = 0;
    msg_badread = false;
}

#ifdef QW_HACK
int
MSG_GetReadCount(void)
{
    return msg_readcount;
}
#endif

// returns -1 and sets msg_badread if no more characters are available
int
MSG_ReadChar(void)
{
    int c;

    if (msg_readcount + 1 > net_message.cursize) {
	msg_badread = true;
	return -1;
    }

    c = (signed char)net_message.data[msg_readcount];
    msg_readcount++;

    return c;
}

int
MSG_ReadByte(void)
{
    int c;

    if (msg_readcount + 1 > net_message.cursize) {
	msg_badread = true;
	return -1;
    }

    c = (unsigned char)net_message.data[msg_readcount];
    msg_readcount++;

    return c;
}

int
MSG_ReadShort(void)
{
    int c;

    if (msg_readcount + 2 > net_message.cursize) {
	msg_badread = true;
	return -1;
    }

    c = (short)(net_message.data[msg_readcount]
		+ (net_message.data[msg_readcount + 1] << 8));

    msg_readcount += 2;

    return c;
}

int
MSG_ReadLong(void)
{
    int c;

    if (msg_readcount + 4 > net_message.cursize) {
	msg_badread = true;
	return -1;
    }

    c = net_message.data[msg_readcount]
	+ (net_message.data[msg_readcount + 1] << 8)
	+ (net_message.data[msg_readcount + 2] << 16)
	+ (net_message.data[msg_readcount + 3] << 24);

    msg_readcount += 4;

    return c;
}

float
MSG_ReadFloat(void)
{
    union {
	byte b[4];
	float f;
	int l;
    } dat;

    dat.b[0] = net_message.data[msg_readcount];
    dat.b[1] = net_message.data[msg_readcount + 1];
    dat.b[2] = net_message.data[msg_readcount + 2];
    dat.b[3] = net_message.data[msg_readcount + 3];
    msg_readcount += 4;

    dat.l = LittleLong(dat.l);

    return dat.f;
}

char *
MSG_ReadString(void)
{
    char *buf;
    int len, c;

    buf = COM_GetStrBuf();
    len = 0;
    do {
	c = MSG_ReadChar();
	if (c == -1 || c == 0)
	    break;
	buf[len++] = c;
    } while (len < COM_STRBUF_LEN - 1);

    buf[len] = 0;

    return buf;
}

#ifdef QW_HACK
char *
MSG_ReadStringLine(void)
{
    char *buf;
    int len, c;

    buf = COM_GetStrBuf();
    len = 0;
    do {
	c = MSG_ReadChar();
	if (c == -1 || c == 0 || c == '\n')
	    break;
	buf[len++] = c;
    } while (len < COM_STRBUF_LEN - 1);

    buf[len] = 0;

    return buf;
}
#endif

float
MSG_ReadCoord(void)
{
    /*
     * Co-ords are send as shorts, with the low 3 bits being the fractional
     * component
     */
    return MSG_ReadShort() * (1.0 / (1 << 3));
}

float
MSG_ReadAngle(void)
{
    return MSG_ReadChar() * (360.0 / 256);
}

float
MSG_ReadAngle16(void)
{
    return MSG_ReadShort() * (360.0 / 65536);
}

#ifdef QW_HACK
void
MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *move)
{
    int bits;

    memcpy(move, from, sizeof(*move));

    bits = MSG_ReadByte();

// read current angles
    if (bits & CM_ANGLE1)
	move->angles[0] = MSG_ReadAngle16();
    if (bits & CM_ANGLE2)
	move->angles[1] = MSG_ReadAngle16();
    if (bits & CM_ANGLE3)
	move->angles[2] = MSG_ReadAngle16();

// read movement
    if (bits & CM_FORWARD)
	move->forwardmove = MSG_ReadShort();
    if (bits & CM_SIDE)
	move->sidemove = MSG_ReadShort();
    if (bits & CM_UP)
	move->upmove = MSG_ReadShort();

// read buttons
    if (bits & CM_BUTTONS)
	move->buttons = MSG_ReadByte();

    if (bits & CM_IMPULSE)
	move->impulse = MSG_ReadByte();

// read time to run command
    move->msec = MSG_ReadByte();
}
#endif /* QW_HACK */

#ifdef NQ_HACK
/*
 * Read back the message control header
 * Essentially this is MSG_ReadLong, but big-endian byte order.
 */
int
MSG_ReadControlHeader(void)
{
    int c;

    if (msg_readcount + 4 > net_message.cursize) {
	msg_badread = true;
	return -1;
    }

    c = (net_message.data[msg_readcount] << 24)
	+ (net_message.data[msg_readcount + 1] << 16)
	+ (net_message.data[msg_readcount + 2] << 8)
	+ net_message.data[msg_readcount + 3];

    msg_readcount += 4;

    return c;
}
#endif

//===========================================================================

#ifdef NQ_HACK
void
SZ_Alloc(sizebuf_t *buf, int startsize)
{
    if (startsize < 256)
	startsize = 256;
    buf->data = Hunk_AllocName(startsize, "sizebuf");
    buf->maxsize = startsize;
    buf->cursize = 0;
}

void
SZ_Free(sizebuf_t *buf)
{
//      Z_Free (buf->data);
//      buf->data = NULL;
//      buf->maxsize = 0;
    buf->cursize = 0;
}
#endif

void
SZ_Clear(sizebuf_t *buf)
{
    buf->cursize = 0;
    buf->overflowed = false;
}

static void *
SZ_GetSpace(sizebuf_t *buf, int length)
{
    void *data;

    if (buf->cursize + length > buf->maxsize) {
	if (!buf->allowoverflow)
	    Sys_Error("%s: overflow without allowoverflow set (%d > %d)",
		      __func__, buf->cursize + length, buf->maxsize);
	if (length > buf->maxsize)
	    Sys_Error("%s: %d is > full buffer size", __func__, length);
	if (developer.value)
	    /* Con_Printf may be redirected */
	    Sys_Printf("%s: overflow\n", __func__);
	SZ_Clear(buf);
	buf->overflowed = true;
    }
    data = buf->data + buf->cursize;
    buf->cursize += length;

    return data;
}

void
SZ_Write(sizebuf_t *buf, const void *data, int length)
{
    memcpy(SZ_GetSpace(buf, length), data, length);
}

void
SZ_Print(sizebuf_t *buf, const char *data)
{
    size_t len = strlen(data);

    /* If buf->data has a trailing zero, overwrite it */
    if (!buf->cursize || buf->data[buf->cursize - 1])
	memcpy(SZ_GetSpace(buf, len + 1), data, len + 1);
    else
	memcpy(SZ_GetSpace(buf, len) - 1, data, len + 1);
}


//============================================================================


/*
============
COM_SkipPath
============
*/
const char *
COM_SkipPath(const char *pathname)
{
    const char *last;

    last = pathname;
    while (*pathname) {
	if (*pathname == '/')
	    last = pathname + 1;
	pathname++;
    }
    return last;
}

/*
============
COM_StripExtension
============
*/
void
COM_StripExtension(const char *filename, char *out, size_t buflen)
{
    const char *start, *pos;
    size_t copylen;

    start = COM_SkipPath(filename);
    pos = strrchr(start, '.');
    if (out == filename) {
	if (pos && *pos)
	    out[pos - filename] = 0;
	return;
    }

    copylen = qmin((size_t)(pos - filename), buflen - 1);
    memcpy(out, filename, copylen);
    out[copylen] = 0;
}

/*
============
COM_FileExtension
============
*/
#ifdef NQ_HACK
static const char *
COM_FileExtension(const char *in)
{
    static char exten[8];
    const char *dot;
    int i;

    in = COM_SkipPath(in);
    dot = strrchr(in, '.');
    if (!dot)
	return "";

    dot++;
    for (i = 0; i < sizeof(exten) - 1 && *dot; i++, dot++)
	exten[i] = *dot;
    exten[i] = 0;

    return exten;
}
#endif

/*
============
COM_FileBase
============
*/
void
COM_FileBase(const char *in, char *out, size_t buflen)
{
    const char *dot;
    int copylen;

    in = COM_SkipPath(in);
    dot = strrchr(in, '.');
    copylen = dot ? dot - in : strlen(in);

    if (copylen < 2) {
	in = "?model?";
	copylen = strlen(in);
    }
    snprintf(out, buflen, "%.*s", copylen, in);
}


/*
==================
COM_DefaultExtension
Returns non-zero if the extension wouldn't fit in the output buffer
==================
*/
int
COM_DefaultExtension(const char *path, const char *extension, char *out,
		     size_t buflen)
{
    COM_StripExtension(path, out, buflen);
    if (strlen(out) + strlen(extension) + 1 > buflen)
	return -1;

    /* Copy extension, including terminating null */
    memcpy(out + strlen(out), extension, strlen(extension) + 1);

    return 0;
}

int
COM_CheckExtension(const char *path, const char *extn)
{
    char *pos;
    int ret = 0;

    pos = strrchr(path, '.');
    if (pos) {
	if (extn[0] != '.')
	    pos++;
	ret = pos && !strcasecmp(pos, extn);
    }

    return ret;
}

//============================================================================

static char com_tokenbuf[1024];
const char *com_token = com_tokenbuf;
unsigned com_argc;
const char **com_argv;

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
static const char single_chars[] = "{})(':";

static const char *
COM_Parse_(const char *data, qboolean split_single_chars)
{
    int c;
    int len;

    len = 0;
    com_tokenbuf[0] = 0;

    if (!data)
	return NULL;

// skip whitespace
  skipwhite:
    while ((c = *data) <= ' ') {
	if (c == 0)
	    return NULL;	// end of file;
	data++;
    }

// skip // comments
    if (c == '/' && data[1] == '/') {
	while (*data && *data != '\n')
	    data++;
	goto skipwhite;
    }
// skip /*..*/ comments
    if (c == '/' && data[1] == '*') {
	data += 2;
	while (*data && !(*data == '*' && data[1] == '/'))
	    data++;
	if (*data)
	    data += 2;
	goto skipwhite;
    }
// handle quoted strings specially
    if (c == '\"') {
	data++;
	while (1) {
	    c = *data;
	    if (c)
		data++;
	    if (c == '\"' || !c) {
		com_tokenbuf[len] = 0;
		return data;
	    }
	    if (len < sizeof(com_tokenbuf) - 1)
		com_tokenbuf[len++] = c;
	}
    }
// parse single characters
    if (split_single_chars && strchr(single_chars, c)) {
	if (len < sizeof(com_tokenbuf) - 1)
	    com_tokenbuf[len++] = c;
	com_tokenbuf[len] = 0;
	return data + 1;
    }
// parse a regular word
    do {
	if (len < sizeof(com_tokenbuf) - 1)
	    com_tokenbuf[len++] = c;
	data++;
	c = *data;
	if (split_single_chars && strchr(single_chars, c))
	    break;
    } while (c > 32);

    com_tokenbuf[len] = 0;

    return data;
}

const char *
COM_Parse(const char *data)
{
#ifdef NQ_HACK
    return COM_Parse_(data, true);
#endif
#ifdef QW_HACK
    return COM_Parse_(data, false);
#endif
}

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
unsigned
COM_CheckParm(const char *parm)
{
    unsigned i;

    for (i = 1; i < com_argc; i++) {
	if (!com_argv[i])
	    continue;		// NEXTSTEP sometimes clears appkit vars.
	if (!strcmp(parm, com_argv[i]))
	    return i;
    }

    return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void
COM_CheckRegistered(void)
{
    FILE *f;
    unsigned short check[128];
    int i;

    COM_FOpenFile("gfx/pop.lmp", &f);
    static_registered = 0;

    if (!f) {
	Con_Printf("Playing shareware version.\n");
#ifndef SERVERONLY
	if (com_modified)
#ifdef NQ_HACK
	    Sys_Error("You must have the registered version "
		      "to use modified games");
#endif
#ifdef QW_HACK
	    Sys_Error("You must have the registered version "
		      "to play QuakeWorld");
#endif
#endif /* SERVERONLY */
	return;
    }

    fread(check, 1, sizeof(check), f);
    fclose(f);

    for (i = 0; i < 128; i++)
	if (pop[i] != (unsigned short)BigShort(check[i]))
	    Sys_Error("Corrupted data file.");

#ifdef NQ_HACK
    Cvar_Set("cmdline", com_cmdline);
#endif
    Cvar_Set("registered", "1");
    static_registered = 1;
    Con_Printf("Playing registered version.\n");
}


/*
================
COM_InitArgv
================
*/
void
COM_InitArgv(int argc, const char **argv)
{
    qboolean safe;
    int i;
#ifdef NQ_HACK
    int j, n;

// reconstitute the command line for the cmdline externally visible cvar
    n = 0;
    for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++) {
	i = 0;
	while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i]) {
	    com_cmdline[n++] = argv[j][i++];
	}

	if (n < (CMDLINE_LENGTH - 1))
	    com_cmdline[n++] = ' ';
	else
	    break;
    }
    com_cmdline[n] = 0;
#endif

    safe = false;

    for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc);
	 com_argc++) {
	largv[com_argc] = argv[com_argc];
	if (!strcmp("-safe", argv[com_argc]))
	    safe = true;
    }

    if (safe) {
	// force all the safe-mode switches. Note that we reserved extra space in
	// case we need to add these, so we don't need an overflow check
	for (i = 0; i < NUM_SAFE_ARGVS; i++) {
	    largv[com_argc] = safeargvs[i];
	    com_argc++;
	}
    }

    largv[com_argc] = argvdummy;
    com_argv = largv;

#ifdef NQ_HACK
    if (COM_CheckParm("-rogue")) {
	rogue = true;
	standard_quake = false;
    }

    if (COM_CheckParm("-hipnotic") || COM_CheckParm("-quoth")) {
	hipnotic = true;
	standard_quake = false;
    }
#endif
}

/*
================
COM_AddParm

Adds the given string at the end of the current argument list
================
*/
#ifdef QW_HACK
void
COM_AddParm(const char *parm)
{
    largv[com_argc++] = parm;
}
#endif

/*
================
COM_Init
================
*/
void
COM_Init(void)
{
    union {
	byte b[2];
	short s;
    } swaptest = {
	.b = { 1, 0 }
    };

// set the byte swapping variables in a portable manner
    if (swaptest.s == 1) {
	bigendien = false;
	BigShort = ShortSwap;
	LittleShort = ShortNoSwap;
	BigLong = LongSwap;
	LittleLong = LongNoSwap;
	BigFloat = FloatSwap;
	LittleFloat = FloatNoSwap;
    } else {
	bigendien = true;
	BigShort = ShortNoSwap;
	LittleShort = ShortSwap;
	BigLong = LongNoSwap;
	LittleLong = LongSwap;
	BigFloat = FloatNoSwap;
	LittleFloat = FloatSwap;
    }

    Cvar_RegisterVariable(&registered);
#ifdef NQ_HACK
    Cvar_RegisterVariable(&cmdline);
#endif
    Cmd_AddCommand("path", COM_Path_f);

    COM_InitFilesystem();
    COM_CheckRegistered();
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
============
*/
char *
va(const char *format, ...)
{
    va_list argptr;
    char *buf;
    int len;

    buf = COM_GetStrBuf();
    va_start(argptr, format);
    len = vsnprintf(buf, COM_STRBUF_LEN, format, argptr);
    va_end(argptr);

    if (len > COM_STRBUF_LEN - 1)
	Con_DPrintf("%s: overflow (string truncated)\n", __func__);

    return buf;
}


/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int com_filesize;


//
// in memory
//

typedef struct {
    char name[MAX_QPATH];
    int filepos, filelen;
} packfile_t;

typedef struct pack_s {
    char filename[MAX_OSPATH];
    int numfiles;
    packfile_t *files;
} pack_t;

//
// on disk
//
#define MAX_PACKPATH 56
typedef struct {
    char name[MAX_PACKPATH];
    int filepos, filelen;
} dpackfile_t;

typedef struct {
    char id[4];
    int dirofs;
    int dirlen;
} dpackheader_t;

char com_gamedir[MAX_OSPATH];
char com_basedir[MAX_OSPATH];

typedef struct searchpath_s {
    char filename[MAX_OSPATH];
    pack_t *pack;		// only one of filename / pack will be used
    struct searchpath_s *next;
} searchpath_t;

static searchpath_t *com_searchpaths;
#ifdef QW_HACK
static searchpath_t *com_base_searchpaths;	// without gamedirs
#endif

/*
================
COM_filelength
================
*/
static int
COM_filelength(FILE *f)
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

static int
COM_FileOpenRead(const char *path, FILE **hndl)
{
    FILE *f;

    f = fopen(path, "rb");
    if (!f) {
	*hndl = NULL;
	return -1;
    }
    *hndl = f;

    return COM_filelength(f);
}

/*
============
COM_Path_f

============
*/
static void
COM_Path_f(void)
{
    searchpath_t *s;

    Con_Printf("Current search path:\n");
    for (s = com_searchpaths; s; s = s->next) {
#ifdef QW_HACK
	if (s == com_base_searchpaths)
	    Con_Printf("----------\n");
#endif
	if (s->pack)
	    Con_Printf("%s (%i files)\n", s->pack->filename,
		       s->pack->numfiles);
	else
	    Con_Printf("%s\n", s->filename);
    }
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void
COM_WriteFile(const char *filename, const void *data, int len)
{
    FILE *f;
    char name[MAX_OSPATH];

    snprintf(name, sizeof(name), "%s/%s", com_gamedir, filename);

    f = fopen(name, "wb");
    if (!f) {
	Sys_mkdir(com_gamedir);
	f = fopen(name, "wb");
	if (!f)
	    Sys_Error("Error opening %s", filename);
    }
    fwrite(data, 1, len, f);
    fclose(f);
}


/*
============
COM_CreatePath
============
*/
void
COM_CreatePath(const char *path)
{
    char part[MAX_OSPATH];
    char *ofs;

    if (!path || !path[0])
	return;

    strncpy(part, path, sizeof(part));
    part[MAX_OSPATH - 1] = 0;

    for (ofs = part + 1; *ofs; ofs++) {
	if (*ofs == '/') {	// create the directory
	    *ofs = 0;
	    Sys_mkdir(part);
	    *ofs = '/';
	}
    }
}

/*
===========
COM_FOpenFile

Finds the file in the search path.
Sets com_filesize
If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
int file_from_pak;	// global indicating file came from pack file

int
COM_FOpenFile(const char *filename, FILE **file)
{
    searchpath_t *search;
    char path[MAX_OSPATH];
    pack_t *pak;
    int i;
    int findtime;

    file_from_pak = 0;

//
// search through the path, one element at a time
//
    for (search = com_searchpaths; search; search = search->next) {
	// is the element a pak file?
	if (search->pack) {
	    // look through all the pak file elements
	    pak = search->pack;
	    for (i = 0; i < pak->numfiles; i++)
		if (!strcmp(pak->files[i].name, filename)) {	// found it!
		    // open a new file on the pakfile
		    *file = fopen(pak->filename, "rb");
		    if (!*file)
			Sys_Error("Couldn't reopen %s", pak->filename);
		    fseek(*file, pak->files[i].filepos, SEEK_SET);
		    com_filesize = pak->files[i].filelen;
		    file_from_pak = 1;
		    return com_filesize;
		}
	} else {
	    // check a file in the directory tree
	    if (!static_registered) {
		// if not a registered version, don't ever go beyond base
		if (strchr(filename, '/') || strchr(filename, '\\'))
		    continue;
	    }
	    snprintf(path, sizeof(path), "%s/%s", search->filename, filename);
	    findtime = Sys_FileTime(path);
	    if (findtime == -1)
		continue;

	    *file = fopen(path, "rb");
	    com_filesize = COM_filelength(*file);
	    return com_filesize;
	}
    }

    Sys_Printf("FindFile: can't find %s\n", filename);
    *file = NULL;
    com_filesize = -1;

    return -1;
}

static void
COM_ScanDirDir(struct stree_root *root, DIR *dir, const char *pfx,
	       const char *ext, qboolean stripext)
{
    int pfx_len, ext_len;
    struct dirent *d;
    char *fname;

    pfx_len = pfx ? strlen(pfx) : 0;
    ext_len = ext ? strlen(ext) : 0;

    while ((d = readdir(dir))) {
	if ((!pfx || !strncasecmp(d->d_name, pfx, pfx_len)) &&
	    (!ext || COM_CheckExtension(d->d_name, ext))) {
	    int len = strlen(d->d_name);
	    if (ext && stripext)
		len -= ext_len;
	    fname = Z_Malloc(len + 1);
	    if (fname) {
		strncpy(fname, d->d_name, len);
		fname[len] = '\0';
		STree_InsertAlloc(root, fname, true);
		Z_Free(fname);
	    }
	}
    }
}

static void
COM_ScanDirPak(struct stree_root *root, pack_t *pak, const char *path,
	       const char *pfx, const char *ext, qboolean stripext)
{
    int i, path_len, pfx_len, ext_len, len;
    char *pak_f, *fname;

    path_len = path ? strlen(path) : 0;
    pfx_len = pfx ? strlen(pfx) : 0;
    ext_len = ext ? strlen(ext) : 0;

    for (i = 0; i < pak->numfiles; i++) {
	/* Check the path prefix */
	pak_f = pak->files[i].name;
	if (path && path_len) {
	    if (strncasecmp(pak_f, path, path_len))
		continue;
	    if (pak_f[path_len] != '/')
		continue;
	    pak_f += path_len + 1;
	}

	/* Don't match sub-directories */
	if (strchr(pak_f, '/'))
	    continue;

	/* Check the prefix and extension, if set */
	if (pfx && pfx_len && strncasecmp(pak_f, pfx, pfx_len))
	    continue;
	if (ext && ext_len && !COM_CheckExtension(pak_f, ext))
	    continue;

	/* Ok, we have a match. Add it */
	len = strlen(pak_f);
	if (ext && stripext)
	    len -= ext_len;
	fname = Z_Malloc(len + 1);
	if (fname) {
	    strncpy(fname, pak_f, len);
	    fname[len] = '\0';
	    STree_InsertAlloc(root, fname, true);
	    Z_Free(fname);
	}
    }
}

/*
============
COM_ScanDir

Scan the contents of a the given directory. Any filenames that match
both the given prefix and extension are added to the string tree.
Caller MUST have already called STree_AllocInit()
============
*/
void
COM_ScanDir(struct stree_root *root, const char *path, const char *pfx,
	    const char *ext, qboolean stripext)
{
    searchpath_t *search;
    char fullpath[MAX_OSPATH];
    DIR *dir;

    for (search = com_searchpaths; search; search = search->next) {
	if (search->pack) {
	    COM_ScanDirPak(root, search->pack, path, pfx, ext, stripext);
	} else {
	    snprintf(fullpath, MAX_OSPATH, "%s/%s", search->filename, path);
	    fullpath[MAX_OSPATH - 1] = '\0';
	    dir = opendir(fullpath);
	    if (dir) {
		COM_ScanDirDir(root, dir, pfx, ext, stripext);
		closedir(dir);
	    }
	}
    }
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Optionally return length; set null if you don't want it.
Always appends a 0 byte to the loaded data.
============
*/
static cache_user_t *loadcache;
static byte *loadbuf;
static int loadsize;

static void *
COM_LoadFile(const char *path, int usehunk, size_t *size)
{
    FILE *f;
    byte *buf;
    char base[32];
    int len;

    buf = NULL;			// quiet compiler warning

// look for it in the filesystem or pack files
    len = com_filesize = COM_FOpenFile(path, &f);
    if (!f)
	return NULL;

    if (size)
	*size = len;

// extract the filename base name for hunk tag
    COM_FileBase(path, base, sizeof(base));

    if (usehunk == 1)
	buf = Hunk_AllocName(len + 1, base);
    else if (usehunk == 2)
	buf = Hunk_TempAlloc(len + 1);
    else if (usehunk == 0)
	buf = Z_Malloc(len + 1);
    else if (usehunk == 3)
	buf = Cache_Alloc(loadcache, len + 1, base);
    else if (usehunk == 4) {
	if (len + 1 > loadsize)
	    buf = Hunk_TempAlloc(len + 1);
	else
	    buf = loadbuf;
    } else
	Sys_Error("%s: bad usehunk", __func__);

    if (!buf)
	Sys_Error("%s: not enough space for %s", __func__, path);

    buf[len] = 0;

#ifndef SERVERONLY
    Draw_BeginDisc();
#endif
    fread(buf, 1, len, f);
    fclose(f);
#ifndef SERVERONLY
    Draw_EndDisc();
#endif

    return buf;
}

void *
COM_LoadHunkFile(const char *path)
{
    return COM_LoadFile(path, 1, NULL);
}

void *
COM_LoadTempFile(const char *path)
{
    return COM_LoadFile(path, 2, NULL);
}

void
COM_LoadCacheFile(const char *path, struct cache_user_s *cu)
{
    loadcache = cu;
    COM_LoadFile(path, 3, NULL);
}

// uses temp hunk if larger than bufsize
// size is size of loaded file in bytes
void *
COM_LoadStackFile(const char *path, void *buffer, int bufsize, size_t *size)
{
    byte *buf;

    loadbuf = (byte *)buffer;
    loadsize = bufsize;
    buf = COM_LoadFile(path, 4, size);

    return buf;
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *
COM_LoadPackFile(const char *packfile)
{
    FILE *packhandle;
    dpackheader_t header;
    dpackfile_t *dfiles;
    packfile_t *mfiles;
    pack_t *pack;
    int i, numfiles;
    unsigned short crc;

    if (COM_FileOpenRead(packfile, &packhandle) == -1)
	return NULL;

    fread(&header, 1, sizeof(header), packhandle);
    if (header.id[0] != 'P' || header.id[1] != 'A'
	|| header.id[2] != 'C' || header.id[3] != 'K')
	Sys_Error("%s is not a packfile", packfile);
    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);

    numfiles = header.dirlen / sizeof(dpackfile_t);

    if (numfiles != ID1_PAK0_COUNT)
	com_modified = true;	// not the original file

#ifdef NQ_HACK
    mfiles = Hunk_AllocName(numfiles * sizeof(*mfiles), "packfile");
    int mark = Hunk_LowMark();
    dfiles = Hunk_AllocName(numfiles * sizeof(*dfiles), "packfile");
#endif
#ifdef QW_HACK
    mfiles = Z_Malloc(numfiles * sizeof(*mfiles));
    dfiles = Z_Malloc(numfiles * sizeof(*dfiles));
#endif

    fseek(packhandle, header.dirofs, SEEK_SET);
    fread(dfiles, 1, header.dirlen, packhandle);

    /* crc the directory to check for modifications */
    crc = CRC_Block((byte *)dfiles, header.dirlen);
    switch (crc) {
#ifdef NQ_HACK
    case ID1_PAK0_CRC_V100:
    case ID1_PAK0_CRC_V101:
    case ID1_PAK0_CRC_V106:
#endif
#ifdef QW_HACK
    case QW_PAK0_CRC:
#endif
	com_modified = false;
	break;
    default:
	com_modified = true;
	break;
    }

    /* parse the directory */
    for (i = 0; i < numfiles; i++) {
	snprintf(mfiles[i].name, sizeof(mfiles[i].name), "%s", dfiles[i].name);
	mfiles[i].filepos = LittleLong(dfiles[i].filepos);
	mfiles[i].filelen = LittleLong(dfiles[i].filelen);
    }

#ifdef NQ_HACK
    Hunk_FreeToLowMark(mark);
    pack = Hunk_Alloc(sizeof(pack_t));
#endif
#ifdef QW_HACK
    Z_Free(dfiles);
    pack = Z_Malloc(sizeof(pack_t));
#endif
    snprintf(pack->filename, sizeof(pack->filename), "%s", packfile);
    pack->numfiles = numfiles;
    pack->files = mfiles;

    Con_Printf("Added packfile %s (%i files)\n", packfile, numfiles);

    return pack;
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
static void
COM_AddGameDirectory(const char *base, const char *dir)
{
    int i;
    searchpath_t *search;
    pack_t *pak;
    char pakfile[MAX_OSPATH];

    if (!base)
	return;

    strcpy(com_gamedir, va("%s/%s", base, dir));
#ifdef QW_HACK
    {
	char *p;
	p = strrchr(com_gamedir, '/');
	strcpy(gamedirfile, ++p);
    }
#endif

//
// add the directory to the search path
//
    search = Hunk_Alloc(sizeof(searchpath_t));
    strcpy(search->filename, com_gamedir);
    search->next = com_searchpaths;
    com_searchpaths = search;

//
// add any pak files in the format pak0.pak pak1.pak, ...
//
    for (i = 0;; i++) {
	snprintf(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
	pak = COM_LoadPackFile(pakfile);
	if (!pak)
	    break;
	search = Hunk_Alloc(sizeof(searchpath_t));
	search->pack = pak;
	search->next = com_searchpaths;
	com_searchpaths = search;
    }
}

/*
================
COM_Gamedir

Sets the gamedir and path to a different directory.

FIXME - if home dir is available, should we create ~/.tyrquake/gamedir ??
================
*/
#ifdef QW_HACK
void
COM_Gamedir(const char *dir)
{
    searchpath_t *search, *next;
    int i;
    pack_t *pak;
    char pakfile[MAX_OSPATH];

    if (strstr(dir, "..") || strstr(dir, "/")
	|| strstr(dir, "\\") || strstr(dir, ":")) {
	Con_Printf("Gamedir should be a single filename, not a path\n");
	return;
    }

    if (!strcmp(gamedirfile, dir))
	return;			// still the same
    strcpy(gamedirfile, dir);

    //
    // free up any current game dir info
    //
    while (com_searchpaths != com_base_searchpaths) {
	if (com_searchpaths->pack) {
	    Z_Free(com_searchpaths->pack->files);
	    Z_Free(com_searchpaths->pack);
	}
	next = com_searchpaths->next;
	Z_Free(com_searchpaths);
	com_searchpaths = next;
    }

    //
    // flush all data, so it will be forced to reload
    //
    Cache_Flush();

    if (!strcmp(dir, "id1") || !strcmp(dir, "qw"))
	return;

    snprintf(com_gamedir, sizeof(com_gamedir), "%s/%s", com_basedir, dir);

    //
    // add the directory to the search path
    //
    search = Z_Malloc(sizeof(searchpath_t));
    strcpy(search->filename, com_gamedir);
    search->next = com_searchpaths;
    com_searchpaths = search;

    //
    // add any pak files in the format pak0.pak pak1.pak, ...
    //
    for (i = 0;; i++) {
	snprintf(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
	pak = COM_LoadPackFile(pakfile);
	if (!pak)
	    break;
	search = Z_Malloc(sizeof(searchpath_t));
	search->pack = pak;
	search->next = com_searchpaths;
	com_searchpaths = search;
    }
}
#endif

/*
================
COM_InitFilesystem
================
*/
static void
COM_InitFilesystem(void)
{
    int i;
    char *home;
#ifdef NQ_HACK
    searchpath_t *search;
#endif

    home = getenv("HOME");

//
// -basedir <path>
// Overrides the system supplied base directory (under id1)
//
    i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1)
	strcpy(com_basedir, com_argv[i + 1]);
    else
	strcpy(com_basedir, host_parms.basedir);

//
// start up with id1 by default
//
    COM_AddGameDirectory(com_basedir, "id1");
    COM_AddGameDirectory(home, ".blinky/id1");

#ifdef NQ_HACK
    if (COM_CheckParm("-rogue")) {
	COM_AddGameDirectory(com_basedir, "rogue");
	COM_AddGameDirectory(home, ".blinky/rogue");
    }
    if (COM_CheckParm("-hipnotic")) {
	COM_AddGameDirectory(com_basedir, "hipnotic");
	COM_AddGameDirectory(home, ".blinky/hipnotic");
    }
    if (COM_CheckParm("-quoth")) {
	COM_AddGameDirectory(com_basedir, "quoth");
	COM_AddGameDirectory(home, ".blinky/quoth");
    }

//
// -game <gamedir>
// Adds basedir/gamedir as an override game
//
    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1) {
	com_modified = true;
	COM_AddGameDirectory(com_basedir, com_argv[i + 1]);
	COM_AddGameDirectory(home, va(".blinky/%s", com_argv[i + 1]));
    }
#endif
#ifdef QW_HACK
    COM_AddGameDirectory(com_basedir, "qw");
    COM_AddGameDirectory(home, ".blinky/qw");
#endif

    /* If home is available, create the game directory */
    if (home) {
	COM_CreatePath(com_gamedir);
	Sys_mkdir(com_gamedir);
    }

//
// -path <dir or packfile> [<dir or packfile>] ...
// Fully specifies the exact search path, overriding the generated one
//
#ifdef NQ_HACK
    i = COM_CheckParm("-path");
    if (i) {
	com_modified = true;
	com_searchpaths = NULL;
	while (++i < com_argc) {
	    if (!com_argv[i] || com_argv[i][0] == '+'
		|| com_argv[i][0] == '-')
		break;

	    search = Hunk_Alloc(sizeof(searchpath_t));
	    if (!strcmp(COM_FileExtension(com_argv[i]), "pak")) {
		search->pack = COM_LoadPackFile(com_argv[i]);
		if (!search->pack)
		    Sys_Error("Couldn't load packfile: %s", com_argv[i]);
	    } else
		strcpy(search->filename, com_argv[i]);
	    search->next = com_searchpaths;
	    com_searchpaths = search;
	}
    }
#endif
#ifdef QW_HACK
    // any set gamedirs will be freed up to here
    com_base_searchpaths = com_searchpaths;
#endif
}

// FIXME - everything below is QW only... move it?
#ifdef QW_HACK
/*
=====================================================================

  INFO STRINGS

=====================================================================
*/

/*
 * INFO HELPER FUNCTIONS
 *
 * Helper fuction to copy off the next string in the info string
 * Copies the next '\\' separated string into the buffer of size buflen
 * Always zero terminates the buffer
 * Returns a pointer to the char following the returned string.
 *
 * Key/Value checks are consolidated into Info_ReadKey and Info_ReadValue
 */
static const char *
Info_ReadString(const char *infostring, char *buffer, int buflen)
{
    char *out = buffer;

    while (out - buffer < buflen - 1) {
	if (!*infostring || *infostring == '\\')
	    break;
	*out++ = *infostring++;
    }
    *out = 0;

    return infostring;
}

static const char *
Info_ReadKey(const char *infostring, char *buffer, int buflen)
{
    const char *pkey;

    if (*infostring == '\\')
	infostring++;

    pkey = infostring;
    infostring = Info_ReadString(infostring, buffer, buflen);

    /* If we aren't at a separator, then the key was too long */
    if (*buffer && *infostring != '\\')
	Con_DPrintf("WARNING: No separator after info key (%s)\n", pkey);

    return infostring;
}

static const char *
Info_ReadValue(const char *infostring, char *buffer, int buflen)
{
    infostring = Info_ReadString(infostring, buffer, buflen);

    /* If we aren't at a separator, then the value was too long */
    if (*infostring && *infostring != '\\')
	Con_DPrintf("WARNING: info value too long? (%s)\n", buffer);

    return infostring;
}

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
char *
Info_ValueForKey(const char *infostring, const char *key)
{
    /* use multiple buffers so compares work without stomping on each other */
    static char buffers[4][MAX_INFO_STRING];
    static int buffer_index;

    char pkey[MAX_INFO_STRING];
    char *buf;

    buffer_index = (buffer_index + 1) & 3;
    buf = buffers[buffer_index];

    while (1) {
	/* Retrieve the key */
	infostring = Info_ReadKey(infostring, pkey, sizeof(pkey));
	if (*infostring != '\\') {
	    *buf = 0;
	    return buf;
	}
	infostring++;

	/* Retrieve the value */
	infostring = Info_ReadString(infostring, buf, sizeof(buffers[0]));
	if (*infostring && *infostring != '\\') {
	    *buf = 0;
	    return buf;
	}

	/* If the keys match, return the value in the buffer */
	if (!strcmp(key, pkey))
	    return buf;

	/* Return if we've reached the end of the infostring */
	if (!*infostring) {
	    *buf = 0;
	    return buf;
	}
	infostring++;
    }
}

void
Info_RemoveKey(char *infostring, const char *key)
{
    char *start;
    char pkey[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];

    if (strstr(key, "\\")) {
	Con_Printf("Can't use a key with a \\\n");
	return;
    }

    while (1) {
	start = infostring;

	infostring = (char *)Info_ReadKey(infostring, pkey, sizeof(pkey));
	if (*infostring)
	    infostring++;
	infostring = (char *)Info_ReadValue(infostring, value, sizeof(value));
	if (*infostring)
	    infostring++;

	/* If the keys match, remove this part of the string */
	if (!strcmp(key, pkey)) {
	    memmove(start, infostring, strlen(infostring) + 1);
	    return;
	}
	if (!*infostring)
	    return;
    }
}

void
Info_RemovePrefixedKeys(char *infostring, char prefix)
{
    char *start;
    char pkey[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];

    start = infostring;
    while (1) {
	infostring = (char *)Info_ReadKey(infostring, pkey, sizeof(pkey));
	if (*infostring)
	    infostring++;
	infostring = (char *)Info_ReadValue(infostring, value, sizeof(value));
	if (*infostring)
	    infostring++;

	/* If the prefix matches, remove the key */
	if (pkey[0] == prefix) {
	    Info_RemoveKey(start, pkey);
	    infostring = start;
	}
	if (!*infostring)
	    return;
    }
}

void
Info_SetValueForStarKey(char *infostring, const char *key, const char *value,
			int maxsize)
{
    char buffer[MAX_INFO_STRING * 2];
    char *oldvalue, *info;
    int c, len;

    if (strstr(key, "\\") || strstr(value, "\\")) {
	Con_Printf("Can't use keys or values with a \\\n");
	return;
    }
    if (strstr(key, "\"") || strstr(value, "\"")) {
	Con_Printf("Can't use keys or values with a \"\n");
	return;
    }
    if (strlen(key) > 63 || strlen(value) > 63) {
	Con_Printf("Keys and values must be < 64 characters.\n");
	return;
    }

    oldvalue = Info_ValueForKey(infostring, key);
    if (*oldvalue) {
	/*
	 * Key exists. Make sure we have enough room for new value.
	 * If we don't, don't change it!
	 */
	len = strlen(infostring) - strlen(oldvalue) + strlen(value);
	if (len > maxsize - 1)
	    goto ErrTooLong;
    }

    Info_RemoveKey(infostring, key);
    if (!value || !strlen(value))
	return;

    len = snprintf(buffer, sizeof(buffer), "\\%s\\%s", key, value);
    if (len > sizeof(buffer) - 1)
	goto ErrTooLong;

    len += strlen(infostring);
    if (len > maxsize - 1)
	goto ErrTooLong;

    /* Append the new key/value pair to the info string */
    infostring += strlen(infostring);
    info = buffer;
    while (*info) {
	c = (unsigned char)*info++;
#ifndef SERVERONLY
	/* client only allows highbits on name */
	if (strcasecmp(key, "name")) {
	    c &= 127;
	    if (c < 32 || c > 127)
		continue;
	    /* auto lowercase team */
	    if (!strcasecmp(key, "team"))
		c = tolower(c);
	}
#else
	if (!sv_highchars.value) {
	    c &= 127;
	    if (c < 32 || c > 127)
		continue;
	}
#endif
	if (c > 13)
	    *infostring++ = c;
    }
    *infostring = 0;
    return;

 ErrTooLong:
	Con_Printf("Info string length exceeded\n");
}

void
Info_SetValueForKey(char *infostring, const char *key, const char *value,
		    int maxsize)
{
    if (key[0] == '*') {
	Con_Printf("Can't set * keys\n");
	return;
    }
    Info_SetValueForStarKey(infostring, key, value, maxsize);
}

void
Info_Print(const char *infostring)
{
    char key[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];

    while (*infostring) {
	infostring = Info_ReadKey(infostring, key, sizeof(key));
	if (*infostring)
	    infostring++;
	infostring = Info_ReadValue(infostring, value, sizeof(value));
	if (*infostring)
	    infostring++;

	Con_Printf("%-20.20s %s\n", key, *value ? value : "MISSING VALUE");
    }
}

static byte chktbl[1024 + 4] = {
    0x78, 0xd2, 0x94, 0xe3, 0x41, 0xec, 0xd6, 0xd5, 0xcb, 0xfc, 0xdb, 0x8a, 0x4b, 0xcc, 0x85, 0x01,
    0x23, 0xd2, 0xe5, 0xf2, 0x29, 0xa7, 0x45, 0x94, 0x4a, 0x62, 0xe3, 0xa5, 0x6f, 0x3f, 0xe1, 0x7a,
    0x64, 0xed, 0x5c, 0x99, 0x29, 0x87, 0xa8, 0x78, 0x59, 0x0d, 0xaa, 0x0f, 0x25, 0x0a, 0x5c, 0x58,
    0xfb, 0x00, 0xa7, 0xa8, 0x8a, 0x1d, 0x86, 0x80, 0xc5, 0x1f, 0xd2, 0x28, 0x69, 0x71, 0x58, 0xc3,
    0x51, 0x90, 0xe1, 0xf8, 0x6a, 0xf3, 0x8f, 0xb0, 0x68, 0xdf, 0x95, 0x40, 0x5c, 0xe4, 0x24, 0x6b,
    0x29, 0x19, 0x71, 0x3f, 0x42, 0x63, 0x6c, 0x48, 0xe7, 0xad, 0xa8, 0x4b, 0x91, 0x8f, 0x42, 0x36,
    0x34, 0xe7, 0x32, 0x55, 0x59, 0x2d, 0x36, 0x38, 0x38, 0x59, 0x9b, 0x08, 0x16, 0x4d, 0x8d, 0xf8,
    0x0a, 0xa4, 0x52, 0x01, 0xbb, 0x52, 0xa9, 0xfd, 0x40, 0x18, 0x97, 0x37, 0xff, 0xc9, 0x82, 0x27,
    0xb2, 0x64, 0x60, 0xce, 0x00, 0xd9, 0x04, 0xf0, 0x9e, 0x99, 0xbd, 0xce, 0x8f, 0x90, 0x4a, 0xdd,
    0xe1, 0xec, 0x19, 0x14, 0xb1, 0xfb, 0xca, 0x1e, 0x98, 0x0f, 0xd4, 0xcb, 0x80, 0xd6, 0x05, 0x63,
    0xfd, 0xa0, 0x74, 0xa6, 0x86, 0xf6, 0x19, 0x98, 0x76, 0x27, 0x68, 0xf7, 0xe9, 0x09, 0x9a, 0xf2,
    0x2e, 0x42, 0xe1, 0xbe, 0x64, 0x48, 0x2a, 0x74, 0x30, 0xbb, 0x07, 0xcc, 0x1f, 0xd4, 0x91, 0x9d,
    0xac, 0x55, 0x53, 0x25, 0xb9, 0x64, 0xf7, 0x58, 0x4c, 0x34, 0x16, 0xbc, 0xf6, 0x12, 0x2b, 0x65,
    0x68, 0x25, 0x2e, 0x29, 0x1f, 0xbb, 0xb9, 0xee, 0x6d, 0x0c, 0x8e, 0xbb, 0xd2, 0x5f, 0x1d, 0x8f,
    0xc1, 0x39, 0xf9, 0x8d, 0xc0, 0x39, 0x75, 0xcf, 0x25, 0x17, 0xbe, 0x96, 0xaf, 0x98, 0x9f, 0x5f,
    0x65, 0x15, 0xc4, 0x62, 0xf8, 0x55, 0xfc, 0xab, 0x54, 0xcf, 0xdc, 0x14, 0x06, 0xc8, 0xfc, 0x42,
    0xd3, 0xf0, 0xad, 0x10, 0x08, 0xcd, 0xd4, 0x11, 0xbb, 0xca, 0x67, 0xc6, 0x48, 0x5f, 0x9d, 0x59,
    0xe3, 0xe8, 0x53, 0x67, 0x27, 0x2d, 0x34, 0x9e, 0x9e, 0x24, 0x29, 0xdb, 0x69, 0x99, 0x86, 0xf9,
    0x20, 0xb5, 0xbb, 0x5b, 0xb0, 0xf9, 0xc3, 0x67, 0xad, 0x1c, 0x9c, 0xf7, 0xcc, 0xef, 0xce, 0x69,
    0xe0, 0x26, 0x8f, 0x79, 0xbd, 0xca, 0x10, 0x17, 0xda, 0xa9, 0x88, 0x57, 0x9b, 0x15, 0x24, 0xba,
    0x84, 0xd0, 0xeb, 0x4d, 0x14, 0xf5, 0xfc, 0xe6, 0x51, 0x6c, 0x6f, 0x64, 0x6b, 0x73, 0xec, 0x85,
    0xf1, 0x6f, 0xe1, 0x67, 0x25, 0x10, 0x77, 0x32, 0x9e, 0x85, 0x6e, 0x69, 0xb1, 0x83, 0x00, 0xe4,
    0x13, 0xa4, 0x45, 0x34, 0x3b, 0x40, 0xff, 0x41, 0x82, 0x89, 0x79, 0x57, 0xfd, 0xd2, 0x8e, 0xe8,
    0xfc, 0x1d, 0x19, 0x21, 0x12, 0x00, 0xd7, 0x66, 0xe5, 0xc7, 0x10, 0x1d, 0xcb, 0x75, 0xe8, 0xfa,
    0xb6, 0xee, 0x7b, 0x2f, 0x1a, 0x25, 0x24, 0xb9, 0x9f, 0x1d, 0x78, 0xfb, 0x84, 0xd0, 0x17, 0x05,
    0x71, 0xb3, 0xc8, 0x18, 0xff, 0x62, 0xee, 0xed, 0x53, 0xab, 0x78, 0xd3, 0x65, 0x2d, 0xbb, 0xc7,
    0xc1, 0xe7, 0x70, 0xa2, 0x43, 0x2c, 0x7c, 0xc7, 0x16, 0x04, 0xd2, 0x45, 0xd5, 0x6b, 0x6c, 0x7a,
    0x5e, 0xa1, 0x50, 0x2e, 0x31, 0x5b, 0xcc, 0xe8, 0x65, 0x8b, 0x16, 0x85, 0xbf, 0x82, 0x83, 0xfb,
    0xde, 0x9f, 0x36, 0x48, 0x32, 0x79, 0xd6, 0x9b, 0xfb, 0x52, 0x45, 0xbf, 0x43, 0xf7, 0x0b, 0x0b,
    0x19, 0x19, 0x31, 0xc3, 0x85, 0xec, 0x1d, 0x8c, 0x20, 0xf0, 0x3a, 0xfa, 0x80, 0x4d, 0x2c, 0x7d,
    0xac, 0x60, 0x09, 0xc0, 0x40, 0xee, 0xb9, 0xeb, 0x13, 0x5b, 0xe8, 0x2b, 0xb1, 0x20, 0xf0, 0xce,
    0x4c, 0xbd, 0xc6, 0x04, 0x86, 0x70, 0xc6, 0x33, 0xc3, 0x15, 0x0f, 0x65, 0x19, 0xfd, 0xc2, 0xd3,

// map checksum goes here
    0x00, 0x00, 0x00, 0x00
};


#if 0
/*
====================
COM_BlockSequenceCheckByte

For proxy protecting
====================
*/
byte
COM_BlockSequenceCheckByte(const byte *base, int length, int sequence,
			   unsigned mapchecksum)
{
    static unsigned last_mapchecksum;
    unsigned char chkbuf[16 + 60 + 4];
    int checksum;
    byte *p;

    if (last_mapchecksum != mapchecksum) {
	last_mapchecksum = mapchecksum;
	chktbl[1024] = (mapchecksum & 0xff000000) >> 24;
	chktbl[1025] = (mapchecksum & 0x00ff0000) >> 16;
	chktbl[1026] = (mapchecksum & 0x0000ff00) >> 8;
	chktbl[1027] = (mapchecksum & 0x000000ff);

	Com_BlockFullChecksum(chktbl, sizeof(chktbl), chkbuf);
    }

    p = chktbl + (sequence % (sizeof(chktbl) - 8));

    if (length > 60)
	length = 60;
    memcpy(chkbuf + 16, base, length);

    length += 16;

    chkbuf[length] = (sequence & 0xff) ^ p[0];
    chkbuf[length + 1] = p[1];
    chkbuf[length + 2] = ((sequence >> 8) & 0xff) ^ p[2];
    chkbuf[length + 3] = p[3];

    length += 4;

    checksum = LittleLong(Com_BlockChecksum(chkbuf, length));

    checksum &= 0xff;

    return checksum;
}
#endif

/*
====================
COM_BlockSequenceCRCByte

For proxy protecting
====================
*/
byte
COM_BlockSequenceCRCByte(const byte *base, int length, int sequence)
{
    unsigned short crc;
    const byte *p;
    byte chkb[60 + 4];

    p = chktbl + (sequence % (sizeof(chktbl) - 8));

    if (length > 60)
	length = 60;
    memcpy(chkb, base, length);

    chkb[length] = (sequence & 0xff) ^ p[0];
    chkb[length + 1] = p[1];
    chkb[length + 2] = ((sequence >> 8) & 0xff) ^ p[2];
    chkb[length + 3] = p[3];

    length += 4;

    crc = CRC_Block(chkb, length);

    crc &= 0xff;

    return crc;
}

// char *date = "Oct 24 1996";
static const char *date = __DATE__;
static const char *mon[12] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
    "Nov", "Dec"
};
static char mond[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// returns days since Oct 24 1996
int
build_number(void)
{
    int m = 0;
    int d = 0;
    int y = 0;
    static int b = 0;

    if (b != 0)
	return b;

    for (m = 0; m < 11; m++) {
	if (strncasecmp(&date[0], mon[m], 3) == 0)
	    break;
	d += mond[m];
    }

    d += atoi(&date[4]) - 1;

    y = atoi(&date[7]) - 1900;

    b = d + (int)((y - 1) * 365.25);

    if (((y % 4) == 0) && m > 1) {
	b += 1;
    }

    b -= 35778;			// Dec 16 1998

    return b;
}
#endif /* QW_HACK */
