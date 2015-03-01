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
// sv_user.c -- server code for moving users

#include "client.h"
#include "cmd.h"
#include "console.h"
#include "host.h"
#include "keys.h"
#include "net.h"
#include "progs.h"
#include "protocol.h"
#include "quakedef.h"
#include "server.h"
#include "sys.h"
#include "view.h"
#include "world.h"

cvar_t sv_idealpitchscale = { "sv_idealpitchscale", "0.8" };
cvar_t sv_edgefriction = { "edgefriction", "2" };

/*
===============
SV_SetIdealPitch
===============
*/
#define	MAX_FORWARD	6
void
SV_SetIdealPitch(edict_t *player)
{
    float angleval, sinval, cosval;
    trace_t trace;
    vec3_t top, bottom;
    float z[MAX_FORWARD];
    int i, j;
    int step, dir, steps;

    if (!((int)player->v.flags & FL_ONGROUND))
	return;

    angleval = player->v.angles[YAW] * M_PI * 2 / 360;
    sinval = sin(angleval);
    cosval = cos(angleval);

    for (i = 0; i < MAX_FORWARD; i++) {
	top[0] = player->v.origin[0] + cosval * (i + 3) * 12;
	top[1] = player->v.origin[1] + sinval * (i + 3) * 12;
	top[2] = player->v.origin[2] + player->v.view_ofs[2];

	bottom[0] = top[0];
	bottom[1] = top[1];
	bottom[2] = top[2] - 160;

	SV_TraceLine(top, bottom, MOVE_NOMONSTERS, player, &trace);
	if (trace.allsolid)
	    return;		// looking at a wall, leave ideal the way is was

	if (trace.fraction == 1)
	    return;		// near a dropoff

	z[i] = top[2] + trace.fraction * (bottom[2] - top[2]);
    }

    dir = 0;
    steps = 0;
    for (j = 1; j < i; j++) {
	step = z[j] - z[j - 1];
	if (step > -ON_EPSILON && step < ON_EPSILON)
	    continue;

	if (dir && (step - dir > ON_EPSILON || step - dir < -ON_EPSILON))
	    return;		// mixed changes

	steps++;
	dir = step;
    }

    if (!dir) {
	player->v.idealpitch = 0;
	return;
    }

    if (steps < 2)
	return;
    player->v.idealpitch = -dir * sv_idealpitchscale.value;
}


/*
==================
SV_UserFriction

==================
*/
static void
SV_UserFriction(edict_t *player)
{
    const vec_t *velocity = player->v.velocity;
    const vec_t *origin = player->v.origin;
    vec_t speed, newspeed, control, friction;
    vec3_t start, stop;
    trace_t trace;

    speed = sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    if (!speed)
	return;

    /* if the leading edge is over a dropoff, increase friction */
    start[0] = stop[0] = origin[0] + velocity[0] / speed * 16;
    start[1] = stop[1] = origin[1] + velocity[1] / speed * 16;
    start[2] = origin[2] + player->v.mins[2];
    stop[2] = start[2] - 34;

    SV_TraceLine(start, stop, MOVE_NOMONSTERS, player, &trace);
    if (trace.fraction == 1.0)
	friction = sv_friction.value * sv_edgefriction.value;
    else
	friction = sv_friction.value;

    /* apply friction */
    control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
    newspeed = speed - host_frametime * control * friction;

    if (newspeed < 0)
	newspeed = 0;
    newspeed /= speed;

    VectorScale(velocity, newspeed, player->v.velocity);
}

/*
==============
SV_Accelerate
==============
*/
cvar_t sv_maxspeed = { "sv_maxspeed", "320", false, true };
cvar_t sv_accelerate = { "sv_accelerate", "10" };

static void
SV_Accelerate(const vec3_t wishdir, const vec_t wishspeed, vec3_t velocity)
{
    int i;
    float addspeed, accelspeed, currentspeed;

    currentspeed = DotProduct(velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0)
	return;
    accelspeed = sv_accelerate.value * host_frametime * wishspeed;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    for (i = 0; i < 3; i++)
	velocity[i] += accelspeed * wishdir[i];
}

