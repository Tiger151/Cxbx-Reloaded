// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
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
// *  (c) 2017 blueshogun96
// *  (c) 2017-2020 RadWolfie
// *
// *  All rights reserved
// *
// ******************************************************************
#define LOG_PREFIX CXBXR_MODULE::DSSTREAM

// prevent name collisions
namespace xboxkrnl {
    #include <xboxkrnl/xboxkrnl.h>
};

#include <dsound.h>
#include "DirectSoundGlobal.hpp" // Global variables

#include "Logging.h"
#include "DirectSoundLogging.hpp"
#include "..\XbDSoundLogging.hpp"


// TODO: Tasks need to do for DirectSound HLE
// * Missing CDirectSoundStream patch
//   * CDirectSoundStream_Set3DVoiceData (new, undocument)
//   * CDirectSoundStream_Use3DVoiceData (new, undocument)
//   * IDirectSoundStream_QueryInterface (not require)
//   * IDirectSoundStream_QueryInterfaceC (not require)

XTL::X_CMcpxStream::_vtbl XTL::X_CMcpxStream::vtbl =
{
    0xBEEFC001,                     // 0x00
    0xBEEFC002,                     // 0x04
    0xBEEFC003,                     // 0x08
    0xBEEFC004,                     // 0x0C
    &XTL::EMUPATCH(CMcpxStream_Dummy_0x10),// 0x10
};

XTL::X_CDirectSoundStream::_vtbl XTL::X_CDirectSoundStream::vtbl =
{
    &XTL::EMUPATCH(CDirectSoundStream_AddRef),          // 0x00
    &XTL::EMUPATCH(CDirectSoundStream_Release),         // 0x04
/*
    STDMETHOD(GetInfo)(THIS_ LPXMEDIAINFO pInfo) PURE;
*/
    &XTL::EMUPATCH(CDirectSoundStream_GetInfo),         // 0x08
    &XTL::EMUPATCH(CDirectSoundStream_GetStatus),       // 0x0C
    &XTL::EMUPATCH(CDirectSoundStream_Process),         // 0x10
    &XTL::EMUPATCH(CDirectSoundStream_Discontinuity),   // 0x14
    &XTL::EMUPATCH(CDirectSoundStream_Flush),           // 0x18
    0xBEEFB003,                                         // 0x1C
    0xBEEFB004,                                         // 0x20
    0xBEEFB005,                                         // 0x24
    0xBEEFB006,                                         // 0x28
    0xBEEFB007,                                         // 0x2C
    0xBEEFB008,                                         // 0x30
    0xBEEFB009,                                         // 0x34
    0xBEEFB00A,                                         // 0x38
};

/* NOTE: SUCCEEDED define is only checking for is equal or greater than zero value.
    And FAILED check for less than zero value. Since DS_OK is only 0 base on DirectSound documentation,
    there is chance of failure which contain value greater than 0.
 */

#include "DirectSoundInline.hpp"

// ******************************************************************
// * patch: DirectSoundDoWork (stream)
// ******************************************************************
void DirectSoundDoWork_Stream(xboxkrnl::LARGE_INTEGER& time)
{
    // Actually, DirectSoundStream need to process buffer packets here.
    vector_ds_stream::iterator ppDSStream = g_pDSoundStreamCache.begin();
    for (; ppDSStream != g_pDSoundStreamCache.end(); ppDSStream++) {
        if ((*ppDSStream)->Host_BufferPacketArray.size() == 0) {
            continue;
        }
        XTL::X_CDirectSoundStream* pThis = (*ppDSStream);
        // TODO: Do we need this in async thread loop?
        if (pThis->Xb_rtPauseEx != 0 && pThis->Xb_rtPauseEx <= time.QuadPart) {
            pThis->Xb_rtPauseEx = 0LL;
            pThis->EmuFlags ^= DSE_FLAG_PAUSE;
            // Don't call play here, let DSoundStreamProcess deal with it.
        }
        if ((pThis->EmuFlags & DSE_FLAG_FLUSH_ASYNC) == 0) {
            DSoundStreamProcess(pThis);
        } else {
            // Confirmed flush packet must be done in DirectSoundDoWork only when title is ready.
            if (pThis->Xb_rtFlushEx != 0 && pThis->Xb_rtFlushEx <= time.QuadPart) {
                pThis->Xb_rtFlushEx = 0LL;
                DSoundStreamProcess(pThis);
            }
        }
    }
}

