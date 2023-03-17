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

#define CONST_VTABLE
#include <stdarg.h>
#include <string.h>

#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>

#include "dsound_private.h"
#include "eax-presets.h"


#ifndef E_PROP_ID_UNSUPPORTED
#define E_PROP_ID_UNSUPPORTED            ((HRESULT)0x80070490)
#endif


static IDirectSoundBufferVtbl DSPrimary_Vtbl;
static IDirectSound3DListenerVtbl DSPrimary3D_Vtbl;
static IKsPropertySetVtbl DSPrimaryProp_Vtbl;

static void DSPrimary_SetParams(DSPrimary *This, const DS3DLISTENER *params, LONG flags);


static inline DSPrimary *impl_from_IDirectSoundBuffer(IDirectSoundBuffer *iface)
{
    return CONTAINING_RECORD(iface, DSPrimary, IDirectSoundBuffer_iface);
}

static inline DSPrimary *impl_from_IDirectSound3DListener(IDirectSound3DListener *iface)
{
    return CONTAINING_RECORD(iface, DSPrimary, IDirectSound3DListener_iface);
}

static inline DSPrimary *impl_from_IKsPropertySet(IKsPropertySet *iface)
{
    return CONTAINING_RECORD(iface, DSPrimary, IKsPropertySet_iface);
}


static void trigger_elapsed_notifies(DSBuffer *buf, DWORD lastpos, DWORD curpos)
{
    DSBPOSITIONNOTIFY *not = buf->notify;
    DSBPOSITIONNOTIFY *not_end = not + buf->nnotify;
    for(;not != not_end;++not)
    {
        HANDLE event = not->hEventNotify;
        DWORD ofs = not->dwOffset;

        if(ofs == (DWORD)DSBPN_OFFSETSTOP)
            continue;

        if(curpos < lastpos) /* Wraparound case */
        {
            if(ofs < curpos || ofs >= lastpos)
            {
                TRACE("Triggering notification %d from buffer %p\n", (int)(not-buf->notify), buf);
                SetEvent(event);
            }
        }
        else if(ofs >= lastpos && ofs < curpos) /* Normal case */
        {
            TRACE("Triggering notification %d from buffer %p\n", (int)(not-buf->notify), buf);
            SetEvent(event);
        }
    }
}

static void trigger_stop_notifies(DSBuffer *buf)
{
    DSBPOSITIONNOTIFY *not = buf->notify;
    DSBPOSITIONNOTIFY *not_end = not + buf->nnotify;
    for(;not != not_end;++not)
    {
        if(not->dwOffset != (DWORD)DSBPN_OFFSETSTOP)
            continue;
        TRACE("Triggering notification %d from buffer %p\n", (int)(not-buf->notify), buf);
        SetEvent(not->hEventNotify);
    }
}

void DSPrimary_triggernots(DSPrimary *prim)
{
    DSBuffer **curnot, **endnot;

    curnot = prim->notifies;
    endnot = curnot + prim->nnotifies;
    while(curnot != endnot)
    {
        DSBuffer *buf = *curnot;
        DSData *data = buf->buffer;
        DWORD curpos = buf->lastpos;
        ALint state = 0;
        ALint ofs;

        alGetSourcei(buf->source, AL_BYTE_OFFSET, &ofs);
        alGetSourcei(buf->source, AL_SOURCE_STATE, &state);
        if(buf->segsize == 0)
            curpos = (state == AL_STOPPED) ? data->buf_size : ofs;
        else
        {
            if(state != AL_STOPPED)
                curpos = ofs + buf->queue_base;
            else
            {
                ALint queued;
                alGetSourcei(buf->source, AL_BUFFERS_QUEUED, &queued);
                curpos = buf->segsize*queued + buf->queue_base;
            }

            if(curpos >= (DWORD)data->buf_size)
            {
                if(buf->islooping)
                    curpos %= (DWORD)data->buf_size;
                else if(buf->isplaying)
                {
                    curpos = data->buf_size;
                    alSourceStop(buf->source);
                    alSourcei(buf->source, AL_BUFFER, 0);
                    buf->curidx = 0;
                    buf->isplaying = FALSE;
                }
            }

            if(state != AL_PLAYING)
                state = buf->isplaying ? AL_PLAYING : AL_PAUSED;
        }
        checkALError();

        if(buf->lastpos != curpos)
        {
            trigger_elapsed_notifies(buf, buf->lastpos, curpos);
            buf->lastpos = curpos;
        }
        if(state != AL_PLAYING)
        {
            /* Remove this buffer from list and put another at the current
             * position; don't increment i
             */
            trigger_stop_notifies(buf);
            *curnot = *(--endnot);
            prim->nnotifies--;
            continue;
        }
        curnot++;
    }
    checkALError();
}

