/* DirectSound COM interface
 *
 * Copyright 2009 Maarten Lankhorst
 *
 * Some code taken from the original dsound-openal implementation
 *    Copyright 2007-2009 Chris Robinson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#ifdef __WINESRC__

#define COBJMACROS
#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "vfwmsgs.h"
#include "mmsystem.h"
#include "winternl.h"
#include "mmddk.h"
#include "wine/debug.h"
#include "dsound.h"

#include "dsound_private.h"

#include "mmreg.h"
#include "ks.h"
#include "ksmedia.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsound);

#else

#define WINVER 0x0600
#include <windows.h>
#include <dsound.h>

#include "dsound_private.h"

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

#ifndef E_PROP_ID_UNSUPPORTED
#define E_PROP_ID_UNSUPPORTED            ((HRESULT)0x80070490)
#endif

#endif

static const IDirectSoundBufferVtbl DS8Primary_Vtbl;
static const IDirectSound3DListenerVtbl DS8Primary3D_Vtbl;
static const IKsPropertySetVtbl DS8PrimaryProp_Vtbl;

static void AL_APIENTRY wrap_DeferUpdates(void)
{ alcSuspendContext(alcGetCurrentContext()); }
static void AL_APIENTRY wrap_ProcessUpdates(void)
{ alcProcessContext(alcGetCurrentContext()); }


static void trigger_notifies(DS8Buffer *buf, DWORD lastpos, DWORD curpos)
{
    DWORD i;
    for(i = 0; i < buf->nnotify; ++i)
    {
        DSBPOSITIONNOTIFY *not = &buf->notify[i];
        HANDLE event = not->hEventNotify;
        DWORD ofs = not->dwOffset;

        if(ofs == (DWORD)DSBPN_OFFSETSTOP)
            continue;

        /* Wraparound case */
        if(curpos < lastpos)
        {
            if(ofs < curpos || ofs >= lastpos)
                SetEvent(event);
            continue;
        }

        /* Normal case */
        if(ofs >= lastpos && ofs < curpos)
            SetEvent(event);
    }
}

static DWORD CALLBACK ThreadProc(void *dwUser)
{
    DS8Primary *prim = (DS8Primary*)dwUser;
    DWORD i;
    MSG msg;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while(GetMessageA(&msg, NULL, 0, 0))
    {
        if(msg.message != WM_USER)
            continue;

        EnterCriticalSection(&prim->crst);
        setALContext(prim->ctx);

        /* OpenAL doesn't support our lovely buffer extensions
         * so just make sure enough buffers are queued
         */
        if(!prim->ExtAL.BufferSamplesSOFT && !prim->ExtAL.BufferSubData &&
           !prim->ExtAL.BufferDataStatic)
        {
            for(i = 0;i < prim->nbuffers;++i)
            {
                DS8Buffer *buf = prim->buffers[i];
                ALint done = 0, queued = QBUFFERS, state = AL_PLAYING;
                ALuint which, ofs;

                if(buf->buffer->numsegs == 1 || !buf->isplaying)
                    continue;

                alGetSourcei(buf->source, AL_SOURCE_STATE, &state);
                alGetSourcei(buf->source, AL_BUFFERS_QUEUED, &queued);
                alGetSourcei(buf->source, AL_BUFFERS_PROCESSED, &done);

                queued -= done;
                while(done--)
                    alSourceUnqueueBuffers(buf->source, 1, &which);
                while(queued < QBUFFERS)
                {
                    which = buf->buffer->buffers[buf->curidx];
                    ofs = buf->curidx*buf->buffer->segsize;
                    if(buf->curidx < buf->buffer->numsegs-1)
                        alBufferData(which, buf->buffer->buf_format,
                                     buf->buffer->data + ofs, buf->buffer->segsize,
                                     buf->buffer->format.Format.nSamplesPerSec);
                    else
                        alBufferData(which, buf->buffer->buf_format,
                                     buf->buffer->data + ofs, buf->buffer->lastsegsize,
                                     buf->buffer->format.Format.nSamplesPerSec);

                    alSourceQueueBuffers(buf->source, 1, &which);
                    buf->curidx = (buf->curidx+1)%buf->buffer->numsegs;
                    queued++;

                    if(!buf->curidx && !buf->islooping)
                    {
                        buf->isplaying = FALSE;
                        break;
                    }
                }
                if(state != AL_PLAYING)
                {
                    if(!queued)
                    {
                        IDirectSoundBuffer8_Stop(&buf->IDirectSoundBuffer8_iface);
                        continue;
                    }
                    alSourcePlay(buf->source);
                }
                getALError();
            }
        }

        for(i = 0;i < prim->nnotifies;)
        {
            DS8Buffer *buf = prim->notifies[i];
            IDirectSoundBuffer8 *dsb = &buf->IDirectSoundBuffer8_iface;
            DWORD status, curpos;
            HRESULT hr;

            hr = IDirectSoundBuffer8_GetStatus(dsb, &status);
            if(SUCCEEDED(hr))
            {
                if(!(status&DSBSTATUS_PLAYING))
                {
                    /* Stop will remove this buffer from list,
                     * and put another at the current position
                     * don't increment i
                     */
                    IDirectSoundBuffer8_Stop(dsb);
                    continue;
                }
                hr = IDirectSoundBuffer8_GetCurrentPosition(dsb, &curpos, NULL);
                if(SUCCEEDED(hr) && buf->lastpos != curpos)
                {
                    trigger_notifies(buf, buf->lastpos, curpos);
                    buf->lastpos = curpos;
                }
            }
            i++;
        }
        popALContext();
        LeaveCriticalSection(&prim->crst);
    }
    return 0;
}


