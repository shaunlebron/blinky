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

/*
 * snd_dma.c -- main control for any streaming sound output device
 */

#include "bspfile.h"
#include "client.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "input.h"
#include "quakedef.h"
#include "sound.h"
#include "sys.h"

#ifdef GLQUAKE
#include "gl_model.h"
#else
#include "model.h"
#endif

#ifdef NQ_HACK
#include "host.h"
#endif

/* FIXME - reorder to remove forward decls? */
static void S_Play(void);
static void S_PlayVol(void);
static void S_SoundList(void);
static void S_Update_();
static void S_StopAllSoundsC(void);

/*
 * Internal sound data & structures
 */

channel_t channels[MAX_CHANNELS];
int total_channels;

int snd_blocked = 0;
static qboolean snd_ambient = 1;
static qboolean snd_initialized = false;

/* pointer should go away (JC?) */
volatile dma_t *shm = 0;
volatile dma_t sn;

static vec3_t listener_origin;
static vec3_t listener_forward;
static vec3_t listener_right;
static vec3_t listener_up;
static vec_t sound_nominal_clip_dist = 1000.0;

static int soundtime;		/* sample PAIRS */
int paintedtime;		/* sample PAIRS */

#define	MAX_SFX 512
static sfx_t *known_sfx;	/* hunk allocated [MAX_SFX] */
static int num_sfx;

static sfx_t *ambient_sfx[NUM_AMBIENTS];

static int sound_started = 0;

cvar_t volume = { "volume", "0.7", true };
cvar_t loadas8bit = { "loadas8bit", "0" };

static cvar_t nosound = { "nosound", "0" };
static cvar_t precache = { "precache", "1" };
static cvar_t bgmbuffer = { "bgmbuffer", "4096" };
static cvar_t ambient_level = { "ambient_level", "0.3" };
static cvar_t ambient_fade = { "ambient_fade", "100" };
static cvar_t snd_noextraupdate = { "snd_noextraupdate", "0" };
static cvar_t snd_show = { "snd_show", "0" };
static cvar_t _snd_mixahead = { "_snd_mixahead", "0.1", true };

/*
 * User-setable variables
 *
 * Fake dma is a synchronous faking of the DMA progress used for isolating
 * performance in the renderer.
 */

static qboolean fakedma = false;

/* FIXME - unused functions? */
#if 0
static void
S_AmbientOff(void)
{
    snd_ambient = false;
}

static void
S_AmbientOn(void)
{
    snd_ambient = true;
}
#endif

static void
S_SoundInfo_f(void)
{
    if (!sound_started || !shm) {
	Con_Printf("sound system not started\n");
	return;
    }

    Con_Printf("%5d stereo\n", shm->channels - 1);
    Con_Printf("%5d samples\n", shm->samples);
    Con_Printf("%5d samplepos\n", shm->samplepos);
    Con_Printf("%5d samplebits\n", shm->samplebits);
    Con_Printf("%5d submission_chunk\n", shm->submission_chunk);
    Con_Printf("%5d speed\n", shm->speed);
    Con_Printf("%p dma buffer\n", shm->buffer);
    Con_Printf("%5d total_channels\n", total_channels);
}

/*
 * ================
 * S_Startup
 * ================
 */

void
S_Startup(void)
{
    int rc;

    if (!snd_initialized)
	return;
    if (!fakedma) {
	rc = SNDDMA_Init();
	if (!rc) {
	    Con_Printf("%s: SNDDMA_Init failed.\n", __func__);
	    sound_started = 0;
	    return;
	}
    }
    sound_started = 1;
}


/*
 * ================
 * S_Init
 * ================
 */
