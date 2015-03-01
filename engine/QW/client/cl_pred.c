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

#include "client.h"
#include "console.h"
#include "cvar.h"
#include "mathlib.h"
#include "model.h"
#include "pmove.h"
#include "quakedef.h"

#ifdef _WIN32
#include "winquake.h"
#endif

cvar_t cl_nopred = { "cl_nopred", "0" };
cvar_t cl_pushlatency = { "pushlatency", "-999" };

static void
CL_PlayerMove(const player_state_t *from, player_state_t *to,
	      const usercmd_t *cmd, const physent_stack_t *pestack,
	      qboolean spectator)
{
    playermove_t pmove;

    /* Setup the player move info */
    VectorCopy(from->origin, pmove.origin);
    VectorCopy(cmd->angles, pmove.angles);
    VectorCopy(from->velocity, pmove.velocity);
    pmove.oldbuttons = from->oldbuttons;
    pmove.waterjumptime = from->waterjumptime;
    pmove.dead = cl.stats[STAT_HEALTH] <= 0;
    pmove.spectator = spectator;
    pmove.cmd = cmd;

    PlayerMove(&pmove, pestack);

    /* Copy out the changes */
    to->waterjumptime = pmove.waterjumptime;
    to->oldbuttons = pmove.oldbuttons;
    VectorCopy(pmove.origin, to->origin);
    VectorCopy(pmove.angles, to->viewangles);
    VectorCopy(pmove.velocity, to->velocity);
    to->onground = !!pmove.onground;
    to->weaponframe = from->weaponframe;
}

/*
==============
CL_PredictUsercmd
==============
*/
void
CL_PredictUsercmd(const player_state_t *from, player_state_t *to,
		  const usercmd_t *cmd, const physent_stack_t *pestack,
		  qboolean spectator)
{
    /* split up very long moves */
    if (cmd->msec > 50) {
	player_state_t temp;
	usercmd_t split;

	split = *cmd;
	split.msec /= 2;

	CL_PredictUsercmd(from, &temp, &split, pestack, spectator);
	CL_PredictUsercmd(&temp, to, &split, pestack, spectator);
	return;
    }
    CL_PlayerMove(from, to, cmd, pestack, spectator);
}

/*
==============
CL_PredictMove
==============
*/
void
CL_PredictMove(physent_stack_t *pestack)
{
    int i;
    float fraction;
    const frame_t *from;
    const player_state_t *fromstate;
    frame_t *to;
    player_state_t *tostate;
    int oldphysent;

    if (cl_pushlatency.value > 0)
	Cvar_Set("pushlatency", "0");

    if (cl.paused)
	return;

    cl.time = realtime - cls.latency - cl_pushlatency.value * 0.001;
    if (cl.time > realtime)
	cl.time = realtime;

    if (cl.intermission)
	return;

    if (!cl.validsequence)
	return;

    if (cls.netchan.outgoing_sequence - cls.netchan.incoming_sequence >=
	UPDATE_BACKUP - 1)
	return;

    VectorCopy(cl.viewangles, cl.simangles);

    /* this is the last frame received from the server */
    from = &cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK];
    fromstate = &from->playerstate[cl.playernum];

    /* we can now render a frame */
    if (cls.state == ca_onserver) {
	/* first update is the final signon stage */
	char text[1024];

	cls.state = ca_active;
	snprintf(text, sizeof(text), "QuakeWorld: %s", cls.servername);
#ifdef _WIN32
	SetWindowText(mainwindow, text);
#endif
    }

    if (cl_nopred.value) {
	VectorCopy(fromstate->velocity, cl.simvel);
	VectorCopy(fromstate->origin, cl.simorg);
	return;
    }

    /* predict forward until cl.time <= to->senttime */
    to = NULL;
    tostate = NULL;
    oldphysent = pestack->numphysent;
    CL_SetSolidPlayers(pestack, cl.playernum);

    for (i = 1; i < UPDATE_BACKUP - 1 && cls.netchan.incoming_sequence + i <
	 cls.netchan.outgoing_sequence; i++) {
	to = &cl.frames[(cls.netchan.incoming_sequence + i) & UPDATE_MASK];
	tostate = &to->playerstate[cl.playernum];
	CL_PredictUsercmd(fromstate, tostate, &to->cmd, pestack, cl.spectator);
	if (to->senttime >= cl.time)
	    break;
	from = to;
	fromstate = tostate;
    }
    pestack->numphysent = oldphysent;

    /* bail if net hasn't delivered packets in a long time... */
    if (i == UPDATE_BACKUP - 1 || !to || !tostate)
	return;

    for (i = 0; i < 3; i++) {
	if (fabs(fromstate->origin[i] - tostate->origin[i]) > 128) {
	    /* teleported, so don't lerp */
	    VectorCopy(tostate->velocity, cl.simvel);
	    VectorCopy(tostate->origin, cl.simorg);
	    return;
	}
    }

    /* interpolate some fraction of the final frame */
    if (to->senttime == from->senttime) {
	fraction = 0;
    } else {
	fraction = (cl.time - from->senttime) / (to->senttime - from->senttime);
	if (fraction < 0)
	    fraction = 0;
	else if (fraction > 1)
	    fraction = 1;
    }
    VectorSubtract(tostate->origin, fromstate->origin, cl.simorg);
    VectorMA(fromstate->origin, fraction, cl.simorg, cl.simorg);
    VectorSubtract(tostate->velocity, fromstate->velocity, cl.simvel);
    VectorMA(fromstate->velocity, fraction, cl.simvel, cl.simvel);
}


/*
==============
CL_InitPrediction
==============
*/
void
CL_InitPrediction(void)
{
    Cvar_RegisterVariable(&cl_pushlatency);
    Cvar_RegisterVariable(&cl_nopred);
}