// ******************************************************************
// * patch: CDirectSoundStream_AddRef
// ******************************************************************
ULONG WINAPI XTL::EMUPATCH(CDirectSoundStream_AddRef)
(
    X_CDirectSoundStream*   pThis)
{
    DSoundMutexGuardLock;

	LOG_FUNC_ONE_ARG(pThis);

    ULONG uRet = HybridDirectSoundBuffer_AddRef(pThis->EmuDirectSoundBuffer8);

    return uRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_Release
// ******************************************************************
ULONG WINAPI XTL::EMUPATCH(CDirectSoundStream_Release)
(
    X_CDirectSoundStream*   pThis)
{
    DSoundMutexGuardLock;

	LOG_FUNC_ONE_ARG(pThis);

    ULONG uRet = 0;
    if (pThis != 0 && (pThis->EmuDirectSoundBuffer8 != 0)) {
        uRet = pThis->EmuDirectSoundBuffer8->Release();

        if (uRet == 0) {
            if (pThis->EmuDirectSound3DBuffer8 != nullptr) {
                pThis->EmuDirectSound3DBuffer8->Release();
            }

            // remove cache entry
            vector_ds_stream::iterator ppDSStream = std::find(g_pDSoundStreamCache.begin(), g_pDSoundStreamCache.end(), pThis);
            if (ppDSStream != g_pDSoundStreamCache.end()) {
                g_pDSoundStreamCache.erase(ppDSStream);
            }

            for (auto buffer = pThis->Host_BufferPacketArray.begin(); buffer != pThis->Host_BufferPacketArray.end();) {
                DSoundStreamClearPacket(buffer, XMP_STATUS_RELEASE_CXBXR, nullptr, nullptr, pThis);
            }

            if (pThis->EmuBufferDesc.lpwfxFormat != nullptr) {
                free(pThis->EmuBufferDesc.lpwfxFormat);
            }
            // NOTE: Do not release X_BufferCache! X_BufferCache is using xbox buffer.

            delete pThis;
        }
    }

    return uRet;
}

// ******************************************************************
// * patch: DirectSoundCreateStream
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(DirectSoundCreateStream)
(
    X_DSSTREAMDESC*         pdssd,
    OUT X_CDirectSoundStream**  ppStream)
{
    DSoundMutexGuardLock;

    // Research reveal DirectSound creation check is part of the requirement.
    if (!g_pDSound8 && !g_bDSoundCreateCalled) {
        HRESULT hRet;

        hRet = XTL::EMUPATCH(DirectSoundCreate)(nullptr, &g_pDSound8, nullptr);
        if (hRet != DS_OK) {
            CxbxKrnlCleanup("Unable to initialize DirectSound!");
        }
    }

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pdssd)
		LOG_FUNC_ARG_OUT(ppStream)
		LOG_FUNC_END;

    HRESULT hRet = DS_OK;

    //If out of space, return out of memory.
    if (X_DIRECTSOUND_CACHE_COUNT == X_DIRECTSOUND_CACHE_MAX) {

        hRet = DSERR_OUTOFMEMORY;
        *ppStream = xbnullptr;
    } else {
        // TODO: Garbage Collection
        *ppStream = new X_CDirectSoundStream();

        DSBUFFERDESC DSBufferDesc = { 0 };


        DWORD dwAcceptableMask = 0x00000010; // TODO: Note 0x00040000 is being ignored (DSSTREAMCAPS_LOCDEFER)

        if (pdssd->dwFlags & (~dwAcceptableMask)) {
            EmuLog(LOG_LEVEL::WARNING, "Use of unsupported pdssd->dwFlags mask(s) (0x%.08X)", pdssd->dwFlags & (~dwAcceptableMask));
        }
        DSBufferDesc.dwSize = sizeof(DSBUFFERDESC);
        //DSBufferDesc->dwFlags = (pdssd->dwFlags & dwAcceptableMask) | DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2;
        DSBufferDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GETCURRENTPOSITION2 | //aka DSBCAPS_DEFAULT + control position
            (g_XBAudio.mute_on_unfocus ? 0 : DSBCAPS_GLOBALFOCUS);

        if ((pdssd->dwFlags & DSBCAPS_CTRL3D) > 0) {
            DSBufferDesc.dwFlags |= DSBCAPS_CTRL3D;
        } else {
            DSBufferDesc.dwFlags |= DSBCAPS_CTRLPAN;
        }

        DSoundBufferSetDefault((*ppStream), DSBPLAY_LOOPING);
        (*ppStream)->Xb_rtFlushEx = 0LL;

        // We have to set DSBufferDesc last due to EmuFlags must be either 0 or previously written value to preserve other flags.
        GeneratePCMFormat(DSBufferDesc, pdssd->lpwfxFormat, (*ppStream)->EmuFlags, 0,
                          xbnullptr, (*ppStream)->X_BufferCacheSize, (*ppStream)->Xb_VoiceProperties, pdssd->lpMixBinsOutput);

        // Test case: Star Wars: KotOR has one packet greater than 5 seconds worth. Increasing to 10 seconds works out fine, can increase more if need to.
        // Allocate at least 10 second worth of bytes in PCM format.
        DSBufferDesc.dwBufferBytes = DSBufferDesc.lpwfxFormat->nAvgBytesPerSec * 10;
        (*ppStream)->EmuBufferDesc = DSBufferDesc;

        (*ppStream)->Host_dwTriggerRange = (DSBufferDesc.lpwfxFormat->nSamplesPerSec / DSBufferDesc.lpwfxFormat->wBitsPerSample);

        (*ppStream)->X_MaxAttachedPackets = pdssd->dwMaxAttachedPackets;
        (*ppStream)->Host_BufferPacketArray.reserve(pdssd->dwMaxAttachedPackets);
        (*ppStream)->Host_dwWriteOffsetNext = 0;
        (*ppStream)->Host_isProcessing = false;
        (*ppStream)->Xb_lpfnCallback = pdssd->lpfnCallback;
        (*ppStream)->Xb_lpvContext = pdssd->lpvContext;
        //TODO: Implement mixbin variable support. Or just merge pdssd struct into DS Stream class.

        EmuLog(LOG_LEVEL::DEBUG, "DirectSoundCreateStream, *ppStream := 0x%.08X", *ppStream);

        hRet = DSoundBufferCreate(&DSBufferDesc, (*ppStream)->EmuDirectSoundBuffer8);
        if (FAILED(hRet)) {
            std::stringstream output;
            output << "Xbox:\n" << pdssd;
            output << "\nHost converison:\n" << DSBufferDesc;
            EmuLog(LOG_LEVEL::WARNING, output.str().c_str());
            output.str("");
            output << static_cast<DS_RESULT>(hRet);
            CxbxKrnlCleanup("DSS: DSoundBufferCreate error: %s", output.str().c_str());
        }
        else {
            if (DSBufferDesc.dwFlags & DSBCAPS_CTRL3D) {
                DSound3DBufferCreate((*ppStream)->EmuDirectSoundBuffer8, (*ppStream)->EmuDirectSound3DBuffer8);
                (*ppStream)->Xb_dwHeadroom = 0; // Default for 3D
            }

            DSoundDebugMuteFlag((*ppStream)->EmuBufferDesc.dwBufferBytes, (*ppStream)->EmuFlags);

            // Pre-set volume to enforce silence if one of audio codec is disabled.
            HybridDirectSoundBuffer_SetVolume((*ppStream)->EmuDirectSoundBuffer8, 0L, (*ppStream)->EmuFlags, nullptr,
                (*ppStream)->Xb_VolumeMixbin, (*ppStream)->Xb_dwHeadroom);

            g_pDSoundStreamCache.push_back(*ppStream);
        }
    }

    return hRet;
}

