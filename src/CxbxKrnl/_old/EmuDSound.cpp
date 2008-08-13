// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuDSound.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_DEFEXTRN_

// prevent name collisions
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "CxbxKrnl.h"
#include "Emu.h"
#include "EmuFS.h"
#include "EmuShared.h"
#include "EmuAlloc.h"
#include "EmuXTL.h"

#include <mmreg.h>
#include <msacm.h>
#include <process.h>
#include <locale.h>

XTL::X_CMcpxStream::_vtbl XTL::X_CMcpxStream::vtbl = 
{
    0xBEEFC001,                     // 0x00
    0xBEEFC002,                     // 0x04
    0xBEEFC003,                     // 0x08
    0xBEEFC004,                     // 0x0C
    &XTL::EmuCMcpxStream_Dummy_0x10,// 0x10
};

XTL::X_CDirectSoundStream::_vtbl XTL::X_CDirectSoundStream::vtbl = 
{
    &XTL::EmuCDirectSoundStream_AddRef,         // 0x00
    &XTL::EmuCDirectSoundStream_Release,        // 0x04
/*
    STDMETHOD(GetInfo)(THIS_ LPXMEDIAINFO pInfo) PURE;
*/
    0xBEEFB001,                                 // 0x08
    &XTL::EmuCDirectSoundStream_GetStatus,      // 0x0C
    &XTL::EmuCDirectSoundStream_Process,        // 0x10
    &XTL::EmuCDirectSoundStream_Discontinuity,  // 0x14
    &XTL::EmuCDirectSoundStream_Flush,          // 0x18
    0xBEEFB003,                                 // 0x1C
    0xBEEFB004,                                 // 0x20
    0xBEEFB005,                                 // 0x24
    0xBEEFB006,                                 // 0x28
    0xBEEFB007,                                 // 0x2C
    0xBEEFB008,                                 // 0x30
    0xBEEFB009,                                 // 0x34
    0xBEEFB00A,                                 // 0x38
};

// size of sound buffer cache (used for periodic sound buffer updates)
#define SOUNDBUFFER_CACHE_SIZE 0x100

// size of sound stream cache (used for periodic sound stream updates)
#define SOUNDSTREAM_CACHE_SIZE 0x100

// Static Variable(s)
static XTL::LPDIRECTSOUND8          g_pDSound8 = NULL;
static int                          g_pDSound8RefCount = 0;
static XTL::X_CDirectSoundBuffer   *g_pDSoundBufferCache[SOUNDBUFFER_CACHE_SIZE];
static XTL::X_CDirectSoundStream   *g_pDSoundStreamCache[SOUNDSTREAM_CACHE_SIZE];
static XTL::LPDIRECTSOUND3DLISTENER g_pDSound3DListener = NULL;
//static XTL::X_CDirectSound3DBuffer	g_DSound3DBufferCache[SOUNDBUFFER_CACHE_SIZE];

// periodically update sound buffers
static void HackUpdateSoundBuffers()
{
    for(int v=0;v<SOUNDBUFFER_CACHE_SIZE;v++)
    {
        if(g_pDSoundBufferCache[v] == 0 || g_pDSoundBufferCache[v]->EmuBuffer == 0)
            continue;

        PVOID pAudioPtr, pAudioPtr2;
        DWORD dwAudioBytes, dwAudioBytes2;

        // unlock existing lock
        if(g_pDSoundBufferCache[v]->EmuLockPtr1 != 0)
            g_pDSoundBufferCache[v]->EmuDirectSoundBuffer8->Unlock(g_pDSoundBufferCache[v]->EmuLockPtr1, g_pDSoundBufferCache[v]->EmuLockBytes1, g_pDSoundBufferCache[v]->EmuLockPtr2, g_pDSoundBufferCache[v]->EmuLockBytes2);

        HRESULT hRet = g_pDSoundBufferCache[v]->EmuDirectSoundBuffer8->Lock(0, g_pDSoundBufferCache[v]->EmuBufferDesc->dwBufferBytes, &pAudioPtr, &dwAudioBytes, &pAudioPtr2, &dwAudioBytes2, 0);

        if(SUCCEEDED(hRet))
        {
            if(pAudioPtr != 0)
                memcpy(pAudioPtr,  g_pDSoundBufferCache[v]->EmuBuffer, dwAudioBytes);

            if(pAudioPtr2 != 0)
                memcpy(pAudioPtr2, (PVOID)((DWORD)g_pDSoundBufferCache[v]->EmuBuffer+dwAudioBytes), dwAudioBytes2);

            g_pDSoundBufferCache[v]->EmuDirectSoundBuffer8->Unlock(pAudioPtr, dwAudioBytes, pAudioPtr2, dwAudioBytes2);
        }

        // TODO: relock old lock ??
    }
}

// periodically update sound streams
static void HackUpdateSoundStreams()
{
    for(int v=0;v<SOUNDSTREAM_CACHE_SIZE;v++)
    {
        if(g_pDSoundStreamCache[v] == 0 || g_pDSoundStreamCache[v]->EmuBuffer == NULL)
            continue;

        PVOID pAudioPtr, pAudioPtr2;
        DWORD dwAudioBytes, dwAudioBytes2;

        HRESULT hRet = g_pDSoundStreamCache[v]->EmuDirectSoundBuffer8->Lock(0, g_pDSoundStreamCache[v]->EmuBufferDesc->dwBufferBytes, &pAudioPtr, &dwAudioBytes, &pAudioPtr2, &dwAudioBytes2, 0);

        if(SUCCEEDED(hRet))
        {
            if(pAudioPtr != 0)
                memcpy(pAudioPtr,  g_pDSoundStreamCache[v]->EmuBuffer, dwAudioBytes);

            if(pAudioPtr2 != 0)
                memcpy(pAudioPtr2, (PVOID)((DWORD)g_pDSoundStreamCache[v]->EmuBuffer+dwAudioBytes), dwAudioBytes2);

            g_pDSoundStreamCache[v]->EmuDirectSoundBuffer8->Unlock(pAudioPtr, dwAudioBytes, pAudioPtr2, dwAudioBytes2);
        }

        g_pDSoundStreamCache[v]->EmuDirectSoundBuffer8->SetCurrentPosition(0);
        g_pDSoundStreamCache[v]->EmuDirectSoundBuffer8->Play(0, 0, 0);
    }

    return;
}

