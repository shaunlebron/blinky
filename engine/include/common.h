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
// common.h -- general definitions

#ifndef COMMON_H
#define COMMON_H

#include <stdarg.h>
#include <stdio.h>

#include "qtypes.h"
#include "shell.h"

#ifdef NQ_HACK
#include "quakedef.h"
#endif
#ifdef QW_HACK
#include "bothdefs.h"
#include "protocol.h"
#endif

#define MAX_NUM_ARGVS 50

#define stringify__(x) #x
#define stringify(x) stringify__(x)

#ifdef QW_HACK
#define	MAX_INFO_STRING 196
#define	MAX_SERVERINFO_STRING 512
#define	MAX_LOCALINFO_STRING 32768
#endif

//============================================================================

typedef struct sizebuf_s {
    qboolean allowoverflow;	// if false, do a Sys_Error
    qboolean overflowed;	// set to true if the buffer size failed
    byte *data;
    int maxsize;
    int cursize;
} sizebuf_t;

#ifdef NQ_HACK
void SZ_Alloc(sizebuf_t *buf, int startsize);
void SZ_Free(sizebuf_t *buf);
#endif
void SZ_Clear(sizebuf_t *buf);
void SZ_Write(sizebuf_t *buf, const void *data, int length);
void SZ_Print(sizebuf_t *buf, const char *data); // strcats onto the sizebuf

//============================================================================

typedef struct link_s {
    struct link_s *prev, *next;
} link_t;

void ClearLink(link_t *l);
void RemoveLink(link_t *l);
void InsertLinkBefore(link_t *l, link_t *before);
void InsertLinkAfter(link_t *l, link_t *after);

//============================================================================

#ifndef NULL
#define NULL ((void *)0)
#endif

#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT ((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT ((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

extern qboolean bigendien;

extern short (*BigShort) (short l);
extern short (*LittleShort) (short l);
extern int (*BigLong) (int l);
extern int (*LittleLong) (int l);
extern float (*BigFloat) (float l);
extern float (*LittleFloat) (float l);

//============================================================================

#ifdef QW_HACK
extern struct usercmd_s nullcmd;
#endif

void MSG_WriteChar(sizebuf_t *sb, int c);
void MSG_WriteByte(sizebuf_t *sb, int c);
void MSG_WriteShort(sizebuf_t *sb, int c);
void MSG_WriteLong(sizebuf_t *sb, int c);
void MSG_WriteFloat(sizebuf_t *sb, float f);
void MSG_WriteString(sizebuf_t *sb, const char *s);
void MSG_WriteStringf(sizebuf_t *sb, const char *fmt, ...)
    __attribute__((format(printf,2,3)));
void MSG_WriteStringvf(sizebuf_t *sb, const char *fmt, va_list ap)
    __attribute__((format(printf,2,0)));
void MSG_WriteCoord(sizebuf_t *sb, float f);
void MSG_WriteAngle(sizebuf_t *sb, float f);
void MSG_WriteAngle16(sizebuf_t *sb, float f);
#ifdef QW_HACK
void MSG_WriteDeltaUsercmd(sizebuf_t *sb, const struct usercmd_s *from,
			   const struct usercmd_s *cmd);
#endif
#ifdef NQ_HACK
void MSG_WriteControlHeader(sizebuf_t *sb);
#endif

extern int msg_readcount;
extern qboolean msg_badread;	// set if a read goes beyond end of message

void MSG_BeginReading(void);
#ifdef QW_HACK
int MSG_GetReadCount(void);
#endif
int MSG_ReadChar(void);
int MSG_ReadByte(void);
int MSG_ReadShort(void);
int MSG_ReadLong(void);
float MSG_ReadFloat(void);
char *MSG_ReadString(void);
#ifdef QW_HACK
char *MSG_ReadStringLine(void);
#endif

float MSG_ReadCoord(void);
float MSG_ReadAngle(void);
float MSG_ReadAngle16(void);
#ifdef QW_HACK
void MSG_ReadDeltaUsercmd(const struct usercmd_s *from, struct usercmd_s *cmd);
#endif
#ifdef NQ_HACK
int MSG_ReadControlHeader(void);
#endif

//============================================================================

int Q_atoi(const char *str);
float Q_atof(const char *str);

//============================================================================

extern const char *com_token;
extern qboolean com_eof;

const char *COM_Parse(const char *data);

extern unsigned com_argc;
extern const char **com_argv;

unsigned COM_CheckParm(const char *parm);
#ifdef QW_HACK
void COM_AddParm(const char *parm);
#endif

void COM_Init(void);
void COM_InitArgv(int argc, const char **argv);

const char *COM_SkipPath(const char *pathname);
void COM_StripExtension(const char *filename, char *out, size_t buflen);
void COM_FileBase(const char *in, char *out, size_t buflen);
int COM_DefaultExtension(const char *path, const char *extension,
			 char *out, size_t buflen);
int COM_CheckExtension(const char *path, const char *extn);

char *va(const char *format, ...) __attribute__((format(printf,1,2)));

// does a varargs printf into a temp buffer

//============================================================================

extern int com_filesize;
struct cache_user_s;

extern char com_basedir[MAX_OSPATH];
extern char com_gamedir[MAX_OSPATH];

void COM_WriteFile(const char *filename, const void *data, int len);
int COM_FOpenFile(const char *filename, FILE **file);
void COM_ScanDir(struct stree_root *root, const char *path,
		 const char *pfx, const char *ext, qboolean stripext);

void *COM_LoadStackFile(const char *path, void *buffer, int bufsize,
			size_t *size);
void *COM_LoadTempFile(const char *path);
void *COM_LoadHunkFile(const char *path);
void COM_LoadCacheFile(const char *path, struct cache_user_s *cu);
#ifdef QW_HACK
void COM_CreatePath(const char *path);
void COM_Gamedir(const char *dir);
#endif

extern struct cvar_s registered;
extern qboolean standard_quake, rogue, hipnotic;

#ifdef QW_HACK
char *Info_ValueForKey(const char *infostring, const char *key);
void Info_RemoveKey(char *infostring, const char *key);
void Info_RemovePrefixedKeys(char *infostring, char prefix);
void Info_SetValueForKey(char *infostring, const char *key, const char *value,
			 int maxsize);
void Info_SetValueForStarKey(char *infostring, const char *key,
			     const char *value, int maxsize);
void Info_Print(const char *infostring);

unsigned Com_BlockChecksum(const void *buffer, int length);
void Com_BlockFullChecksum(const void *buffer, int len,
			   unsigned char outbuf[16]);
byte COM_BlockSequenceCheckByte(const byte *base, int length, int sequence,
				unsigned mapchecksum);
byte COM_BlockSequenceCRCByte(const byte *base, int length, int sequence);

int build_number(void);

extern char gamedirfile[];
#endif

#endif /* COMMON_H */