// ******************************************************************
// * patch: IDirectSound_CreateSoundStream
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSound_CreateSoundStream)
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC*         pdssd,
    OUT X_CDirectSoundStream**  ppStream,
    PVOID                   pUnknown)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("DirectSoundCreateStream");

    HRESULT hRet = EMUPATCH(DirectSoundCreateStream)(pdssd, ppStream);

    return hRet;
}

// ******************************************************************
// * patch: CMcpxStream_Dummy_0x10
// ******************************************************************
VOID WINAPI XTL::EMUPATCH(CMcpxStream_Dummy_0x10)(DWORD dwDummy1, DWORD dwDummy2)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(dwDummy1)
		LOG_FUNC_ARG(dwDummy2)
		LOG_FUNC_END;

    // Causes deadlock in Halo...
    // TODO: Verify that this is a Vista related problem (I HATE Vista!)
//    EmuLog(LOG_LEVEL::WARNING, "EmuCMcpxStream_Dummy_0x10 is ignored!");

    return;
}

/* ------------- Sorted relative functions begin ------------------*/

// ******************************************************************
// * patch: CDirectSoundStream_Discontinuity
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_Discontinuity)
(
    X_CDirectSoundStream*   pThis)
{
    DSoundMutexGuardLock;

	LOG_FUNC_ONE_ARG(pThis);

    // NOTE: Perform check if has pending data. if so, stop stream.
    // default ret = DSERR_GENERIC

    pThis->EmuDirectSoundBuffer8->Stop();
    pThis->Host_isProcessing = false;

    // NOTE: Must reset flags in discontinuity and rtTimeStamps.
    pThis->EmuFlags &= ~(DSE_FLAG_PAUSE | DSE_FLAG_FLUSH_ASYNC | DSE_FLAG_ENVELOPE | DSE_FLAG_ENVELOPE2);
    DSoundBufferSynchPlaybackFlagRemove(pThis->EmuFlags);
    pThis->Xb_rtFlushEx = 0LL;
    pThis->Xb_rtPauseEx = 0LL;

    for (auto buffer = pThis->Host_BufferPacketArray.begin(); buffer != pThis->Host_BufferPacketArray.end();) {
        DSoundStreamClearPacket(buffer, XMP_STATUS_FLUSHED, pThis->Xb_lpfnCallback, pThis->Xb_lpvContext, pThis);
    }

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_Flush
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_Flush)
(
    X_CDirectSoundStream*   pThis)
{
    DSoundMutexGuardLock;

	LOG_FUNC_ONE_ARG(pThis);

    DSoundBufferSynchPlaybackFlagRemove(pThis->EmuFlags);

    // Remove flags only (This is the only place it will remove other than FlushEx perform set/remove the flags.)
    pThis->EmuFlags &= ~(DSE_FLAG_FLUSH_ASYNC | DSE_FLAG_ENVELOPE | DSE_FLAG_ENVELOPE2);
    pThis->Xb_rtFlushEx = 0LL;

    while (DSoundStreamProcess(pThis));

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_FlushEx
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_FlushEx)
(
    X_CDirectSoundStream*   pThis,
    REFERENCE_TIME          rtTimeStamp,
    DWORD                   dwFlags)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(rtTimeStamp)
		LOG_FUNC_ARG(dwFlags)
		LOG_FUNC_END;

    HRESULT hRet = DSERR_INVALIDPARAM;

    // Cannot use rtTimeStamp here, it must be flush.
    if (dwFlags == X_DSSFLUSHEX_IMMEDIATE) {

        hRet = XTL::EMUPATCH(CDirectSoundStream_Flush)(pThis);

    }
    // Remaining flags require X_DSSFLUSHEX_ASYNC to be include.
    else if ((dwFlags & X_DSSFLUSHEX_ASYNC) > 0) {

        pThis->EmuFlags |= DSE_FLAG_FLUSH_ASYNC;
        pThis->Xb_rtFlushEx = rtTimeStamp;

        // Set or remove flags (This is the only place it will set/remove other than Flush perform remove the flags.)
        if ((dwFlags & X_DSSFLUSHEX_ENVELOPE) > 0) {
            if (rtTimeStamp == 0LL) {
                xboxkrnl::LARGE_INTEGER getTime;
                xboxkrnl::KeQuerySystemTime(&getTime);
                pThis->Xb_rtFlushEx = getTime.QuadPart;
            }
            pThis->Xb_rtFlushEx += (pThis->Xb_EnvolopeDesc.dwRelease * 512) / 48000;
        }

        if ((dwFlags & X_DSSFLUSHEX_ENVELOPE2) > 0) {
            pThis->EmuFlags |= DSE_FLAG_ENVELOPE2;
        }
        else {
            pThis->EmuFlags ^= DSE_FLAG_ENVELOPE2;
        }
    }

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_GetInfo
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_GetInfo)
(
    X_CDirectSoundStream*   pThis,
    OUT LPXMEDIAINFO            pInfo)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG_OUT(pInfo)
		LOG_FUNC_END;

    if (pInfo) {
        pInfo->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
        pInfo->dwInputSize = pThis->EmuBufferDesc.lpwfxFormat->nBlockAlign;
        pInfo->dwOutputSize = 0;
        pInfo->dwMaxLookahead = std::max(static_cast<uint32_t>(pThis->EmuBufferDesc.lpwfxFormat->nChannels * static_cast<uint32_t>(pThis->EmuBufferDesc.lpwfxFormat->wBitsPerSample) / 8) * 32, static_cast<uint32_t>(pThis->EmuBufferDesc.lpwfxFormat->nBlockAlign) * 2);
    }

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_GetStatus
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_GetStatus)
(
    X_CDirectSoundStream*   pThis,
    OUT DWORD*              pdwStatus)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG_OUT(pdwStatus)
        LOG_FUNC_END;

    DWORD dwStatusXbox = 0, dwStatusHost;
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetStatus(&dwStatusHost);

    // Convert host to xbox status flag.
    if (hRet == DS_OK) {
        DWORD testSize = pThis->Host_BufferPacketArray.size();
        if ((dwStatusHost & DSBSTATUS_PLAYING) > 0) {
            dwStatusXbox |= X_DSSSTATUS_PLAYING;

        } else {

            if ((pThis->EmuFlags & DSE_FLAG_PAUSE) > 0) {
                dwStatusXbox |= X_DSSSTATUS_PAUSED;

            // Set to paused when has packet(s) queued and is not processing.
            } else if (pThis->Host_BufferPacketArray.size() != 0 && pThis->Host_isProcessing == false) {
                dwStatusXbox |= X_DSSSTATUS_PAUSED;
            }

            if (pThis->Host_BufferPacketArray.size() == 0) {
                dwStatusXbox |= X_DSSSTATUS_STARVED;

                if ((pThis->EmuFlags & DSE_FLAG_ENVELOPE2) > 0) {
                    dwStatusXbox |= X_DSSSTATUS_ENVELOPECOMPLETE;
                }
            }
        }
        if (pThis->Host_BufferPacketArray.size() != pThis->X_MaxAttachedPackets) {
            dwStatusXbox |= X_DSSSTATUS_READY;
        }
        *pdwStatus = dwStatusXbox;

    } else if (pdwStatus != xbnullptr) {
        *pdwStatus = 0;
    }

    EmuLog(LOG_LEVEL::DEBUG, "SET dwStatus = %08X; packet array size: %d",
        *pdwStatus, pThis->Host_BufferPacketArray.size());

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_GetVoiceProperties
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_GetVoiceProperties)
(
    X_CDirectSoundStream*   pThis,
    OUT X_DSVOICEPROPS*     pVoiceProps
)
{
    DSoundMutexGuardLock;
 LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG_OUT(pVoiceProps)
        LOG_FUNC_END;

    if (pVoiceProps == xbnullptr) {
        LOG_TEST_CASE("pVoiceProps == xbnullptr");
        RETURN(DS_OK);
    }

    HRESULT hRet = HybridDirectSoundBuffer_GetVoiceProperties(pThis->Xb_VoiceProperties, pVoiceProps);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_Pause
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_Pause)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwPause)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwPause)
		LOG_FUNC_END;

	if (!pThis) {
		LOG_TEST_CASE("CDirectSoundStream_Pause called with pThis = nullptr");
		return STATUS_SUCCESS;
	}

    HRESULT hRet = HybridDirectSoundBuffer_Pause(pThis->EmuDirectSoundBuffer8, dwPause, pThis->EmuFlags, pThis->EmuPlayFlags,
                                                 pThis->Host_isProcessing, 0LL, pThis->Xb_rtPauseEx);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_PauseEx
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_PauseEx)
(
    X_CDirectSoundStream   *pThis,
    REFERENCE_TIME          rtTimestamp,
    DWORD                   dwPause)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(rtTimestamp)
        LOG_FUNC_ARG(dwPause)
        LOG_FUNC_END;

    // This function wasn't part of the XDK until 4721. (Same as IDirectSoundBuffer_PauseEx?)
    // TODO: Implement time stamp feature (a thread maybe?)

    HRESULT hRet = HybridDirectSoundBuffer_Pause(pThis->EmuDirectSoundBuffer8, dwPause, pThis->EmuFlags, pThis->EmuPlayFlags, 
                                                pThis->Host_isProcessing, rtTimestamp, pThis->Xb_rtPauseEx);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_Process
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_Process)
(
    X_CDirectSoundStream   *pThis,
    PXMEDIAPACKET           pInputBuffer,
    PXMEDIAPACKET           pOutputBuffer)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pInputBuffer)
		LOG_FUNC_ARG(pOutputBuffer)
		LOG_FUNC_END;

    // Research data:
    // * Max packet size permitted is 0x2000 (or 8,192 decimal) of buffer.
    //   * Somehow other titles are using more than 0x2000 for max size. Am using a hacky host method for now (see pBuffer_data).

    if (pThis->EmuDirectSoundBuffer8 != nullptr) {

        if (pInputBuffer != xbnullptr) {

            // Add packets from title until it gets full.
            if (pThis->Host_BufferPacketArray.size() != pThis->X_MaxAttachedPackets) {
                host_voice_packet packet_input;
                packet_input.xmp_data = *pInputBuffer;
                packet_input.xmp_data.dwMaxSize = DSoundBufferGetPCMBufferSize(pThis->EmuFlags, pInputBuffer->dwMaxSize);
                if (packet_input.xmp_data.dwMaxSize == 0) {
                    packet_input.pBuffer_data = nullptr;
                } else {
                    packet_input.pBuffer_data = malloc(packet_input.xmp_data.dwMaxSize);
                    DSoundSGEMemAlloc(packet_input.xmp_data.dwMaxSize);
                }
                packet_input.rangeStart = pThis->Host_dwWriteOffsetNext;
                pThis->Host_dwWriteOffsetNext += packet_input.xmp_data.dwMaxSize;
                if (pThis->EmuBufferDesc.dwBufferBytes <= pThis->Host_dwWriteOffsetNext) {
                    pThis->Host_dwWriteOffsetNext -= pThis->EmuBufferDesc.dwBufferBytes;
                }
                packet_input.isWritten = false;
                packet_input.isPlayed = false;

                DSoundBufferOutputXBtoHost(pThis->EmuFlags, pThis->EmuBufferDesc, pInputBuffer->pvBuffer, pInputBuffer->dwMaxSize, packet_input.pBuffer_data, packet_input.xmp_data.dwMaxSize);
                pThis->Host_BufferPacketArray.push_back(packet_input);

                if (pInputBuffer->pdwStatus != xbnullptr) {
                    (*pInputBuffer->pdwStatus) = XMP_STATUS_PENDING;
                }
                if (pInputBuffer->pdwCompletedSize != xbnullptr) {
                    (*pInputBuffer->pdwCompletedSize) = 0;
                }
                if (pThis->Host_isProcessing == false && pThis->Host_BufferPacketArray.size() == 1) {
                    pThis->EmuDirectSoundBuffer8->SetCurrentPosition(packet_input.rangeStart);
                }
            // Once full it needs to change status to flushed when cannot hold any more packets.
            } else {
                if (pInputBuffer->pdwStatus != xbnullptr) {
                    (*pInputBuffer->pdwStatus) = XMP_STATUS_FLUSHED;
                }
            }
        }

        //TODO: What to do with output buffer audio variable? Need test case or functional source code.
        // NOTE: pOutputBuffer is reserved, must be set to NULL from titles.
        if (pOutputBuffer != xbnullptr) {
            LOG_TEST_CASE("pOutputBuffer is not nullptr, please report title test case to issue tracker. Thanks!");
        }

    } else {
        if (pInputBuffer != xbnullptr && pInputBuffer->pdwStatus != xbnullptr) {
            (*pInputBuffer->pdwStatus) = XMP_STATUS_SUCCESS;
        }
    }

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetAllParameters
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetAllParameters)
(
    X_CDirectSoundStream*   pThis,
    X_DS3DBUFFER*           pc3DBufferParameters,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pc3DBufferParameters)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetAllParameters(pThis->EmuDirectSound3DBuffer8, pc3DBufferParameters, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetConeAngles
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetConeAngles)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwInsideConeAngle,
    DWORD                   dwOutsideConeAngle,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwInsideConeAngle)
		LOG_FUNC_ARG(dwOutsideConeAngle)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetConeAngles(pThis->EmuDirectSound3DBuffer8, dwInsideConeAngle, dwOutsideConeAngle, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetConeOrientation
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetConeOrientation)
(
    X_CDirectSoundStream*   pThis,
    D3DVALUE                x,
    D3DVALUE                y,
    D3DVALUE                z,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(x)
		LOG_FUNC_ARG(y)
		LOG_FUNC_ARG(z)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetConeOrientation(pThis->EmuDirectSound3DBuffer8, x, y, z, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetConeOutsideVolume
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetConeOutsideVolume)
(
    X_CDirectSoundStream*   pThis,
    LONG                    lConeOutsideVolume,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(lConeOutsideVolume)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetConeOutsideVolume(pThis->EmuDirectSound3DBuffer8, lConeOutsideVolume, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetDistanceFactor
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetDistanceFactor)
(
    X_CDirectSoundStream*   pThis,
    FLOAT                   flDistanceFactor,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(flDistanceFactor)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DListener_SetDistanceFactor(g_pDSoundPrimary3DListener8, flDistanceFactor, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetDopplerFactor
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetDopplerFactor)
(
    X_CDirectSoundStream*   pThis,
    FLOAT                   flDopplerFactor,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(flDopplerFactor)
        LOG_FUNC_ARG(dwApply)
        LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DListener_SetDopplerFactor(g_pDSoundPrimary3DListener8, flDopplerFactor, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetEG
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetEG)
(
    X_CDirectSoundStream*   pThis,
    X_DSENVOLOPEDESC*       pEnvelopeDesc)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pEnvelopeDesc)
		LOG_FUNC_END;

    // NOTE: DSP relative function

    LOG_NOT_SUPPORTED();

    pThis->Xb_EnvolopeDesc = *pEnvelopeDesc;

    return S_OK;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetEG
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetEG)
(
    X_CDirectSoundStream*   pThis,
    X_DSENVOLOPEDESC*       pEnvelopeDesc)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetEG");

    return XTL::EMUPATCH(CDirectSoundStream_SetEG)(pThis, pEnvelopeDesc);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetFilter
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetFilter)
(
    X_CDirectSoundStream*   pThis,
    X_DSFILTERDESC*         pFilterDesc)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pFilterDesc)
		LOG_FUNC_END;

    // NOTE: DSP relative function

    LOG_NOT_SUPPORTED();

    return S_OK;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetFilter
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetFilter)
(
    X_CDirectSoundStream*   pThis,
    X_DSFILTERDESC*         pFilterDesc)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetFilter");

    return XTL::EMUPATCH(CDirectSoundStream_SetFilter)(pThis, pFilterDesc);
}