// resize an emulated directsound buffer, if necessary
static void EmuResizeIDirectSoundBuffer8(XTL::X_CDirectSoundBuffer *pThis, DWORD dwBytes)
{
    if(dwBytes == pThis->EmuBufferDesc->dwBufferBytes || dwBytes == 0)
        return;

    DbgPrintf("EmuResizeIDirectSoundBuffer8 : Resizing! (0x%.08X->0x%.08X)\n", pThis->EmuBufferDesc->dwBufferBytes, dwBytes);

    DWORD dwPlayCursor, dwWriteCursor, dwStatus;

    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetCurrentPosition(&dwPlayCursor, &dwWriteCursor);

    if(FAILED(hRet))
        CxbxKrnlCleanup("Unable to retrieve current position for resize reallocation!");

    hRet = pThis->EmuDirectSoundBuffer8->GetStatus(&dwStatus);

    if(FAILED(hRet))
        CxbxKrnlCleanup("Unable to retrieve current status for resize reallocation!");

    // release old buffer
    while(pThis->EmuDirectSoundBuffer8->Release() > 0) { }

    pThis->EmuBufferDesc->dwBufferBytes = dwBytes;

    hRet = g_pDSound8->CreateSoundBuffer(pThis->EmuBufferDesc, &pThis->EmuDirectSoundBuffer8, NULL);

    if(FAILED(hRet))
        CxbxKrnlCleanup("IDirectSoundBuffer8 resize Failed!");

    pThis->EmuDirectSoundBuffer8->SetCurrentPosition(dwPlayCursor);

    if(dwStatus & DSBSTATUS_PLAYING)
        pThis->EmuDirectSoundBuffer8->Play(0, 0, pThis->EmuPlayFlags);
}

// resize an emulated directsound stream, if necessary
static void EmuResizeIDirectSoundStream8(XTL::X_CDirectSoundStream *pThis, DWORD dwBytes)
{
    if(dwBytes == pThis->EmuBufferDesc->dwBufferBytes)
        return;

    DWORD dwPlayCursor, dwWriteCursor, dwStatus;

    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetCurrentPosition(&dwPlayCursor, &dwWriteCursor);

    if(FAILED(hRet))
        CxbxKrnlCleanup("Unable to retrieve current position for resize reallocation!");

    hRet = pThis->EmuDirectSoundBuffer8->GetStatus(&dwStatus);

    if(FAILED(hRet))
        CxbxKrnlCleanup("Unable to retrieve current status for resize reallocation!");

    // release old buffer
    while(pThis->EmuDirectSoundBuffer8->Release() > 0) { }

    pThis->EmuBufferDesc->dwBufferBytes = dwBytes;

    hRet = g_pDSound8->CreateSoundBuffer(pThis->EmuBufferDesc, &pThis->EmuDirectSoundBuffer8, NULL);

    if(FAILED(hRet))
        CxbxKrnlCleanup("IDirectSoundBuffer8 resize Failed!");

    pThis->EmuDirectSoundBuffer8->SetCurrentPosition(dwPlayCursor);

    if(dwStatus & DSBSTATUS_PLAYING)
        pThis->EmuDirectSoundBuffer8->Play(0, 0, pThis->EmuPlayFlags);
}

// Creates the DirectSound listener.  Xbox only has one listener and it's handled by IDirectSound8
static void EmuCreateIDirectSound3DListener( void )
{
	XTL::DSBUFFERDESC dsbd;
	XTL::LPDIRECTSOUNDBUFFER lpDSBPrimary = NULL;	// Must be IDirectSoundBuffer (afaik)
	HRESULT hr = DS_OK;

	// Acquire a primary audio buffer (one with 3D caps)
	::ZeroMemory( &dsbd, sizeof( XTL::DSBUFFERDESC ) );
	dsbd.dwSize = sizeof( XTL::DSBUFFERDESC );
	dsbd.dwFlags = DSBCAPS_CTRL3D | DSBCAPS_PRIMARYBUFFER; 

	hr = g_pDSound8->CreateSoundBuffer( &dsbd, &lpDSBPrimary, NULL );
	if( SUCCEEDED( hr ) )
	{
		hr = lpDSBPrimary->QueryInterface( XTL::IID_IDirectSound3DListener, (LPVOID*) &g_pDSound3DListener );
		if( FAILED( hr ) )
		{
			lpDSBPrimary->Release();
			CxbxKrnlCleanup( "Unable to create DirectSound3D Listener!" );
		}

		lpDSBPrimary->Release();
	}
	else
		CxbxKrnlCleanup( "Unable to retreive primary DirectSound Buffer!" );
}

// Finds the original IDirectSoundBuffer associated with the IDirectSound3DBuffer we need to access
//LPVOID EmuLocateDirectSound3DBuffer( DWORD dwOriginalBufferLocation )
//{
//	// blueshogun96 4/29/08 10:39 PM
//	// Loop through the entire cache of DirectSound3D buffers created and return a buffer
//	// with a matching location.  Yeah I know it's cheap...
//
//	for( int i = 0; i < SOUNDBUFFER_CACHE_SIZE; i++ )
//	{
//		if( g_DSound3DBufferCache[i].OriginalBufferLocation == dwOriginalBufferLocation )
//		{
//			return *(LPVOID*) g_DSound3DBufferCache[i].EmuDirectSound3DBuffer;
//		}
//	}
//
//	// No match found...
//	CxbxKrnlCleanup("EmuLocateOriginalDirectSoundBuffer():\n\nFailed to locate matching original IDirectSoundBuffer!");
//
//	return NULL;
//}

// Creates a PC compatible DirectSound3D buffer for use with 3D sound APIs
HRESULT EmuCreateDirectSound3DBuffer( XTL::IDirectSoundBuffer8* pBuffer, XTL::IDirectSound3DBuffer8** pp3DBuffer )
{
	// Query a 3D sound buffer from the original buffer
	HRESULT hr = pBuffer->QueryInterface( XTL::IID_IDirectSound3DBuffer, (LPVOID*) pp3DBuffer );

	return hr;
}

