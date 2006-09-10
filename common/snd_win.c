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

#include <dsound.h>

#include "common.h"
#include "console.h"
#include "quakedef.h"
#include "sound.h"
#include "sys.h"
#include "winquake.h"

static HRESULT (WINAPI * pDirectSoundCreate)(GUID FAR *lpGUID,
					     LPDIRECTSOUND FAR *lplpDS,
					     IUnknown FAR *pUnkOuter);

// 64K is > 1 second at 16-bit, 22050 Hz
#define	WAV_BUFFERS		0x40
#define	WAV_MASK		(WAV_BUFFERS - 1)
#define	WAV_BUFFER_SIZE		0x0400
#define SECONDARY_BUFFER_SIZE	0x10000

typedef enum { SIS_SUCCESS, SIS_FAILURE, SIS_NOTAVAIL } sndinitstat;

static qboolean wavonly;
static qboolean dsound_init;
static qboolean wav_init;
static qboolean snd_firsttime = true, snd_isdirect, snd_iswave;
static qboolean primary_format_set;

static int sample16;
static int snd_sent, snd_completed;

static HANDLE hData;
static LPVOID lpData;

static HGLOBAL hWaveHdr;
static LPWAVEHDR lpWaveHdr;
static HWAVEOUT hWaveOut;

static DWORD gSndBufSize;
static MMTIME mmstarttime;

static LPDIRECTSOUND pDS;
static LPDIRECTSOUNDBUFFER pDSBuf, pDSPBuf;
static HINSTANCE hInstDS;

static sndinitstat SNDDMA_InitDirect(void);
static qboolean SNDDMA_InitWav(void);


/*
==================
S_BlockSound
==================
*/
void
S_BlockSound(void)
{
    /* DirectSound takes care of blocking itself */
    if (snd_iswave) {
	snd_blocked++;
	if (snd_blocked == 1)
	    waveOutReset(hWaveOut);
    }
}


/*
==================
S_UnblockSound
==================
*/
void
S_UnblockSound(void)
{
    /* DirectSound takes care of blocking itself */
    if (snd_iswave)
	snd_blocked--;
}


/*
==================
FreeSound
==================
*/
static void
FreeSound(void)
{
    int i;

    if (pDSBuf) {
	pDSBuf->lpVtbl->Stop(pDSBuf);
	pDSBuf->lpVtbl->Release(pDSBuf);
    }

    /*
     * Only release primary buffer if it's not also the mixing buffer we just
     * released
     */
    if (pDSPBuf && (pDSBuf != pDSPBuf)) {
	pDSPBuf->lpVtbl->Release(pDSPBuf);
    }

    if (pDS) {
	pDS->lpVtbl->SetCooperativeLevel(pDS, mainwindow, DSSCL_NORMAL);
	pDS->lpVtbl->Release(pDS);
    }

    if (hWaveOut) {
	waveOutReset(hWaveOut);
	if (lpWaveHdr) {
	    for (i = 0; i < WAV_BUFFERS; i++)
		waveOutUnprepareHeader(hWaveOut, lpWaveHdr + i,
				       sizeof(WAVEHDR));
	}
	waveOutClose(hWaveOut);

	if (hWaveHdr) {
	    GlobalUnlock(hWaveHdr);
	    GlobalFree(hWaveHdr);
	}
	if (hData) {
	    GlobalUnlock(hData);
	    GlobalFree(hData);
	}
    }

    pDS = NULL;
    pDSBuf = NULL;
    pDSPBuf = NULL;
    hWaveOut = 0;
    hData = 0;
    hWaveHdr = 0;
    lpData = NULL;
    lpWaveHdr = NULL;
    dsound_init = false;
    wav_init = false;
}

static DWORD dsLockSize;
static qboolean dsIsLocked = false;