static void
SV_AirAccelerate(vec3_t wishveloc, const vec_t wishspeed, vec3_t velocity)
{
    int i;
    float addspeed, wishspd, accelspeed, currentspeed;

    wishspd = VectorNormalize(wishveloc);
    if (wishspd > 30)
	wishspd = 30;
    currentspeed = DotProduct(velocity, wishveloc);
    addspeed = wishspd - currentspeed;
    if (addspeed <= 0)
	return;
    accelspeed = sv_accelerate.value * wishspeed * host_frametime;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    for (i = 0; i < 3; i++)
	velocity[i] += accelspeed * wishveloc[i];
}


static void
DropPunchAngle(vec3_t punchangle)
{
    vec_t length;

    length = VectorNormalize(punchangle);
    length -= 10 * host_frametime;
    if (length < 0)
	length = 0;
    VectorScale(punchangle, length, punchangle);
}

/*
===================
SV_WaterMove

===================
*/
static void
SV_WaterMove(const usercmd_t *cmd, edict_t *player)
{
    int i;
    vec3_t wishvel, forward, right, up;
    float speed, newspeed, wishspeed, addspeed, accelspeed;

    /* user intentions */
    AngleVectors(player->v.v_angle, forward, right, up);
    for (i = 0; i < 3; i++)
	wishvel[i] = forward[i] * cmd->forwardmove + right[i] * cmd->sidemove;

    if (!cmd->forwardmove && !cmd->sidemove && !cmd->upmove)
	wishvel[2] -= 60;	/* drift towards bottom */
    else
	wishvel[2] += cmd->upmove;

    wishspeed = Length(wishvel);
    if (wishspeed > sv_maxspeed.value) {
	VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
	wishspeed = sv_maxspeed.value;
    }
    wishspeed *= 0.7;

    /* water friction */
    speed = Length(player->v.velocity);
    if (speed) {
	newspeed = speed - host_frametime * speed * sv_friction.value;
	if (newspeed < 0)
	    newspeed = 0;
	VectorScale(player->v.velocity, newspeed / speed, player->v.velocity);
    } else
	newspeed = 0;

    /* water acceleration */
    if (!wishspeed)
	return;

    addspeed = wishspeed - newspeed;
    if (addspeed <= 0)
	return;

    VectorNormalize(wishvel);
    accelspeed = sv_accelerate.value * wishspeed * host_frametime;
    if (accelspeed > addspeed)
	accelspeed = addspeed;

    VectorMA(player->v.velocity, accelspeed, wishvel, player->v.velocity);
}

static void
SV_WaterJump(edict_t *player)
{
    if (sv.time > player->v.teleport_time || !player->v.waterlevel) {
	player->v.flags = (int)player->v.flags & ~FL_WATERJUMP;
	player->v.teleport_time = 0;
    }
    player->v.velocity[0] = player->v.movedir[0];
    player->v.velocity[1] = player->v.movedir[1];
}


/*
===================
SV_AirMove

===================
*/
static void
SV_AirMove(const usercmd_t *cmd, edict_t *player)
{
    qboolean onground = (int)player->v.flags & FL_ONGROUND;

    vec3_t wishvel, wishdir, forward, right, up;
    vec_t wishspeed, fmove, smove;
    int i;

    AngleVectors(player->v.angles, forward, right, up);

    fmove = cmd->forwardmove;
    smove = cmd->sidemove;

    /* hack to not let you back into teleporter */
    if (sv.time < player->v.teleport_time && fmove < 0)
	fmove = 0;

    for (i = 0; i < 3; i++)
	wishvel[i] = forward[i] * fmove + right[i] * smove;

    if ((int)player->v.movetype != MOVETYPE_WALK)
	wishvel[2] = cmd->upmove;
    else
	wishvel[2] = 0;

    VectorCopy(wishvel, wishdir);
    wishspeed = VectorNormalize(wishdir);
    if (wishspeed > sv_maxspeed.value) {
	VectorScale(wishvel, sv_maxspeed.value / wishspeed, wishvel);
	wishspeed = sv_maxspeed.value;
    }

    if (player->v.movetype == MOVETYPE_NOCLIP) {
	VectorCopy(wishvel, player->v.velocity);
    } else if (onground) {
	SV_UserFriction(player);
	SV_Accelerate(wishdir, wishspeed, player->v.velocity);
    } else {
	/* not on ground, so little effect on velocity */
	SV_AirAccelerate(wishvel, wishspeed, player->v.velocity);
    }
}