void
S_Init(void)
{
    Con_Printf("\nSound Initialization\n");

    if (COM_CheckParm("-nosound"))
	return;
    if (COM_CheckParm("-simsound"))
	fakedma = true;

    Cmd_AddCommand("play", S_Play);
    Cmd_AddCommand("playvol", S_PlayVol);
    Cmd_AddCommand("stopsound", S_StopAllSoundsC);
    Cmd_AddCommand("soundlist", S_SoundList);
    Cmd_AddCommand("soundinfo", S_SoundInfo_f);

    Cvar_RegisterVariable(&nosound);
    Cvar_RegisterVariable(&volume);
    Cvar_RegisterVariable(&precache);
    Cvar_RegisterVariable(&loadas8bit);
    Cvar_RegisterVariable(&bgmbuffer);
    Cvar_RegisterVariable(&ambient_level);
    Cvar_RegisterVariable(&ambient_fade);
    Cvar_RegisterVariable(&snd_noextraupdate);
    Cvar_RegisterVariable(&snd_show);
    Cvar_RegisterVariable(&_snd_mixahead);

    if (host_parms.memsize < 0x800000) {
	Cvar_Set("loadas8bit", "1");
	Con_Printf("loading all sounds as 8bit\n");
    }

    snd_initialized = true;

    S_Startup();

    SND_InitScaletable();

    known_sfx = Hunk_AllocName(MAX_SFX * sizeof(sfx_t), "sfx_t");
    num_sfx = 0;

    /* create a piece of DMA memory */
    if (fakedma) {
	shm = (void *)Hunk_AllocName(sizeof(*shm), "shm");
	shm->samplebits = 16;
	shm->speed = 22050;
	shm->channels = 2;
	shm->samples = 32768;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = Hunk_AllocName(1 << 16, "shmbuf");
    }

    if (sound_started)
	Con_Printf("Sound sampling rate: %i\n", shm->speed);

#if 0
    /* provides a tick sound until washed clean; for debugging */
    if (shm->buffer)
	shm->buffer[4] = shm->buffer[5] = 0x7f;
#endif

    ambient_sfx[AMBIENT_WATER] = S_PrecacheSound("ambience/water1.wav");
    ambient_sfx[AMBIENT_SKY] = S_PrecacheSound("ambience/wind2.wav");

    S_StopAllSounds(true);
}


/*
 * Shutdown sound engine
 */
void
S_Shutdown(void)
{

    if (!sound_started)
	return;

    shm = 0;
    sound_started = 0;

    if (!fakedma)
	SNDDMA_Shutdown();
}


/*
 * ==================
 * S_FindName
 * ==================
 */
static sfx_t *
S_FindName(char *name)
{
    int i;
    sfx_t *sfx;

    if (!name)
	Sys_Error("%s: NULL", __func__);
    if (strlen(name) >= MAX_QPATH)
	Sys_Error("%s: name too long: %s", __func__, name);

    /* see if already loaded */
    for (i = 0; i < num_sfx; i++)
	if (!strcmp(known_sfx[i].name, name))
	    return &known_sfx[i];

    if (num_sfx == MAX_SFX)
	Sys_Error("%s: out of sfx_t", __func__);

    sfx = &known_sfx[i];
    strcpy(sfx->name, name);

    num_sfx++;

    return sfx;
}


/*
 * ==================
 * S_TouchSound
 * ==================
 */
void
S_TouchSound(char *name)
{
    sfx_t *sfx;

    if (!sound_started)
	return;

    sfx = S_FindName(name);
    Cache_Check(&sfx->cache);
}

/*
 * ==================
 * S_PrecacheSound
 * ==================
 */
sfx_t *
S_PrecacheSound(char *name)
{
    sfx_t *sfx;

    if (!sound_started || nosound.value)
	return NULL;

    sfx = S_FindName(name);

    /* cache it in */
    if (precache.value)
	S_LoadSound(sfx);

    return sfx;
}


/*
 * =================
 * SND_PickChannel
 * =================
 */
static channel_t *
SND_PickChannel(int entnum, int entchannel)
{
    int i;
    int life_left;
    channel_t *channel;
    channel_t *first_to_die = NULL;

    /* Check for replacement sound, or find the best one to replace */
    life_left = 0x7fffffff;
    for (i = NUM_AMBIENTS; i < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS; i++) {
	channel = &channels[i];
	/*
	 * - channel 0 never overrides
	 * - allways override sound from same entity
	 */
	if (entchannel != 0 && channel->entnum == entnum &&
	    (channel->entchannel == entchannel || entchannel == -1)) {
	    first_to_die = channel;
	    break;
	}
	/* don't let monster sounds override player sounds */
#ifdef NQ_HACK
	if (channel->entnum == cl.viewentity
	    && entnum != cl.viewentity && channel->sfx)
	    continue;
#endif
#ifdef QW_HACK
	if (channel->entnum == cl.playernum + 1
	    && entnum != cl.playernum + 1 && channel->sfx)
	    continue;
#endif
	if (channel->end - paintedtime < life_left) {
	    life_left = channel->end - paintedtime;
	    first_to_die = channel;
	}
    }
    if (first_to_die && first_to_die->sfx)
	first_to_die->sfx = NULL;

    return first_to_die;
}

/*
 * =================
 * SND_Spatialize
 *=================
 */