int
SNDDMA_LockBuffer(void)
{
    int reps;
    void *pData;
    HRESULT hresult;
    DWORD dwStatus;

    if (dsIsLocked)
	Sys_Error("%s: Recursive locking detected!", __func__);

    if (pDSBuf) {
	/* if the buffer was lost or stopped, restore it and/or restart it */
	if (pDSBuf) {
	    if (pDSBuf->lpVtbl->GetStatus(pDSBuf, &dwStatus) != DS_OK)
		Con_Printf("Couldn't get sound buffer status\n");
	    if (dwStatus & DSBSTATUS_BUFFERLOST)
		pDSBuf->lpVtbl->Restore(pDSBuf);
	    if (!(dwStatus & DSBSTATUS_PLAYING))
		pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);
	}

	reps = 0;
	while ((hresult =
		pDSBuf->lpVtbl->Lock(pDSBuf, 0, gSndBufSize, &pData,
				     &dsLockSize, NULL, NULL, 0)) != DS_OK) {
	    if (hresult != DSERR_BUFFERLOST) {
		Con_Printf("%s: DS::Lock Sound Buffer Failed\n", __func__);
		return 1;
	    }
	    if (++reps > 10000) {
		Con_Printf("%s: DS: couldn't restore buffer\n", __func__);
		return 1;
	    }
	}
	shm->buffer = pData;
    }
    dsIsLocked = true;

    return 0;
}

void
SNDDMA_UnlockBuffer()
{
    if (!dsIsLocked)
	Sys_Error("%s: Detected unlock without lock", __func__);
    if (pDSBuf) {
	pDSBuf->lpVtbl->Unlock(pDSBuf, shm->buffer, dsLockSize, NULL, 0);
	shm->buffer = NULL;
    }
    dsIsLocked = false;
}