// ******************************************************************
// * patch: CDirectSoundStream::SetFormat
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetFormat)
(
    X_CDirectSoundStream*   pThis,
    LPCWAVEFORMATEX         pwfxFormat)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pwfxFormat)
		LOG_FUNC_END;

    pThis->Host_isProcessing = false;
    pThis->EmuDirectSoundBuffer8->Stop();

    for (auto buffer = pThis->Host_BufferPacketArray.begin(); buffer != pThis->Host_BufferPacketArray.end();) {
        // TODO: Also need to pass down callback and context as well?
        DSoundStreamClearPacket(buffer, XMP_STATUS_FLUSHED, nullptr, nullptr, pThis);
    }

    HRESULT hRet = HybridDirectSoundBuffer_SetFormat(pThis->EmuDirectSoundBuffer8, pwfxFormat, pThis->EmuBufferDesc,
                                             pThis->EmuFlags, pThis->EmuPlayFlags, pThis->EmuDirectSound3DBuffer8,
                                             0, pThis->X_BufferCache, pThis->X_BufferCacheSize,
                                             pThis->Xb_VoiceProperties, xbnullptr, pThis->Xb_Frequency);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetFrequency)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwFrequency)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwFrequency)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetFrequency(pThis->EmuDirectSoundBuffer8, dwFrequency, pThis->Xb_Frequency);

    return hRet;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetFrequency)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwFrequency)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetFrequency");

    return XTL::EMUPATCH(CDirectSoundStream_SetFrequency)(pThis, dwFrequency);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetHeadroom)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwHeadroom)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwHeadroom)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetHeadroom(pThis->EmuDirectSoundBuffer8, dwHeadroom, pThis->Xb_dwHeadroom,
                                                       pThis->Xb_Volume, pThis->Xb_VolumeMixbin, pThis->EmuFlags);

    return hRet;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetHeadroom)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwHeadroom)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetHeadroom");

    return XTL::EMUPATCH(CDirectSoundStream_SetHeadroom)(pThis, dwHeadroom);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetI3DL2Source)