static void do_buffer_stream(DSBuffer *buf, BYTE *scratch_mem)
{
    DSData *data = buf->buffer;
    ALint ofs, done = 0, queued = QBUFFERS, state = AL_PLAYING;
    ALuint which;

    alGetSourcei(buf->source, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(buf->source, AL_SOURCE_STATE, &state);
    alGetSourcei(buf->source, AL_BUFFERS_PROCESSED, &done);

    if(done > 0)
    {
        ALuint bids[QBUFFERS];
        queued -= done;

        alSourceUnqueueBuffers(buf->source, done, bids);
        buf->queue_base = (buf->queue_base + buf->segsize*done) % data->buf_size;
    }
    while(queued < QBUFFERS)
    {
        which = buf->stream_bids[buf->curidx];
        ofs = buf->data_offset;

        if(buf->segsize < data->buf_size - ofs)
        {
            alBufferData(which, data->buf_format, data->data + ofs, buf->segsize,
                         data->format.Format.nSamplesPerSec);
            buf->data_offset = ofs + buf->segsize;
        }
        else if(buf->islooping)
        {
            ALsizei rem = data->buf_size - ofs;
            if(rem > 2048) rem = 2048;

            memcpy(scratch_mem, data->data + ofs, rem);
            while(rem < buf->segsize)
            {
                ALsizei todo = buf->segsize - rem;
                if(todo > data->buf_size)
                    todo = data->buf_size;
                memcpy(scratch_mem + rem, data->data, todo);
                rem += todo;
            }
            alBufferData(which, data->buf_format, scratch_mem, buf->segsize,
                         data->format.Format.nSamplesPerSec);
            buf->data_offset = (ofs+buf->segsize) % data->buf_size;
        }
        else
        {
            ALsizei rem = data->buf_size - ofs;
            if(rem > 2048) rem = 2048;
            if(rem == 0) break;

            memcpy(scratch_mem, data->data + ofs, rem);
            memset(scratch_mem+rem, (data->format.Format.wBitsPerSample==8) ? 128 : 0,
                   buf->segsize - rem);
            alBufferData(which, data->buf_format, scratch_mem, buf->segsize,
                         data->format.Format.nSamplesPerSec);
            buf->data_offset = data->buf_size;
        }

        alSourceQueueBuffers(buf->source, 1, &which);
        buf->curidx = (buf->curidx+1)%QBUFFERS;
        queued++;
    }

    if(!queued)
    {
        buf->data_offset = 0;
        buf->queue_base = data->buf_size;
        buf->curidx = 0;
        buf->isplaying = FALSE;
    }
    else if(state != AL_PLAYING)
        alSourcePlay(buf->source);
}

void DSPrimary_streamfeeder(DSPrimary *prim, BYTE *scratch_mem)
{
    /* OpenAL doesn't support our lovely buffer extensions so just make sure
     * enough buffers are queued for streaming
     */
    if(prim->write_emu)
    {
        DSBuffer *buf = CONTAINING_RECORD(prim->write_emu, DSBuffer, IDirectSoundBuffer8_iface);
        if(buf->segsize != 0 && buf->isplaying)
            do_buffer_stream(buf, scratch_mem);
    }
    else
    {
        struct DSBufferGroup *bufgroup = prim->BufferGroups;
        struct DSBufferGroup *endgroup = bufgroup + prim->NumBufferGroups;
        for(;bufgroup != endgroup;++bufgroup)
        {
            DWORD64 usemask = ~bufgroup->FreeBuffers;
            while(usemask)
            {
                int idx = CTZ64(usemask);
                DSBuffer *buf = bufgroup->Buffers + idx;
                usemask &= ~(U64(1) << idx);

                if(buf->segsize != 0 && buf->isplaying)
                    do_buffer_stream(buf, scratch_mem);
            }
        }
    }
    checkALError();
}


HRESULT DSPrimary_PreInit(DSPrimary *This, DSDevice *parent)
{
    WAVEFORMATEX *wfx;
    DWORD num_srcs;
    DWORD count;
    HRESULT hr;
    DWORD i;

    This->IDirectSoundBuffer_iface.lpVtbl = &DSPrimary_Vtbl;
    This->IDirectSound3DListener_iface.lpVtbl = &DSPrimary3D_Vtbl;
    This->IKsPropertySet_iface.lpVtbl = &DSPrimaryProp_Vtbl;

    This->parent = parent;
    This->share = parent->share;
    This->ctx = parent->share->ctx;
    This->refresh = parent->share->refresh;

    wfx = &This->format.Format;
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 2;
    wfx->wBitsPerSample = 8;
    wfx->nSamplesPerSec = 22050;
    wfx->nBlockAlign = wfx->wBitsPerSample * wfx->nChannels / 8;
    wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
    wfx->cbSize = 0;

    This->stopped = TRUE;

    /* Apparently primary buffer size is always 32k,
     * tested on windows with 192k 24 bits sound @ 6 channels
     * where it will run out in 60 ms and it isn't pointer aligned
     */
    This->buf_size = 32768;

    num_srcs = This->share->sources.maxhw_alloc + This->share->sources.maxsw_alloc;

    hr = DSERR_OUTOFMEMORY;
    This->notifies = HeapAlloc(GetProcessHeap(), 0, num_srcs*sizeof(*This->notifies));
    if(!This->notifies) goto fail;
    This->sizenotifies = num_srcs;

    count = (MAX_HWBUFFERS+63) / 64;
    This->BufferGroups = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   count*sizeof(*This->BufferGroups));
    if(!This->BufferGroups) goto fail;

    for(i = 0;i < count;++i)
    {
        This->BufferGroups[i].Buffers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                  64*sizeof(This->BufferGroups[0].Buffers[0]));
        if(!This->BufferGroups[i].Buffers)
        {
            while(i > 0)
                HeapFree(GetProcessHeap(), 0, This->BufferGroups[--i].Buffers);
            HeapFree(GetProcessHeap(), 0, This->BufferGroups);
            This->BufferGroups = NULL;
            goto fail;
        }
        This->BufferGroups[i].FreeBuffers = ~(DWORD64)0;
    }
    This->NumBufferGroups = count;

    return S_OK;

fail:
    DSPrimary_Clear(This);
    return hr;
}