/*
===================
SV_ClientThink

the move fields specify an intended velocity in pix/sec
the angle fields specify an exact angular motion in degrees
===================
*/
static void
SV_ClientThink(client_t *client)
{
    edict_t *player = client->edict;
    vec3_t v_angle;

    if (player->v.movetype == MOVETYPE_NONE)
	return;

    DropPunchAngle(player->v.punchangle);

    /* if dead, behave differently */
    if (player->v.health <= 0)
	return;

    /* angles - show 1/3 the pitch angle and all the roll angle */
    VectorAdd(player->v.v_angle, player->v.punchangle, v_angle);
    player->v.angles[ROLL] = V_CalcRoll(player->v.angles, player->v.velocity) * 4;
    if (!player->v.fixangle) {
	player->v.angles[PITCH] = -v_angle[PITCH] / 3;
	player->v.angles[YAW] = v_angle[YAW];
    }

    if ((int)player->v.flags & FL_WATERJUMP) {
	SV_WaterJump(player);
	return;
    }

    /* walk */
    if (player->v.waterlevel >= 2 && player->v.movetype != MOVETYPE_NOCLIP) {
	SV_WaterMove(&client->cmd, player);
	return;
    }

    SV_AirMove(&client->cmd, player);
}


/*
===================
SV_ReadClientMove
===================
*/
static void
SV_ReadClientMove(client_t *client, usercmd_t *move)
{
    edict_t *player = client->edict;
    int i, ping, buttonbits, impulse;

    /* read ping time */
    ping = client->num_pings % NUM_PING_TIMES;
    client->ping_times[ping] = sv.time - MSG_ReadFloat();
    client->num_pings++;

    /* read current angles */
    for (i = 0; i < 3; i++) {
	if (sv.protocol == PROTOCOL_VERSION_FITZ)
	    player->v.v_angle[i] = MSG_ReadAngle16();
	else
	    player->v.v_angle[i] = MSG_ReadAngle();
    }

    /* read movement */
    move->forwardmove = MSG_ReadShort();
    move->sidemove = MSG_ReadShort();
    move->upmove = MSG_ReadShort();

    /* read buttons */
    buttonbits = MSG_ReadByte();
    player->v.button0 = buttonbits & 1;
    player->v.button2 = (buttonbits & 2) >> 1;

    impulse = MSG_ReadByte();
    if (impulse)
	player->v.impulse = impulse;
}

/*
 * ------------------------------------------------------------------------
 * CLIENT COMMANDS
 * ------------------------------------------------------------------------
 */

/*
======================
SV_Name_f
======================
*/
static void
SV_Name_f(client_t *client)
{
    char new_name[16];
    const char *arg;

    if (Cmd_Argc() == 1)
	return;

    /* See if the name has changed */
    arg = (Cmd_Argc() == 2) ? Cmd_Argv(1) : Cmd_Args();
    snprintf(new_name, sizeof(new_name), "%s", arg);
    if (!strcmp(client->name, new_name))
	return;

    if (client->name[0] && strcmp(client->name, "unconnected"))
	Con_Printf("%s renamed to %s\n", client->name, new_name);
    strcpy(client->name, new_name);
    client->edict->v.netname = PR_SetString(client->name);

    /* send notification to all clients */
    MSG_WriteByte(&sv.reliable_datagram, svc_updatename);
    MSG_WriteByte(&sv.reliable_datagram, client - svs.clients);
    MSG_WriteString(&sv.reliable_datagram, client->name);
}