static void
SND_Spatialize(channel_t *ch)
{
    vec_t dot;
    vec_t dist;
    vec_t lscale, rscale, scale;
    vec3_t source_vec;
    sfx_t *snd;

    /* anything coming from the view entity will allways be full volume */
#ifdef NQ_HACK
    if (ch->entnum == cl.viewentity) {
	ch->leftvol = ch->master_vol;
	ch->rightvol = ch->master_vol;
	return;
    }
#endif
#ifdef QW_HACK
    if (ch->entnum == cl.playernum + 1) {
	ch->leftvol = ch->master_vol;
	ch->rightvol = ch->master_vol;
	return;
    }
#endif

    /* calculate stereo seperation and distance attenuation */
    snd = ch->sfx;
    VectorSubtract(ch->origin, listener_origin, source_vec);
    dist = VectorNormalize(source_vec) * ch->dist_mult;

    if (shm->channels == 1) {
	rscale = 1.0;
	lscale = 1.0;
    } else {
	dot = DotProduct(listener_right, source_vec);
	rscale = 1.0 + dot;
	lscale = 1.0 - dot;
    }

    /* add in distance effect */
    scale = (1.0 - dist) * rscale;
    ch->rightvol = (int)(ch->master_vol * scale);
    if (ch->rightvol < 0)
	ch->rightvol = 0;

    scale = (1.0 - dist) * lscale;
    ch->leftvol = (int)(ch->master_vol * scale);
    if (ch->leftvol < 0)
	ch->leftvol = 0;
}


void
S_StartSound(int entnum, int entchannel, sfx_t *sfx, vec3_t origin,
	     float fvol, float attenuation)
{
    channel_t *target_chan, *check;
    sfxcache_t *sc;
    int vol;
    int ch_idx;
    int skip;

    if (!sound_started)
	return;
    if (!sfx)
	return;
    if (nosound.value)
	return;

    vol = fvol * 255;

    /* pick a channel to play on */
    target_chan = SND_PickChannel(entnum, entchannel);
    if (!target_chan)
	return;

    /* spatialize */
    memset(target_chan, 0, sizeof(*target_chan));
    VectorCopy(origin, target_chan->origin);
    target_chan->dist_mult = attenuation / sound_nominal_clip_dist;
    target_chan->master_vol = vol;
    target_chan->entnum = entnum;
    target_chan->entchannel = entchannel;
    SND_Spatialize(target_chan);

    if (!target_chan->leftvol && !target_chan->rightvol)
	return;			/* not audible at all */

    /* new channel */
    sc = S_LoadSound(sfx);
    if (!sc) {
	target_chan->sfx = NULL;
	return;			/* couldn't load the sound's data */
    }

    target_chan->sfx = sfx;
    target_chan->pos = 0.0;
    target_chan->end = paintedtime + sc->length;

    /*
     * if an identical sound has also been started this frame, offset the pos
     * a bit to keep it from just making the first one louder
     */
    check = &channels[NUM_AMBIENTS];
    for (ch_idx = NUM_AMBIENTS; ch_idx < NUM_AMBIENTS + MAX_DYNAMIC_CHANNELS;
	 ch_idx++, check++) {
	if (check == target_chan)
	    continue;
	if (check->sfx == sfx && !check->pos) {
	    skip = rand() % (int)(0.1 * shm->speed);
	    if (skip >= target_chan->end)
		skip = target_chan->end - 1;
	    target_chan->pos += skip;
	    target_chan->end -= skip;
	    break;
	}
    }
}

void
S_StopSound(int entnum, int entchannel)
{
    int i;

    for (i = 0; i < MAX_DYNAMIC_CHANNELS; i++) {
	if (channels[i].entnum == entnum
	    && channels[i].entchannel == entchannel) {
	    channels[i].end = 0;
	    channels[i].sfx = NULL;
	    return;
	}
    }
}

void
S_StopAllSounds(qboolean clear)
{
    int i;

    if (!sound_started)
	return;

    /* no statics */
    total_channels = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;

    for (i = 0; i < MAX_CHANNELS; i++)
	if (channels[i].sfx)
	    channels[i].sfx = NULL;

    memset(channels, 0, MAX_CHANNELS * sizeof(channel_t));
    if (clear)
	S_ClearBuffer();
}

static void
S_StopAllSoundsC(void)
{
    S_StopAllSounds(true);
}