/*
==================
SNDDMA_InitDirect

Direct-Sound support
==================
*/
static sndinitstat
SNDDMA_InitDirect(void)
{
    DSBUFFERDESC dsbuf;
    DSBCAPS dsbcaps;
    DWORD dwSize, dwWrite;
    DSCAPS dscaps;
    WAVEFORMATEX format, pformat;
    HRESULT hresult;
    int reps;

    memset((void *)&sn, 0, sizeof(sn));
    shm = &sn;
    shm->channels = 2;
    shm->samplebits = 16;
    shm->speed = 11025;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = shm->channels;
    format.wBitsPerSample = shm->samplebits;
    format.nSamplesPerSec = shm->speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = 0;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    if (!hInstDS) {
	hInstDS = LoadLibrary("dsound.dll");
	if (hInstDS == NULL) {
	    Con_SafePrintf("Couldn't load dsound.dll\n");
	    return SIS_FAILURE;
	}

	pDirectSoundCreate =
	    (void *)GetProcAddress(hInstDS, "DirectSoundCreate");
	if (!pDirectSoundCreate) {
	    Con_SafePrintf("Couldn't get DS proc addr\n");
	    return SIS_FAILURE;
	}
    }

    hresult = pDirectSoundCreate(NULL, &pDS, NULL);
    if (hresult != DS_OK) {
	if (hresult != DSERR_ALLOCATED) {
	    Con_SafePrintf("DirectSound create failed\n");
	    return SIS_FAILURE;
	}
	Con_SafePrintf("DirectSoundCreate failure: hardware already in use\n");
	return SIS_NOTAVAIL;
    }

    dscaps.dwSize = sizeof(dscaps);
    if (DS_OK != pDS->lpVtbl->GetCaps(pDS, &dscaps)) {
	Con_SafePrintf("Couldn't get DS caps\n");
    }

    if (dscaps.dwFlags & DSCAPS_EMULDRIVER) {
	Con_SafePrintf("No DirectSound driver installed\n");
	FreeSound();
	return SIS_FAILURE;
    }

    if (DS_OK !=
	pDS->lpVtbl->SetCooperativeLevel(pDS, mainwindow, DSSCL_EXCLUSIVE)) {
	Con_SafePrintf("Set coop level failed\n");
	FreeSound();
	return SIS_FAILURE;
    }

    /*
     * Get access to the primary buffer, if possible, so we can set the sound
     * hardware format
     */
    memset(&dsbuf, 0, sizeof(dsbuf));
    dsbuf.dwSize = sizeof(DSBUFFERDESC);
    dsbuf.dwFlags = DSBCAPS_PRIMARYBUFFER;
    dsbuf.dwBufferBytes = 0;
    dsbuf.lpwfxFormat = NULL;

    memset(&dsbcaps, 0, sizeof(dsbcaps));
    dsbcaps.dwSize = sizeof(dsbcaps);
    primary_format_set = false;

    if (!COM_CheckParm("-snoforceformat")) {
	if (DS_OK ==
	    pDS->lpVtbl->CreateSoundBuffer(pDS, &dsbuf, &pDSPBuf, NULL)) {
	    pformat = format;

	    if (DS_OK != pDSPBuf->lpVtbl->SetFormat(pDSPBuf, &pformat)) {
		if (snd_firsttime)
		    Con_SafePrintf("Set primary sound buffer format: no\n");
	    } else {
		if (snd_firsttime)
		    Con_SafePrintf("Set primary sound buffer format: yes\n");

		primary_format_set = true;
	    }
	}
    }

    if (!primary_format_set || !COM_CheckParm("-primarysound")) {
	/* create the secondary buffer we'll actually work with */
	memset(&dsbuf, 0, sizeof(dsbuf));
	dsbuf.dwSize = sizeof(DSBUFFERDESC);
	dsbuf.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_LOCSOFTWARE;
	dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	dsbuf.lpwfxFormat = &format;

	memset(&dsbcaps, 0, sizeof(dsbcaps));
	dsbcaps.dwSize = sizeof(dsbcaps);

	if (DS_OK !=
	    pDS->lpVtbl->CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL)) {
	    Con_SafePrintf("DS:CreateSoundBuffer Failed");
	    FreeSound();
	    return SIS_FAILURE;
	}

	shm->channels = format.nChannels;
	shm->samplebits = format.wBitsPerSample;
	shm->speed = format.nSamplesPerSec;

	if (DS_OK != pDSBuf->lpVtbl->GetCaps(pDSBuf, &dsbcaps)) {
	    Con_SafePrintf("DS:GetCaps failed\n");
	    FreeSound();
	    return SIS_FAILURE;
	}

	if (snd_firsttime)
	    Con_SafePrintf("Using secondary sound buffer\n");
    } else {
	if (DS_OK !=
	    pDS->lpVtbl->SetCooperativeLevel(pDS, mainwindow,
					     DSSCL_WRITEPRIMARY)) {
	    Con_SafePrintf("Set coop level failed\n");
	    FreeSound();
	    return SIS_FAILURE;
	}

	if (DS_OK != pDSPBuf->lpVtbl->GetCaps(pDSPBuf, &dsbcaps)) {
	    Con_Printf("DS:GetCaps failed\n");
	    return SIS_FAILURE;
	}

	pDSBuf = pDSPBuf;
	Con_SafePrintf("Using primary sound buffer\n");
    }

    // Make sure mixer is active
    pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

    if (snd_firsttime)
	Con_SafePrintf("   %d channel(s)\n"
		       "   %d bits/sample\n"
		       "   %d bytes/sec\n",
		       shm->channels, shm->samplebits, shm->speed);

    gSndBufSize = dsbcaps.dwBufferBytes;