(
    X_CDirectSoundStream*   pThis,
    X_DSI3DL2BUFFER*        pds3db,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pds3db)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    // NOTE: SetI3DL2Source is using DSFXI3DL2Reverb structure, aka different interface.

    // NOTE: DSP relative function

    LOG_NOT_SUPPORTED();

    return S_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetLFO
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetLFO)
(
    X_CDirectSoundStream*   pThis,
    LPCDSLFODESC            pLFODesc)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(pLFODesc)
        LOG_FUNC_END;

    // NOTE: DSP relative function

    LOG_NOT_SUPPORTED();

    return S_OK;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetLFO
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetLFO)
(
    X_CDirectSoundStream*   pThis,
    LPCDSLFODESC            pLFODesc)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetLFO");

    return XTL::EMUPATCH(CDirectSoundStream_SetLFO)(pThis, pLFODesc);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMaxDistance
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMaxDistance)
(
    X_CDirectSoundStream*   pThis,
    D3DVALUE                flMaxDistance,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(flMaxDistance)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetMaxDistance(pThis->EmuDirectSound3DBuffer8, flMaxDistance, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMinDistance
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMinDistance)
(
    X_CDirectSoundStream*   pThis,
    D3DVALUE                fMinDistance,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(fMinDistance)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetMinDistance(pThis->EmuDirectSound3DBuffer8, fMinDistance, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMixBins
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMixBins)
(
    X_CDirectSoundStream*   pThis,
    X_LPDSMIXBINS           pMixBins)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pMixBins)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetMixBins(pThis->Xb_VoiceProperties, pMixBins, pThis->EmuBufferDesc.lpwfxFormat, pThis->EmuBufferDesc);

    return hRet;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetMixBins
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetMixBins)
(
    X_CDirectSoundStream*   pThis,
    X_LPDSMIXBINS           pMixBins)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetMixBins");

    return XTL::EMUPATCH(CDirectSoundStream_SetMixBins)(pThis, pMixBins);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMode
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMode)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwMode,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwMode)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetMode(pThis->EmuDirectSound3DBuffer8, dwMode, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMixBinVolumes_12
// This revision API was used in XDK 3911 until API had changed in XDK 4039.
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMixBinVolumes_12)
(
    X_CDirectSoundStream*   pThis,
    DWORD                   dwMixBinMask,
    const LONG*             alVolumes)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(dwMixBinMask)
		LOG_FUNC_ARG(alVolumes)
		LOG_FUNC_END;

    // NOTE: Use this function for XDK 3911 only because the implementation was changed
    // somewhere around the March 2002 (4361) update (or earlier, maybe).

    LOG_UNIMPLEMENTED();

    return S_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetMixBinVolumes_8