/*
==================
SV_Color_f
==================
*/
static void
SV_Color_f(client_t *client)
{
    int top, bottom;

    if (Cmd_Argc() == 1) {
	top = client->colors >> 4;
	bottom = client->colors & 15;
	SV_ClientPrintf(client,
			"\"color\" is \"%d %d\"\n"
			"color <0-13> [0-13]\n", top, bottom);
	return;
    }

    if (Cmd_Argc() == 2)
	top = bottom = atoi(Cmd_Argv(1));
    else {
	top = atoi(Cmd_Argv(1));
	bottom = atoi(Cmd_Argv(2));
    }
    top &= 15;
    if (top > 13)
	top = 13;
    bottom &= 15;
    if (bottom > 13)
	bottom = 13;

    client->colors = top * 16 + bottom;
    client->edict->v.team = bottom + 1;

    /* send notification to all clients */
    MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors);
    MSG_WriteByte(&sv.reliable_datagram, client - svs.clients);
    MSG_WriteByte(&sv.reliable_datagram, client->colors);
}

/*
==================
SV_Status_f
==================
*/
static void
SV_Status_f(client_t *client)
{
    client_t *other;
    int seconds;
    int minutes;
    int hours = 0;
    int i;

    SV_ClientPrintf(client, "host:    %s\n", Cvar_VariableString("hostname"));
    SV_ClientPrintf(client, "version: TyrQuake-%s\n", stringify(TYR_VERSION));
    if (tcpipAvailable)
	SV_ClientPrintf(client, "tcp/ip:  %s\n", my_tcpip_address);
    SV_ClientPrintf(client, "map:     %s\n", sv.name);
    SV_ClientPrintf(client, "players: %i active (%i max)\n\n",
		    net_activeconnections, svs.maxclients);

    other = svs.clients;
    for (i = 0; i < svs.maxclients; i++, other++) {
	if (!other->active)
	    continue;

	seconds = (int)(net_time - other->netconnection->connecttime);
	minutes = seconds / 60;
	seconds -= (minutes * 60);
	hours = minutes / 60;
	minutes -= (hours * 60);
	SV_ClientPrintf(client, "#%-2u %-16.16s  %3i  %2i:%02i:%02i\n",
			i + 1, client->name, (int)client->edict->v.frags,
			hours, minutes, seconds);
	SV_ClientPrintf(client, "   %s\n", client->netconnection->address);
    }
}

/*
==================
SV_God_f

Sets client to godmode
==================
*/
static void
SV_God_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    player->v.flags = (int)player->v.flags ^ FL_GODMODE;
    if (!((int)player->v.flags & FL_GODMODE))
	SV_ClientPrintf(client, "godmode OFF\n");
    else
	SV_ClientPrintf(client, "godmode ON\n");
}

/*
==================
SV_Fly_f

Sets client to flymode
==================
*/
static void
SV_Fly_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    if (player->v.movetype != MOVETYPE_FLY) {
	player->v.movetype = MOVETYPE_FLY;
	SV_ClientPrintf(client, "flymode ON\n");
    } else {
	player->v.movetype = MOVETYPE_WALK;
	SV_ClientPrintf(client, "flymode OFF\n");
    }
}

/*
==================
SV_Noclip_f

Sets client to noclip mode
==================
*/
static void
SV_Noclip_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    if (player->v.movetype != MOVETYPE_NOCLIP) {
	noclip_anglehack = true;
	player->v.movetype = MOVETYPE_NOCLIP;
	SV_ClientPrintf(client, "noclip ON\n");
    } else {
	noclip_anglehack = false;
	player->v.movetype = MOVETYPE_WALK;
	SV_ClientPrintf(client, "noclip OFF\n");
    }
}

/*
==================
SV_Notarget_f

Sets client to notarget mode
==================
*/
static void
SV_Notarget_f(client_t *client)
{
    edict_t *player;

    if (pr_global_struct->deathmatch)
	return;

    player = client->edict;
    player->v.flags = (int)player->v.flags ^ FL_NOTARGET;
    if (!((int)player->v.flags & FL_NOTARGET))
	SV_ClientPrintf(client, "notarget OFF\n");
    else
	SV_ClientPrintf(client, "notarget ON\n");
}

/*
==================
SV_Ping_f

==================
*/
static void
SV_Ping_f(client_t *client)
{
    int i, j;
    float total;
    client_t *other;

    SV_ClientPrintf(client, "Client ping times:\n");
    other = svs.clients;
    for (i = 0; i < svs.maxclients; i++, other++) {
	if (!other->active)
	    continue;
	total = 0;
	for (j = 0; j < NUM_PING_TIMES; j++)
	    total += other->ping_times[j];
	total /= NUM_PING_TIMES;
	SV_ClientPrintf(client, "%4i %s\n", (int)(total * 1000), other->name);
    }
}