HRESULT DS8Primary_Create(DS8Primary **ppv, DS8Impl *parent)
{
    HRESULT hr = DSERR_OUTOFMEMORY;
    DS8Primary *This = NULL;
    DS3DLISTENER *listener;
    WAVEFORMATEX *wfx;
    ALuint srcs[256];
    DWORD nsources;

    *ppv = NULL;
    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This) return hr;

    This->IDirectSoundBuffer_iface.lpVtbl = (IDirectSoundBufferVtbl*)&DS8Primary_Vtbl;
    This->IDirectSound3DListener_iface.lpVtbl = (IDirectSound3DListenerVtbl*)&DS8Primary3D_Vtbl;
    This->IKsPropertySet_iface.lpVtbl = (IKsPropertySetVtbl*)&DS8PrimaryProp_Vtbl;

    InitializeCriticalSection(&This->crst);
    This->crst.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": DS8Primary.crst");

    /* Allocate enough for a WAVEFORMATEXTENSIBLE */
    wfx = &This->format.Format;

    hr = DSERR_NODRIVER;
    This->ctx = alcCreateContext(parent->device, NULL);
    if(!This->ctx)
    {
        ALCenum err = alcGetError(parent->device);
        ERR("Could not create context (0x%x)!\n", err);
        goto fail;
    }

    setALContext(This->ctx);
    if(alIsExtensionPresent("AL_EXT_FLOAT32"))
    {
        TRACE("Found AL_EXT_FLOAT32\n");
        This->SupportedExt[EXT_FLOAT32] = AL_TRUE;
    }
    if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
    {
        TRACE("Found AL_EXT_MCFORMATS\n");
        This->SupportedExt[EXT_MCFORMATS] = AL_TRUE;
    }
    if(alIsExtensionPresent("AL_EXT_STATIC_BUFFER"))
    {
        TRACE("Found AL_EXT_STATIC_BUFFER\n");
        This->ExtAL.BufferDataStatic = alGetProcAddress("alBufferDataStatic");
        This->SupportedExt[EXT_STATIC_BUFFER] = AL_TRUE;
    }
    if(alIsExtensionPresent("AL_SOFTX_buffer_samples"))
    {
        TRACE("Found AL_SOFTX_buffer_samples\n");
        This->ExtAL.BufferSamplesSOFT = alGetProcAddress("alBufferSamplesSOFT");
        This->ExtAL.BufferSubSamplesSOFT = alGetProcAddress("alBufferSubSamplesSOFT");
        This->ExtAL.GetBufferSamplesSOFT = alGetProcAddress("alGetBufferSamplesSOFT");
        This->ExtAL.IsBufferFormatSupportedSOFT = alGetProcAddress("alIsBufferFormatSupportedSOFT");
        This->SupportedExt[SOFT_BUFFER_SAMPLES] = AL_TRUE;
    }
    if(alIsExtensionPresent("AL_SOFT_buffer_sub_data"))
    {
        TRACE("Found AL_SOFT_buffer_sub_data\n");
        This->ExtAL.BufferSubData = alGetProcAddress("alBufferSubDataSOFT");
        This->SupportedExt[SOFT_BUFFER_SUB_DATA] = AL_TRUE;
    }
    if(alIsExtensionPresent("AL_SOFTX_deferred_updates"))
    {
        TRACE("Found AL_SOFTX_deferred_updates\n");
        This->ExtAL.DeferUpdates = alGetProcAddress("alDeferUpdatesSOFT");
        This->ExtAL.ProcessUpdates = alGetProcAddress("alProcessUpdatesSOFT");
        This->SupportedExt[SOFT_DEFERRED_UPDATES] = AL_TRUE;
    }
    else
    {
        This->ExtAL.DeferUpdates = wrap_DeferUpdates;
        This->ExtAL.ProcessUpdates = wrap_ProcessUpdates;
    }

    if(alcIsExtensionPresent(parent->device, "ALC_EXT_EFX"))
    {
#define LOAD_FUNC(x) (This->ExtAL.x = alGetProcAddress("al"#x))
        LOAD_FUNC(GenEffects);
        LOAD_FUNC(DeleteEffects);
        LOAD_FUNC(Effecti);
        LOAD_FUNC(Effectf);

        LOAD_FUNC(GenAuxiliaryEffectSlots);
        LOAD_FUNC(DeleteAuxiliaryEffectSlots);
        LOAD_FUNC(AuxiliaryEffectSloti);
#undef LOAD_FUNC
        This->SupportedExt[EXT_EFX] = AL_TRUE;

        This->ExtAL.GenEffects(1, &This->effect);
        This->ExtAL.Effecti(This->effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);

        This->ExtAL.GenAuxiliaryEffectSlots(1, &This->auxslot);
    }
    This->eax_prop = EnvironmentDefaults[EAX_ENVIRONMENT_GENERIC];

    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 2;
    wfx->wBitsPerSample = 8;
    wfx->nSamplesPerSec = 22050;
    wfx->nBlockAlign = wfx->wBitsPerSample * wfx->nChannels / 8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    wfx->cbSize = 0;

    This->stopped = TRUE;
    This->parent = parent;
    /* Apparently primary buffer size is always 32k,
     * tested on windows with 192k 24 bits sound @ 6 channels
     * where it will run out in 60 ms and it isn't pointer aligned
     */
    This->buf_size = 32768;

    if(!This->ExtAL.BufferSubData && !This->ExtAL.BufferSamplesSOFT &&
       !This->ExtAL.BufferDataStatic)
    {
        ERR("Missing alBufferSubDataSOFT, alBufferSamplesSOFT , and alBufferDataStatic on device '%s', sound playback quality may be degraded\n",
             alcGetString(parent->device, ALC_DEVICE_SPECIFIER));
        ERR("Please consider using OpenAL-Soft\n");
    }

    /* Make sure DS3DListener defaults are applied to OpenAL */
    listener = &This->listen;
    listener->dwSize = sizeof(*listener);
    listener->vPosition.x = 0.0;
    listener->vPosition.y = 0.0;
    listener->vPosition.z = 0.0;
    listener->vVelocity.x = 0.0;
    listener->vVelocity.y = 0.0;
    listener->vVelocity.z = 0.0;
    listener->vOrientFront.x = 0.0;
    listener->vOrientFront.y = 0.0;
    listener->vOrientFront.z = 1.0;
    listener->vOrientTop.x = 0.0;
    listener->vOrientTop.y = 1.0;
    listener->vOrientTop.z = 0.0;
    listener->flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
    listener->flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
    listener->flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
    hr = IDirectSound3DListener_SetAllParameters(&This->IDirectSound3DListener_iface, listener, DS3D_IMMEDIATE);
    if(FAILED(hr))
        ERR("Could not set 3d parameters: %08x\n", hr);

    for(nsources = 0;nsources < sizeof(srcs)/sizeof(*srcs);nsources++)
    {
        alGenSources(1, &srcs[nsources]);
        if(alGetError() != AL_NO_ERROR)
            break;
    }
    alDeleteSources(nsources, srcs);
    getALError();

    popALContext();

    This->max_sources = nsources;
    This->sizenotifies = This->sizebuffers = This->sizesources = nsources+1;

    hr = DSERR_OUTOFMEMORY;
    This->sources = HeapAlloc(GetProcessHeap(), 0, nsources*sizeof(*This->sources));
    This->buffers = HeapAlloc(GetProcessHeap(), 0, nsources*sizeof(*This->buffers));
    This->notifies = HeapAlloc(GetProcessHeap(), 0, nsources*sizeof(*This->notifies));
    if(!This->sources || !This->buffers || !This->notifies)
        goto fail;

    This->thread_hdl = CreateThread(NULL, 0, ThreadProc, This, 0, &This->thread_id);
    if(This->thread_hdl == NULL)
        goto fail;

    *ppv = This;
    return S_OK;

fail:
    DS8Primary_Destroy(This);
    return hr;
}

void DS8Primary_Destroy(DS8Primary *This)
{
    TRACE("Destroying primary %p\n", This);

    if(This->timer_id)
    {
        timeKillEvent(This->timer_id);
        timeEndPeriod(This->timer_res);
        TRACE("Killed timer\n");
    }
    if(This->thread_hdl)
    {
        PostThreadMessageA(This->thread_id, WM_QUIT, 0, 0);
        WaitForSingleObject(This->thread_hdl, 1000);
        CloseHandle(This->thread_hdl);
    }

    if(This->ctx)
    {
        /* Calling setALContext is not appropriate here,
         * since we *have* to unset the context before destroying it
         */
        ALCcontext *old_ctx;

        EnterCriticalSection(&openal_crst);
        old_ctx = get_context();
        if(old_ctx != This->ctx)
            set_context(This->ctx);
        else
            old_ctx = NULL;

        while(This->nbuffers--)
            DS8Buffer_Destroy(This->buffers[This->nbuffers]);

        if(This->nsources)
            alDeleteSources(This->nsources, This->sources);

        if(This->effect)
            This->ExtAL.DeleteEffects(1, &This->effect);
        if(This->auxslot)
            This->ExtAL.DeleteAuxiliaryEffectSlots(1, &This->auxslot);

        HeapFree(GetProcessHeap(), 0, This->sources);
        HeapFree(GetProcessHeap(), 0, This->notifies);
        HeapFree(GetProcessHeap(), 0, This->buffers);

        set_context(old_ctx);
        alcDestroyContext(This->ctx);
        LeaveCriticalSection(&openal_crst);
    }

    This->crst.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&This->crst);

    HeapFree(GetProcessHeap(), 0, This);
}