void DSPrimary_Clear(DSPrimary *This)
{
    struct DSBufferGroup *bufgroup;
    DWORD i;

    if(!This->parent)
        return;

    bufgroup = This->BufferGroups;
    for(i = 0;i < This->NumBufferGroups;++i)
    {
        DWORD64 usemask = ~bufgroup[i].FreeBuffers;
        while(usemask)
        {
            int idx = CTZ64(usemask);
            DSBuffer *buf = bufgroup[i].Buffers + idx;
            usemask &= ~(U64(1) << idx);

            DSBuffer_Destroy(buf);
        }
        HeapFree(GetProcessHeap(), 0, This->BufferGroups[i].Buffers);
    }

    HeapFree(GetProcessHeap(), 0, This->BufferGroups);
    HeapFree(GetProcessHeap(), 0, This->notifies);
    memset(This, 0, sizeof(*This));
}

static HRESULT WINAPI DSPrimary_QueryInterface(IDirectSoundBuffer *iface, REFIID riid, LPVOID *ppv)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);

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
    else if(IsEqualIID(riid, &IID_IKsPropertySet))
        *ppv = &This->IKsPropertySet_iface;
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI DSPrimary_AddRef(IDirectSoundBuffer *iface)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->ref);
    if(ret == 1) This->flags = 0;
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSPrimary_Release(IDirectSoundBuffer *iface)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    LONG ref, oldval;

    oldval = *(volatile LONG*)&This->ref;
    do {
        ref = oldval;
        if(ref)
        {
            --ref;
            oldval = InterlockedCompareExchange(&This->ref, ref, ref+1)-1;
        }
    } while(oldval != ref);
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static HRESULT WINAPI DSPrimary_GetCaps(IDirectSoundBuffer *iface, DSBCAPS *caps)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%p)\n", iface, caps);

    if(!caps || caps->dwSize < sizeof(*caps))
    {
        WARN("Invalid DSBCAPS (%p, %lu)\n", caps, caps ? caps->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    caps->dwFlags = This->flags;
    caps->dwBufferBytes = This->buf_size;
    caps->dwUnlockTransferRate = 0;
    caps->dwPlayCpuOverhead = 0;

    return DS_OK;
}

static HRESULT WINAPI DSPrimary_GetCurrentPosition(IDirectSoundBuffer *iface, DWORD *playpos, DWORD *curpos)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_PRIOLEVELNEEDED;

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_GetCurrentPosition(This->write_emu, playpos, curpos);
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_GetFormat(IDirectSoundBuffer *iface, WAVEFORMATEX *wfx, DWORD allocated, DWORD *written)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;
    UINT size;

    if(!wfx && !written)
    {
        WARN("Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
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
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_GetVolume(IDirectSoundBuffer *iface, LONG *volume)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    ALfloat gain;

    TRACE("(%p)->(%p)\n", iface, volume);

    if(!volume)
        return DSERR_INVALIDPARAM;
    *volume = 0;

    if(!(This->flags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    setALContext(This->ctx);
    alGetListenerf(AL_GAIN, &gain);
    checkALError();
    popALContext();

    *volume = clampI(gain_to_mB(gain), DSBVOLUME_MIN, DSBVOLUME_MAX);
    return DS_OK;
}

static HRESULT WINAPI DSPrimary_GetPan(IDirectSoundBuffer *iface, LONG *pan)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DS_OK;

    WARN("(%p)->(%p): semi-stub\n", iface, pan);

    if(!pan)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_GetPan(This->write_emu, pan);
    else if(!(This->flags & DSBCAPS_CTRLPAN))
        hr = DSERR_CONTROLUNAVAIL;
    else
        *pan = 0;
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_GetFrequency(IDirectSoundBuffer *iface, DWORD *freq)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DS_OK;

    WARN("(%p)->(%p): semi-stub\n", iface, freq);

    if(!freq)
        return DSERR_INVALIDPARAM;

    if(!(This->flags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    EnterCriticalSection(&This->share->crst);
    *freq = This->format.Format.nSamplesPerSec;
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_GetStatus(IDirectSoundBuffer *iface, DWORD *status)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%p)\n", iface, status);

    if(!status)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&This->share->crst);
    *status = DSBSTATUS_PLAYING|DSBSTATUS_LOOPING;
    if((This->flags&DSBCAPS_LOCDEFER))
        *status |= DSBSTATUS_LOCHARDWARE;

    if(This->stopped)
    {
        struct DSBufferGroup *bufgroup = This->BufferGroups;
        DWORD i, state = 0;
        HRESULT hr;

        for(i = 0;i < This->NumBufferGroups;++i)
        {
            DWORD64 usemask = ~bufgroup[i].FreeBuffers;
            while(usemask)
            {
                int idx = CTZ64(usemask);
                DSBuffer *buf = bufgroup[i].Buffers + idx;
                usemask &= ~(U64(1) << idx);

                hr = DSBuffer_GetStatus(&buf->IDirectSoundBuffer8_iface, &state);
                if(SUCCEEDED(hr) && (state&DSBSTATUS_PLAYING)) break;
            }
        }
        if(!(state&DSBSTATUS_PLAYING))
        {
            /* Primary stopped and no buffers playing.. */
            *status = 0;
        }
    }
    LeaveCriticalSection(&This->share->crst);

    return DS_OK;
}

HRESULT WINAPI DSPrimary_Initialize(IDirectSoundBuffer *iface, IDirectSound *ds, const DSBUFFERDESC *desc)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr;

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
        WARN("Bad dwFlags %08lx\n", desc->dwFlags);
        return DSERR_INVALIDPARAM;
    }

    /* Should be 0 if not initialized */
    if(This->flags)
        return DSERR_ALREADYINITIALIZED;

    hr = DS_OK;
    if(This->parent->prio_level == DSSCL_WRITEPRIMARY)
    {
        DSBUFFERDESC emudesc;
        DSBuffer *emu;

        if(This->write_emu)
        {
            ERR("There shouldn't be a write_emu!\n");
            IDirectSoundBuffer_Release(This->write_emu);
            This->write_emu = NULL;
        }

        memset(&emudesc, 0, sizeof(emudesc));
        emudesc.dwSize = sizeof(emudesc);
        emudesc.dwFlags = DSBCAPS_LOCHARDWARE | (desc->dwFlags&DSBCAPS_CTRLPAN);
        /* Dont play last incomplete sample */
        emudesc.dwBufferBytes = This->buf_size - (This->buf_size%This->format.Format.nBlockAlign);
        emudesc.lpwfxFormat = &This->format.Format;

        hr = DSBuffer_Create(&emu, This, NULL);
        if(SUCCEEDED(hr))
        {
            hr = DSBuffer_Initialize(&emu->IDirectSoundBuffer8_iface, ds, &emudesc);
            if(SUCCEEDED(hr))
                hr = DSBuffer_GetInterface(emu, &IID_IDirectSoundBuffer, (void**)&This->write_emu);
            if(FAILED(hr))
                DSBuffer_Destroy(emu);
        }
    }

    if(SUCCEEDED(hr))
    {
        This->current.ds3d.dwSize = sizeof(This->current.ds3d);
        This->current.ds3d.vPosition.x = 0.0f;
        This->current.ds3d.vPosition.y = 0.0f;
        This->current.ds3d.vPosition.z = 0.0f;
        This->current.ds3d.vVelocity.x = 0.0f;
        This->current.ds3d.vVelocity.y = 0.0f;
        This->current.ds3d.vVelocity.z = 0.0f;
        This->current.ds3d.vOrientFront.x = 0.0f;
        This->current.ds3d.vOrientFront.y = 0.0f;
        This->current.ds3d.vOrientFront.z = 1.0f;
        This->current.ds3d.vOrientTop.x = 0.0f;
        This->current.ds3d.vOrientTop.y = 1.0f;
        This->current.ds3d.vOrientTop.z = 0.0f;
        This->current.ds3d.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
        This->current.ds3d.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
        This->current.ds3d.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
        This->deferred.ds3d = This->current.ds3d;

        This->flags = desc->dwFlags | DSBCAPS_LOCHARDWARE;

        if((This->flags&DSBCAPS_CTRL3D))
        {
            union PrimaryParamFlags dirty = { 0l };

            dirty.bit.pos = 1;
            dirty.bit.vel = 1;
            dirty.bit.orientation = 1;
            dirty.bit.distancefactor = 1;
            dirty.bit.rollofffactor = 1;
            dirty.bit.dopplerfactor = 1;
            DSPrimary_SetParams(This, &This->deferred.ds3d, dirty.flags);
        }
    }
    return hr;
}

static HRESULT WINAPI DSPrimary_Lock(IDirectSoundBuffer *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_PRIOLEVELNEEDED;

    TRACE("(%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", iface, ofs, bytes, ptr1, len1, ptr2, len2, flags);

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_Lock(This->write_emu, ofs, bytes, ptr1, len1, ptr2, len2, flags);
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_Play(IDirectSoundBuffer *iface, DWORD res1, DWORD res2, DWORD flags)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr;

    TRACE("(%p)->(%lu, %lu, %lu)\n", iface, res1, res2, flags);

    if(!(flags & DSBPLAY_LOOPING))
    {
        WARN("Flags (%08lx) not set to DSBPLAY_LOOPING\n", flags);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    hr = S_OK;
    if(This->write_emu)
        hr = IDirectSoundBuffer_Play(This->write_emu, res1, res2, flags);
    if(SUCCEEDED(hr))
        This->stopped = FALSE;
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_SetCurrentPosition(IDirectSoundBuffer *iface, DWORD pos)
{
    WARN("(%p)->(%lu)\n", iface, pos);
    return DSERR_INVALIDCALL;
}

/* Just assume the format is crap, and clean up the damage */
static HRESULT copy_waveformat(WAVEFORMATEX *wfx, const WAVEFORMATEX *from)
{
    if(from->nChannels <= 0)
    {
        WARN("Invalid Channels %d\n", from->nChannels);
        return DSERR_INVALIDPARAM;
    }
    if(from->nSamplesPerSec < DSBFREQUENCY_MIN || from->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN("Invalid SamplesPerSec %lu\n", from->nSamplesPerSec);
        return DSERR_INVALIDPARAM;
    }
    if(from->nBlockAlign <= 0)
    {
        WARN("Invalid BlockAlign %d\n", from->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }
    if(from->wBitsPerSample == 0 || (from->wBitsPerSample%8) != 0)
    {
        WARN("Invalid BitsPerSample %d\n", from->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(from->nBlockAlign != from->nChannels*from->wBitsPerSample/8)
    {
        WARN("Invalid BlockAlign %d (expected %u = %u*%u/8)\n",
             from->nBlockAlign, from->nChannels*from->wBitsPerSample/8,
             from->nChannels, from->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(from->nAvgBytesPerSec != from->nBlockAlign*from->nSamplesPerSec)
    {
        WARN("Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
             from->nAvgBytesPerSec, from->nSamplesPerSec*from->nBlockAlign,
             from->nSamplesPerSec, from->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }

    if(from->wFormatTag == WAVE_FORMAT_PCM)
    {
        if(from->wBitsPerSample > 32)
            return DSERR_INVALIDPARAM;
        wfx->cbSize = 0;
    }
    else if(from->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        if(from->wBitsPerSample != 32)
            return DSERR_INVALIDPARAM;
        wfx->cbSize = 0;
    }
    else if(from->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)wfx;
        const WAVEFORMATEXTENSIBLE *fromx = (const WAVEFORMATEXTENSIBLE*)from;
        const WORD size = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

        /* Fail silently.. */
        if(from->cbSize < size) return DS_OK;
        if(fromx->Samples.wValidBitsPerSample > fromx->Format.wBitsPerSample)
            return DSERR_INVALIDPARAM;

        if(IsEqualGUID(&fromx->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
        {
            if(from->wBitsPerSample > 32)
                return DSERR_INVALIDPARAM;
        }
        else if(IsEqualGUID(&fromx->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            if(from->wBitsPerSample != 32)
                return DSERR_INVALIDPARAM;
        }
        else
        {
            ERR("Unhandled extensible format: %s\n", debugstr_guid(&fromx->SubFormat));
            return DSERR_INVALIDPARAM;
        }

        wfe->Format.cbSize = size;
        wfe->Samples.wValidBitsPerSample = fromx->Samples.wValidBitsPerSample;
        if(!wfe->Samples.wValidBitsPerSample)
            wfe->Samples.wValidBitsPerSample = fromx->Format.wBitsPerSample;
        wfe->dwChannelMask = fromx->dwChannelMask;
        wfe->SubFormat = fromx->SubFormat;
    }
    else
    {
        ERR("Unhandled format tag %04x\n", from->wFormatTag);
        return DSERR_INVALIDPARAM;
    }

    wfx->wFormatTag = from->wFormatTag;
    wfx->nChannels = from->nChannels;
    wfx->nSamplesPerSec = from->nSamplesPerSec;
    wfx->nAvgBytesPerSec = from->nSamplesPerSec * from->nBlockAlign;
    wfx->nBlockAlign = from->wBitsPerSample * from->nChannels / 8;
    wfx->wBitsPerSample = from->wBitsPerSample;
    return DS_OK;
}

static HRESULT WINAPI DSPrimary_SetFormat(IDirectSoundBuffer *iface, const WAVEFORMATEX *wfx)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p)\n", iface, wfx);

    if(!wfx)
    {
        WARN("Missing format\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);

    if(This->parent->prio_level < DSSCL_PRIORITY)
    {
        hr = DSERR_PRIOLEVELNEEDED;
        goto out;
    }

    TRACE("Requested primary format:\n"
          "    FormatTag      = %04x\n"
          "    Channels       = %u\n"
          "    SamplesPerSec  = %lu\n"
          "    AvgBytesPerSec = %lu\n"
          "    BlockAlign     = %u\n"
          "    BitsPerSample  = %u\n",
          wfx->wFormatTag, wfx->nChannels,
          wfx->nSamplesPerSec, wfx->nAvgBytesPerSec,
          wfx->nBlockAlign, wfx->wBitsPerSample);

    hr = copy_waveformat(&This->format.Format, wfx);
    if(SUCCEEDED(hr) && This->write_emu)
    {
        DSBuffer *buf;
        DSBUFFERDESC desc;

        IDirectSoundBuffer_Release(This->write_emu);
        This->write_emu = NULL;

        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_CTRLPAN;
        desc.dwBufferBytes = This->buf_size - (This->buf_size % This->format.Format.nBlockAlign);
        desc.lpwfxFormat = &This->format.Format;

        hr = DSBuffer_Create(&buf, This, NULL);
        if(SUCCEEDED(hr))
        {
            hr = DSBuffer_Initialize(&buf->IDirectSoundBuffer8_iface,
                                     &This->parent->IDirectSound_iface, &desc);
            if(SUCCEEDED(hr))
                hr = DSBuffer_GetInterface(buf, &IID_IDirectSoundBuffer, (void**)&This->write_emu);
            if(FAILED(hr))
                DSBuffer_Destroy(buf);
        }
    }

out:
    LeaveCriticalSection(&This->share->crst);
    return hr;
}

static HRESULT WINAPI DSPrimary_SetVolume(IDirectSoundBuffer *iface, LONG vol)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);

    TRACE("(%p)->(%ld)\n", iface, vol);

    if(vol > DSBVOLUME_MAX || vol < DSBVOLUME_MIN)
    {
        WARN("Invalid volume (%ld)\n", vol);
        return DSERR_INVALIDPARAM;
    }

    if(!(This->flags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    setALContext(This->ctx);
    alListenerf(AL_GAIN, mB_to_gain((float)vol));
    popALContext();

    return DS_OK;
}

static HRESULT WINAPI DSPrimary_SetPan(IDirectSoundBuffer *iface, LONG pan)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr;

    TRACE("(%p)->(%ld)\n", iface, pan);

    if(pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
    {
        WARN("invalid parameter: pan = %ld\n", pan);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(!(This->flags&DSBCAPS_CTRLPAN))
    {
        WARN("control unavailable\n");
        hr = DSERR_CONTROLUNAVAIL;
    }
    else if(This->write_emu)
        hr = IDirectSoundBuffer_SetPan(This->write_emu, pan);
    else
    {
        FIXME("Not supported\n");
        hr = E_NOTIMPL;
    }
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_SetFrequency(IDirectSoundBuffer *iface, DWORD freq)
{
    WARN("(%p)->(%lu)\n", iface, freq);
    return DSERR_CONTROLUNAVAIL;
}

static HRESULT WINAPI DSPrimary_Stop(IDirectSoundBuffer *iface)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_Stop(This->write_emu);
    if(SUCCEEDED(hr))
        This->stopped = TRUE;
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_Unlock(IDirectSoundBuffer *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = DSERR_INVALIDCALL;

    TRACE("(%p)->(%p, %lu, %p, %lu)\n", iface, ptr1, len1, ptr2, len2);

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_Unlock(This->write_emu, ptr1, len1, ptr2, len2);
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSPrimary_Restore(IDirectSoundBuffer *iface)
{
    DSPrimary *This = impl_from_IDirectSoundBuffer(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->share->crst);
    if(This->write_emu)
        hr = IDirectSoundBuffer_Restore(This->write_emu);
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static IDirectSoundBufferVtbl DSPrimary_Vtbl =
{
    DSPrimary_QueryInterface,
    DSPrimary_AddRef,
    DSPrimary_Release,
    DSPrimary_GetCaps,
    DSPrimary_GetCurrentPosition,
    DSPrimary_GetFormat,
    DSPrimary_GetVolume,
    DSPrimary_GetPan,
    DSPrimary_GetFrequency,
    DSPrimary_GetStatus,
    DSPrimary_Initialize,
    DSPrimary_Lock,
    DSPrimary_Play,
    DSPrimary_SetCurrentPosition,
    DSPrimary_SetFormat,
    DSPrimary_SetVolume,
    DSPrimary_SetPan,
    DSPrimary_SetFrequency,
    DSPrimary_Stop,
    DSPrimary_Unlock,
    DSPrimary_Restore
};


static void DSPrimary_SetParams(DSPrimary *This, const DS3DLISTENER *params, LONG flags)
{
    union PrimaryParamFlags dirty = { flags };
    DWORD i;

    if(dirty.bit.pos)
        This->current.ds3d.vPosition = params->vPosition;
    if(dirty.bit.vel)
        This->current.ds3d.vVelocity = params->vVelocity;
    if(dirty.bit.orientation)
    {
        This->current.ds3d.vOrientFront = params->vOrientFront;
        This->current.ds3d.vOrientTop = params->vOrientTop;
    }
    if(dirty.bit.distancefactor)
        This->current.ds3d.flDistanceFactor = params->flDistanceFactor;
    if(dirty.bit.rollofffactor)
        This->current.ds3d.flRolloffFactor = params->flRolloffFactor;
    if(dirty.bit.dopplerfactor)
        This->current.ds3d.flDopplerFactor = params->flDopplerFactor;

    if(dirty.bit.pos)
        alListener3f(AL_POSITION, params->vPosition.x, params->vPosition.y,
                                 -params->vPosition.z);
    if(dirty.bit.vel)
        alListener3f(AL_VELOCITY, params->vVelocity.x, params->vVelocity.y,
                                 -params->vVelocity.z);
    if(dirty.bit.orientation)
    {
        ALfloat orient[6] = {
            params->vOrientFront.x, params->vOrientFront.y, -params->vOrientFront.z,
            params->vOrientTop.x, params->vOrientTop.y, -params->vOrientTop.z
        };
        alListenerfv(AL_ORIENTATION, orient);
    }
    if(dirty.bit.distancefactor)
        alSpeedOfSound(343.3f/params->flDistanceFactor);
    if(dirty.bit.rollofffactor)
    {
        struct DSBufferGroup *bufgroup = This->BufferGroups;
        ALfloat rolloff = params->flRolloffFactor;

        for(i = 0;i < This->NumBufferGroups;++i)
        {
            DWORD64 usemask = ~bufgroup[i].FreeBuffers;
            while(usemask)
            {
                int idx = CTZ64(usemask);
                DSBuffer *buf = bufgroup[i].Buffers + idx;
                usemask &= ~(U64(1) << idx);

                if(buf->source)
                    alSourcef(buf->source, AL_ROLLOFF_FACTOR, rolloff);
            }
        }
    }
    if(dirty.bit.dopplerfactor)
        alDopplerFactor(params->flDopplerFactor);
}

static HRESULT WINAPI DSPrimary3D_QueryInterface(IDirectSound3DListener *iface, REFIID riid, void **ppv)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);
    return DSPrimary_QueryInterface(&This->IDirectSoundBuffer_iface, riid, ppv);
}

static ULONG WINAPI DSPrimary3D_AddRef(IDirectSound3DListener *iface)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->ds3d_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSPrimary3D_Release(IDirectSound3DListener *iface)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->ds3d_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}


static HRESULT WINAPI DSPrimary3D_GetDistanceFactor(IDirectSound3DListener *iface, D3DVALUE *distancefactor)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, distancefactor);

    if(!distancefactor)
    {
        WARN("Invalid parameter %p\n", distancefactor);
        return DSERR_INVALIDPARAM;
    }

    *distancefactor = This->current.ds3d.flDistanceFactor;
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetDopplerFactor(IDirectSound3DListener *iface, D3DVALUE *dopplerfactor)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, dopplerfactor);

    if(!dopplerfactor)
    {
        WARN("Invalid parameter %p\n", dopplerfactor);
        return DSERR_INVALIDPARAM;
    }

    *dopplerfactor = This->current.ds3d.flDopplerFactor;
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetOrientation(IDirectSound3DListener *iface, D3DVECTOR *front, D3DVECTOR *top)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p, %p)\n", iface, front, top);

    if(!front || !top)
    {
        WARN("Invalid parameter %p %p\n", front, top);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *front = This->current.ds3d.vOrientFront;
    *top = This->current.ds3d.vOrientTop;
    LeaveCriticalSection(&This->share->crst);
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetPosition(IDirectSound3DListener *iface, D3DVECTOR *pos)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, pos);

    if(!pos)
    {
        WARN("Invalid parameter %p\n", pos);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *pos = This->current.ds3d.vPosition;
    LeaveCriticalSection(&This->share->crst);
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetRolloffFactor(IDirectSound3DListener *iface, D3DVALUE *rollofffactor)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, rollofffactor);

    if(!rollofffactor)
    {
        WARN("Invalid parameter %p\n", rollofffactor);
        return DSERR_INVALIDPARAM;
    }

    *rollofffactor = This->current.ds3d.flRolloffFactor;
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetVelocity(IDirectSound3DListener *iface, D3DVECTOR *velocity)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, velocity);

    if(!velocity)
    {
        WARN("Invalid parameter %p\n", velocity);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *velocity = This->current.ds3d.vVelocity;
    LeaveCriticalSection(&This->share->crst);
    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_GetAllParameters(IDirectSound3DListener *iface, DS3DLISTENER *listener)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p)\n", iface, listener);

    if(!listener || listener->dwSize < sizeof(*listener))
    {
        WARN("Invalid DS3DLISTENER %p %lu\n", listener, listener ? listener->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    listener->vPosition = This->current.ds3d.vPosition;
    listener->vVelocity = This->current.ds3d.vVelocity;
    listener->vOrientFront = This->current.ds3d.vOrientFront;
    listener->vOrientTop = This->current.ds3d.vOrientTop;
    listener->flDistanceFactor = This->current.ds3d.flDistanceFactor;
    listener->flRolloffFactor = This->current.ds3d.flRolloffFactor;
    listener->flDopplerFactor = This->current.ds3d.flDopplerFactor;
    LeaveCriticalSection(&This->share->crst);

    return DS_OK;
}


static HRESULT WINAPI DSPrimary3D_SetDistanceFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %lu)\n", iface, factor, apply);

    if(factor < DS3D_MINDISTANCEFACTOR ||
       factor > DS3D_MAXDISTANCEFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.flDistanceFactor = factor;
        This->dirty.bit.distancefactor = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.flDistanceFactor = factor;
        alSpeedOfSound(343.3f/factor);
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetDopplerFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %lu)\n", iface, factor, apply);

    if(factor < DS3D_MINDOPPLERFACTOR ||
       factor > DS3D_MAXDOPPLERFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.flDopplerFactor = factor;
        This->dirty.bit.dopplerfactor = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.flDopplerFactor = factor;
        alDopplerFactor(factor);
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetOrientation(IDirectSound3DListener *iface, D3DVALUE xFront, D3DVALUE yFront, D3DVALUE zFront, D3DVALUE xTop, D3DVALUE yTop, D3DVALUE zTop, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %f, %f, %f, %lu)\n", iface, xFront, yFront, zFront, xTop, yTop, zTop, apply);

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.vOrientFront.x = xFront;
        This->deferred.ds3d.vOrientFront.y = yFront;
        This->deferred.ds3d.vOrientFront.z = zFront;
        This->deferred.ds3d.vOrientTop.x = xTop;
        This->deferred.ds3d.vOrientTop.y = yTop;
        This->deferred.ds3d.vOrientTop.z = zTop;
        This->dirty.bit.orientation = 1;
    }
    else
    {
        ALfloat orient[6] = {
            xFront, yFront, -zFront,
            xTop, yTop, -zTop
        };
        This->current.ds3d.vOrientFront.x = xFront;
        This->current.ds3d.vOrientFront.y = yFront;
        This->current.ds3d.vOrientFront.z = zFront;
        This->current.ds3d.vOrientTop.x = xTop;
        This->current.ds3d.vOrientTop.y = yTop;
        This->current.ds3d.vOrientTop.z = zTop;

        setALContext(This->ctx);
        alListenerfv(AL_ORIENTATION, orient);
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetPosition(IDirectSound3DListener *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %lu)\n", iface, x, y, z, apply);

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.vPosition.x = x;
        This->deferred.ds3d.vPosition.y = y;
        This->deferred.ds3d.vPosition.z = z;
        This->dirty.bit.pos = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.vPosition.x = x;
        This->current.ds3d.vPosition.y = y;
        This->current.ds3d.vPosition.z = z;
        alListener3f(AL_POSITION, x, y, -z);
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetRolloffFactor(IDirectSound3DListener *iface, D3DVALUE factor, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %lu)\n", iface, factor, apply);

    if(factor < DS3D_MINROLLOFFFACTOR ||
       factor > DS3D_MAXROLLOFFFACTOR)
    {
        WARN("Invalid parameter %f\n", factor);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.flRolloffFactor = factor;
        This->dirty.bit.rollofffactor = 1;
    }
    else
    {
        struct DSBufferGroup *bufgroup = This->BufferGroups;
        DWORD i;

        This->current.ds3d.flRolloffFactor = factor;

        setALContext(This->ctx);
        for(i = 0;i < This->NumBufferGroups;++i)
        {
            DWORD64 usemask = ~bufgroup[i].FreeBuffers;
            while(usemask)
            {
                int idx = CTZ64(usemask);
                DSBuffer *buf = bufgroup[i].Buffers + idx;
                usemask &= ~(U64(1) << idx);

                if(buf->source)
                    alSourcef(buf->source, AL_ROLLOFF_FACTOR, factor);
            }
        }
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetVelocity(IDirectSound3DListener *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%f, %f, %f, %lu)\n", iface, x, y, z, apply);

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.vVelocity.x = x;
        This->deferred.ds3d.vVelocity.y = y;
        This->deferred.ds3d.vVelocity.z = z;
        This->dirty.bit.vel = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.vVelocity.x = x;
        This->current.ds3d.vVelocity.y = y;
        This->current.ds3d.vVelocity.z = z;
        alListener3f(AL_VELOCITY, x, y, -z);
        checkALError();
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSPrimary3D_SetAllParameters(IDirectSound3DListener *iface, const DS3DLISTENER *listen, DWORD apply)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);

    TRACE("(%p)->(%p, %lu)\n", iface, listen, apply);

    if(!listen || listen->dwSize < sizeof(*listen))
    {
        WARN("Invalid parameter %p %lu\n", listen, listen ? listen->dwSize : 0);
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

    if(apply == DS3D_DEFERRED)
    {
        EnterCriticalSection(&This->share->crst);
        This->deferred.ds3d = *listen;
        This->deferred.ds3d.dwSize = sizeof(This->deferred.ds3d);
        This->dirty.bit.pos = 1;
        This->dirty.bit.vel = 1;
        This->dirty.bit.orientation = 1;
        This->dirty.bit.distancefactor = 1;
        This->dirty.bit.rollofffactor = 1;
        This->dirty.bit.dopplerfactor = 1;
        LeaveCriticalSection(&This->share->crst);
    }
    else
    {
        union PrimaryParamFlags dirty = { 0l };
        dirty.bit.pos = 1;
        dirty.bit.vel = 1;
        dirty.bit.orientation = 1;
        dirty.bit.distancefactor = 1;
        dirty.bit.rollofffactor = 1;
        dirty.bit.dopplerfactor = 1;

        EnterCriticalSection(&This->share->crst);
        setALContext(This->ctx);
        DSPrimary_SetParams(This, listen, dirty.flags);
        checkALError();
        popALContext();
        LeaveCriticalSection(&This->share->crst);
    }

    return S_OK;
}

HRESULT WINAPI DSPrimary3D_CommitDeferredSettings(IDirectSound3DListener *iface)
{
    DSPrimary *This = impl_from_IDirectSound3DListener(iface);
    struct DSBufferGroup *bufgroup;
    LONG flags;
    DWORD i;

    EnterCriticalSection(&This->share->crst);
    setALContext(This->ctx);
    alDeferUpdatesSOFT();

    if((flags=InterlockedExchange(&This->dirty.flags, 0)) != 0)
    {
        DSPrimary_SetParams(This, &This->deferred.ds3d, flags);
        /* checkALError is here for debugging */
        checkALError();
    }
    TRACE("Dirty flags was: 0x%02lx\n", flags);

    bufgroup = This->BufferGroups;
    for(i = 0;i < This->NumBufferGroups;++i)
    {
        DWORD64 usemask = ~bufgroup[i].FreeBuffers;
        while(usemask)
        {
            int idx = CTZ64(usemask);
            DSBuffer *buf = bufgroup[i].Buffers + idx;
            usemask &= ~(U64(1) << idx);

            if((flags=InterlockedExchange(&buf->dirty.flags, 0)) != 0)
                DSBuffer_SetParams(buf, &buf->deferred.ds3d, flags);
        }
    }
    alProcessUpdatesSOFT();
    checkALError();

    popALContext();
    LeaveCriticalSection(&This->share->crst);

    return DS_OK;
}

static IDirectSound3DListenerVtbl DSPrimary3D_Vtbl =
{
    DSPrimary3D_QueryInterface,
    DSPrimary3D_AddRef,
    DSPrimary3D_Release,
    DSPrimary3D_GetAllParameters,
    DSPrimary3D_GetDistanceFactor,
    DSPrimary3D_GetDopplerFactor,
    DSPrimary3D_GetOrientation,
    DSPrimary3D_GetPosition,
    DSPrimary3D_GetRolloffFactor,
    DSPrimary3D_GetVelocity,
    DSPrimary3D_SetAllParameters,
    DSPrimary3D_SetDistanceFactor,
    DSPrimary3D_SetDopplerFactor,
    DSPrimary3D_SetOrientation,
    DSPrimary3D_SetPosition,
    DSPrimary3D_SetRolloffFactor,
    DSPrimary3D_SetVelocity,
    DSPrimary3D_CommitDeferredSettings
};


static HRESULT WINAPI DSPrimaryProp_QueryInterface(IKsPropertySet *iface, REFIID riid, void **ppv)
{
    DSPrimary *This = impl_from_IKsPropertySet(iface);
    return DSPrimary_QueryInterface(&This->IDirectSoundBuffer_iface, riid, ppv);
}

static ULONG WINAPI DSPrimaryProp_AddRef(IKsPropertySet *iface)
{
    DSPrimary *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->prop_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSPrimaryProp_Release(IKsPropertySet *iface)
{
    DSPrimary *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->prop_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static HRESULT WINAPI DSPrimaryProp_Get(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData,
  ULONG *pcbReturned)
{
    (void)iface;
    (void)dwPropID;
    (void)pInstanceData;
    (void)cbInstanceData;
    (void)pPropData;
    (void)cbPropData;
    (void)pcbReturned;

    FIXME("Unhandled propset: %s\n", debugstr_guid(guidPropSet));

    return E_PROP_ID_UNSUPPORTED;
}

static HRESULT WINAPI DSPrimaryProp_Set(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData)
{
    (void)iface;
    (void)dwPropID;
    (void)pInstanceData;
    (void)cbInstanceData;
    (void)pPropData;
    (void)cbPropData;

    FIXME("Unhandled propset: %s\n", debugstr_guid(guidPropSet));

    return E_PROP_ID_UNSUPPORTED;
}

static HRESULT WINAPI DSPrimaryProp_QuerySupport(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  ULONG *pTypeSupport)
{
    (void)iface;
    (void)pTypeSupport;

    FIXME("Unhandled propset: %s (propid: %lu)\n", debugstr_guid(guidPropSet), dwPropID);

    return E_PROP_ID_UNSUPPORTED;
}

static IKsPropertySetVtbl DSPrimaryProp_Vtbl =
{
    DSPrimaryProp_QueryInterface,
    DSPrimaryProp_AddRef,
    DSPrimaryProp_Release,
    DSPrimaryProp_Get,
    DSPrimaryProp_Set,
    DSPrimaryProp_QuerySupport
};