void
S_ClearBuffer(void)
{
    int err;
    int clear;

    if (!sound_started || !shm)
	return;

    err = SNDDMA_LockBuffer();
    if (err) {
	S_Shutdown();
	return;
    }

    clear = (shm->samplebits == 8) ? 0x80 : 0;
    memset(shm->buffer, clear, shm->samples * shm->samplebits / 8);
    SNDDMA_UnlockBuffer();
}


/*
 * =================
 * S_StaticSound
 * =================
 */
void
S_StaticSound(sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
    channel_t *ss;
    sfxcache_t *sc;

    if (!sfx)
	return;
    if (total_channels == MAX_CHANNELS) {
	Con_Printf("total_channels == MAX_CHANNELS\n");
	return;
    }

    ss = &channels[total_channels];
    total_channels++;

    sc = S_LoadSound(sfx);
    if (!sc)
	return;

    if (sc->loopstart == -1) {
	Con_Printf("Sound %s not looped\n", sfx->name);
	return;
    }

    ss->sfx = sfx;
    VectorCopy(origin, ss->origin);
    ss->master_vol = vol;
    ss->dist_mult = (attenuation / 64) / sound_nominal_clip_dist;
    ss->end = paintedtime + sc->length;

    SND_Spatialize(ss);
}


/*
 * ===================
 * S_UpdateAmbientSounds
 * ===================
 */
static void
S_UpdateAmbientSounds(void)
{
    mleaf_t *l;
    float vol;
    int ambient_channel;
    channel_t *chan;

    if (!snd_ambient)
	return;

    /* calc ambient sound levels */
    if (!cl.worldmodel)
	return;

    l = Mod_PointInLeaf(listener_origin, cl.worldmodel);
    if (!l || !ambient_level.value) {
	for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS;
	     ambient_channel++)
	    channels[ambient_channel].sfx = NULL;
	return;
    }

    for (ambient_channel = 0; ambient_channel < NUM_AMBIENTS;
	 ambient_channel++) {
	chan = &channels[ambient_channel];
	chan->sfx = ambient_sfx[ambient_channel];

	vol = ambient_level.value * l->ambient_sound_level[ambient_channel];
	if (vol < 8)
	    vol = 0;

	/* don't adjust volume too fast */
	if (chan->master_vol < vol) {
	    chan->master_vol += host_frametime * ambient_fade.value;
	    if (chan->master_vol > vol)
		chan->master_vol = vol;
	} else if (chan->master_vol > vol) {
	    chan->master_vol -= host_frametime * ambient_fade.value;
	    if (chan->master_vol < vol)
		chan->master_vol = vol;
	}

	chan->leftvol = chan->rightvol = chan->master_vol;
    }
}


/*
 * ============
 * S_Update
 *
 * Called once each time through the main loop
 * ============
 */
void
S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
    int i, j;
    int total;
    channel_t *ch;
    channel_t *combine;

    if (!sound_started || (snd_blocked > 0))
	return;

    VectorCopy(origin, listener_origin);
    VectorCopy(forward, listener_forward);
    VectorCopy(right, listener_right);
    VectorCopy(up, listener_up);

    /* update general area ambient sound sources */
    S_UpdateAmbientSounds();

    combine = NULL;

    /* update spatialization for static and dynamic sounds */
    ch = channels + NUM_AMBIENTS;
    for (i = NUM_AMBIENTS; i < total_channels; i++, ch++) {
	if (!ch->sfx)
	    continue;
	SND_Spatialize(ch);	/* respatialize channel */
	if (!ch->leftvol && !ch->rightvol)
	    continue;

	/*
	 * try to combine static sounds with a previous channel of the same
	 * sound effect so we don't mix five torches every frame
	 */
	if (i >= MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS) {
	    /* see if it can just use the last one */
	    if (combine && combine->sfx == ch->sfx) {
		combine->leftvol += ch->leftvol;
		combine->rightvol += ch->rightvol;
		ch->leftvol = ch->rightvol = 0;
		continue;
	    }
	    /* search for one */
	    combine = channels + MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS;
	    for (j = MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS; j < i;
		 j++, combine++)
		if (combine->sfx == ch->sfx)
		    break;

	    if (j == total_channels) {
		combine = NULL;
	    } else {
		if (combine != ch) {
		    combine->leftvol += ch->leftvol;
		    combine->rightvol += ch->rightvol;
		    ch->leftvol = ch->rightvol = 0;
		}
		continue;
	    }
	}
    }

    /*
     * debugging output
     */
    if (snd_show.value) {
	total = 0;
	ch = channels;
	for (i = 0; i < total_channels; i++, ch++)
	    if (ch->sfx && (ch->leftvol || ch->rightvol))
		total++;
	Con_Printf("----(%i)----\n", total);
    }
    /* mix some sound */
    S_Update_();
}