static inline DS8Primary *impl_from_IDirectSoundBuffer(IDirectSoundBuffer *iface)
{
    return CONTAINING_RECORD(iface, DS8Primary, IDirectSoundBuffer_iface);
}

static HRESULT WINAPI DS8Primary_QueryInterface(IDirectSoundBuffer *iface, REFIID riid, LPVOID *ppv)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown) ||
       IsEqualIID(riid, &IID_IDirectSoundBuffer))
        *ppv = &This->IDirectSoundBuffer_iface;
    else if(IsEqualIID(riid, &IID_IDirectSound3DListener))
    {
        if((This->flags&DSBCAPS_CTRL3D))
            *ppv = &This->IDirectSound3DListener_iface;
    }
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI DS8Primary_AddRef(IDirectSoundBuffer *iface)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->ref);
    if(This->ref == 1)
        This->flags = 0;

    return ret;
}

static ULONG WINAPI DS8Primary_Release(IDirectSoundBuffer *iface)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->ref);

    return ret;
}

static HRESULT WINAPI DS8Primary_GetCaps(IDirectSoundBuffer *iface, DSBCAPS *caps)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%p)\n", iface, caps);

    if(!caps || caps->dwSize < sizeof(*caps))
    {
        WARN("Invalid DSBCAPS (%p, %u)\n", caps, caps ? caps->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    caps->dwFlags = This->flags;
    caps->dwBufferBytes = This->buf_size;
    caps->dwUnlockTransferRate = 0;
    caps->dwPlayCpuOverhead = 0;
    LeaveCriticalSection(&This->crst);

    return DS_OK;
}

static HRESULT WINAPI DS8Primary_GetCurrentPosition(IDirectSoundBuffer *iface, DWORD *playpos, DWORD *curpos)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_PRIOLEVELNEEDED;

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_GetCurrentPosition(This->write_emu, playpos, curpos);
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_GetFormat(IDirectSoundBuffer *iface, WAVEFORMATEX *wfx, DWORD allocated, DWORD *written)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;
    UINT size;

    if(!wfx && !written)
    {
        WARN("Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    size = sizeof(This->format.Format) + This->format.Format.cbSize;
    if(written)
        *written = size;
    if(wfx)
    {
        if(allocated < size)
            hr = DSERR_INVALIDPARAM;
        else
            memcpy(wfx, &This->format.Format, size);
    }
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_GetVolume(IDirectSoundBuffer *iface, LONG *volume)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p)\n", iface, volume);

    if(!volume)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->crst);
    if(!(This->flags & DSBCAPS_CTRLVOLUME))
        hr = DSERR_CONTROLUNAVAIL;
    else
    {
        ALfloat gain;
        LONG vol;

        setALContext(This->ctx);
        alGetListenerf(AL_GAIN, &gain);
        getALError();
        popALContext();

        vol = gain_to_mB(gain);
        vol = max(vol, DSBVOLUME_MIN);
        *volume = min(vol, DSBVOLUME_MAX);
    }
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_GetPan(IDirectSoundBuffer *iface, LONG *pan)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    WARN("(%p)->(%p): semi-stub\n", iface, pan);

    if(!pan)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_GetPan(This->write_emu, pan);
    else if(!(This->flags & DSBCAPS_CTRLPAN))
        hr = DSERR_CONTROLUNAVAIL;
    else
        *pan = 0;
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_GetFrequency(IDirectSoundBuffer *iface, DWORD *freq)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    WARN("(%p)->(%p): semi-stub\n", iface, freq);

    if(!freq)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->crst);
    if(!(This->flags & DSBCAPS_CTRLFREQUENCY))
        hr = DSERR_CONTROLUNAVAIL;
    else
        *freq = This->format.Format.nSamplesPerSec;
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_GetStatus(IDirectSoundBuffer *iface, DWORD *status)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%p)\n", iface, status);

    if(!status)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->crst);

    *status = DSBSTATUS_PLAYING|DSBSTATUS_LOOPING;
    if((This->flags&DSBCAPS_LOCDEFER))
        *status |= DSBSTATUS_LOCHARDWARE;

    if(This->stopped)
    {
        DWORD i, state;
        HRESULT hr;

        for(i = 0;i < This->nbuffers;++i)
        {
            hr = IDirectSoundBuffer_GetStatus((IDirectSoundBuffer*)This->buffers[i], &state);
            if(SUCCEEDED(hr) && (state&DSBSTATUS_PLAYING))
                break;
        }
        if(i == This->nbuffers)
        {
            /* Primary stopped and no buffers playing.. */
            *status = 0;
        }
    }

    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary_Initialize(IDirectSoundBuffer *iface, IDirectSound *ds, const DSBUFFERDESC *desc)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p, %p)\n", iface, ds, desc);

    if(!desc || desc->lpwfxFormat || desc->dwBufferBytes)
    {
        WARN("Bad DSBDESC for primary buffer\n");
        return DSERR_INVALIDPARAM;
    }
    if((desc->dwFlags&DSBCAPS_CTRLFX) ||
       (desc->dwFlags&DSBCAPS_CTRLPOSITIONNOTIFY) ||
       (desc->dwFlags&DSBCAPS_LOCSOFTWARE))
    {
        WARN("Bad dwFlags %08x\n", desc->dwFlags);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    /* Should be 0 if not initialized */
    if(This->flags)
    {
        hr = DSERR_ALREADYINITIALIZED;
        goto out;
    }

    if(This->parent->prio_level == DSSCL_WRITEPRIMARY)
    {
        DSBUFFERDESC emudesc;
        DS8Buffer *emu;

        if(This->write_emu)
        {
            ERR("There shouldn't be a write_emu!\n");
            IDirectSoundBuffer8_Release(This->write_emu);
            This->write_emu = NULL;
        }

        memset(&emudesc, 0, sizeof(emudesc));
        emudesc.dwSize = sizeof(emudesc);
        emudesc.dwFlags = DSBCAPS_LOCHARDWARE | (desc->dwFlags&DSBCAPS_CTRLPAN);
        /* Dont play last incomplete sample */
        emudesc.dwBufferBytes = This->buf_size - (This->buf_size%This->format.Format.nBlockAlign);
        emudesc.lpwfxFormat = &This->format.Format;

        hr = DS8Buffer_Create(&emu, This, NULL);
        if(SUCCEEDED(hr))
        {
            This->write_emu = &emu->IDirectSoundBuffer8_iface;
            hr = IDirectSoundBuffer8_Initialize(This->write_emu, ds, &emudesc);
            if(FAILED(hr))
            {
                IDirectSoundBuffer8_Release(This->write_emu);
                This->write_emu = NULL;
            }
        }
    }

    if(SUCCEEDED(hr))
        This->flags = desc->dwFlags | DSBCAPS_LOCHARDWARE;
out:
    LeaveCriticalSection(&This->crst);
    return hr;
}

