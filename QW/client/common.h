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

#include <stdio.h>

#include "qtypes.h"
#include "bothdefs.h"
#include "shell.h"

#define stringify__(x) #x
#define stringify(x) stringify__(x)

// FIXME - cannot include protocol.h (cyclic includes)
//#include "protocol.h" // struct usercmd_s
struct usercmd_s;

#define	MAX_INFO_STRING	196
#define	MAX_SERVERINFO_STRING	512
#define	MAX_LOCALINFO_STRING	32768

//============================================================================

typedef struct sizebuf_s {
    qboolean allowoverflow;	// if false, do a Sys_Error
    qboolean overflowed;	// set to true if the buffer size failed
    byte *data;
    int maxsize;
    int cursize;
} sizebuf_t;

void SZ_Clear(sizebuf_t *buf);
void *SZ_GetSpace(sizebuf_t *buf, int length);
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

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

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

extern struct usercmd_s nullcmd;

void MSG_WriteChar(sizebuf_t *sb, int c);
void MSG_WriteByte(sizebuf_t *sb, int c);
void MSG_WriteShort(sizebuf_t *sb, int c);
void MSG_WriteLong(sizebuf_t *sb, int c);
void MSG_WriteFloat(sizebuf_t *sb, float f);
void MSG_WriteString(sizebuf_t *sb, char *s);
void MSG_WriteCoord(sizebuf_t *sb, float f);
void MSG_WriteAngle(sizebuf_t *sb, float f);
void MSG_WriteAngle16(sizebuf_t *sb, float f);
void MSG_WriteDeltaUsercmd(sizebuf_t *sb, struct usercmd_s *from,
			   struct usercmd_s *cmd);

extern int msg_readcount;
extern qboolean msg_badread;	// set if a read goes beyond end of message

void MSG_BeginReading(void);
int MSG_GetReadCount(void);
int MSG_ReadChar(void);
int MSG_ReadByte(void);
int MSG_ReadShort(void);
int MSG_ReadLong(void);
float MSG_ReadFloat(void);
char *MSG_ReadString(void);
char *MSG_ReadStringLine(void);

float MSG_ReadCoord(void);
float MSG_ReadAngle(void);
float MSG_ReadAngle16(void);
void MSG_ReadDeltaUsercmd(struct usercmd_s *from, struct usercmd_s *cmd);

//============================================================================

int Q_atoi(const char *str);
float Q_atof(const char *str);

//============================================================================

extern char com_token[1024];
extern qboolean com_eof;

char *COM_Parse(char *data);


extern unsigned com_argc;
extern char **com_argv;

unsigned COM_CheckParm(char *parm);
void COM_AddParm(char *parm);

void COM_Init(void);
void COM_InitArgv(int argc, char **argv);

const char *COM_SkipPath(const char *pathname);
void COM_StripExtension(const char *in, char *out);
void COM_FileBase(const char *in, char *out);
void COM_DefaultExtension(char *path, const char *extension);
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
void COM_CloseFile(FILE *h);
void COM_ScanDir(struct stree_root *root, const char *path,
		 const char *pfx, const char *ext, qboolean stripext);

byte *COM_LoadStackFile(const char *path, void *buffer, int bufsize,
			unsigned long *length);
byte *COM_LoadTempFile(const char *path);
byte *COM_LoadHunkFile(const char *path);
void COM_LoadCacheFile(const char *path, struct cache_user_s *cu);
void COM_CreatePath(char *path);
void COM_Gamedir(const char *dir);

extern struct cvar_s registered;
extern qboolean standard_quake, rogue, hipnotic;

char *Info_ValueForKey(char *s, char *key);
void Info_RemoveKey(char *s, char *key);
void Info_RemovePrefixedKeys(char *start, char prefix);
void Info_SetValueForKey(char *s, char *key, char *value, int maxsize);
void Info_SetValueForStarKey(char *s, char *key, char *value, int maxsize);
void Info_Print(char *s);

unsigned Com_BlockChecksum(void *buffer, int length);
void Com_BlockFullChecksum(void *buffer, int len, unsigned char *outbuf);
byte COM_BlockSequenceCheckByte(byte *base, int length, int sequence,
				unsigned mapchecksum);
byte COM_BlockSequenceCRCByte(byte *base, int length, int sequence);

int build_number(void);

extern char gamedirfile[];

#endif /* COMMON_H */
