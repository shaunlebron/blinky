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

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/soundcard.h>
#include <stdio.h>

#include "common.h"
#include "console.h"
#include "quakedef.h"
#include "sound.h"

static int audio_fd;
static int snd_inited;
static const char *snd_dev = "/dev/dsp";

static const int tryrates[] = { 11025, 22050, 22051, 44100, 48000, 8000 };
static const unsigned num_tryrates = sizeof(tryrates) / sizeof(int);

/* Request sound format as signed 16-bit (host-endian) */
#ifdef __BIG_ENDIAN__
#define TYRQ_AFMT_S16 AFMT_S16_BE
#else
#define TYRQ_AFMT_S16 AFMT_S16_LE
#endif

qboolean
SNDDMA_Init(void)
{
    int rc;
    int tmp;
    unsigned i;
    char *s;
    struct audio_buf_info info;
    int caps;

    snd_inited = 0;

    // open snd_dev, confirm capability to mmap

    audio_fd = open(snd_dev, O_RDWR);
    if (audio_fd < 0) {
	perror(snd_dev);
	Con_Printf("Could not open %s\n", snd_dev);
	return 0;
    }

    if (ioctl(audio_fd, SNDCTL_DSP_GETCAPS, &caps) == -1) {
	perror(snd_dev);
	Con_Printf("Driver for %s too old\n", snd_dev);
	close(audio_fd);
	return 0;
    }

    Con_Printf("DSP_CAP_TRIGGER = %d\nDSP_CAP_MMAP = %d\n",caps &  DSP_CAP_TRIGGER,caps & DSP_CAP_MMAP);

    if (!(caps & DSP_CAP_TRIGGER) || !(caps & DSP_CAP_MMAP)) {
	Con_Printf("%s lacks required features\n", snd_dev);
	close(audio_fd);
	return 0;
    }

    shm = &sn;

    // Get sample format
    s = getenv("QUAKE_SOUND_SAMPLEBITS");
    if (s)
	shm->samplebits = atoi(s);
    else if ((i = COM_CheckParm("-sndbits")) != 0)
	shm->samplebits = atoi(com_argv[i + 1]);
    else
	shm->samplebits = 0;

    if (shm->samplebits != 16 && shm->samplebits != 8)
	shm->samplebits = 0;

    // Try to set the requested format
    if (shm->samplebits == 16 || shm->samplebits == 0) {
	tmp = TYRQ_AFMT_S16;
	rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &tmp);
	if (rc == -1)
	    perror(snd_dev);
	if (rc == -1 || tmp != TYRQ_AFMT_S16) {
	    if (shm->samplebits == 16) {
		Con_Printf("Could not support 16-bit data. Try 8-bit.\n");
		close(audio_fd);
		return 0;
	    } else {
		Con_Printf("Couldn't support 16-bit data, trying 8-bit...\n");
		shm->samplebits = 8;
	    }
	} else {
	    shm->samplebits = 16;	// In case zero, set it.
	}
    }
    if (shm->samplebits == 8) {
	tmp = AFMT_U8;
	rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &tmp);
	if (rc == -1)
	    perror(snd_dev);
	if (rc == -1 || tmp != AFMT_U8) {
	    Con_Printf("Could not support 8-bit data.\n");
	    close(audio_fd);
	    return 0;
	}
    }
    // Set number of channels (only allow 1 or 2 for now)
    s = getenv("QUAKE_SOUND_CHANNELS");
    if (s) {
	shm->channels = atoi(s);
	if (shm->channels != 1 && shm->channels != 2) {
	    Con_DPrintf("QUAKE_SOUND_CHANNELS = %s"
			" -> unsupported, trying stereo\n", s);
	    shm->channels = 2;
	}
    } else if ((i = COM_CheckParm("-sndmono")) != 0) {
	shm->channels = 1;
    } else if ((i = COM_CheckParm("-sndstereo")) != 0) {
	shm->channels = 2;
    } else {
	shm->channels = 2;
    }

    tmp = shm->channels;
    rc = ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &shm->channels);
    if (rc == -1)
	perror(snd_dev);
    if (rc == -1 || tmp != shm->channels)
	Con_Printf("Could not select channels == %d on %s\n", tmp, snd_dev);
    if (rc != -1 && tmp != shm->channels)
	Con_Printf("Driver fell back with channels == %d\n", shm->channels);
    if (rc == -1 || (shm->channels != 1 && shm->channels != 2)) {
	close(audio_fd);
	return 0;
    }
    // Now get the requested speed or try to find one that works
    s = getenv("QUAKE_SOUND_SPEED");
    if (s)
	shm->speed = atoi(s);
    else if ((i = COM_CheckParm("-sndspeed")) != 0)
	shm->speed = atoi(com_argv[i + 1]);
    else
	shm->speed = 0;

    if (shm->speed == 0) {
	for (i = 0; i < num_tryrates; ++i) {
	    shm->speed = tryrates[i];
	    if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &shm->speed) == -1)
		perror(snd_dev);
	    else if (tryrates[i] == shm->speed)
		break;
	}
	if (i == num_tryrates) {
	    Con_Printf("Couldn't find a suitable sample speed.\n");
	    close(audio_fd);
	    return 0;
	}
    } else {
	tmp = shm->speed;
	rc = ioctl(audio_fd, SNDCTL_DSP_SPEED, &shm->speed);
	if (rc == -1)
	    perror(snd_dev);
	if (rc == -1 || tmp != shm->speed) {
	    Con_Printf("Could not set sample speed to %d", tmp);
	    close(audio_fd);
	    return 0;
	}
    }

    // Check how much space we have for non-blocking output
    if (ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &info) == -1) {
	perror("GETOSPACE");
	Con_Printf("Um, can't do GETOSPACE?\n");
	close(audio_fd);
	return 0;
    }

    shm->samples = info.fragstotal * info.fragsize / (shm->samplebits / 8);
    shm->submission_chunk = 1;