// ******************************************************************
// * func: EmuDirectSoundCreate
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreate
(
    LPVOID          pguidDeviceId,
    LPDIRECTSOUND8 *ppDirectSound,
    LPUNKNOWN       pUnknown
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundCreate\n"
           "(\n"
           "   pguidDeviceId             : 0x%.08X\n"
           "   ppDirectSound             : 0x%.08X\n"
           "   pUnknown                  : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pguidDeviceId, ppDirectSound, pUnknown);

    static bool initialized = false;

    HRESULT hRet = DS_OK;

    if(!initialized)
    {
        hRet = DirectSoundCreate8(NULL, ppDirectSound, NULL);

        if(FAILED(hRet))
            CxbxKrnlCleanup("DirectSoundCreate8 Failed!");

        g_pDSound8 = *ppDirectSound;

        hRet = g_pDSound8->SetCooperativeLevel(g_hEmuWindow, DSSCL_PRIORITY);

        if(FAILED(hRet))
            CxbxKrnlCleanup("g_pDSound8->SetCooperativeLevel Failed!");

        int v=0;
        // clear sound buffer cache
        for(v=0;v<SOUNDBUFFER_CACHE_SIZE;v++)
            g_pDSoundBufferCache[v] = 0;

        // clear sound stream cache
        for(v=0;v<SOUNDSTREAM_CACHE_SIZE;v++)
            g_pDSoundStreamCache[v] = 0;

		// Clear the 3D sound buffer cache
		/*for(v=0;v<SOUNDBUFFER_CACHE_SIZE;v++)
		{
			g_DSound3DBufferCache[v].EmuDirectSound3DBuffer = NULL;
			g_DSound3DBufferCache[v].OriginalBufferLocation = 0;
		}*/

		// Create the listener (for 3D Audio)
#ifndef _DISABLE_3D_SOUND
		EmuCreateIDirectSound3DListener();
#endif

        initialized = true;
    }

    g_pDSound8RefCount = 1;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSound8_AddRef
(
    LPDIRECTSOUND8          pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_AddRef\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = g_pDSound8RefCount++;

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSound8_Release
(
    LPDIRECTSOUND8          pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_Release\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = g_pDSound8RefCount--;

    /* temporarily (?) disabled
    if(uRet == 1)
        pThis->Release();
    //*/

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuCDirectSound_GetSpeakerConfig
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSound_GetSpeakerConfig
(
    X_CDirectSound         *pThis,
    PDWORD                  pdwSpeakerConfig
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSound_GetSpeakerConfig\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pdwSpeakerConfig          : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pdwSpeakerConfig);

    *pdwSpeakerConfig = 0; // STEREO

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_DownloadEffectsImage
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_DownloadEffectsImage
(
    LPDIRECTSOUND8          pThis,
    LPCVOID                 pvImageBuffer,
    DWORD                   dwImageSize,
    PVOID                   pImageLoc,      // TODO: Use this param
    PVOID                  *ppImageDesc     // TODO: Use this param
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_DownloadEffectsImage\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pvImageBuffer             : 0x%.08X\n"
           "   dwImageSize               : 0x%.08X\n"
           "   pImageLoc                 : 0x%.08X\n"
           "   ppImageDesc               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pvImageBuffer, dwImageSize, pImageLoc, ppImageDesc);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuDirectSoundDoWork
// ******************************************************************
VOID WINAPI XTL::EmuDirectSoundDoWork()
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundDoWork();\n", GetCurrentThreadId());

    HackUpdateSoundBuffers();
    HackUpdateSoundStreams();

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetOrientation
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetOrientation
(
    LPDIRECTSOUND8  pThis,
    FLOAT           xFront,
    FLOAT           yFront,
    FLOAT           zFront,
    FLOAT           xTop,
    FLOAT           yTop,
    FLOAT           zTop,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetOrientation\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   xFront                    : %f\n"
           "   yFront                    : %f\n"
           "   zFront                    : %f\n"
           "   xTop                      : %f\n"
           "   yTop                      : %f\n"
           "   zTop                      : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, xFront, yFront, zFront, xTop, yTop, zTop, dwApply);

    HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetOrientation( xFront, yFront, zFront,
										 xTop, yTop, zTop, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetDistanceFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetDistanceFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDistanceFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetDistanceFactor\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fDistanceFactor           : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fDistanceFactor, dwApply);

    HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetDistanceFactor( fDistanceFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetRolloffFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fRolloffFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetRolloffFactor\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fRolloffFactor            : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fRolloffFactor, dwApply);

    HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetRolloffFactor( fRolloffFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetDopplerFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetDopplerFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDopplerFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetDopplerFactor\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fDopplerFactor            : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fDopplerFactor, dwApply);

	HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
    hr = g_pDSound3DListener->SetDopplerFactor( fDopplerFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetI3DL2Listener
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetI3DL2Listener
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pDummy, // TODO: fill this out
    DWORD                   dwApply
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetI3DL2Listener\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pDummy                    : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pDummy, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetMixBinHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetMixBinHeadroom
(
    LPDIRECTSOUND8          pThis,
    DWORD                   dwMixBinMask,
    DWORD                   dwHeadroom
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetMixBinHeadroom\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwMixBinMask              : 0x%.08X\n"
               "   dwHeadroom                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwMixBinMask, dwHeadroom);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBins
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMixBins
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pMixBins
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMixBins\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pMixBins                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pMixBins);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBinVolumes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMixBinVolumes
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pMixBins
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMixBinVolumes\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pMixBins                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pMixBins);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetPosition
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
     EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetPosition\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   x                         : %f\n"
            "   y                         : %f\n"
            "   z                         : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, x, y, z, dwApply);

	HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetPosition( x, y, z, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetVelocity
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetVelocity\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   x                         : %f\n"
            "   y                         : %f\n"
            "   z                         : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, x, y, z, dwApply);

	HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetVelocity( x, y, z, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetAllParameters
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetAllParameters
(
    LPDIRECTSOUND8          pThis,
    LPVOID                  pTodo,  // TODO: LPCDS3DLISTENER
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_SetAllParameters\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   pTodo                     : 0x%.08X\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, pTodo, dwApply);

	// blueshogun96 4/26/08
	// The LPCDS3DLISTENER structure looks the same on PC to me.
	HRESULT hr = DS_OK;

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetAllParameters( (LPCDS3DLISTENER) pTodo, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuCDirectSound_CommitDeferredSettings
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSound_CommitDeferredSettings
(
    X_CDirectSound         *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSound_CommitDeferredSettings\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

	HRESULT hr = DS_OK;

    // TODO: Translate params, then make the PC DirectSound call
#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->CommitDeferredSettings();
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuDirectSoundCreateBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreateBuffer
(
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundCreateBuffer\n"
           "(\n"
           "   pdsbd                     : 0x%.08X\n"
           "   ppBuffer                  : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pdsbd, ppBuffer);

    DWORD dwEmuFlags = 0;
	bool bIsMonoFormat = false;

    DSBUFFERDESC *pDSBufferDesc = (DSBUFFERDESC*)CxbxMalloc(sizeof(DSBUFFERDESC));
    
    // convert from Xbox to PC DSound
    {
        DWORD dwAcceptableMask = 0x00000010 | 0x00000020 | 0x00000080 | 0x00000100 | 0x00002000 | 0x00040000 | 0x00080000;

        if(pdsbd->dwFlags & (~dwAcceptableMask))
            EmuWarning("Use of unsupported pdsbd->dwFlags mask(s) (0x%.08X)", pdsbd->dwFlags & (~dwAcceptableMask));

        pDSBufferDesc->dwSize = sizeof(DSBUFFERDESC);
        pDSBufferDesc->dwFlags = (pdsbd->dwFlags & dwAcceptableMask) | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;
        pDSBufferDesc->dwBufferBytes = pdsbd->dwBufferBytes;

        if(pDSBufferDesc->dwBufferBytes < DSBSIZE_MIN)
            pDSBufferDesc->dwBufferBytes = DSBSIZE_MIN;
        else if(pDSBufferDesc->dwBufferBytes > DSBSIZE_MAX) 
            pDSBufferDesc->dwBufferBytes = DSBSIZE_MAX;

        pDSBufferDesc->dwReserved = 0;

        if(pdsbd->lpwfxFormat != NULL)
        {
            pDSBufferDesc->lpwfxFormat = (WAVEFORMATEX*)CxbxMalloc(sizeof(WAVEFORMATEX)+pdsbd->lpwfxFormat->cbSize);
            memcpy(pDSBufferDesc->lpwfxFormat, pdsbd->lpwfxFormat, sizeof(WAVEFORMATEX));

            if(pDSBufferDesc->lpwfxFormat->wFormatTag == /*WAVE_FORMAT_XBOX_ADPCM*/0x0069)
            {
                dwEmuFlags |= DSB_FLAG_ADPCM;

                EmuWarning("WAVE_FORMAT_XBOX_ADPCM Unsupported!");

                pDSBufferDesc->lpwfxFormat->wFormatTag = WAVE_FORMAT_PCM;
                pDSBufferDesc->lpwfxFormat->nBlockAlign = (pDSBufferDesc->lpwfxFormat->nChannels*pDSBufferDesc->lpwfxFormat->wBitsPerSample)/8;

                // the above calculation can yield zero for wBitsPerSample < 8, so we'll bound it to 1 byte minimum
                if(pDSBufferDesc->lpwfxFormat->nBlockAlign == 0)
                    pDSBufferDesc->lpwfxFormat->nBlockAlign = 1;

                pDSBufferDesc->lpwfxFormat->nAvgBytesPerSec = pDSBufferDesc->lpwfxFormat->nSamplesPerSec*pDSBufferDesc->lpwfxFormat->nBlockAlign;
                pDSBufferDesc->lpwfxFormat->wBitsPerSample = 8;

                /* TODO: Get ADPCM working!
                pDSBufferDesc->lpwfxFormat->cbSize = 32;
                #define WAVE_FORMAT_ADPCM 2
                pDSBufferDesc->lpwfxFormat->wFormatTag = WAVE_FORMAT_ADPCM;
                */

#ifndef _DISABLE_3D_SOUND
				// If this is a mono format buffer, give it the possibility of having 3D sound capabilities.
				if( pDSBufferDesc->lpwfxFormat->nChannels == 1 )
				{
					bIsMonoFormat = true;
					pDSBufferDesc->dwFlags |= DSBCAPS_CTRL3D;
				}
#endif
            }
        }

#ifndef _DISABLE_3D_SOUND
        pDSBufferDesc->guid3DAlgorithm = DS3DALG_HRTF_FULL;	// DS3DALG_DEFAULT;
#endif
    }

    // sanity check
    if(pDSBufferDesc->lpwfxFormat->nBlockAlign != (pDSBufferDesc->lpwfxFormat->nChannels*pDSBufferDesc->lpwfxFormat->wBitsPerSample)/8)
    {
        pDSBufferDesc->lpwfxFormat->nBlockAlign = (2*pDSBufferDesc->lpwfxFormat->wBitsPerSample)/8;
        pDSBufferDesc->lpwfxFormat->nAvgBytesPerSec = pDSBufferDesc->lpwfxFormat->nSamplesPerSec * pDSBufferDesc->lpwfxFormat->nBlockAlign;
    }

    // TODO: Garbage Collection
    *ppBuffer = new X_CDirectSoundBuffer();

    (*ppBuffer)->EmuDirectSoundBuffer8 = 0;
    (*ppBuffer)->EmuBuffer = 0;
    (*ppBuffer)->EmuBufferDesc = pDSBufferDesc;
    (*ppBuffer)->EmuLockPtr1 = 0;
    (*ppBuffer)->EmuLockBytes1 = 0;
    (*ppBuffer)->EmuLockPtr2 = 0;
    (*ppBuffer)->EmuLockBytes2 = 0;
    (*ppBuffer)->EmuFlags = dwEmuFlags;

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundCreateBuffer, *ppBuffer := 0x%.08X, bytes := 0x%.08X\n", GetCurrentThreadId(), *ppBuffer, pDSBufferDesc->dwBufferBytes);

    HRESULT hRet = g_pDSound8->CreateSoundBuffer(pDSBufferDesc, &((*ppBuffer)->EmuDirectSoundBuffer8), NULL);

    if(FAILED(hRet))
        EmuWarning("CreateSoundBuffer Failed!");

    // cache this sound buffer
    {
        int v=0;
        for(v=0;v<SOUNDBUFFER_CACHE_SIZE;v++)
        {
            if(g_pDSoundBufferCache[v] == 0)
            {
                g_pDSoundBufferCache[v] = *ppBuffer;
                break;
            }
        }

        if(v == SOUNDBUFFER_CACHE_SIZE)
            CxbxKrnlCleanup("SoundBuffer cache out of slots!");
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdssd,
    X_CDirectSoundBuffer  **ppBuffer,
    PVOID                   pUnknown
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
       EmuSwapFS();   // Win2k/XP FS
       DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_CreateBuffer\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppBuffer                  : 0x%.08X\n"
               "   pUnknown                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdssd, ppBuffer, pUnknown);
       EmuSwapFS();   // XBox FS
    }
    #endif

    EmuDirectSoundCreateBuffer(pdssd, ppBuffer);

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateSoundBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateSoundBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer,
    LPUNKNOWN               pUnkOuter
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_CreateSoundBuffer\n"
               "(\n"
               "   pdsbd                     : 0x%.08X\n"
               "   ppBuffer                  : 0x%.08X\n"
               "   pUnkOuter                 : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pdsbd, ppBuffer, pUnkOuter);
        EmuSwapFS();   // XBox FS
    }
    #endif

    return EmuDirectSoundCreateBuffer(pdsbd, ppBuffer);
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetBufferData
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetBufferData
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pvBufferData,
    DWORD                   dwBufferBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetBufferData\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pvBufferData              : 0x%.08X\n"
           "   dwBufferBytes             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pvBufferData, dwBufferBytes);

    // update buffer data cache
    pThis->EmuBuffer = pvBufferData;

    EmuResizeIDirectSoundBuffer8(pThis, dwBufferBytes);

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetPlayRegion
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetPlayRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwPlayStart,
    DWORD                   dwPlayLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetPlayRegion\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwPlayStart               : 0x%.08X\n"
           "   dwPlayLength              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwPlayStart, dwPlayLength);

    // TODO: Translate params, then make the PC DirectSound call

    // TODO: Ensure that 4627 & 4361 are intercepting far enough back
    // (otherwise pThis is manipulated!)

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Lock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Lock
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwOffset, 
    DWORD                   dwBytes, 
    LPVOID                 *ppvAudioPtr1, 
    LPDWORD                 pdwAudioBytes1, 
    LPVOID                 *ppvAudioPtr2, 
    LPDWORD                 pdwAudioBytes2, 
    DWORD                   dwFlags 
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Lock\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwOffset                  : 0x%.08X\n"
           "   dwBytes                   : 0x%.08X\n"
           "   ppvAudioPtr1              : 0x%.08X\n"
           "   pdwAudioBytes1            : 0x%.08X\n"
           "   ppvAudioPtr2              : 0x%.08X\n"
           "   pdwAudioBytes2            : 0x%.08X\n"
           "   dwFlags                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1,
           ppvAudioPtr2, pdwAudioBytes2, dwFlags);

    HRESULT hRet = D3D_OK;

    if(pThis->EmuBuffer != 0)
    {
        *ppvAudioPtr1 = pThis->EmuBuffer;
        *pdwAudioBytes1 = dwBytes;
    }
    else
    {
        if(dwBytes > pThis->EmuBufferDesc->dwBufferBytes)
            EmuResizeIDirectSoundBuffer8(pThis, dwBytes);

        if(pThis->EmuLockPtr1 != 0)
            pThis->EmuDirectSoundBuffer8->Unlock(pThis->EmuLockPtr1, pThis->EmuLockBytes1, pThis->EmuLockPtr2, pThis->EmuLockBytes2);

        // TODO: Verify dwFlags is the same as windows
        hRet = pThis->EmuDirectSoundBuffer8->Lock(dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1, ppvAudioPtr2, pdwAudioBytes2, dwFlags);

        if(FAILED(hRet))
            CxbxKrnlCleanup("DirectSoundBuffer Lock Failed!");

        pThis->EmuLockPtr1 = *ppvAudioPtr1;
        pThis->EmuLockBytes1 = *pdwAudioBytes1;
        pThis->EmuLockPtr2 = (ppvAudioPtr2 != NULL) ? *ppvAudioPtr2 : NULL;
        pThis->EmuLockBytes2 = (pdwAudioBytes2 != NULL) ? *pdwAudioBytes2 : NULL;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetHeadroom
(
    X_CDirectSoundBuffer  *pThis,
    DWORD                  dwHeadroom
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetHeadroom\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwHeadroom                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwHeadroom); 

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetLoopRegion
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetLoopRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwLoopStart,
    DWORD                   dwLoopLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetLoopRegion\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwLoopStart               : 0x%.08X\n"
           "   dwLoopLength              : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwLoopStart, dwLoopLength);

    // TODO: Ensure that 4627 & 4361 are intercepting far enough back
    // (otherwise pThis is manipulated!)

    //EmuResizeIDirectSoundBuffer8(pThis, dwLoopLength);

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSoundBuffer8_Release
(
    X_CDirectSoundBuffer   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Release\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = 0;

    if(pThis != 0)
    {
        uRet = pThis->EmuDirectSoundBuffer8->Release();

        if(uRet == 0)
        {
            // remove cache entry
            for(int v=0;v<SOUNDBUFFER_CACHE_SIZE;v++)
            {
                if(g_pDSoundBufferCache[v] == pThis)
                    g_pDSoundBufferCache[v] = 0;
            }

            if(pThis->EmuBufferDesc->lpwfxFormat != NULL)
                CxbxFree(pThis->EmuBufferDesc->lpwfxFormat);

            CxbxFree(pThis->EmuBufferDesc);

            delete pThis;
        }
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetPitch
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetPitch
(
    X_CDirectSoundBuffer   *pThis,
    LONG                    lPitch
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetPitch\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   lPitch                    : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, lPitch);

    // TODO: Translate params, then make the PC DirectSound call

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetStatus
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_GetStatus
(
    X_CDirectSoundBuffer   *pThis,
    LPDWORD                 pdwStatus
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_GetStatus\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pdwStatus                 : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pdwStatus);

    HRESULT hRet = DS_OK;

    if(pThis != 0 && pThis->EmuBuffer == 0)
    {
        hRet = pThis->EmuDirectSoundBuffer8->GetStatus(pdwStatus);
    }
    else
    {
        *pdwStatus = 0;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetCurrentPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwNewPosition
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetCurrentPosition\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwNewPosition             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwNewPosition);

    // NOTE: TODO: This call *will* (by MSDN) fail on primary buffers!
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->SetCurrentPosition(dwNewPosition);

    if(FAILED(hRet))
        EmuWarning("SetCurrentPosition Failed!");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetCurrentPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_GetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    PDWORD                  pdwCurrentPlayCursor,
    PDWORD                  pdwCurrentWriteCursor
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_GetCurrentPosition\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pdwCurrentPlayCursor      : 0x%.08X\n"
           "   pdwCurrentWriteCursor     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pdwCurrentPlayCursor, pdwCurrentWriteCursor);

    HackUpdateSoundBuffers();
    HackUpdateSoundStreams();

    // NOTE: TODO: This call always seems to fail on primary buffers!
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetCurrentPosition(pdwCurrentPlayCursor, pdwCurrentWriteCursor);

    if(FAILED(hRet))
        EmuWarning("GetCurrentPosition Failed!");

    if(pdwCurrentPlayCursor != 0 && pdwCurrentWriteCursor != 0)
    {
        DbgPrintf("*pdwCurrentPlayCursor := %d, *pdwCurrentWriteCursor := %d\n", *pdwCurrentPlayCursor, *pdwCurrentWriteCursor);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Play
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Play
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwReserved1,
    DWORD                   dwReserved2,
    DWORD                   dwFlags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Play\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwReserved1               : 0x%.08X\n"
           "   dwReserved2               : 0x%.08X\n"
           "   dwFlags                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwReserved1, dwReserved2, dwFlags);

    if(dwFlags & (~DSBPLAY_LOOPING))
        CxbxKrnlCleanup("Unsupported Playing Flags");

    HackUpdateSoundBuffers();

    // close any existing locks
    if(pThis->EmuLockPtr1 != 0)
    {
        pThis->EmuDirectSoundBuffer8->Unlock
        (
            pThis->EmuLockPtr1,
            pThis->EmuLockBytes1,
            pThis->EmuLockPtr2,
            pThis->EmuLockBytes2
        );

        pThis->EmuLockPtr1 = 0;
    }
    
    HRESULT hRet;

    if(pThis->EmuFlags & DSB_FLAG_ADPCM)
    {
        hRet = D3D_OK;
    }
    else
    {
        hRet = pThis->EmuDirectSoundBuffer8->Play(0, 0, dwFlags);
    }

    pThis->EmuPlayFlags = dwFlags;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Stop
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Stop
(
    X_CDirectSoundBuffer   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Stop\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    HRESULT hRet = pThis->EmuDirectSoundBuffer8->Stop();

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_StopEx
// ******************************************************************
extern "C" HRESULT __stdcall XTL::EmuIDirectSoundBuffer8_StopEx
(
    X_CDirectSoundBuffer *pBuffer,
    REFERENCE_TIME        rtTimeStamp,
    DWORD                 dwFlags
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_StopEx\n"
           "(\n"
           "   pBuffer                   : 0x%.08X\n"
           "   rtTimeStamp               : 0x%.08X\n"
           "   dwFlags                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pBuffer, rtTimeStamp, dwFlags);

    if(pBuffer->EmuDirectSoundBuffer8 == 0)
        EmuWarning("pBuffer->EmuDirectSoundBuffer8 == 0");

    EmuWarning("StopEx not yet implemented!");

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetVolume
(
    X_CDirectSoundBuffer   *pThis,
    LONG                    lVolume
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetVolume\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   lVolume                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, lVolume);

    // TODO: Ensure that 4627 & 4361 are intercepting far enough back
    // (otherwise pThis is manipulated!)

//    HRESULT hRet = pThis->EmuDirectSoundBuffer8->SetVolume(lVolume);

    EmuSwapFS();   // XBox FS

//    return hRet;
    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetFrequency
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwFrequency
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetFrequency\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwFrequency               : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwFrequency);

//    HRESULT hRet = pThis->EmuDirectSoundBuffer8->SetFrequency(dwFrequency);

    EmuSwapFS();   // XBox FS

//    return hRet;
    return S_OK;
}

// ******************************************************************
// * func: EmuDirectSoundCreateStream
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreateStream
(
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundCreateStream\n"
           "(\n"
           "   pdssd                     : 0x%.08X\n"
           "   ppStream                  : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pdssd, ppStream);

    // TODO: Garbage Collection
    *ppStream = new X_CDirectSoundStream();
    
    DSBUFFERDESC *pDSBufferDesc = (DSBUFFERDESC*)CxbxMalloc(sizeof(DSBUFFERDESC));
    
    // convert from Xbox to PC DSound
    {
        DWORD dwAcceptableMask = 0x00000010; // TODO: Note 0x00040000 is being ignored (DSSTREAMCAPS_LOCDEFER)

        if(pdssd->dwFlags & (~dwAcceptableMask))
            EmuWarning("Use of unsupported pdssd->dwFlags mask(s) (0x%.08X)", pdssd->dwFlags & (~dwAcceptableMask));

        pDSBufferDesc->dwSize = sizeof(DSBUFFERDESC);
//        pDSBufferDesc->dwFlags = (pdssd->dwFlags & dwAcceptableMask) | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;
        pDSBufferDesc->dwFlags = DSBCAPS_CTRLVOLUME;
        pDSBufferDesc->dwBufferBytes = DSBSIZE_MIN;

        pDSBufferDesc->dwReserved = 0;

        if(pdssd->lpwfxFormat != NULL)
        {
            pDSBufferDesc->lpwfxFormat = (WAVEFORMATEX*)CxbxMalloc(sizeof(WAVEFORMATEX));
            memcpy(pDSBufferDesc->lpwfxFormat, pdssd->lpwfxFormat, sizeof(WAVEFORMATEX));
        }

        pDSBufferDesc->guid3DAlgorithm = DS3DALG_DEFAULT;

        if(pDSBufferDesc->lpwfxFormat != NULL && pDSBufferDesc->lpwfxFormat->wFormatTag != WAVE_FORMAT_PCM)
        {
            EmuWarning("Invalid WAVE_FORMAT!");
			if(pDSBufferDesc->lpwfxFormat->wFormatTag == /*WAVE_FORMAT_XBOX_ADPCM*/0x0069)
				EmuWarning("WAVE_FORMAT_XBOX_ADPCM Unsupported!");

            (*ppStream)->EmuDirectSoundBuffer8 = 0;

            EmuSwapFS();   // XBox FS

            return DS_OK;
        }

        // we only support 2 channels right now
        if(pDSBufferDesc->lpwfxFormat->nChannels > 2)
        {
            pDSBufferDesc->lpwfxFormat->nChannels = 2;
            pDSBufferDesc->lpwfxFormat->nBlockAlign = (2*pDSBufferDesc->lpwfxFormat->wBitsPerSample)/8;
            pDSBufferDesc->lpwfxFormat->nAvgBytesPerSec = pDSBufferDesc->lpwfxFormat->nSamplesPerSec * pDSBufferDesc->lpwfxFormat->nBlockAlign;
        }
    }

    (*ppStream)->EmuBuffer = 0;
    (*ppStream)->EmuBufferDesc = pDSBufferDesc;
    (*ppStream)->EmuLockPtr1 = 0;
    (*ppStream)->EmuLockBytes1 = 0;
    (*ppStream)->EmuLockPtr2 = 0;
    (*ppStream)->EmuLockBytes2 = 0;

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundCreateStream, *ppStream := 0x%.08X\n", GetCurrentThreadId(), *ppStream);

    HRESULT hRet = g_pDSound8->CreateSoundBuffer(pDSBufferDesc, &(*ppStream)->EmuDirectSoundBuffer8, NULL);

    if(FAILED(hRet))
        EmuWarning("CreateSoundBuffer Failed!");

    // cache this sound stream
    {
        int v=0;
        for(v=0;v<SOUNDSTREAM_CACHE_SIZE;v++)
        {
            if(g_pDSoundStreamCache[v] == 0)
            {
                g_pDSoundStreamCache[v] = *ppStream;
                break;
            }
        }

        if(v == SOUNDSTREAM_CACHE_SIZE)
            CxbxKrnlCleanup("SoundStream cache out of slots!");
    }

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateStream
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateStream
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream,
    PVOID                   pUnknown
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSound8_CreateStream\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppStream                  : 0x%.08X\n"
               "   pUnknown                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdssd, ppStream, pUnknown);
        EmuSwapFS();   // XBox FS
    }
    #endif

    EmuDirectSoundCreateStream(pdssd, ppStream);

    return DS_OK;
}

// ******************************************************************
// * func: EmuCMcpxStream_Dummy_0x10
// ******************************************************************
VOID WINAPI XTL::EmuCMcpxStream_Dummy_0x10(DWORD dwDummy1, DWORD dwDummy2)
{
    EmuWarning("EmuCMcpxStream_Dummy_0x10 is ignored!");
    return;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVolume
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_SetVolume(X_CDirectSoundStream *pThis, LONG lVolume)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetVolume\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   lVolume                   : %d\n"
           ");\n",
           GetCurrentThreadId(), pThis, lVolume);

    // TODO: Actually SetVolume

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetRolloffFactor
(
    X_CDirectSoundStream *pThis,
    FLOAT                 fRolloffFactor,
    DWORD                 dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetRolloffFactor\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fRolloffFactor            : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fRolloffFactor, dwApply);

    // TODO: Actually SetRolloffFactor

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_AddRef(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_AddRef\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    if(pThis != 0)
        pThis->EmuDirectSoundBuffer8->AddRef();

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Release
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_Release(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_Release\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    ULONG uRet = 0;

    if(pThis != 0 && (pThis->EmuDirectSoundBuffer8 != 0))
    {
        uRet = pThis->EmuDirectSoundBuffer8->Release();

        if(uRet == 0)
        {
            // remove cache entry
            for(int v=0;v<SOUNDSTREAM_CACHE_SIZE;v++)
            {
                if(g_pDSoundStreamCache[v] == pThis)
                    g_pDSoundStreamCache[v] = 0;
            }

            if(pThis->EmuBufferDesc->lpwfxFormat != NULL)
                CxbxFree(pThis->EmuBufferDesc->lpwfxFormat);

            CxbxFree(pThis->EmuBufferDesc);

            delete pThis;
        }
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_GetStatus
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_GetStatus
(
    X_CDirectSoundStream   *pThis,
    DWORD                  *pdwStatus
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_GetStatus\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pdwStatus                 : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pdwStatus);

    EmuWarning("EmuCDirectSoundStream_GetStatus is not yet implemented");

    *pdwStatus = DSBSTATUS_PLAYING;

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Process
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Process
(
    X_CDirectSoundStream   *pThis,
    PXMEDIAPACKET           pInputBuffer,
    PXMEDIAPACKET           pOutputBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_Process\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pInputBuffer              : 0x%.08X\n"
           "   pOutputBuffer             : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pInputBuffer, pOutputBuffer);

    if(pThis->EmuDirectSoundBuffer8 != NULL)
    {
        // update buffer data cache
        pThis->EmuBuffer = pInputBuffer->pvBuffer;

        EmuResizeIDirectSoundStream8(pThis, pInputBuffer->dwMaxSize);

        if(pInputBuffer->pdwStatus != 0)
            *pInputBuffer->pdwStatus = S_OK;

        HackUpdateSoundStreams();
    }
    else
    {
        if(pInputBuffer->pdwStatus != 0)
            *pInputBuffer->pdwStatus = S_OK;
    }

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Discontinuity
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Discontinuity(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_Discontinuity\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis);

    // TODO: Actually Process

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Flush
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Flush(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_Flush();\n",
           GetCurrentThreadId(), pThis);

    // TODO: Actually Flush

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSound_SynchPlayback
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSound_SynchPlayback(PVOID pUnknown)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSound_SynchPlayback(0x%.08X);\n",
           GetCurrentThreadId());

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Pause
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Pause
(
    PVOID   pStream,
    DWORD   dwPause
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_Pause\n"
           "(\n"
           "   pStream                   : 0x%.08X\n"
           "   dwPause                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pStream, dwPause);

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetHeadroom
(
    PVOID   pThis,
    DWORD   dwHeadroom
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundStream_SetHeadroom\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwHeadroom                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwHeadroom);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeAngles
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeAngles
(
    PVOID   pThis,
    DWORD   dwInsideConeAngle,
    DWORD   dwOutsideConeAngle,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeAngles\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwInsideConeAngle         : 0x%.08X\n"
           "   dwOutsideConeAngle        : 0x%.08X\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwInsideConeAngle, dwOutsideConeAngle, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOutsideVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeOutsideVolume
(
    PVOID   pThis,
    LONG    lConeOutsideVolume,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeOutsideVolume\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   lConeOutsideVolume        : %d\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, lConeOutsideVolume, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetAllParameters
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetAllParameters
(
    PVOID    pThis,
    PVOID    pUnknown,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetAllParameters\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pUnknown                  : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pUnknown, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMaxDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetMaxDistance
(
    PVOID    pThis,
    D3DVALUE fMaxDistance,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetMaxDistance\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fMaxDistance              : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fMaxDistance, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMinDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetMinDistance
(
    PVOID    pThis,
    D3DVALUE fMinDistance,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetMinDistance\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   fMinDistance              : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, fMinDistance, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVelocity
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetVelocity
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetVelocity\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   x                         : %f\n"
           "   y                         : %f\n"
           "   z                         : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, x, y, z, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOrientation
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeOrientation
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeOrientation\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   x                         : %f\n"
           "   y                         : %f\n"
           "   z                         : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, x, y, z, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetPosition
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetPosition\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   x                         : %f\n"
           "   y                         : %f\n"
           "   z                         : %f\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, x, y, z, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetFrequency
(
    PVOID   pThis,
    DWORD   dwFrequency
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuCDirectSoundStream_SetFrequency\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwFrequency               : %d\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwFrequency);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetI3DL2Source
(
    PVOID   pThis,
    PVOID   pds3db,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundStream_SetI3DL2Source\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pds3db                    : 0x%.08X\n"
           "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pds3db, dwApply);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_Unknown1
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_Unknown1
(
    PVOID   pThis,
    DWORD   dwUnknown1
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundStream_Unknown1\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   dwUnknown1                : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, dwUnknown1);

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// s+
// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMaxDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMaxDistance
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   flMaxDistance,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMaxDistance\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   flMaxDistance             : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, flMaxDistance, dwApply);

    HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetMaxDistance( flMaxDistance, dwApply );

	p3DBuffer->Release();
#endif

	EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMinDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMinDistance
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   flMinDistance,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMinDistance\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   flMinDistance             : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, flMinDistance, dwApply);

    HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetMinDistance( flMinDistance, dwApply );
	p3DBuffer->Release();
#endif

	EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetRolloffFactor
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   flRolloffFactor,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetRolloffFactor\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   flRolloffFactor           : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, flRolloffFactor, dwApply);

	HRESULT hr = DS_OK;
	/*XTL::IDirectSound3DBuffer* p3DBuffer = (XTL::IDirectSound3DBuffer*) EmuLocateDirectSound3DBuffer( *(DWORD*) pThis );

	if( p3DBuffer )
	{
		hr = p3DBuffer->SetRolloffFactor( flRolloffFactor, dwApply );
	}*/
#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetRolloffFactor( flRolloffFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetDistanceFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetDistanceFactor
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   flDistanceFactor,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetDistanceFactor\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   flDistanceFactor          : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, flDistanceFactor, dwApply);

	HRESULT hr = DS_OK;/*
	XTL::IDirectSound3DBuffer* p3DBuffer = (XTL::IDirectSound3DBuffer*) EmuLocateDirectSound3DBuffer( *(DWORD*) pThis );

	if( p3DBuffer )
	{
		hr = p3DBuffer->SetDistanceFactor( flDistanceFactor, dwApply );
	}*/

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetDistanceFactor( flDistanceFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS
    
    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetConeAngles
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetConeAngles
(
    LPDIRECTSOUNDBUFFER8    pThis,
    DWORD                   dwInsideConeAngle,
    DWORD                   dwOutsideConeAngle,
    DWORD                   dwApply
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetConeAngles\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   dwInsideConeAngle         : 0x%.08X\n"
            "   dwOutsideConeAngle        : 0x%.08X\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, dwInsideConeAngle,
            dwOutsideConeAngle, dwApply);

	HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetConeAngles( dwInsideConeAngle, dwOutsideConeAngle, dwApply );
	p3DBuffer->Release();
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetConeOrientation
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetConeOrientation
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetConeOrientation\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   x                         : %f\n"
            "   y                         : %f\n"
            "   z                         : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, x, y, z, dwApply);

	HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetConeOrientation( x, y, z, dwApply );
	p3DBuffer->Release();
#endif

    EmuSwapFS();   // XBox FS

    return hr;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetConeOutsideVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetConeOutsideVolume
(
    LPDIRECTSOUNDBUFFER8    pThis,
    LONG                    lConeOutsideVolume,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS
    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetConeOutsideVolume\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   lConeOutsideVolume        : 0x%.08X\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, lConeOutsideVolume, dwApply);
    EmuSwapFS();   // XBox FS

	HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}
		
	hr = p3DBuffer->SetConeOutsideVolume( lConeOutsideVolume, dwApply );
	p3DBuffer->Release();
#endif

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetPosition
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetPosition\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   x                         : %f\n"
            "   y                         : %f\n"
            "   z                         : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, x, y, z, dwApply);

	HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetPosition( x, y, z, dwApply );
	p3DBuffer->Release();
#endif

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetVelocity
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetVelocity
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
	EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetVelocity\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   x                         : %f\n"
            "   y                         : %f\n"
            "   z                         : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, x, y, z, dwApply);

	HRESULT hr = DS_OK;
	LPDIRECTSOUND3DBUFFER8 p3DBuffer = NULL;
	
#ifndef _DISABLE_3D_SOUND
	// Create a 3D buffer
	hr = EmuCreateDirectSound3DBuffer( pThis, &p3DBuffer );
	if( FAILED( hr ) )
	{
		EmuWarning( "Unable to create 3D buffer!" );
		__asm int 3
	}

	hr = p3DBuffer->SetVelocity( x, y, z, dwApply );
	p3DBuffer->Release();
#endif

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetDopplerFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetDopplerFactor
(
    LPDIRECTSOUNDBUFFER8    pThis,
    FLOAT                   flDopplerFactor,
    DWORD                   dwApply
)
{
	EmuSwapFS();   // Win2k/XP FS
    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetConeOutsideVolume\n"
            "(\n"
            "   pThis                     : 0x%.08X\n"
            "   flDopplerFactor           : %f\n"
            "   dwApply                   : 0x%.08X\n"
            ");\n",
            GetCurrentThreadId(), pThis, flDopplerFactor, dwApply);

	HRESULT hr = DS_OK;/*
	XTL::IDirectSound3DBuffer* p3DBuffer = (XTL::IDirectSound3DBuffer*) EmuLocateDirectSound3DBuffer( *(DWORD*) pThis );

	if( p3DBuffer )
	{
		hr = p3DBuffer->SetDopplerFactor( flDopplerFactor, dwApply );
	}*/

#ifndef _DISABLE_3D_SOUND
	hr = g_pDSound3DListener->SetDopplerFactor( flDopplerFactor, dwApply );
#endif

    EmuSwapFS();   // XBox FS
    
    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetI3DL2Source
(
    LPDIRECTSOUNDBUFFER8    pThis,
    LPCDSI3DL2BUFFER        pds3db,
    DWORD                   dwApply
)
{
    // debug trace
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetI3DL2Source\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pds3db                    : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pds3db, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}
// +s
// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFormat
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetFormat
(
    X_CDirectSoundBuffer *pBuffer, 
    LPCWAVEFORMATEX pwfxFormat
)
{
    EmuSwapFS();   // Win2k/XP FS

    // debug trace
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetFormat\n"
               "(\n"
               "   pBuffer                   : 0x%.08X\n"
               "   pwfxFormat                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pBuffer,pwfxFormat);
    }
    #endif

    HRESULT hRet = DS_OK;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuDirectSoundUseFullHRTF
// ******************************************************************
STDAPI_(void) EmuDirectSoundUseFullHRTF
(
	void
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuDirectSoundUseFullHRTF()\n", GetCurrentThreadId());

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetLFO
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetLFO
(
	LPDIRECTSOUNDBUFFER  pThis,
	LPCDSLFODESC         pLFODesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetLFO\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pLFODesc                  : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pLFODesc);

	// TODO: Implement

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuXAudioCreateAdpcmFormat
// ******************************************************************
VOID WINAPI XTL::EmuXAudioCreateAdpcmFormat
(
	WORD                   nChannels,
	DWORD                  nSamplesPerSec,
	LPXBOXADPCMWAVEFORMAT  pwfx
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuXAudioCreateAdpcmFormat\n"
           "(\n"
           "   nChannels                 : 0x%.04X\n"
           "   nSamplesPerSec            : 0x%.08X\n"
           "   pwfx                      : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), nChannels, nSamplesPerSec, pwfx);

	// Fill out the pwfx structure with the appropriate data
	pwfx->wfx.wFormatTag		= WAVE_FORMAT_XBOX_ADPCM;
	pwfx->wfx.nChannels			= nChannels;
	pwfx->wfx.nSamplesPerSec	= nSamplesPerSec;
	pwfx->wfx.nAvgBytesPerSec	= (nSamplesPerSec*nChannels * 36)/64;
	pwfx->wfx.nBlockAlign		= nChannels * 36;
	pwfx->wfx.wBitsPerSample	= 4;
	pwfx->wfx.cbSize			= 2;
	pwfx->wSamplesPerBlock		= 64;

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetRolloffCurve
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetRolloffCurve
(
	LPDIRECTSOUNDBUFFER  pThis,
	const FLOAT         *pflPoints,
	DWORD                dwPointCount,
	DWORD                dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetRolloffCurve\n"
           "(\n"
           "   pThis                     : 0x%.08X\n"
           "   pflPoints                 : 0x%.08X\n"
           "   dwPointCount              : 0x%.08X\n"
		   "   dwApply                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pThis, pflPoints, dwPointCount, dwApply);

	// TODO: Implement

    EmuSwapFS();   // XBox FS

	return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetVolume
(
	LPDIRECTSOUNDSTREAM pStream,
	LONG                lVolume
)
{
    EmuSwapFS();   // Win2k/XP FS

    DbgPrintf("EmuDSound (0x%X): EmuIDirectSoundStream_SetVolume\n"
           "(\n"
           "   pStream                   : 0x%.08X\n"
           "   lVolume                   : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), pStream, lVolume);

	// TODO: Implement

    EmuSwapFS();   // XBox FS

	return DS_OK;
}

// ******************************************************************
// * func: EmuCMcpxBuffer_Stop
// ******************************************************************
HRESULT WINAPI XTL::EmuCMcpxBuffer_Stop
(
	LPVOID				pThis,
	ULONG				Unknown1
)
{
	EmuSwapFS();	// Win2k/XP FS

	DbgPrintf("EmuDSound (0x%X): EmuCMcpxBuffer_Stop\n"
			"(\n"
			"	pThis					: 0x%.08X\n"
			"	Unknown1				: 0x%.08X\n"
			");\n",
			GetCurrentThreadId(), Unknown1 );

	// blueshogun96 3/4/2008
	// Added for Robotech: Battlecry.  Since I don't have a
	// 4928 it took me forever to find this!

	// TODO: Implement ... ?
//	__asm int 3;

	EmuSwapFS();	// XBox FS

	return S_OK;
}