static HRESULT WINAPI DS8Primary_Lock(IDirectSoundBuffer *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_PRIOLEVELNEEDED;

    TRACE("(%p)->(%u, %u, %p, %p, %p, %p, %u)\n", iface, ofs, bytes, ptr1, len1, ptr2, len2, flags);

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_Lock(This->write_emu, ofs, bytes, ptr1, len1, ptr2, len2, flags);
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_Play(IDirectSoundBuffer *iface, DWORD res1, DWORD res2, DWORD flags)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr;

    TRACE("(%p)->(%u, %u, %u)\n", iface, res1, res2, flags);

    if(!(flags & DSBPLAY_LOOPING))
    {
        WARN("Flags (%08x) not set to DSBPLAY_LOOPING\n", flags);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    hr = S_OK;
    if(This->write_emu)
        hr = IDirectSoundBuffer8_Play(This->write_emu, res1, res2, flags);
    if(SUCCEEDED(hr))
        This->stopped = FALSE;
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_SetCurrentPosition(IDirectSoundBuffer *iface, DWORD pos)
{
    WARN("(%p)->(%u)\n", iface, pos);
    return DSERR_INVALIDCALL;
}

/* Just assume the format is crap, and clean up the damage */
static void copy_waveformat(WAVEFORMATEX *wfx, const WAVEFORMATEX *from)
{
    if(from->wFormatTag == WAVE_FORMAT_PCM)
    {
        wfx->cbSize = 0;
        if(from->wBitsPerSample == 8 ||
           from->wBitsPerSample == 16 ||
           from->wBitsPerSample == 24 ||
           from->wBitsPerSample == 32)
            wfx->wBitsPerSample = from->wBitsPerSample;
    }
    else if(from->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)wfx;
        const WAVEFORMATEXTENSIBLE *fromx = (const WAVEFORMATEXTENSIBLE*)from;
        DWORD size = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

        /* Fail silently.. */
        if(from->cbSize < size)
            return;
        if(!fromx->Samples.wValidBitsPerSample &&
           !fromx->Format.wBitsPerSample)
            return;

        if(!IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM) &&
           !IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            ERR("Unhandled extensible format: %s\n", debugstr_guid(&wfe->SubFormat));
            return;
        }

        wfe->Format.wBitsPerSample = from->wBitsPerSample;
        wfe->Samples.wValidBitsPerSample = fromx->Samples.wValidBitsPerSample;
        if(!wfe->Samples.wValidBitsPerSample)
            wfe->Samples.wValidBitsPerSample = wfe->Format.wBitsPerSample;
        wfe->Format.cbSize = size;
        wfe->dwChannelMask = fromx->dwChannelMask;
        wfe->SubFormat = fromx->SubFormat;
    }
    else
    {
        ERR("Unhandled format tag %04x\n", from->wFormatTag);
        return;
    }

    if(from->nChannels)
        wfx->nChannels = from->nChannels;
    wfx->wFormatTag = from->wFormatTag;
    if(from->nSamplesPerSec >= DSBFREQUENCY_MIN &&
       from->nSamplesPerSec <= DSBFREQUENCY_MAX)
        wfx->nSamplesPerSec = from->nSamplesPerSec;
    wfx->nBlockAlign = wfx->wBitsPerSample * wfx->nChannels / 8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
}

static HRESULT WINAPI DS8Primary_SetFormat(IDirectSoundBuffer *iface, const WAVEFORMATEX *wfx)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;
    ALCint freq;

    TRACE("(%p)->(%p)\n", iface, wfx);

    if(!wfx)
    {
        WARN("Missing format\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);

    if(This->parent->prio_level < DSSCL_PRIORITY)
    {
        hr = DSERR_PRIOLEVELNEEDED;
        goto out;
    }

    TRACE("Requested primary format:\n"
          "    FormatTag      = %04x\n"
          "    Channels       = %u\n"
          "    SamplesPerSec  = %u\n"
          "    AvgBytesPerSec = %u\n"
          "    BlockAlign     = %u\n"
          "    BitsPerSample  = %u\n",
          wfx->wFormatTag, wfx->nChannels,
          wfx->nSamplesPerSec, wfx->nAvgBytesPerSec,
          wfx->nBlockAlign, wfx->wBitsPerSample);

    copy_waveformat(&This->format.Format, wfx);

    freq = This->format.Format.nSamplesPerSec;
    alcGetIntegerv(This->parent->device, ALC_FREQUENCY, 1, &freq);
    getALCError(This->parent->device);

    This->format.Format.nSamplesPerSec = freq;
    This->format.Format.nAvgBytesPerSec = This->format.Format.nBlockAlign *
                                          This->format.Format.nSamplesPerSec;

    if(This->write_emu)
    {
        DS8Buffer *buf;
        DSBUFFERDESC desc;

        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_LOCHARDWARE|DSBCAPS_CTRLPAN;
        desc.dwBufferBytes = This->buf_size - (This->buf_size % This->format.Format.nBlockAlign);
        desc.lpwfxFormat = &This->format.Format;

        hr = DS8Buffer_Create(&buf, This, NULL);
        if(FAILED(hr))
            goto out;

        hr = IDirectSoundBuffer8_Initialize(&buf->IDirectSoundBuffer8_iface, (IDirectSound*)&This->parent->IDirectSound8_iface, &desc);
        if(FAILED(hr))
            DS8Buffer_Destroy(buf);
        else
        {
            IDirectSoundBuffer8_Release(This->write_emu);
            This->write_emu = &buf->IDirectSoundBuffer8_iface;
        }
    }

out:
    LeaveCriticalSection(&This->crst);
    return hr;
}

static HRESULT WINAPI DS8Primary_SetVolume(IDirectSoundBuffer *iface, LONG vol)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%d)\n", iface, vol);

    if(vol > DSBVOLUME_MAX || vol < DSBVOLUME_MIN)
    {
        WARN("Invalid volume (%d)\n", vol);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    if(!(This->flags&DSBCAPS_CTRLVOLUME))
        hr = DSERR_CONTROLUNAVAIL;
    if(SUCCEEDED(hr))
    {
        setALContext(This->ctx);
        alListenerf(AL_GAIN, mB_to_gain(vol));
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_SetPan(IDirectSoundBuffer *iface, LONG pan)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr;

    TRACE("(%p)->(%d)\n", iface, pan);

    if(pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
    {
        WARN("invalid parameter: pan = %d\n", pan);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    if(!(This->flags&DSBCAPS_CTRLPAN))
    {
        WARN("control unavailable\n");
        hr = DSERR_CONTROLUNAVAIL;
    }
    else if(This->write_emu)
        hr = IDirectSoundBuffer8_SetPan(This->write_emu, pan);
    else
    {
        FIXME("Not supported\n");
        hr = E_NOTIMPL;
    }
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_SetFrequency(IDirectSoundBuffer *iface, DWORD freq)
{
    WARN("(%p)->(%u)\n", iface, freq);
    return DSERR_CONTROLUNAVAIL;
}

static HRESULT WINAPI DS8Primary_Stop(IDirectSoundBuffer *iface)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_Stop(This->write_emu);
    if(SUCCEEDED(hr))
        This->stopped = TRUE;
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_Unlock(IDirectSoundBuffer *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_INVALIDCALL;

    TRACE("(%p)->(%p, %u, %p, %u)\n", iface, ptr1, len1, ptr2, len2);

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_Unlock(This->write_emu, ptr1, len1, ptr2, len2);
    LeaveCriticalSection(&This->crst);

    return hr;
}

static HRESULT WINAPI DS8Primary_Restore(IDirectSoundBuffer *iface)
{
    DS8Primary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer8_Restore(This->write_emu);
    LeaveCriticalSection(&This->crst);

    return hr;
}

static const IDirectSoundBufferVtbl DS8Primary_Vtbl =
{
    DS8Primary_QueryInterface,
    DS8Primary_AddRef,
    DS8Primary_Release,
    DS8Primary_GetCaps,
    DS8Primary_GetCurrentPosition,
    DS8Primary_GetFormat,
    DS8Primary_GetVolume,
    DS8Primary_GetPan,
    DS8Primary_GetFrequency,
    DS8Primary_GetStatus,
    DS8Primary_Initialize,
    DS8Primary_Lock,
    DS8Primary_Play,
    DS8Primary_SetCurrentPosition,
    DS8Primary_SetFormat,
    DS8Primary_SetVolume,
    DS8Primary_SetPan,
    DS8Primary_SetFrequency,
    DS8Primary_Stop,
    DS8Primary_Unlock,
    DS8Primary_Restore
};

static inline DS8Primary *impl_from_IDirectSound3DListener(IDirectSound3DListener *iface)
{
    return CONTAINING_RECORD(iface, DS8Primary, IDirectSound3DListener_iface);
}

static HRESULT WINAPI DS8Primary3D_QueryInterface(IDirectSound3DListener *iface, REFIID riid, void **ppv)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    return IDirectSoundBuffer_QueryInterface((IDirectSoundBuffer*)This, riid, ppv);
}

static ULONG WINAPI DS8Primary3D_AddRef(IDirectSound3DListener *iface)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->ds3d_ref);
    TRACE("new refcount %d\n", ret);

    return ret;
}


/* Considering the primary buffer doesn't get destroyed
 * it doesn't make sense to destroy ds3d here
 */
static ULONG WINAPI DS8Primary3D_Release(IDirectSound3DListener *iface)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->ds3d_ref);
    TRACE("new refcount %d\n", ret);

    return ret;
}


