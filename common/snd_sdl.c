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

#include <stdio.h>

#include "SDL.h"
#include "SDL_audio.h"
#include "SDL_endian.h"

#include "console.h"
#include "quakedef.h"
#include "sdl_common.h"
#include "sound.h"
#include "sys.h"

static dma_t the_shm;
static int snd_inited;

static int desired_speed = 11025;
static int desired_bits = 8;

/* must not be modified while sound system is running */
static unsigned sdl_buflen;

/* must only be accessed between SDL_LockAudio() and SDL_UnlockAudio() */
static unsigned char *sdl_buf;
static unsigned rpos;
static unsigned wpos;

/*
 * paint_audio()
 *
 * SDL calls this function from another thread, so any shared variables need
 * access to be serialised in some way. We use SDL_LockAudio() to ensure the
 * SDL thread is locked out before we update the shared buffer or read/write
 * positions.
 */
static void
paint_audio(void *unused, Uint8 *stream, int len)
{
    while (rpos + len > sdl_buflen) {
	memcpy(stream, sdl_buf + rpos, sdl_buflen - rpos);
	stream += sdl_buflen - rpos;
	len -= sdl_buflen - rpos;
	rpos = 0;
    }
    if (len) {
	memcpy(stream, sdl_buf + rpos, len);
	rpos += len;
    }
}

qboolean
SNDDMA_Init(void)
{
    SDL_AudioSpec desired, obtained;

    snd_inited = 0;

    /* Set up the desired format */
    desired.freq = desired_speed;
    switch (desired_bits) {
    case 8:
	desired.format = AUDIO_U8;
	break;
    case 16:
	if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
	    desired.format = AUDIO_S16MSB;
	else
	    desired.format = AUDIO_S16LSB;
	break;
    default:
	Con_Printf("Unknown number of audio bits: %d\n", desired_bits);
	return false;
    }
    desired.channels = 2;
    desired.samples = 512; /* FIXME ~= rate * _s_mixahead / 2 ? */
    desired.callback = paint_audio;

    /* Init the SDL Audio Sub-system */
    Q_SDL_InitOnce();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
	Con_Printf("Couldn't init SDL audio: %s\n", SDL_GetError());
	return false;
    }

    /* Open the audio device */
    if (SDL_OpenAudio(&desired, &obtained) < 0) {
	Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
	return false;
    }

    /* Make sure we can support the audio format */
    switch (obtained.format) {
    case AUDIO_U8:
	/* Supported */
	break;
    case AUDIO_S16LSB:
    case AUDIO_S16MSB:
	if (((obtained.format == AUDIO_S16LSB) &&
	     (SDL_BYTEORDER == SDL_LIL_ENDIAN)) ||
	    ((obtained.format == AUDIO_S16MSB) &&
	     (SDL_BYTEORDER == SDL_BIG_ENDIAN))) {
	    /* Supported */
	    break;
	}
	/* Unsupported, fall through */;
    default:
	/* Not supported -- force SDL to do our bidding */
	SDL_CloseAudio();
	if (SDL_OpenAudio(&desired, NULL) < 0) {
	    Con_Printf("Couldn't open SDL audio: %s\n",
		       SDL_GetError());
	    return 0;
	}
	memcpy(&obtained, &desired, sizeof(desired));
	break;
    }

    /* Fill the audio DMA information block */
    shm = &the_shm;
    shm->samplebits = (obtained.format & 0xFF);
    shm->speed = obtained.freq;
    shm->channels = obtained.channels;
    shm->samplepos = 0;
    shm->submission_chunk = 1;

    /* Allow enough buffer for ~0.5s of mix ahead */
    shm->samples = obtained.samples * obtained.channels * 8;
    sdl_buflen = shm->samples * (shm->samplebits / 8);

    shm->buffer = Hunk_AllocName(sdl_buflen, "shm->buffer");
    sdl_buf = Hunk_AllocName(sdl_buflen, "sdl_buf");

    memset(shm->buffer, obtained.silence, sdl_buflen);
    memset(sdl_buf, obtained.silence, sdl_buflen);

    if (!shm->buffer || !sdl_buf)
	Sys_Error("%s: Failed to allocate buffer for sound!", __func__);

    rpos = wpos = 0;
    snd_inited = 1;

    /* FIXME - hack because sys_win does this differently */
    snd_blocked = 0;

    SDL_PauseAudio(0);

    return true;
}

int
SNDDMA_GetDMAPos(void)
{
    if (!snd_inited)
	return 0;

    SDL_LockAudio();
    shm->samplepos = rpos / (shm->samplebits / 8);
    SDL_UnlockAudio();

    return shm->samplepos;
}

void
SNDDMA_Shutdown(void)
{
    if (snd_inited) {
	SDL_CloseAudio();
	snd_inited = 0;
    }
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit(void)
{
    static unsigned old_paintedtime;
    unsigned len;

    if (!snd_inited || snd_blocked)
	return;

    SDL_LockAudio();

    if (paintedtime < old_paintedtime)
	old_paintedtime = 0;

    len = paintedtime - old_paintedtime;
    len *= shm->channels * (shm->samplebits / 8);
    old_paintedtime = paintedtime;

    while (wpos + len > sdl_buflen) {
	memcpy(sdl_buf + wpos, shm->buffer + wpos, sdl_buflen - wpos);
	len -= sdl_buflen - wpos;
	wpos = 0;
    }
    if (len) {
	memcpy(sdl_buf + wpos, shm->buffer + wpos, len);
	wpos += len;
    }

    SDL_UnlockAudio();
}

#ifdef _WIN32
void
S_BlockSound(void)
{
    if (!snd_blocked)
	SDL_PauseAudio(1);
    snd_blocked++;
}

void
S_UnblockSound(void)
{
    snd_blocked--;
    if (!snd_blocked)
	SDL_PauseAudio(0);
}
#endif

/*
 * shm->buffer is not the real DMA buffer, so no locking needed here
 */
int SNDDMA_LockBuffer(void) { return 0; }
void SNDDMA_UnlockBuffer(void) { }
