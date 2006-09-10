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
// cmd.c -- Quake script command processing module

#include "client.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "cvar.h"
#include "quakedef.h"
#include "shell.h"
#include "sys.h"
#include "zone.h"

#ifdef NQ_HACK
#include "host.h"
#include "protocol.h"
#endif

void Cmd_ForwardToServer(void);
#if defined(QW_HACK) && !defined(SERVERONLY)
static void Cmd_ForwardToServer_f(void);
#endif

#define	MAX_ALIAS_NAME	32

typedef struct cmdalias_s {
    char name[MAX_ALIAS_NAME];
    char *value;
    struct stree_node stree;
} cmdalias_t;

#define cmdalias_entry(ptr) container_of(ptr, struct cmdalias_s, stree)
static DECLARE_STREE_ROOT(cmdalias_tree);

qboolean cmd_wait;

cvar_t cl_warncmd = { "cl_warncmd", "0" };

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void
Cmd_Wait_f(void)
{
    cmd_wait = true;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

static sizebuf_t cmd_text;
#ifdef QW_HACK
static byte cmd_text_buf[8192];
#endif

/*
============
Cbuf_Init
============
*/
void
Cbuf_Init(void)
{
#ifdef NQ_HACK
    SZ_Alloc(&cmd_text, 8192);
#endif
#ifdef QW_HACK
    cmd_text.data = cmd_text_buf;
    cmd_text.maxsize = sizeof(cmd_text_buf);
    cmd_text.cursize = 0;
#endif
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void
Cbuf_AddText(const char *text)
{
    int l = strlen(text);

    if (cmd_text.cursize + l < cmd_text.maxsize)
	SZ_Write(&cmd_text, text, l);
    else
	Con_Printf("Cbuf_AddText: overflow\n");
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void
Cbuf_InsertText(char *text)
{
    char *temp;
    int templen;

// copy off any commands still remaining in the exec buffer
    templen = cmd_text.cursize;
    if (templen) {
	temp = Z_Malloc(templen);
	memcpy(temp, cmd_text.data, templen);
	SZ_Clear(&cmd_text);
    } else
	temp = NULL;		// shut up compiler

// add the entire text of the file
    Cbuf_AddText(text);
    SZ_Write(&cmd_text, "\n", 1);

// add the copied off data
    if (templen) {
	SZ_Write(&cmd_text, temp, templen);
	Z_Free(temp);
    }
}

/*
============
Cbuf_Execute
============
*/
void
Cbuf_Execute(void)
{
    int i;
    char *text;
    char line[1024];
    int quotes;

    while (cmd_text.cursize) {
	/* find a \n or ; line break */
	text = (char *)cmd_text.data;

	quotes = 0;
	for (i = 0; i < cmd_text.cursize; i++) {
	    if (text[i] == '"')
		quotes++;
	    if (!(quotes & 1) && text[i] == ';')
		break;		/* don't break if inside a quoted string */
	    if (text[i] == '\n')
		break;
	}
	memcpy(line, text, i);
	line[i] = 0;

	/*
	 * delete the text from the command buffer and move remaining commands
	 * down this is necessary because commands (exec, alias) can insert
	 * data at the beginning of the text buffer
	 */
	if (i == cmd_text.cursize)
	    cmd_text.cursize = 0;
	else {
	    i++;
	    cmd_text.cursize -= i;
	    memmove(text, text + i, cmd_text.cursize);
	}

	/* execute the command line */
#ifdef NQ_HACK
	Cmd_ExecuteString(line, src_command);
#endif
#ifdef QW_HACK
	Cmd_ExecuteString(line);
#endif

	if (cmd_wait) {
	    /*
	     * skip out while text still remains in buffer, leaving it for
	     * next frame
	     */
	    cmd_wait = false;
	    break;
	}
    }
}

/*
==============================================================================

				SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void
Cmd_StuffCmds_f(void)
{
    int i, j;
    int s;
    char *text, *build, c;

    if (Cmd_Argc() != 1) {
	Con_Printf("stuffcmds : execute command line parameters\n");
	return;
    }
// build the combined string to parse from
    s = 0;
    for (i = 1; i < com_argc; i++) {
	if (!com_argv[i])
	    continue;		// NEXTSTEP nulls out -NXHost
	s += strlen(com_argv[i]) + 1;
    }
    if (!s)
	return;

    text = Z_Malloc(s + 1);
    text[0] = 0;
    for (i = 1; i < com_argc; i++) {
	if (!com_argv[i])
	    continue;		// NEXTSTEP nulls out -NXHost
	strcat(text, com_argv[i]);
	if (i != com_argc - 1)
	    strcat(text, " ");
    }

// pull out the commands
    build = Z_Malloc(s + 1);
    build[0] = 0;

    for (i = 0; i < s - 1; i++) {
	if (text[i] == '+') {
	    i++;

	    for (j = i;
		 (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++);

	    c = text[j];
	    text[j] = 0;

	    strcat(build, text + i);
	    strcat(build, "\n");
	    text[j] = c;
	    i = j - 1;
	}
    }

    if (build[0])
	Cbuf_InsertText(build);

    Z_Free(text);
    Z_Free(build);
}


/*
===============
Cmd_Exec_f
===============
*/
void
Cmd_Exec_f(void)
{
    char *f;
    int mark;

    if (Cmd_Argc() != 2) {
	Con_Printf("exec <filename> : execute a script file\n");
	return;
    }
    // FIXME: is this safe freeing the hunk here???
    mark = Hunk_LowMark();
    f = (char *)COM_LoadHunkFile(Cmd_Argv(1));
    if (!f) {
	Con_Printf("couldn't exec %s\n", Cmd_Argv(1));
	return;
    }
    if (cl_warncmd.value || developer.value)
	Con_Printf("execing %s\n", Cmd_Argv(1));

    Cbuf_InsertText(f);
    Hunk_FreeToLowMark(mark);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void
Cmd_Echo_f(void)
{
    int i;

    for (i = 1; i < Cmd_Argc(); i++)
	Con_Printf("%s ", Cmd_Argv(i));
    Con_Printf("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/

char *
CopyString(char *in)
{
    char *out;

    out = Z_Malloc(strlen(in) + 1);
    strcpy(out, in);
    return out;
}

static struct cmdalias_s *
Cmd_Alias_Find(const char *name)
{
    struct cmdalias_s *ret = NULL;
    struct stree_node *n;

    n = STree_Find(&cmdalias_tree, name);
    if (n)
	ret = cmdalias_entry(n);

    return ret;
}

void
Cmd_Alias_f(void)
{
    cmdalias_t *a;
    char cmd[1024];
    int i, c;
    char *s;
    size_t cmd_len;
    struct stree_node *node;

    if (Cmd_Argc() == 1) {
	Con_Printf("Current alias commands:\n");
	STree_ForEach(&cmdalias_tree, node) {
	    a = cmdalias_entry(node);
	    Con_Printf("%s : %s\n", a->name, a->value);
	}
	return;
    }

    s = Cmd_Argv(1);
    if (strlen(s) >= MAX_ALIAS_NAME) {
	Con_Printf("Alias name is too long\n");
	return;
    }

    // if the alias already exists, reuse it
    a = Cmd_Alias_Find(s);
    if (a)
	Z_Free(a->value);

    if (!a) {
	a = Z_Malloc(sizeof(cmdalias_t));
	strcpy(a->name, s);
	a->stree.string = a->name;
	STree_Insert(&cmdalias_tree, &a->stree);
    }

// copy the rest of the command line
    cmd[0] = 0;			// start out with a null string
    c = Cmd_Argc();
    cmd_len = 1;
    for (i = 2; i < c; i++) {
	cmd_len += strlen(Cmd_Argv(i));
	if (i != c - 1)
		cmd_len++;
	if (cmd_len >= sizeof(cmd)) {
	    Con_Printf("Alias value is too long\n");
	    cmd[0] = 0;	// nullify the string
	    break;
	}
	strcat(cmd, Cmd_Argv(i));
	if (i != c - 1)
	    strcat(cmd, " ");
    }
    strcat(cmd, "\n");

    a->value = CopyString(cmd);
}

/*
=============================================================================

				COMMAND EXECUTION

=============================================================================
*/

typedef struct cmd_function_s {
    const char *name;
    xcommand_t function;
    cmd_arg_f completion;
    struct stree_node stree;
} cmd_function_t;

#define cmd_entry(ptr) container_of(ptr, struct cmd_function_s, stree)
static DECLARE_STREE_ROOT(cmd_tree);

#define	MAX_ARGS		80
static int cmd_argc;
static char *cmd_argv[MAX_ARGS];
static char *cmd_null_string = "";
static char *cmd_args = NULL;

#ifdef NQ_HACK
cmd_source_t cmd_source;
#endif

/*
============
Cmd_Init
============
*/
void
Cmd_Init(void)
{
//
// register our commands
//
    Cmd_AddCommand("stuffcmds", Cmd_StuffCmds_f);
    Cmd_AddCommand("exec", Cmd_Exec_f);
    Cmd_AddCommand("echo", Cmd_Echo_f);
    Cmd_AddCommand("alias", Cmd_Alias_f);
    Cmd_AddCommand("wait", Cmd_Wait_f);
#ifdef NQ_HACK
    Cmd_AddCommand("cmd", Cmd_ForwardToServer);
#elif defined(QW_HACK) && !defined(SERVERONLY)
    Cmd_AddCommand("cmd", Cmd_ForwardToServer_f);
#endif
}

/*
============
Cmd_Argc
============
*/
int
Cmd_Argc(void)
{
    return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *
Cmd_Argv(int arg)
{
    if (arg >= cmd_argc)
	return cmd_null_string;
    return cmd_argv[arg];
}

/*
============
Cmd_Args

Returns a single string containing argv(1) to argv(argc()-1)
============
*/
char *
Cmd_Args(void)
{
    // FIXME - check necessary?
    if (!cmd_args)
	return "";
    return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void
Cmd_TokenizeString(char *text)
{
    int i;

// clear the args from the last string
    for (i = 0; i < cmd_argc; i++)
	Z_Free(cmd_argv[i]);

    cmd_argc = 0;
    cmd_args = NULL;

    while (1) {
// skip whitespace up to a /n
	while (*text && *text <= ' ' && *text != '\n') {
	    text++;
	}

	if (*text == '\n') {	// a newline seperates commands in the buffer
	    text++;
	    break;
	}

	if (!*text)
	    return;

	if (cmd_argc == 1)
	    cmd_args = text;

	text = COM_Parse(text);
	if (!text)
	    return;

	if (cmd_argc < MAX_ARGS) {
	    cmd_argv[cmd_argc] = Z_Malloc(strlen(com_token) + 1);
	    strcpy(cmd_argv[cmd_argc], com_token);
	    cmd_argc++;
	}
    }

}

static struct cmd_function_s *
Cmd_FindCommand(const char *name)
{
    struct cmd_function_s *ret = NULL;
    struct stree_node *n;

    n = STree_Find(&cmd_tree, name);
    if (n)
	ret = cmd_entry(n);

    return ret;
}

/*
============
Cmd_AddCommand
============
*/
void
Cmd_AddCommand(const char *cmd_name, xcommand_t function)
{
    cmd_function_t *cmd;

    if (host_initialized)	// because hunk allocation would get stomped
	Sys_Error("%s: called after host_initialized", __func__);

// fail if the command is a variable name
    if (Cvar_VariableString(cmd_name)[0]) {
	Con_Printf("%s: %s already defined as a var\n", __func__, cmd_name);
	return;
    }
// fail if the command already exists
    cmd = Cmd_FindCommand(cmd_name);
    if (cmd) {
	Con_Printf("%s: %s already defined\n", __func__, cmd_name);
	return;
    }

    cmd = Hunk_Alloc(sizeof(cmd_function_t));
    cmd->name = cmd_name;
    cmd->function = function;
    cmd->completion = NULL;
    cmd->stree.string = cmd->name;
    STree_Insert(&cmd_tree, &cmd->stree);
}

void
Cmd_SetCompletion(const char *cmd_name, cmd_arg_f completion)
{
    cmd_function_t *cmd;

    cmd = Cmd_FindCommand(cmd_name);
    if (cmd)
	cmd->completion = completion;
    else
	Sys_Error("%s: no such command - %s", __func__, cmd_name);
}

/*
============
Cmd_Exists
============
*/
qboolean
Cmd_Exists(char *cmd_name)
{
    return Cmd_FindCommand(cmd_name) != NULL;
}

qboolean
Cmd_Alias_Exists(char *cmd_name)
{
    return Cmd_Alias_Find(cmd_name) != NULL;
}


#ifdef NQ_HACK
/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void
Cmd_ForwardToServer(void)
{
    if (cls.state < ca_connected) {
	Con_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
	return;
    }

    if (cls.demoplayback)
	return;			// not really connected

    MSG_WriteByte(&cls.message, clc_stringcmd);
    if (strcasecmp(Cmd_Argv(0), "cmd") != 0) {
	SZ_Print(&cls.message, Cmd_Argv(0));
	SZ_Print(&cls.message, " ");
    }
    if (Cmd_Argc() > 1)
	SZ_Print(&cls.message, Cmd_Args());
    else
	SZ_Print(&cls.message, "\n");
}
#endif
#ifdef QW_HACK
#ifndef SERVERONLY
/*
===================
Cmd_ForwardToServer

adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void
Cmd_ForwardToServer(void)
{
    if (cls.state == ca_disconnected) {
	Con_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
	return;
    }

    if (cls.demoplayback)
	return;			// not really connected

    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Print(&cls.netchan.message, Cmd_Argv(0));
    if (Cmd_Argc() > 1) {
	SZ_Print(&cls.netchan.message, " ");
	SZ_Print(&cls.netchan.message, Cmd_Args());
    }
}

// don't forward the first argument
static void
Cmd_ForwardToServer_f(void)
{
    if (cls.state == ca_disconnected) {
	Con_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
	return;
    }

    if (strcasecmp(Cmd_Argv(1), "snap") == 0) {
	Cbuf_InsertText("snap\n");
	return;
    }

    if (cls.demoplayback)
	return;			// not really connected

    if (Cmd_Argc() > 1) {
	MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
	SZ_Print(&cls.netchan.message, Cmd_Args());
    }
}
#else
void
Cmd_ForwardToServer(void)
{
}
#endif /* SERVERONLY */
#endif /* QW_HACK */

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void
#ifdef NQ_HACK
Cmd_ExecuteString(char *text, cmd_source_t src)
#endif
#ifdef QW_HACK
Cmd_ExecuteString(char *text)
#endif
{
    cmd_function_t *cmd;
    cmdalias_t *a;

#ifdef NQ_HACK
    cmd_source = src;
#endif
    Cmd_TokenizeString(text);

// execute the command line
    if (!Cmd_Argc())
	return;			// no tokens

// check functions
    cmd = Cmd_FindCommand(cmd_argv[0]);
    if (cmd) {
	if (cmd->function)
	    cmd->function();
#ifdef QW_HACK
	else
	    Cmd_ForwardToServer();
#endif
	return;
    }

// check alias
    a = Cmd_Alias_Find(cmd_argv[0]);
    if (a) {
	Cbuf_InsertText(a->value);
	return;
    }

// check cvars
    if (!Cvar_Command() && (cl_warncmd.value || developer.value))
	Con_Printf("Unknown command \"%s\"\n", Cmd_Argv(0));
}

/*
 * Return a string tree with all possible argument completions of the given
 * buffer for the given command.
 */
struct stree_root *
Cmd_ArgCompletions(const char *name, const char *buf)
{
    cmd_function_t *cmd;
    struct stree_root *root = NULL;

    cmd = Cmd_FindCommand(name);
    if (cmd && cmd->completion)
	root = cmd->completion(buf);

    return root;
}

/*
 * Call the argument completion function for cmd "name".
 * Returned result should be Z_Free'd after use.
 */
char *
Cmd_ArgComplete(const char *name, const char *buf)
{
    char *result = NULL;
    struct stree_root *root;

    root = Cmd_ArgCompletions(name, buf);
    if (root) {
	result = STree_MaxMatch(root, buf);
	Z_Free(root);
    }

    return result;
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/
int
Cmd_CheckParm(char *parm)
{
    int i;

    if (!parm)
	Sys_Error("Cmd_CheckParm: NULL");

    for (i = 1; i < Cmd_Argc(); i++)
	if (!strcasecmp(parm, Cmd_Argv(i)))
	    return i;

    return 0;
}

struct stree_root *
Cmd_CommandCompletions(const char *buf)
{
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    *root = STREE_ROOT;

    STree_AllocInit();

    STree_Completions(root, &cmd_tree, buf);
    STree_Completions(root, &cmdalias_tree, buf);
    STree_Completions(root, &cvar_tree, buf);

    return root;
}

char *
Cmd_CommandComplete(const char *buf)
{
    struct stree_root *root;
    char *ret;

    root = Cmd_CommandCompletions(buf);
    ret = STree_MaxMatch(root, buf);
    Z_Free(root);

    return ret;
}