static qboolean
SV_Give_Hipnotic(edict_t *player, char item, char extra)
{
    switch (item) {
    case '0':
	player->v.items = (int)player->v.items | HIT_MJOLNIR;
	return true;
    case '9':
	player->v.items = (int)player->v.items | HIT_LASER_CANNON;
	return true;
    case '6':
	if (extra == 'a') {
	    player->v.items = (int)player->v.items | HIT_PROXIMITY_GUN;
	    return true;
	}
    }
    return false;
}

static qboolean
SV_Give_Rogue(edict_t *player, char item, int amount)
{
    eval_t *ammofield;
    const char *ammo;

    switch (item) {
    case 's': ammo = "ammo_shells1";       break;
    case 'n': ammo = "ammo_nails1";        break;
    case 'l': ammo = "ammo_lava_nails";    break;
    case 'r': ammo = "ammo_rockets1";      break;
    case 'm': ammo = "ammo_multi_rockets"; break;
    case 'c': ammo = "ammo_cells1";        break;
    case 'p': ammo = "ammo_plasma";        break;
    default:
	return false;
    }

    ammofield = GetEdictFieldValue(player, ammo);
    if (ammofield) {
	ammofield->_float = amount;
	switch (item) {
	case 'n':
	    if (player->v.weapon <= IT_LIGHTNING)
		player->v.ammo_nails = amount;
	    break;
	case 'l':
	    if (player->v.weapon > IT_LIGHTNING)
		player->v.ammo_nails = amount;
	    break;
	case 'r':
	    if (player->v.weapon <= IT_LIGHTNING)
		player->v.ammo_rockets = amount;
	    break;
	case 'm':
	    if (player->v.weapon > IT_LIGHTNING)
		player->v.ammo_rockets = amount;
	    break;
	case 'c':
	    if (player->v.weapon <= IT_LIGHTNING)
		player->v.ammo_cells = amount;
	    break;
	case 'p':
	    if (player->v.weapon > IT_LIGHTNING)
		player->v.ammo_cells = amount;
	    break;
	}
	return true;
    }
    return false;
}

/*
==================
SV_Give_f
==================
*/
static void
SV_Give_f(client_t *client)
{
    edict_t *player;
    char item, extra;
    int amount;

    if (pr_global_struct->deathmatch)
	return;

    item = Cmd_Argv(1)[0];
    extra = item ? Cmd_Argv(1)[1] : 0;
    amount = atoi(Cmd_Argv(2));
    player = client->edict;

    /* handle Hipnotic/Rogue specific extensions */
    if (hipnotic && SV_Give_Hipnotic(player, item, extra))
	return;
    if (rogue && SV_Give_Rogue(player, item, amount))
	return;

    /* Standard id items */
    switch (item) {
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
	player->v.items = (int)player->v.items | IT_SHOTGUN << (item - '2');
	break;
    case 's':
	player->v.ammo_shells = amount;
	break;
    case 'n':
	player->v.ammo_nails = amount;
	break;
    case 'r':
	player->v.ammo_rockets = amount;
	break;
    case 'h':
	player->v.health = amount;
	break;
    case 'c':
	player->v.ammo_cells = amount;
	break;
    }
}

/*
==================
SV_Kill_f
==================
*/
static void
SV_Kill_f(client_t *client)
{
    edict_t *player = client->edict;

    if (player->v.health <= 0) {
	SV_ClientPrintf(client, "Can't suicide -- already dead!\n");
	return;
    }

    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(player);
    PR_ExecuteProgram(pr_global_struct->ClientKill);
}

/*
==================
SV_Pause_f
==================
*/
static void
SV_Pause_f(client_t *client)
{
    edict_t *player;

    if (!pausable.value) {
	SV_ClientPrintf(client, "Pause not allowed.\n");
	return;
    }

    player = client->edict;
    sv.paused ^= 1;
    if (sv.paused)
	SV_BroadcastPrintf("%s paused the game\n",
			   PR_GetString(player->v.netname));
    else
	SV_BroadcastPrintf("%s unpaused the game\n",
			   PR_GetString(player->v.netname));

    // send notification to all clients
    MSG_WriteByte(&sv.reliable_datagram, svc_setpause);
    MSG_WriteByte(&sv.reliable_datagram, sv.paused);
}