// initialize the buffer
    reps = 0;

    while ((hresult =
	    pDSBuf->lpVtbl->Lock(pDSBuf, 0, gSndBufSize, &lpData, &dwSize,
				 NULL, NULL, 0)) != DS_OK) {
	if (hresult != DSERR_BUFFERLOST) {
	    Con_SafePrintf
		("SNDDMA_InitDirect: DS::Lock Sound Buffer Failed\n");
	    FreeSound();
	    return SIS_FAILURE;
	}

	if (++reps > 10000) {
	    Con_SafePrintf
		("SNDDMA_InitDirect: DS: couldn't restore buffer\n");
	    FreeSound();
	    return SIS_FAILURE;
	}
    }

    memset(lpData, 0, dwSize);
    pDSBuf->lpVtbl->Unlock(pDSBuf, lpData, dwSize, NULL, 0);

    /*
     * We don't want anyone to access the buffer directly without locking it
     * first.
     */
    lpData = NULL;

    pDSBuf->lpVtbl->Stop(pDSBuf);
    pDSBuf->lpVtbl->GetCurrentPosition(pDSBuf, &mmstarttime.u.sample,
				       &dwWrite);
    pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

    shm->samples = gSndBufSize / (shm->samplebits / 8);
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = (unsigned char *)lpData;
    sample16 = (shm->samplebits / 8) - 1;

    dsound_init = true;

    return SIS_SUCCESS;
}


/*
==================
SNDDM_InitWav

Crappy windows multimedia base
==================
*/
static qboolean
SNDDMA_InitWav(void)
{
    WAVEFORMATEX format;
    int i;
    HRESULT hr;

    snd_sent = 0;
    snd_completed = 0;

    shm = &sn;

    shm->channels = 2;
    shm->samplebits = 16;
    shm->speed = 11025;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = shm->channels;
    format.wBitsPerSample = shm->samplebits;
    format.nSamplesPerSec = shm->speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = 0;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    /* Open a waveform device for output using window callback. */
    hr = waveOutOpen((LPHWAVEOUT) & hWaveOut, WAVE_MAPPER, &format,
		     0, 0L, CALLBACK_NULL);
    if (hr != MMSYSERR_NOERROR) {
	if (hr != MMSYSERR_ALLOCATED) {
	    Con_SafePrintf("waveOutOpen failed\n");
	    return false;
	}
	Con_SafePrintf("waveOutOpen failure; hardware already in use\n");
	return false;
    }

    /*
     * Allocate and lock memory for the waveform data. The memory
     * for waveform data must be globally allocated with
     * GMEM_MOVEABLE and GMEM_SHARE flags.
     */
    gSndBufSize = WAV_BUFFERS * WAV_BUFFER_SIZE;
    hData = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, gSndBufSize);
    if (!hData) {
	Con_SafePrintf("Sound: Out of memory.\n");
	FreeSound();
	return false;
    }
    lpData = GlobalLock(hData);
    if (!lpData) {
	Con_SafePrintf("Sound: Failed to lock.\n");
	FreeSound();
	return false;
    }
    memset(lpData, 0, gSndBufSize);

    /*
     * Allocate and lock memory for the header. This memory must
     * also be globally allocated with GMEM_MOVEABLE and
     * GMEM_SHARE flags.
     */
    hWaveHdr = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE,
			   (DWORD)sizeof(WAVEHDR) * WAV_BUFFERS);

    if (hWaveHdr == NULL) {
	Con_SafePrintf("Sound: Failed to Alloc header.\n");
	FreeSound();
	return false;
    }

    lpWaveHdr = (LPWAVEHDR)GlobalLock(hWaveHdr);
    if (lpWaveHdr == NULL) {
	Con_SafePrintf("Sound: Failed to lock header.\n");
	FreeSound();
	return false;
    }

    memset(lpWaveHdr, 0, sizeof(WAVEHDR) * WAV_BUFFERS);

    /* After allocation, set up and prepare headers. */
    for (i = 0; i < WAV_BUFFERS; i++) {
	lpWaveHdr[i].dwBufferLength = WAV_BUFFER_SIZE;
	lpWaveHdr[i].lpData = (LPSTR)lpData + i * WAV_BUFFER_SIZE;

	if (waveOutPrepareHeader(hWaveOut, lpWaveHdr + i, sizeof(WAVEHDR))
	    != MMSYSERR_NOERROR) {
	    Con_SafePrintf("Sound: failed to prepare wave headers\n");
	    FreeSound();
	    return false;
	}
    }

    shm->samples = gSndBufSize / (shm->samplebits / 8);
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = (unsigned char *)lpData;
    sample16 = (shm->samplebits / 8) - 1;

    wav_init = true;

    return true;
}