static void
GetSoundtime(void)
{
    int samplepos;
    static int buffers;
    static int oldsamplepos;
    int fullsamples;

    fullsamples = shm->samples / shm->channels;

    /*
     * it is possible to miscount buffers if it has wrapped twice between
     * calls to S_Update.  Oh well.
     */
    samplepos = SNDDMA_GetDMAPos();

    /* Check for buffer wrap */
    if (samplepos < oldsamplepos) {
	buffers++;

	/* time to chop things off to avoid 32 bit limits */
	if (paintedtime > 0x40000000) {
	    buffers = 0;
	    paintedtime = fullsamples;
	    S_StopAllSounds(true);
	}
    }
    oldsamplepos = samplepos;

    soundtime = buffers * fullsamples + samplepos / shm->channels;
}

void
S_ExtraUpdate(void)
{
#ifdef _WIN32
    IN_Accumulate();
#endif
    if (snd_noextraupdate.value)
	return;			/* don't pollute timings */
    S_Update_();
}


static void
S_Update_(void)
{
    unsigned endtime;
    int samps;

    if (!sound_started || (snd_blocked > 0))
	return;

    /* Updates DMA time */
    GetSoundtime();

    /* check to make sure that we haven't overshot */
    if (paintedtime < soundtime) {
	/* Con_Printf ("S_Update_ : overflow\n"); */
	paintedtime = soundtime;
    }
    /* mix ahead of current position */
    endtime = soundtime + _snd_mixahead.value * shm->speed;
    samps = shm->samples >> (shm->channels - 1);
    if (endtime - soundtime > samps)
	endtime = soundtime + samps;

    S_PaintChannels(endtime);
    SNDDMA_Submit();
}

/*
===============================================================================

console functions

===============================================================================
*/

static void
S_Play(void)
{
    static int hash = 345;
    int i;
    char name[256];
    sfx_t *sfx;

    i = 1;
    while (i < Cmd_Argc()) {
	if (!strrchr(Cmd_Argv(i), '.')) {
	    strcpy(name, Cmd_Argv(i));
	    strcat(name, ".wav");
	} else
	    strcpy(name, Cmd_Argv(i));
	sfx = S_PrecacheSound(name);
	S_StartSound(hash++, 0, sfx, listener_origin, 1.0, 1.0);
	i++;
    }
}

static void
S_PlayVol(void)
{
    static int hash = 543;
    int i;
    float vol;
    char name[256];
    sfx_t *sfx;

    i = 1;
    while (i < Cmd_Argc()) {
	if (!strrchr(Cmd_Argv(i), '.')) {
	    strcpy(name, Cmd_Argv(i));
	    strcat(name, ".wav");
	} else
	    strcpy(name, Cmd_Argv(i));
	sfx = S_PrecacheSound(name);
	vol = Q_atof(Cmd_Argv(i + 1));
	S_StartSound(hash++, 0, sfx, listener_origin, vol, 1.0);
	i += 2;
    }
}

static void
S_SoundList(void)
{
    int i;
    sfx_t *sfx;
    sfxcache_t *sc;
    int size, total;

    total = 0;
    for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
	sc = Cache_Check(&sfx->cache);
	if (!sc)
	    continue;
	size = sc->length * sc->width * (sc->stereo + 1);
	total += size;
	if (sc->loopstart >= 0)
	    Con_Printf("L");
	else
	    Con_Printf(" ");
	Con_Printf("(%2db) %6i : %s\n", sc->width * 8, size, sfx->name);
    }
    Con_Printf("Total resident: %i\n", total);
}


void
S_LocalSound(char *sound)
{
    sfx_t *sfx;

    if (nosound.value)
	return;
    if (!sound_started)
	return;

    sfx = S_PrecacheSound(sound);
    if (!sfx) {
	Con_Printf("%s: can't cache %s\n", __func__, sound);
	return;
    }
#ifdef NQ_HACK
    S_StartSound(cl.viewentity, -1, sfx, vec3_origin, 1, 1);
#endif
#ifdef QW_HACK
    S_StartSound(cl.playernum + 1, -1, sfx, vec3_origin, 1, 1);
#endif
}


/* FIXME - unused function? */
#if 0
static void
S_ClearPrecache(void)
{
}
#endif

void
S_BeginPrecaching(void)
{
}


void
S_EndPrecaching(void)
{
}