/* ------------------------------------------------------------------------ */

/*
==================
SV_PreSpawn_f
==================
*/
static void
SV_PreSpawn_f(client_t *client)
{
    if (client->spawned) {
	SV_ClientPrintf(client, "prespawn not valid -- already spawned\n");
	return;
    }
    SZ_Write(&client->message, sv.signon.data, sv.signon.cursize);
    MSG_WriteByte(&client->message, svc_signonnum);
    MSG_WriteByte(&client->message, 2);
    client->sendsignon = true;
}

/*
==================
SV_Spawn_f
==================
*/
static void
SV_Spawn_f(client_t *client)
{
    client_t *other;
    edict_t *player;
    int i;

    if (client->spawned) {
	SV_ClientPrintf(client, "spawn not valid -- already spawned\n");
	return;
    }

    if (sv.loadgame) {
	/* Loaded games are fully inited, make sure to unpause */
	sv.paused = false;
    } else {
	/* run the entrance script */
	player = client->edict;

	memset(&player->v, 0, progs->entityfields * 4);
	player->v.colormap = NUM_FOR_EDICT(player);
	player->v.team = (client->colors & 15) + 1;
	player->v.netname = PR_SetString(client->name);

	/* copy spawn parms out of the client */
	for (i = 0; i < NUM_SPAWN_PARMS; i++)
	    (&pr_global_struct->parm1)[i] = client->spawn_parms[i];

	/* call the spawn function */
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(player);
	PR_ExecuteProgram(pr_global_struct->ClientConnect);

	if ((Sys_DoubleTime() - client->netconnection->connecttime) <= sv.time)
	    Sys_Printf("%s entered the game\n", client->name);

	PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    }

    /* send all current names, colors, and frag counts */
    SZ_Clear(&client->message);

    /* send time of update */
    MSG_WriteByte(&client->message, svc_time);
    MSG_WriteFloat(&client->message, sv.time);

    other = svs.clients;
    for (i = 0; i < svs.maxclients; i++, other++) {
	MSG_WriteByte(&client->message, svc_updatename);
	MSG_WriteByte(&client->message, i);
	MSG_WriteString(&client->message, other->name);
	MSG_WriteByte(&client->message, svc_updatefrags);
	MSG_WriteByte(&client->message, i);
	MSG_WriteShort(&client->message, other->old_frags);
	MSG_WriteByte(&client->message, svc_updatecolors);
	MSG_WriteByte(&client->message, i);
	MSG_WriteByte(&client->message, other->colors);
    }

    /* send all current light styles */
    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
	MSG_WriteByte(&client->message, svc_lightstyle);
	MSG_WriteByte(&client->message, (char)i);
	MSG_WriteString(&client->message, sv.lightstyles[i]);
    }

    /* send some stats */
    MSG_WriteByte(&client->message, svc_updatestat);
    MSG_WriteByte(&client->message, STAT_TOTALSECRETS);
    MSG_WriteLong(&client->message, pr_global_struct->total_secrets);

    MSG_WriteByte(&client->message, svc_updatestat);
    MSG_WriteByte(&client->message, STAT_TOTALMONSTERS);
    MSG_WriteLong(&client->message, pr_global_struct->total_monsters);

    MSG_WriteByte(&client->message, svc_updatestat);
    MSG_WriteByte(&client->message, STAT_SECRETS);
    MSG_WriteLong(&client->message, pr_global_struct->found_secrets);

    MSG_WriteByte(&client->message, svc_updatestat);
    MSG_WriteByte(&client->message, STAT_MONSTERS);
    MSG_WriteLong(&client->message, pr_global_struct->killed_monsters);

    /*
     * Send a fixangle - never send a roll angle, because savegames
     * can catch the server in a state where it is expecting the
     * client to correct the angle and it won't happen if the game was
     * just loaded, so you wind up with a permanent head tilt
     */
    player = EDICT_NUM(1 + (client - svs.clients));
    MSG_WriteByte(&client->message, svc_setangle);
    for (i = 0; i < 2; i++)
	MSG_WriteAngle(&client->message, player->v.angles[i]);
    MSG_WriteAngle(&client->message, 0);

    SV_WriteClientdataToMessage(player, &client->message);

    MSG_WriteByte(&client->message, svc_signonnum);
    MSG_WriteByte(&client->message, 3);
    client->sendsignon = true;
}