static HRESULT WINAPI DS8Primary3D_GetAllParameters(IDirectSound3DListener *iface, DS3DLISTENER *listener)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
//    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, listener);

    if(!listener || listener->dwSize < sizeof(*listener))
    {
        WARN("Invalid DS3DLISTENER %p %u\n", listener, listener ? listener->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);
    IDirectSound3DListener_GetPosition(iface, &listener->vPosition);
    IDirectSound3DListener_GetVelocity(iface, &listener->vVelocity);
    IDirectSound3DListener_GetOrientation(iface, &listener->vOrientFront, &listener->vOrientTop);
    IDirectSound3DListener_GetDistanceFactor(iface, &listener->flDistanceFactor);
    IDirectSound3DListener_GetRolloffFactor(iface, &listener->flRolloffFactor);
    IDirectSound3DListener_GetRolloffFactor(iface, &listener->flDopplerFactor);
    popALContext();
    LeaveCriticalSection(&This->crst);

//    return hr;
    return DS_OK;
}

static HRESULT WINAPI DS8Primary3D_GetDistanceFactor(IDirectSound3DListener *iface, D3DVALUE *distancefactor)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, distancefactor);

    if(!distancefactor)
    {
        WARN("Invalid parameter %p\n", distancefactor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);

    *distancefactor = 343.3f/alGetFloat(AL_SPEED_OF_SOUND);
    getALError();

    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_GetDopplerFactor(IDirectSound3DListener *iface, D3DVALUE *dopplerfactor)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, dopplerfactor);

    if(!dopplerfactor)
    {
        WARN("Invalid parameter %p\n", dopplerfactor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);

    *dopplerfactor = alGetFloat(AL_DOPPLER_FACTOR);
    getALError();

    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_GetOrientation(IDirectSound3DListener *iface, D3DVECTOR *front, D3DVECTOR *top)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    ALfloat orient[6];

    TRACE("(%p)->(%p, %p)\n", iface, front, top);

    if(!front || !top)
    {
        WARN("Invalid parameter %p %p\n", front, top);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);

    alGetListenerfv(AL_ORIENTATION, orient);
    getALError();

    front->x =  orient[0];
    front->y =  orient[1];
    front->z = -orient[2];
    top->x =  orient[3];
    top->y =  orient[4];
    top->z = -orient[5];

    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_GetPosition(IDirectSound3DListener *iface, D3DVECTOR *pos)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    ALfloat alpos[3];

    TRACE("(%p)->(%p)\n", iface, pos);

    if(!pos)
    {
        WARN("Invalid parameter %p\n", pos);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);

    alGetListenerfv(AL_POSITION, alpos);
    getALError();

    pos->x =  alpos[0];
    pos->y =  alpos[1];
    pos->z = -alpos[2];

    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_GetRolloffFactor(IDirectSound3DListener *iface, D3DVALUE *rollofffactor)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, rollofffactor);

    if(!rollofffactor)
    {
        WARN("Invalid parameter %p\n", rollofffactor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    *rollofffactor = This->rollofffactor;
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_GetVelocity(IDirectSound3DListener *iface, D3DVECTOR *velocity)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    ALfloat vel[3];

    TRACE("(%p)->(%p)\n", iface, velocity);

    if(!velocity)
    {
        WARN("Invalid parameter %p\n", velocity);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);

    alGetListenerfv(AL_VELOCITY, vel);
    getALError();

    velocity->x =  vel[0];
    velocity->y =  vel[1];
    velocity->z = -vel[2];

    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetAllParameters(IDirectSound3DListener *iface, const DS3DLISTENER *listen, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p, %u)\n", iface, listen, apply);

    if(!listen || listen->dwSize < sizeof(*listen))
    {
        WARN("Invalid parameter %p %u\n", listen, listen ? listen->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(listen->flDistanceFactor > DS3D_MAXDISTANCEFACTOR ||
       listen->flDistanceFactor < DS3D_MINDISTANCEFACTOR)
    {
        WARN("Invalid distance factor (%f)\n", listen->flDistanceFactor);
        return DSERR_INVALIDPARAM;
    }

    if(listen->flDopplerFactor > DS3D_MAXDOPPLERFACTOR ||
       listen->flDopplerFactor < DS3D_MINDOPPLERFACTOR)
    {
        WARN("Invalid doppler factor (%f)\n", listen->flDopplerFactor);
        return DSERR_INVALIDPARAM;
    }

    if(listen->flRolloffFactor < DS3D_MINROLLOFFFACTOR ||
       listen->flRolloffFactor > DS3D_MAXROLLOFFFACTOR)
    {
        WARN("Invalid rolloff factor (%f)\n", listen->flRolloffFactor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);
    IDirectSound3DListener_SetPosition(iface, listen->vPosition.x, listen->vPosition.y, listen->vPosition.z, apply);
    IDirectSound3DListener_SetVelocity(iface, listen->vVelocity.x, listen->vVelocity.y, listen->vVelocity.z, apply);
    IDirectSound3DListener_SetOrientation(iface, listen->vOrientFront.x, listen->vOrientFront.y, listen->vOrientFront.z,
                                          listen->vOrientTop.x, listen->vOrientTop.y, listen->vOrientTop.z, apply);
    IDirectSound3DListener_SetDistanceFactor(iface, listen->flDistanceFactor, apply);
    IDirectSound3DListener_SetRolloffFactor(iface, listen->flRolloffFactor, apply);
    IDirectSound3DListener_SetDopplerFactor(iface, listen->flDopplerFactor, apply);
    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetDistanceFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %u)\n", iface, factor, apply);

    if(factor < DS3D_MINDISTANCEFACTOR ||
       factor > DS3D_MAXDISTANCEFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.flDistanceFactor = factor;
        This->dirty.bit.distancefactor = 1;
    }
    else
    {
        setALContext(This->ctx);
        alSpeedOfSound(343.3f/factor);
        if(This->SupportedExt[EXT_EFX])
            alListenerf(AL_METERS_PER_UNIT, factor);
        getALError();
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetDopplerFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %u)\n", iface, factor, apply);

    if(factor < DS3D_MINDOPPLERFACTOR ||
       factor > DS3D_MAXDOPPLERFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.flDopplerFactor = factor;
        This->dirty.bit.dopplerfactor = 1;
    }
    else
    {
        setALContext(This->ctx);
        alDopplerFactor(factor);
        getALError();
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetOrientation(IDirectSound3DListener *iface, D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront, D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %f, %f, %f, %u)\n", iface, xFront, yFront, zFront, xTop, yTop, zTop, apply);

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.vOrientFront.x = xFront;
        This->listen.vOrientFront.y = yFront;
        This->listen.vOrientFront.z = zFront;
        This->listen.vOrientTop.x = xTop;
        This->listen.vOrientTop.y = yTop;
        This->listen.vOrientTop.z = zTop;
        This->dirty.bit.orientation = 1;
    }
    else
    {
        ALfloat orient[6] = {
            xFront, yFront, -zFront,
            xTop, yTop, -zTop
        };
        setALContext(This->ctx);
        alListenerfv(AL_ORIENTATION, orient);
        getALError();
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetPosition(IDirectSound3DListener *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %u)\n", iface, x, y, z, apply);

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.vPosition.x = x;
        This->listen.vPosition.y = y;
        This->listen.vPosition.z = z;
        This->dirty.bit.pos = 1;
    }
    else
    {
        setALContext(This->ctx);
        alListener3f(AL_POSITION, x, y, -z);
        getALError();
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetRolloffFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %u)\n", iface, factor, apply);

    if(factor < DS3D_MINROLLOFFFACTOR ||
       factor > DS3D_MAXROLLOFFFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.flRolloffFactor = factor;
        This->dirty.bit.rollofffactor = 1;
    }
    else
    {
        DWORD i;

        setALContext(This->ctx);
        for(i = 0;i < This->nbuffers;++i)
        {
            if(This->buffers[i]->ds3dmode != DS3DMODE_DISABLE)
                alSourcef(This->buffers[i]->source, AL_ROLLOFF_FACTOR, factor);
        }
        getALError();
        popALContext();

        This->rollofffactor = factor;
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_SetVelocity(IDirectSound3DListener *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %u)\n", iface, x, y, z, apply);

    EnterCriticalSection(&This->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->listen.vVelocity.x = x;
        This->listen.vVelocity.y = y;
        This->listen.vVelocity.z = z;
        This->dirty.bit.vel = 1;
    }
    else
    {
        setALContext(This->ctx);
        alListener3f(AL_VELOCITY, x, y, -z);
        getALError();
        popALContext();
    }
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static HRESULT WINAPI DS8Primary3D_CommitDeferredSettings(IDirectSound3DListener *iface)
{
    DS8Primary *This = impl_from_IDirectSound3DListener(iface);
    const DS3DLISTENER *listen = &This->listen;
    DWORD i;

    EnterCriticalSection(&This->crst);
    setALContext(This->ctx);
    This->ExtAL.DeferUpdates();

    if(This->dirty.bit.pos)
        alListener3f(AL_POSITION, listen->vPosition.x, listen->vPosition.y, -listen->vPosition.z);
    if(This->dirty.bit.vel)
        alListener3f(AL_VELOCITY, listen->vVelocity.x, listen->vVelocity.y, -listen->vVelocity.z);
    if(This->dirty.bit.orientation)
    {
        ALfloat orient[6] = {
            listen->vOrientFront.x, listen->vOrientFront.y, -listen->vOrientFront.z,
            listen->vOrientTop.x, listen->vOrientTop.y, -listen->vOrientTop.z
        };
        alListenerfv(AL_ORIENTATION, orient);
    }
    if(This->dirty.bit.distancefactor)
    {
        alSpeedOfSound(343.3f/listen->flDistanceFactor);
        if(This->SupportedExt[EXT_EFX])
            alListenerf(AL_METERS_PER_UNIT, listen->flDistanceFactor);
    }

    if(This->dirty.bit.rollofffactor)
    {
        ALfloat rolloff = This->rollofffactor;
        for(i = 0;i < This->nbuffers;++i)
        {
            DS8Buffer *buf = This->buffers[i];
            if(buf->ds3dmode != DS3DMODE_DISABLE)
                alSourcef(buf->source, AL_ROLLOFF_FACTOR, rolloff);
        }
    }

    if(This->dirty.bit.dopplerfactor)
        alDopplerFactor(listen->flDopplerFactor);

    if(This->dirty.bit.effect)
        This->ExtAL.AuxiliaryEffectSloti(This->auxslot, AL_EFFECTSLOT_EFFECT, This->effect);

    /* getALError is here for debugging */
    getALError();

    TRACE("Dirty flags was: 0x%02x\n", This->dirty.flags);
    This->dirty.flags = 0;

    for(i = 0;i < This->nbuffers;++i)
    {
        DS8Buffer *buf = This->buffers[i];

        if(!buf->dirty.flags)
            continue;

        if(buf->dirty.bit.pos)
            alSource3f(buf->source, AL_POSITION,
                       buf->ds3dbuffer.vPosition.x,
                       buf->ds3dbuffer.vPosition.y,
                      -buf->ds3dbuffer.vPosition.z);
        if(buf->dirty.bit.vel)
            alSource3f(buf->source, AL_VELOCITY,
                       buf->ds3dbuffer.vVelocity.x,
                       buf->ds3dbuffer.vVelocity.y,
                      -buf->ds3dbuffer.vVelocity.z);
        if(buf->dirty.bit.cone_angles)
        {
            alSourcei(buf->source, AL_CONE_INNER_ANGLE,
                      buf->ds3dbuffer.dwInsideConeAngle);
            alSourcei(buf->source, AL_CONE_OUTER_ANGLE,
                      buf->ds3dbuffer.dwOutsideConeAngle);
        }
        if(buf->dirty.bit.cone_orient)
            alSource3f(buf->source, AL_DIRECTION,
                       buf->ds3dbuffer.vConeOrientation.x,
                       buf->ds3dbuffer.vConeOrientation.y,
                      -buf->ds3dbuffer.vConeOrientation.z);
        if(buf->dirty.bit.cone_outsidevolume)
            alSourcef(buf->source, AL_CONE_OUTER_GAIN,
                      mB_to_gain(buf->ds3dbuffer.lConeOutsideVolume));
        if(buf->dirty.bit.min_distance)
            alSourcef(buf->source, AL_REFERENCE_DISTANCE, buf->ds3dbuffer.flMinDistance);
        if(buf->dirty.bit.max_distance)
            alSourcef(buf->source, AL_MAX_DISTANCE, buf->ds3dbuffer.flMaxDistance);
        if(buf->dirty.bit.mode)
        {
            buf->ds3dmode = buf->ds3dbuffer.dwMode;
            alSourcei(buf->source, AL_SOURCE_RELATIVE,
                      (buf->ds3dmode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
            alSourcef(buf->source, AL_ROLLOFF_FACTOR,
                      (buf->ds3dmode==DS3DMODE_DISABLE) ? 0.0f : This->rollofffactor);
        }
        buf->dirty.flags = 0;
    }
    getALError();

    This->ExtAL.ProcessUpdates();
    popALContext();
    LeaveCriticalSection(&This->crst);

    return S_OK;
}

static const IDirectSound3DListenerVtbl DS8Primary3D_Vtbl =
{
    DS8Primary3D_QueryInterface,
    DS8Primary3D_AddRef,
    DS8Primary3D_Release,
    DS8Primary3D_GetAllParameters,
    DS8Primary3D_GetDistanceFactor,
    DS8Primary3D_GetDopplerFactor,
    DS8Primary3D_GetOrientation,
    DS8Primary3D_GetPosition,
    DS8Primary3D_GetRolloffFactor,
    DS8Primary3D_GetVelocity,
    DS8Primary3D_SetAllParameters,
    DS8Primary3D_SetDistanceFactor,
    DS8Primary3D_SetDopplerFactor,
    DS8Primary3D_SetOrientation,
    DS8Primary3D_SetPosition,
    DS8Primary3D_SetRolloffFactor,
    DS8Primary3D_SetVelocity,
    DS8Primary3D_CommitDeferredSettings
};

/* NOTE: Although the app handles listener properties through secondary buffers,
 * we pass the requests to the primary buffer though a propertyset interface.
 * These methods are not exposed to the app. */
static inline DS8Primary *impl_from_IKsPropertySet(IKsPropertySet *iface)
{
    return CONTAINING_RECORD(iface, DS8Primary, IKsPropertySet_iface);
}

static HRESULT WINAPI DS8PrimaryProp_QueryInterface(IKsPropertySet *iface, REFIID riid, void **ppv)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    return IDirectSoundBuffer_QueryInterface((IDirectSoundBuffer*)This, riid, ppv);
}

static ULONG WINAPI DS8PrimaryProp_AddRef(IKsPropertySet *iface)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->prop_ref);
    TRACE("new refcount %d\n", ret);

    return ret;
}

static ULONG WINAPI DS8PrimaryProp_Release(IKsPropertySet *iface)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->prop_ref);
    TRACE("new refcount %d\n", ret);

    return ret;
}

static HRESULT WINAPI DS8PrimaryProp_Get(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData,
  PULONG pcbReturned)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    HRESULT res = E_PROP_ID_UNSUPPORTED;

    TRACE("(%p)->(%s, %u, %p, %u, %p, %u, %p)\n", iface, debugstr_guid(guidPropSet),
          dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData, pcbReturned);

    if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties))
    {
        EnterCriticalSection(&This->crst);

        if(This->effect == 0)
            res = E_PROP_ID_UNSUPPORTED;
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ALLPARAMETERS)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(EAXLISTENERPROPERTIES))
            {
                union {
                    void *v;
                    EAXLISTENERPROPERTIES *props;
                } data = { pPropData };

                *data.props = This->eax_prop;
                *pcbReturned = sizeof(EAXLISTENERPROPERTIES);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ROOM)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(LONG))
            {
                union {
                    void *v;
                    LONG *l;
                } data = { pPropData };

                *data.l = This->eax_prop.lRoom;
                *pcbReturned = sizeof(LONG);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ROOMHF)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(LONG))
            {
                union {
                    void *v;
                    LONG *l;
                } data = { pPropData };

                *data.l = This->eax_prop.lRoomHF;
                *pcbReturned = sizeof(LONG);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ROOMROLLOFFFACTOR)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    void *v;
                    FLOAT *fl;
                } data = { pPropData };

                *data.fl = This->eax_prop.flRoomRolloffFactor;
                *pcbReturned = sizeof(FLOAT);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENT)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(DWORD))
            {
                union {
                    void *v;
                    DWORD *dw;
                } data = { pPropData };

                *data.dw = This->eax_prop.dwEnvironment;
                *pcbReturned = sizeof(DWORD);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    void *v;
                    FLOAT *fl;
                } data = { pPropData };

                *data.fl = This->eax_prop.flEnvironmentSize;
                *pcbReturned = sizeof(FLOAT);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENTDIFFUSION)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    void *v;
                    FLOAT *fl;
                } data = { pPropData };

                *data.fl = This->eax_prop.flEnvironmentDiffusion;
                *pcbReturned = sizeof(FLOAT);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_AIRABSORPTIONHF)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    void *v;
                    FLOAT *fl;
                } data = { pPropData };

                *data.fl = This->eax_prop.flAirAbsorptionHF;
                *pcbReturned = sizeof(FLOAT);
                res = DS_OK;
            }
        }
        else if(dwPropID == DSPROPERTY_EAXLISTENER_FLAGS)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(DWORD))
            {
                union {
                    void *v;
                    DWORD *dw;
                } data = { pPropData };

                *data.dw = This->eax_prop.dwFlags;
                *pcbReturned = sizeof(DWORD);
                res = DS_OK;
            }
        }
        else
            FIXME("Unhandled propid: 0x%08x\n", dwPropID);

        LeaveCriticalSection(&This->crst);
    }
    else
        FIXME("Unhandled propset: %s\n", debugstr_guid(guidPropSet));

    return res;
}