// ******************************************************************
// This revision API is only used in XDK 4039 and higher.
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetMixBinVolumes_8)
(
    X_CDirectSoundStream*   pThis,
    X_LPDSMIXBINS           pMixBins)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pMixBins)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetMixBinVolumes_8(pThis->EmuDirectSoundBuffer8, pMixBins, pThis->Xb_VoiceProperties,
                                                              pThis->EmuFlags, pThis->Xb_Volume, pThis->Xb_VolumeMixbin, pThis->Xb_dwHeadroom);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetOutputBuffer
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetOutputBuffer)
(
    X_CDirectSoundStream*   pThis,
    X_CDirectSoundBuffer*   pOutputBuffer)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pOutputBuffer)
		LOG_FUNC_END;

    // NOTE: SetOutputBuffer is not possible in PC's DirectSound due to 3D controller requirement on ouput buffer to work simultaneously.
    // Test case: Red Faction 2
    // Best to emulate this LLE instead of HLE.

    LOG_NOT_SUPPORTED();

    return S_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetPitch
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetPitch)
(
    X_CDirectSoundStream*   pThis,
    LONG                    lPitch)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(lPitch)
        LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetPitch(pThis->EmuDirectSoundBuffer8, lPitch, pThis->Xb_Frequency);

    return hRet;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetPitch
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetPitch)
(
    X_CDirectSoundStream*   pThis,
    LONG                    lPitch)
{
    DSoundMutexGuardLock;

	LOG_FORWARD("CDirectSoundStream_SetPitch");

    return XTL::EMUPATCH(CDirectSoundStream_SetPitch)(pThis, lPitch);
}