/*
==================
SV_Begin_f
==================
*/
static void
SV_Begin_f(client_t *client)
{
    client->spawned = true;
}

/* ------------------------------------------------------------------------ */

/*
==================
SV_Kick_f

Kicks a user off of the server
==================
*/
static void
SV_Kick_f(client_t *client)
{
    const char *message = NULL;
    const char *who;
    client_t *victim;
    int i, clientnum;

    if (pr_global_struct->deathmatch)
	return;

    if (Cmd_Argc() > 2 && strcmp(Cmd_Argv(1), "#") == 0) {
	clientnum = Q_atof(Cmd_Argv(2)) - 1;
	if (clientnum < 0 || clientnum >= svs.maxclients)
	    return;
	victim = svs.clients + clientnum;
	if (!victim->active)
	    return;
    } else {
	victim = svs.clients;
	for (i = 0; i < svs.maxclients; i++, victim++) {
	    if (!victim->active)
		continue;
	    if (!strcasecmp(victim->name, Cmd_Argv(1)))
		break;
	}
	if (i == svs.maxclients)
	    return;
    }

    /* can't kick yourself! */
    if (victim == client)
	return;

    if (Cmd_Argc() > 2) {
	message = COM_Parse(Cmd_Args());
	if (*message == '#') {
	    message++;
	    while (*message == ' ')
		message++;
	    message += strlen(Cmd_Argv(2));
	}
	while (*message && *message == ' ')
	    message++;
    }

    who = (cls.state == ca_dedicated) ? "Console" : client->name;

    if (message)
	SV_ClientPrintf(victim, "Kicked by %s: %s\n", who, message);
    else
	SV_ClientPrintf(victim, "Kicked by %s\n", who);
    SV_DropClient(victim, false);
}

static void
SV_Say(client_t *client, qboolean teamonly)
{
    client_t *recipient;
    int i;
    size_t len, space;
    const char *msg;
    char text[64];
    qboolean fromServer = false;

    if (Cmd_Argc() < 2)
	return;

    if (cls.state == ca_dedicated) {
	fromServer = true;
	teamonly = false;
    }

    /* turn on color set 1 */
    if (!fromServer)
	snprintf(text, sizeof(text), "%c%s: ", 1, client->name);
    else
	snprintf(text, sizeof(text), "%c<%s> ", 1, hostname.string);

    len = strlen(text);
    space = sizeof(text) - len - 2; /* -2 for \n and null terminator */
    msg = Cmd_Args();
    if (*msg == '"') {
	/* remove quotes */
	strncat(text, msg + 1, qmin(strlen(msg) - 2, space));
	text[len + qmin(strlen(msg) - 2, space)] = 0;
    } else {
	strncat(text, msg, space);
	text[len + qmin(strlen(msg), space)] = 0;
    }
    strcat(text, "\n");

    recipient = svs.clients;
    for (i = 0; i < svs.maxclients; i++, recipient++) {
	if (!recipient->active || !recipient->spawned)
	    continue;
	if (teamplay.value && teamonly
	    && client->edict->v.team != recipient->edict->v.team)
	    continue;

	SV_ClientPrintf(recipient, "%s", text);
    }
    Sys_Printf("%s", &text[1]);
}

static void
SV_Say_f(client_t *client)
{
    SV_Say(client, false);
}


static void
SV_Say_Team_f(client_t *client)
{
    SV_Say(client, true);
}