// memory map the dma buffer
// MAP_FILE required for some other unicies (HP-UX is one I think)

    shm->buffer = (unsigned char *)mmap(NULL, info.fragstotal
					* info.fragsize, PROT_WRITE,
					MAP_FILE | MAP_SHARED, audio_fd, 0);

    if (!shm->buffer || shm->buffer == (unsigned char *)MAP_FAILED) {
	perror(snd_dev);
	Con_Printf("Could not mmap %s\n", snd_dev);
	close(audio_fd);
	return 0;
    }
// toggle the trigger & start her up

    tmp = ~PCM_ENABLE_INPUT & ~PCM_ENABLE_OUTPUT;
    if (ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp) == -1) {
	perror(snd_dev);
	Con_Printf("Could not toggle.\n");
	close(audio_fd);
	return 0;
    }
    tmp = PCM_ENABLE_OUTPUT;
    if (ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp) == -1) {
	perror(snd_dev);
	Con_Printf("Could not toggle.\n");
	close(audio_fd);
	return 0;
    }

    shm->samplepos = 0;

    snd_inited = 1;
    return 1;
}

int
SNDDMA_LockBuffer(void)
{
    return 0;
}

void
SNDDMA_UnlockBuffer(void)
{
}

int
SNDDMA_GetDMAPos(void)
{
    struct count_info count;

    if (!snd_inited)
	return 0;

    if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &count) == -1) {
	perror(snd_dev);
	Con_Printf("Uh, sound dead.\n");
	close(audio_fd);
	snd_inited = 0;
	return 0;
    }
//      shm->samplepos = (count.bytes / (shm->samplebits / 8)) & (shm->samples-1);
//      fprintf(stderr, "%d    \r", count.ptr);
    shm->samplepos = count.ptr / (shm->samplebits / 8);

    return shm->samplepos;
}

void
SNDDMA_Shutdown(void)
{
    if (snd_inited) {
	close(audio_fd);
	snd_inited = 0;
    }
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void
SNDDMA_Submit(void)
{
}