static HRESULT WINAPI DS8PrimaryProp_Set(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    HRESULT res = E_PROP_ID_UNSUPPORTED;

    TRACE("(%p)->(%s, %u, %p, %u, %p, %u)\n", iface, debugstr_guid(guidPropSet),
          dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData);

    if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties))
    {
        DWORD propid = dwPropID & ~DSPROPERTY_EAXLISTENER_DEFERRED;
        BOOL immediate = !(dwPropID&DSPROPERTY_EAXLISTENER_DEFERRED);

        EnterCriticalSection(&This->crst);
        setALContext(This->ctx);

        if(This->effect == 0)
            res = E_PROP_ID_UNSUPPORTED;
        else if(propid == DSPROPERTY_EAXLISTENER_ALLPARAMETERS)
        {
        do_allparams:
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(EAXLISTENERPROPERTIES))
            {
                union {
                    const void *v;
                    const EAXLISTENERPROPERTIES *props;
                } data = { pPropData };

                /* FIXME: Need to validate property values... Ignore? Clamp? Error? */
                This->eax_prop = *data.props;
                This->ExtAL.Effectf(This->effect, AL_REVERB_DENSITY,
                                    (data.props->flEnvironmentSize < 2.0f) ?
                                    (data.props->flEnvironmentSize - 1.0f) : 1.0f);
                This->ExtAL.Effectf(This->effect, AL_REVERB_DIFFUSION,
                                    data.props->flEnvironmentDiffusion);

                This->ExtAL.Effectf(This->effect, AL_REVERB_GAIN,
                                    mB_to_gain(data.props->lRoom));
                This->ExtAL.Effectf(This->effect, AL_REVERB_GAINHF,
                                    mB_to_gain(data.props->lRoomHF));

                This->ExtAL.Effectf(This->effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,
                                    data.props->flRoomRolloffFactor);

                This->ExtAL.Effectf(This->effect, AL_REVERB_DECAY_TIME,
                                    data.props->flDecayTime);
                This->ExtAL.Effectf(This->effect, AL_REVERB_DECAY_HFRATIO,
                                    data.props->flDecayHFRatio);

                This->ExtAL.Effectf(This->effect, AL_REVERB_REFLECTIONS_GAIN,
                                    mB_to_gain(data.props->lReflections));
                This->ExtAL.Effectf(This->effect, AL_REVERB_REFLECTIONS_DELAY,
                                    data.props->flReflectionsDelay);

                This->ExtAL.Effectf(This->effect, AL_REVERB_LATE_REVERB_GAIN,
                                    mB_to_gain(data.props->lReverb));
                This->ExtAL.Effectf(This->effect, AL_REVERB_LATE_REVERB_DELAY,
                                    data.props->flReverbDelay);

                This->ExtAL.Effectf(This->effect, AL_REVERB_AIR_ABSORPTION_GAINHF,
                                    mB_to_gain(data.props->flAirAbsorptionHF));

                This->ExtAL.Effecti(This->effect, AL_REVERB_DECAY_HFLIMIT,
                                    (data.props->dwFlags&EAXLISTENERFLAGS_DECAYHFLIMIT) ?
                                    AL_TRUE : AL_FALSE);

                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ROOM)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(LONG))
            {
                union {
                    const void *v;
                    const LONG *l;
                } data = { pPropData };

                This->eax_prop.lRoom = *data.l;
                This->ExtAL.Effectf(This->effect, AL_REVERB_GAIN,
                                    mB_to_gain(This->eax_prop.lRoom));
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ROOMHF)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(LONG))
            {
                union {
                    const void *v;
                    const LONG *l;
                } data = { pPropData };

                This->eax_prop.lRoomHF = *data.l;
                This->ExtAL.Effectf(This->effect, AL_REVERB_GAINHF,
                                    mB_to_gain(This->eax_prop.lRoomHF));
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ROOMROLLOFFFACTOR)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    const void *v;
                    const FLOAT *fl;
                } data = { pPropData };

                This->eax_prop.flRoomRolloffFactor = *data.fl;
                This->ExtAL.Effectf(This->effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,
                                    This->eax_prop.flRoomRolloffFactor);
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ENVIRONMENT)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(DWORD))
            {
                union {
                    const void *v;
                    const DWORD *dw;
                } data = { pPropData };

                if(*data.dw < EAX_ENVIRONMENT_COUNT)
                {
                    /* Get the environment index's default and pass it down to
                     * ALLPARAMETERS */
                    propid = DSPROPERTY_EAXLISTENER_ALLPARAMETERS;
                    pPropData = (void*)&EnvironmentDefaults[*data.dw];
                    cbPropData = sizeof(EnvironmentDefaults[*data.dw]);
                    goto do_allparams;
                }
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    const void *v;
                    const FLOAT *fl;
                } data = { pPropData };

                if(*data.fl >= 1.0f && *data.fl <= 100.0f)
                {
                    double scale = (*data.fl)/This->eax_prop.flEnvironmentSize;

                    This->eax_prop.flEnvironmentSize = *data.fl;

                    if((This->eax_prop.dwFlags&EAXLISTENERFLAGS_DECAYTIMESCALE))
                    {
                        This->eax_prop.flDecayTime *= scale;
                        This->eax_prop.flDecayTime = clampF(This->eax_prop.flDecayTime, 0.1f, 20.0f);
                    }
                    if((This->eax_prop.dwFlags&EAXLISTENERFLAGS_REFLECTIONSSCALE))
                    {
                        This->eax_prop.lReflections += gain_to_mB(1.0/scale);
                        This->eax_prop.lReflections = clampI(This->eax_prop.lReflections, -10000, 1000);
                    }
                    if((This->eax_prop.dwFlags&EAXLISTENERFLAGS_REFLECTIONSDELAYSCALE))
                    {
                        This->eax_prop.flReflectionsDelay *= scale;
                        This->eax_prop.flReflectionsDelay = clampF(This->eax_prop.flReflectionsDelay, 0.0f, 0.3f);
                    }
                    if((This->eax_prop.dwFlags&EAXLISTENERFLAGS_REVERBSCALE))
                    {
                        This->eax_prop.lReverb += gain_to_mB(1.0/scale);
                        This->eax_prop.lReverb = clampI(This->eax_prop.lReverb, -10000, 2000);
                    }
                    if((This->eax_prop.dwFlags&EAXLISTENERFLAGS_REVERBDELAYSCALE))
                    {
                        This->eax_prop.flReverbDelay *= scale;
                        This->eax_prop.flReverbDelay = clampF(This->eax_prop.flReverbDelay, 0.0f, 0.1f);
                    }

                    /* Pass the updated environment properties down to ALLPARAMETERS */
                    propid = DSPROPERTY_EAXLISTENER_ALLPARAMETERS;
                    pPropData = (void*)&This->eax_prop;
                    cbPropData = sizeof(This->eax_prop);
                    goto do_allparams;
                }
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_ENVIRONMENTDIFFUSION)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    const void *v;
                    const FLOAT *fl;
                } data = { pPropData };

                This->eax_prop.flEnvironmentDiffusion = *data.fl;
                This->ExtAL.Effectf(This->effect, AL_REVERB_DIFFUSION,
                                    This->eax_prop.flEnvironmentDiffusion);
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_AIRABSORPTIONHF)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(FLOAT))
            {
                union {
                    const void *v;
                    const FLOAT *fl;
                } data = { pPropData };

                This->eax_prop.flAirAbsorptionHF = *data.fl;
                This->ExtAL.Effectf(This->effect, AL_REVERB_AIR_ABSORPTION_GAINHF,
                                    mB_to_gain(This->eax_prop.flAirAbsorptionHF));
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid == DSPROPERTY_EAXLISTENER_FLAGS)
        {
            res = DSERR_INVALIDPARAM;
            if(cbPropData >= sizeof(DWORD))
            {
                union {
                    const void *v;
                    const DWORD *dw;
                } data = { pPropData };

                This->eax_prop.dwFlags = *data.dw;
                This->ExtAL.Effecti(This->effect, AL_REVERB_DECAY_HFLIMIT,
                                    (This->eax_prop.dwFlags&EAXLISTENERFLAGS_DECAYHFLIMIT) ?
                                    AL_TRUE : AL_FALSE);
                getALError();

                This->dirty.bit.effect = 1;
                res = DS_OK;
            }
        }
        else if(propid != 0)
            FIXME("Unhandled propid: 0x%08x\n", propid);

        if(res == DS_OK && immediate)
            IDirectSound3DListener_CommitDeferredSettings(&This->IDirectSound3DListener_iface);

        popALContext();
        LeaveCriticalSection(&This->crst);
    }
    else
        FIXME("Unhandled propset: %s\n", debugstr_guid(guidPropSet));

    return res;
}