// ******************************************************************
// * patch: CDirectSoundStream_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetPosition)
(
    X_CDirectSoundStream*   pThis,
    D3DVALUE                x,
    D3DVALUE                y,
    D3DVALUE                z,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(x)
		LOG_FUNC_ARG(y)
		LOG_FUNC_ARG(z)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetPosition(pThis->EmuDirectSound3DBuffer8, x, y, z, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream::SetRolloffCurve
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetRolloffCurve)
(
    X_CDirectSoundBuffer*   pThis,
    const FLOAT*            pflPoints,
    DWORD                   dwPointCount,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(pflPoints)
		LOG_FUNC_ARG(dwPointCount)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    LOG_UNIMPLEMENTED();

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetRolloffFactor)
(
    X_CDirectSoundStream*   pThis,
    FLOAT                   fRolloffFactor,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(fRolloffFactor)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    // NOTE: SetRolloffFactor is only supported for host primary buffer's 3D Listener.

    LOG_UNIMPLEMENTED();

    return DS_OK;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetVelocity
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetVelocity)
(
    X_CDirectSoundStream*   pThis,
    D3DVALUE                x,
    D3DVALUE                y,
    D3DVALUE                z,
    DWORD                   dwApply)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(x)
		LOG_FUNC_ARG(y)
		LOG_FUNC_ARG(z)
		LOG_FUNC_ARG(dwApply)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSound3DBuffer_SetVelocity(pThis->EmuDirectSound3DBuffer8, x, y, z, dwApply);

    return hRet;
}

