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
// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <linux/cdrom.h>
#include <paths.h>

#include "cdaudio.h"
#include "cdaudio_driver.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "quakedef.h"
#include "sound.h"

#ifdef NQ_HACK
#include "client.h"
#endif

static int cdfile = -1;
static char cd_dev[64] = _PATH_DEV "cdrom";
static struct cdrom_volctrl drv_vol_saved;
static struct cdrom_volctrl drv_vol;

void
CDDrv_Eject(void)
{
    if (ioctl(cdfile, CDROMEJECT) == -1)
	Con_DPrintf("ioctl cdromeject failed\n");
}


void
CDDrv_CloseDoor(void)
{
    if (ioctl(cdfile, CDROMCLOSETRAY) == -1)
	Con_DPrintf("ioctl cdromclosetray failed\n");
}

int
CDDrv_GetMaxTrack(byte *maxTrack)
{
    struct cdrom_tochdr tochdr;

    if (ioctl(cdfile, CDROMREADTOCHDR, &tochdr) == -1) {
	Con_DPrintf("ioctl cdromreadtochdr failed\n");
	return -1;
    }

    if (tochdr.cdth_trk0 < 1) {
	Con_DPrintf("CDAudio: no music tracks\n");
	return -1;
    }

    *maxTrack = tochdr.cdth_trk1;

    return 0;
}


int
CDDrv_IsAudioTrack(byte track)
{
    int ret = 1;
    struct cdrom_tocentry entry;

    entry.cdte_track = track;
    entry.cdte_format = CDROM_MSF;
    if (ioctl(cdfile, CDROMREADTOCENTRY, &entry) == -1) {
	ret = 0;
	Con_DPrintf("ioctl cdromreadtocentry failed\n");
    } else if (entry.cdte_ctrl == CDROM_DATA_TRACK)
	ret = 0;

    return ret;
}

int
CDDrv_PlayTrack(byte track)
{
    struct cdrom_ti ti;

    ti.cdti_trk0 = track;
    ti.cdti_trk1 = track;
    ti.cdti_ind0 = 1;
    ti.cdti_ind1 = 99;

    if (ioctl(cdfile, CDROMPLAYTRKIND, &ti) == -1) {
	Con_DPrintf("ioctl cdromplaytrkind failed\n");
	return 1;
    }

    if (ioctl(cdfile, CDROMRESUME) == -1)
	Con_DPrintf("ioctl cdromresume failed\n");

    return 0;
}


void
CDDrv_Stop(void)
{
    if (ioctl(cdfile, CDROMSTOP) == -1)
	Con_DPrintf("ioctl cdromstop failed (%d)\n", errno);
}

void
CDDrv_Pause(void)
{
    if (ioctl(cdfile, CDROMPAUSE) == -1)
	Con_DPrintf("ioctl cdrompause failed\n");
}


void
CDDrv_Resume(byte track)
{
    if (ioctl(cdfile, CDROMRESUME) == -1)
	Con_DPrintf("ioctl cdromresume failed\n");
}

int
CDDrv_SetVolume(byte volume)
{
    drv_vol.channel0 = drv_vol.channel2 = drv_vol.channel1 =
	drv_vol.channel3 = volume;
    if (ioctl(cdfile, CDROMVOLCTRL, &drv_vol) == -1 ) {
	Con_DPrintf("ioctl CDROMVOLCTRL failed\n");
	return -1;
    }

    return volume;
}

int
CDDrv_IsPlaying(byte track)
{
    int err, ret = 1;
    struct cdrom_subchnl subchnl;

    subchnl.cdsc_format = CDROM_MSF;
    err = ioctl(cdfile, CDROMSUBCHNL, &subchnl);
    if (err == -1)
	Con_DPrintf("ioctl cdromsubchnl failed\n");
    else if (subchnl.cdsc_audiostatus != CDROM_AUDIO_PLAY &&
	     subchnl.cdsc_audiostatus != CDROM_AUDIO_PAUSED)
	ret = 0;

    return ret;
}

int
CDDrv_InitDevice(void)
{
    int i;

    if ((i = COM_CheckParm("-cddev")) != 0 && i < com_argc - 1) {
	strncpy(cd_dev, com_argv[i + 1], sizeof(cd_dev));
	cd_dev[sizeof(cd_dev) - 1] = 0;
    }

    if ((cdfile = open(cd_dev, O_RDONLY | O_NONBLOCK)) == -1) {
	Con_Printf("CDAudio_Init: open of \"%s\" failed (%i)\n", cd_dev,
		   errno);
	cdfile = -1;
	return -1;
    }

    /* get drive's current volume */
    if (ioctl(cdfile, CDROMVOLREAD, &drv_vol_saved) == -1) {
	Con_DPrintf("ioctl CDROMVOLREAD failed\n");
	drv_vol_saved.channel0 = drv_vol_saved.channel2 =
	    drv_vol_saved.channel1 = drv_vol_saved.channel3 = 255.0;
    }
    /* set our own volume */
    CDDrv_SetVolume(bgmvolume.value * 255.0);

    return 0;
}


void
CDDrv_CloseDevice(void)
{
    /* Restore the saved volume setting */
    if (ioctl(cdfile, CDROMVOLCTRL, &drv_vol_saved) == -1)
	Con_DPrintf("ioctl CDROMVOLCTRL failed\n");

    close(cdfile);
    cdfile = -1;
}