static void
SV_Tell_f(client_t *client)
{
    client_t *recipient;
    int i, len, space;
    const char *msg;
    char text[64];

    if (Cmd_Argc() < 3)
	return;

    snprintf(text, sizeof(text), "%s: ", client->name);

    len = strlen(text);
    space = sizeof(text) - len - 2; /* -2 for \n and null terminator */
    msg = Cmd_Args();
    if (*msg == '"') {
	/* remove quotes */
	strncat(text, msg + 1, qmin((int)strlen(msg) - 2, space));
	text[len + qmin((int)strlen(msg) - 2, space)] = 0;
    } else {
	strncat(text, msg, space);
	text[len + qmin((int)strlen(msg), space)] = 0;
    }
    strcat(text, "\n");

    recipient = svs.clients;
    for (i = 0; i < svs.maxclients; i++, recipient++) {
	if (!recipient->active || !recipient->spawned)
	    continue;
	if (strcasecmp(recipient->name, Cmd_Argv(1)))
	    continue;
	SV_ClientPrintf(recipient, "%s", text);
	break;
    }
}

/* ------------------------------------------------------------------------ */

typedef struct {
    const char *name;
    void (*func)(client_t *client);
} client_command_t;

static client_command_t client_commands[] = {
    { "name", SV_Name_f },
    { "color", SV_Color_f },
    { "status", SV_Status_f },
    { "say", SV_Say_f },
    { "say_team", SV_Say_Team_f },
    { "tell", SV_Tell_f },
    { "god", SV_God_f },
    { "fly", SV_Fly_f },
    { "noclip", SV_Noclip_f },
    { "notarget", SV_Notarget_f },
    { "give", SV_Give_f },
    { "ping", SV_Ping_f },
    { "kill", SV_Kill_f },
    { "pause", SV_Pause_f },
    { "kick", SV_Kick_f },
    { "ban", NET_Ban_f },
    { "prespawn", SV_PreSpawn_f },
    { "spawn", SV_Spawn_f },
    { "begin", SV_Begin_f },
    { NULL, NULL },
};

static void
SV_ExecuteClientCommand(const char *command_string, client_t *client)
{
    client_command_t *command;

    // TODO: begin/end redirect like QW?
    Cmd_TokenizeString(command_string);
    for (command = client_commands; command->name; command++) {
	if (!strcmp(Cmd_Argv(0), command->name)) {
	    command->func(client);
	    return;
	}
    }
}

/*
===================
SV_ReadClientMessage

Returns false if the client should be killed
===================
*/
static qboolean
SV_ReadClientMessage(client_t *client)
{
    const char *message;
    int ret, cmd;

    do {
      nextmsg:
	ret = NET_GetMessage(client->netconnection);
	if (ret == -1) {
	    Sys_Printf("%s: NET_GetMessage failed\n", __func__);
	    return false;
	}
	if (!ret)
	    return true;

	MSG_BeginReading();

	while (1) {
	    if (!client->active)
		return false;	// a command caused an error

	    if (msg_badread) {
		Sys_Printf("%s: badread\n", __func__);
		return false;
	    }

	    cmd = MSG_ReadChar();

	    switch (cmd) {
	    case -1:
		goto nextmsg;	// end of message

	    default:
		Sys_Printf("%s: unknown command char\n", __func__);
		return false;

	    case clc_nop:
		//Sys_Printf ("clc_nop\n");
		break;

	    case clc_stringcmd:
		message = MSG_ReadString();
		SV_ExecuteClientCommand(message, client);
		break;

	    case clc_disconnect:
		//Sys_Printf ("%s: client disconnected\n", __func__);
		return false;

	    case clc_move:
		SV_ReadClientMove(client, &client->cmd);
		break;
	    }
	}
    } while (ret == 1);

    return true;
}


/*
==================
SV_RunClients
==================
*/
void
SV_RunClients(void)
{
    client_t *client;
    int i;

    client = svs.clients;
    for (i = 0; i < svs.maxclients; i++, client++) {
	if (!client->active)
	    continue;

	if (!SV_ReadClientMessage(client)) {
	    /* client misbehaved... */
	    SV_DropClient(client, false);
	    continue;
	}

	if (!client->spawned) {
	    /* clear client movement until a new packet is received */
	    memset(&client->cmd, 0, sizeof(client->cmd));
	    continue;
	}

	/* always pause in single player if in console or menus */
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
	    SV_ClientThink(client);
    }
}