static HRESULT WINAPI DS8PrimaryProp_QuerySupport(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  PULONG pTypeSupport)
{
    DS8Primary *This = impl_from_IKsPropertySet(iface);
    HRESULT res = E_PROP_ID_UNSUPPORTED;

    TRACE("(%p)->(%s, %u, %p)\n", iface, debugstr_guid(guidPropSet), dwPropID, pTypeSupport);

    if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties))
    {
        EnterCriticalSection(&This->crst);

        if(This->effect == 0)
            res = E_PROP_ID_UNSUPPORTED;
        else if(dwPropID == DSPROPERTY_EAXLISTENER_ALLPARAMETERS ||
                dwPropID == DSPROPERTY_EAXLISTENER_ROOM ||
                dwPropID == DSPROPERTY_EAXLISTENER_ROOMHF ||
                dwPropID == DSPROPERTY_EAXLISTENER_ROOMROLLOFFFACTOR ||
                dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENT ||
                dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE ||
                dwPropID == DSPROPERTY_EAXLISTENER_ENVIRONMENTDIFFUSION ||
                dwPropID == DSPROPERTY_EAXLISTENER_AIRABSORPTIONHF ||
                dwPropID == DSPROPERTY_EAXLISTENER_FLAGS)
        {
            *pTypeSupport = KSPROPERTY_SUPPORT_GET|KSPROPERTY_SUPPORT_SET;
            res = DS_OK;
        }
        else
            FIXME("Unhandled propid: 0x%08x\n", dwPropID);

        LeaveCriticalSection(&This->crst);
    }
    else
        FIXME("Unhandled propset: %s\n", debugstr_guid(guidPropSet));

    return res;
}

static const IKsPropertySetVtbl DS8PrimaryProp_Vtbl =
{
    DS8PrimaryProp_QueryInterface,
    DS8PrimaryProp_AddRef,
    DS8PrimaryProp_Release,
    DS8PrimaryProp_Get,
    DS8PrimaryProp_Set,
    DS8PrimaryProp_QuerySupport
};