/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/

qboolean
SNDDMA_Init(void)
{
    sndinitstat stat;

    if (COM_CheckParm("-wavonly"))
	wavonly = true;

    dsound_init = wav_init = 0;
    stat = SIS_FAILURE;

    /* Init DirectSound */
    if (!wavonly) {
	if (snd_firsttime || snd_isdirect) {
	    stat = SNDDMA_InitDirect();
	    if (stat == SIS_SUCCESS) {
		snd_isdirect = true;
		if (snd_firsttime)
		    Con_SafePrintf("DirectSound initialized\n");
	    } else {
		snd_isdirect = false;
		Con_SafePrintf("DirectSound failed to init\n");
	    }
	}
    }
    /*
     * If DirectSound didn't succeed in initializing, try to initialize
     * waveOut sound, unless DirectSound failed because the hardware is
     * already allocated (in which case the user has already chosen not to
     * have sound)
     */
    if (!dsound_init && (stat != SIS_NOTAVAIL)) {
	if (snd_firsttime || snd_iswave) {
	    snd_iswave = SNDDMA_InitWav();
	    if (snd_iswave) {
		if (snd_firsttime)
		    Con_SafePrintf("Wave sound initialized\n");
	    } else {
		Con_SafePrintf("Wave sound failed to init\n");
	    }
	}
    }

    if (!dsound_init && !wav_init) {
	if (snd_firsttime) {
	    Con_SafePrintf("No sound device initialized\n");
	    snd_firsttime = false;
	}
	return false;
    }
    snd_firsttime = false;

    return true;
}

/*
==============
SNDDMA_GetDMAPos

return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int
SNDDMA_GetDMAPos(void)
{
    MMTIME mmtime;
    int s;
    DWORD dwWrite;

    if (dsound_init) {
	mmtime.wType = TIME_SAMPLES;
	pDSBuf->lpVtbl->GetCurrentPosition(pDSBuf, &mmtime.u.sample,
					   &dwWrite);
	s = mmtime.u.sample - mmstarttime.u.sample;
    } else if (wav_init) {
	s = snd_sent * WAV_BUFFER_SIZE;
    } else {
	Sys_Error("%s: no dsound or wav to init.", __func__);
    }

    s >>= sample16;
    s &= (shm->samples - 1);

    return s;
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
    LPWAVEHDR h;
    int wResult;

    if (!wav_init)
	return;

    /* Find which sound blocks have completed */
    while (1) {
	if (snd_completed == snd_sent) {
	    Con_DPrintf("Sound overrun\n");
	    break;
	}
	if (!(lpWaveHdr[snd_completed & WAV_MASK].dwFlags & WHDR_DONE)) {
	    break;
	}
	snd_completed++;	// this buffer has been played
    }

    /* Submit two new sound blocks */
    while (((snd_sent - snd_completed) >> sample16) < 4) {
	h = lpWaveHdr + (snd_sent & WAV_MASK);
	snd_sent++;

	/*
	 * Now the data block can be sent to the output device. The
	 * waveOutWrite function returns immediately and waveform
	 * data is sent to the output device in the background.
	 */
	wResult = waveOutWrite(hWaveOut, h, sizeof(WAVEHDR));
	if (wResult != MMSYSERR_NOERROR) {
	    Con_SafePrintf("Failed to write block to device\n");
	    FreeSound();
	    return;
	}
    }
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void
SNDDMA_Shutdown(void)
{
    FreeSound();
    if (hInstDS) {
	FreeLibrary(hInstDS);
	hInstDS = NULL;
    }
}
