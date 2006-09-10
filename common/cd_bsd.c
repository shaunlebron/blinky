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

#include <sys/cdio.h>
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
static struct ioc_vol drv_vol_saved;
static struct ioc_vol drv_vol;

void
CDDrv_Eject(void)
{
    if (ioctl(cdfile, CDIOCEJECT) == -1)
	Con_DPrintf("ioctl cdioceject failed\n");
}


void
CDDrv_CloseDoor(void)
{
    if (ioctl(cdfile, CDIOCCLOSE) == -1)
	Con_DPrintf("ioctl cdiocclose failed\n");
}

int
CDDrv_GetMaxTrack(byte *maxTrack)
{
    struct ioc_toc_header tochdr;

    if (ioctl(cdfile, CDIOREADTOCHEADER, &tochdr) == -1) {
	Con_DPrintf("ioctl cdioreadtocheader failed\n");
	return -1;
    }

    if (tochdr.starting_track < 1) {
	Con_DPrintf("CDAudio: no music tracks\n");
	return -1;
    }

    *maxTrack = tochdr.ending_track;

    return 0;
}


int
CDDrv_IsAudioTrack(byte track)
{
    int ret = 1;
    struct ioc_read_toc_entry entry;
    struct cd_toc_entry toc_buffer;

#define CDROM_DATA_TRACK 4
    memset(&toc_buffer, 0, sizeof(toc_buffer));
    entry.data_len = sizeof(toc_buffer);
    entry.data = &toc_buffer;
    entry.starting_track = track;
    entry.address_format = CD_MSF_FORMAT;
    if (ioctl(cdfile, CDIOREADTOCENTRYS, &entry) == -1) {
	ret = 0;
	Con_DPrintf("ioctl cdioreadtocentrys failed\n");
    } else if (toc_buffer.control == CDROM_DATA_TRACK)
	ret = 0;

    return ret;
}

int
CDDrv_PlayTrack(byte track)
{
    struct ioc_play_track ti;

    ti.start_track = track;
    ti.end_track = track;
    ti.start_index = 1;
    ti.end_index = 99;

    if (ioctl(cdfile, CDIOCPLAYTRACKS, &ti) == -1) {
	Con_DPrintf("ioctl cdiocplaytracks failed\n");
	return 1;
    }

    if (ioctl(cdfile, CDIOCRESUME) == -1)
	Con_DPrintf("ioctl cdiocresume failed\n");

    return 0;
}


void
CDDrv_Stop(void)
{
    if (ioctl(cdfile, CDIOCSTOP) == -1)
	Con_DPrintf("ioctl cdiocstop failed (%d)\n", errno);
}

void
CDDrv_Pause(void)
{
    if (ioctl(cdfile, CDIOCPAUSE) == -1)
	Con_DPrintf("ioctl cdiocpause failed\n");
}


void
CDDrv_Resume(byte track)
{
    if (ioctl(cdfile, CDIOCRESUME) == -1)
	Con_DPrintf("ioctl cdiocresume failed\n");
}

int
CDDrv_SetVolume(byte volume)
{
    drv_vol.vol[0] = drv_vol.vol[1] =
	drv_vol.vol[2] = drv_vol.vol[3] = volume;
    if (ioctl(cdfile, CDIOCSETVOL, &drv_vol) == -1 ) {
	Con_DPrintf("ioctl CDIOCSETVOL failed\n");
	return -1;
    }

    return volume;
}

int
CDDrv_IsPlaying(byte track)
{
    int err, ret = 1;
    struct ioc_read_subchannel subchnl;
    struct cd_sub_channel_info data;

    subchnl.address_format = CD_MSF_FORMAT;
    subchnl.data_format = CD_CURRENT_POSITION;
    subchnl.data_len = sizeof(data);
    subchnl.track = track;
    subchnl.data = &data;
    err = ioctl(cdfile, CDIOCREADSUBCHANNEL, &subchnl);
    if (err == -1)
	Con_DPrintf("ioctl cdiocreadsubchannel failed\n");
    else if (subchnl.data->header.audio_status != CD_AS_PLAY_IN_PROGRESS &&
	     subchnl.data->header.audio_status != CD_AS_PLAY_PAUSED)
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
    if (ioctl(cdfile, CDIOCGETVOL, &drv_vol_saved) == -1) {
	Con_DPrintf("ioctl CDIOCGETVOL failed\n");
	drv_vol_saved.vol[0] = drv_vol_saved.vol[1] =
	    drv_vol_saved.vol[2] = drv_vol_saved.vol[3] = 255.0;
    }
    /* set our own volume */
    CDDrv_SetVolume(bgmvolume.value * 255.0);

    return 0;
}


void
CDDrv_CloseDevice(void)
{
    /* Restore the saved volume setting */
    if (ioctl(cdfile, CDIOCSETVOL, &drv_vol_saved) == -1)
	Con_DPrintf("ioctl CDIOCSETVOL failed\n");

    close(cdfile);
    cdfile = -1;
}