// ******************************************************************
// * patch: CDirectSoundStream_SetVolume
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(CDirectSoundStream_SetVolume)
(
    X_CDirectSoundStream*   pThis,
    LONG                    lVolume)
{
    DSoundMutexGuardLock;

	LOG_FUNC_BEGIN
		LOG_FUNC_ARG(pThis)
		LOG_FUNC_ARG(lVolume)
		LOG_FUNC_END;

    HRESULT hRet = HybridDirectSoundBuffer_SetVolume(pThis->EmuDirectSoundBuffer8, lVolume, pThis->EmuFlags, &pThis->Xb_Volume,
                                                     pThis->Xb_VolumeMixbin, pThis->Xb_dwHeadroom);

    return hRet;
}

// ******************************************************************
// * patch: IDirectSoundStream_SetVolume
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_SetVolume)
(
    X_CDirectSoundStream*   pThis,
    LONG                    lVolume
)
{
    DSoundMutexGuardLock;

    LOG_FORWARD("CDirectSoundStream_SetVolume");

    return XTL::EMUPATCH(CDirectSoundStream_SetVolume)(pThis, lVolume);
}

// ******************************************************************
// * patch:  IDirectSoundStream_Set3DVoiceData
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_Set3DVoiceData)
(
    X_CDirectSoundStream*   pThis,
    DWORD a2
)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(a2)
        LOG_FUNC_END;

    LOG_UNIMPLEMENTED();

    RETURN(STATUS_SUCCESS);
}

// ******************************************************************
// * patch:  IDirectSoundBuffer_Use3DVoiceData
// ******************************************************************
HRESULT WINAPI XTL::EMUPATCH(IDirectSoundStream_Use3DVoiceData)
(
    X_CDirectSoundStream*   pThis,
    DWORD a2
)
{
    DSoundMutexGuardLock;

    LOG_FUNC_BEGIN
        LOG_FUNC_ARG(pThis)
        LOG_FUNC_ARG(a2)
        LOG_FUNC_END;

    LOG_UNIMPLEMENTED();

    RETURN(STATUS_SUCCESS);
}
