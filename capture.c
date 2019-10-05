/* DirectSound capture buffer interface
 *
 * Copyright 2009 Maarten Lankhorst
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

#include <windows.h>
#include <dsound.h>

#include "dsound_private.h"

#ifndef DSCBPN_OFFSET_STOP
#define DSCBPN_OFFSET_STOP          0xffffffff
#endif


typedef struct DSCImpl DSCImpl;
typedef struct DSCBuffer DSCBuffer;

struct DSCImpl {
    /* IDirectSoundCapture and IDirectSoundCapture8 are aliases */
    IDirectSoundCapture IDirectSoundCapture_iface;
    IUnknown IUnknown_iface;
    LONG allref, dscref, unkref;

    BOOL is_8;

    ALCchar *device_name;
    DSCBuffer *buf;

    CRITICAL_SECTION crst;
};

struct DSCBuffer {
    IDirectSoundCaptureBuffer8 IDirectSoundCaptureBuffer8_iface;
    IDirectSoundNotify IDirectSoundNotify_iface;
    LONG ref, not_ref;
    LONG all_ref;

    DSCImpl *parent;
    ALCdevice *device;

    DWORD buf_size;
    BYTE *buf;

    LONG locked;

    WAVEFORMATEXTENSIBLE format;

    DSBPOSITIONNOTIFY *notify;
    DWORD nnotify;

    HANDLE thread_hdl;
    DWORD thread_id;

    HANDLE queue_timer;
    HANDLE timer_evt;
    volatile LONG quit_now;

    DWORD pos;
    BOOL playing, looping;
};

static IDirectSoundCaptureVtbl DSC_Vtbl;
static IUnknownVtbl DSC_Unknown_Vtbl;
static IDirectSoundCaptureBuffer8Vtbl DSCBuffer_Vtbl;
static IDirectSoundNotifyVtbl DSCNot_Vtbl;

static void DSCImpl_Destroy(DSCImpl *This);

static void trigger_notifies(DSCBuffer *buf, DWORD lastpos, DWORD curpos)
{
    DWORD i;

    if(lastpos == curpos)
        return;

    for(i = 0;i < buf->nnotify;++i)
    {
        DSBPOSITIONNOTIFY *not = &buf->notify[i];
        HANDLE event = not->hEventNotify;
        DWORD ofs = not->dwOffset;

        if (ofs == DSCBPN_OFFSET_STOP)
            continue;

        /* Wraparound case */
        if(curpos < lastpos)
        {
            if(ofs < curpos || ofs >= lastpos)
            {
                TRACE("Triggering notification %lu (%lu) from buffer %p\n", i, ofs, buf);
                SetEvent(event);
            }
            continue;
        }

        /* Normal case */
        if(ofs >= lastpos && ofs < curpos)
        {
            TRACE("Triggering notification %lu (%lu) from buffer %p\n", i, ofs, buf);
            SetEvent(event);
        }
    }
}

static DWORD CALLBACK DSCBuffer_thread(void *param)
{
    DSCBuffer *This = param;
    CRITICAL_SECTION *crst = &This->parent->crst;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while(WaitForSingleObject(This->timer_evt, INFINITE) == WAIT_OBJECT_0 && !This->quit_now)
    {
        ALCint avail = 0;

        alcGetIntegerv(This->device, ALC_CAPTURE_SAMPLES, 1, &avail);
        if(avail == 0 || !This->playing) continue;

        EnterCriticalSection(crst);
        if(!This->playing)
        {
            LeaveCriticalSection(crst);
            continue;
        }
    more_samples:
        avail *= This->format.Format.nBlockAlign;
        if((DWORD)avail > This->buf_size - This->pos)
            avail = This->buf_size - This->pos;

        alcCaptureSamples(This->device, This->buf+This->pos,
                          avail/This->format.Format.nBlockAlign);
        trigger_notifies(This, This->pos, This->pos + avail);
        This->pos += avail;

        if(This->pos == This->buf_size)
        {
            This->pos = 0;
            if(!This->looping)
            {
                DWORD i;
                for(i = 0;i < This->nnotify;++i)
                {
                    if(This->notify[i].dwOffset == DSCBPN_OFFSET_STOP)
                        SetEvent(This->notify[i].hEventNotify);
                }

                This->playing = 0;
                alcCaptureStop(This->device);
            }
            else
            {
                alcGetIntegerv(This->device, ALC_CAPTURE_SAMPLES, 1, &avail);
                if(avail) goto more_samples;
            }
        }

        LeaveCriticalSection(crst);
    }

    return 0;
}


static void CALLBACK DSCBuffer_timer(void *arg, BOOLEAN unused)
{
    (void)unused;
    SetEvent((HANDLE)arg);
}

static void DSCBuffer_starttimer(DSCBuffer *This)
{
    ALint refresh = FAKE_REFRESH_COUNT;
    DWORD triggertime;

    if(This->queue_timer)
        return;

    triggertime = 1000 / refresh * 2 / 3;
    TRACE("Calling timer every %lu ms for %i refreshes per second\n", triggertime, refresh);

    CreateTimerQueueTimer(&This->queue_timer, NULL, DSCBuffer_timer, This->timer_evt,
                          triggertime, triggertime, WT_EXECUTEINTIMERTHREAD);
}

static HRESULT DSCBuffer_Create(DSCBuffer **buf, DSCImpl *parent)
{
    DSCBuffer *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This) return E_OUTOFMEMORY;

    This->IDirectSoundCaptureBuffer8_iface.lpVtbl = &DSCBuffer_Vtbl;
    This->IDirectSoundNotify_iface.lpVtbl = &DSCNot_Vtbl;

    This->all_ref = This->ref = 1;

    This->parent = parent;

    This->quit_now = FALSE;
    This->timer_evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if(!This->timer_evt) goto fail;

    This->queue_timer = NULL;

    This->thread_hdl = CreateThread(NULL, 0, DSCBuffer_thread, This, 0, &This->thread_id);
    if(!This->thread_hdl) goto fail;

    *buf = This;
    return S_OK;

fail:
    if(This->timer_evt)
        CloseHandle(This->timer_evt);
    This->timer_evt = NULL;

    HeapFree(GetProcessHeap(), 0, This);
    return E_FAIL;
}

static void DSCBuffer_Destroy(DSCBuffer *This)
{
    if(This->queue_timer)
        DeleteTimerQueueTimer(NULL, This->queue_timer, INVALID_HANDLE_VALUE);
    This->queue_timer = NULL;

    if(This->thread_hdl)
    {
        InterlockedExchange(&This->quit_now, TRUE);
        SetEvent(This->timer_evt);

        if(WaitForSingleObject(This->thread_hdl, 1000) != WAIT_OBJECT_0)
            ERR("Thread wait timed out\n");

        CloseHandle(This->thread_hdl);
        This->thread_hdl = NULL;
    }

    if(This->timer_evt)
        CloseHandle(This->timer_evt);
    This->timer_evt = NULL;

    if(This->device)
    {
        if(This->playing)
            alcCaptureStop(This->device);
        alcCaptureCloseDevice(This->device);
    }
    This->parent->buf = NULL;

    HeapFree(GetProcessHeap(), 0, This->notify);
    HeapFree(GetProcessHeap(), 0, This->buf);
    HeapFree(GetProcessHeap(), 0, This);
}

static inline DSCBuffer *impl_from_IDirectSoundCaptureBuffer8(IDirectSoundCaptureBuffer8 *iface)
{
    return CONTAINING_RECORD(iface, DSCBuffer, IDirectSoundCaptureBuffer8_iface);
}

static HRESULT WINAPI DSCBuffer_QueryInterface(IDirectSoundCaptureBuffer8 *iface, REFIID riid, void **ppv)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    if(!ppv)
        return E_POINTER;
    *ppv = NULL;

    if(IsEqualIID(riid, &IID_IUnknown) ||
       IsEqualIID(riid, &IID_IDirectSoundCaptureBuffer))
        *ppv = &This->IDirectSoundCaptureBuffer8_iface;
    else if(IsEqualIID(riid, &IID_IDirectSoundCaptureBuffer8))
    {
        if(This->parent->is_8)
            *ppv = &This->IDirectSoundCaptureBuffer8_iface;
    }
    else if(IsEqualIID(riid, &IID_IDirectSoundNotify))
        *ppv = &This->IDirectSoundNotify_iface;
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI DSCBuffer_AddRef(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    LONG ref;

    InterlockedIncrement(&This->all_ref);
    ref = InterlockedIncrement(&This->ref);
    TRACE("Reference count incremented to %li\n", ref);

    return ref;
}

static ULONG WINAPI DSCBuffer_Release(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %li\n", ref);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSCBuffer_Destroy(This);

    return ref;
}

static HRESULT WINAPI DSCBuffer_GetCaps(IDirectSoundCaptureBuffer8 *iface, DSCBCAPS *caps)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);

    if (!caps || caps->dwSize < sizeof(*caps))
        return DSERR_INVALIDPARAM;
    caps->dwSize = sizeof(*caps);
    caps->dwFlags = 0;
    caps->dwBufferBytes = This->buf_size;
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetCurrentPosition(IDirectSoundCaptureBuffer8 *iface, DWORD *cappos, DWORD *readpos)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    DWORD pos1, pos2;

    EnterCriticalSection(&This->parent->crst);
    pos1 = This->pos;
    if(This->playing)
    {
        pos2 = This->format.Format.nSamplesPerSec / 100;
        pos2 *= This->format.Format.nBlockAlign;
        pos2 += pos1;
        if (!This->looping && pos2 >= This->buf_size)
            pos2 = 0;
        else
            pos2 %= This->buf_size;
    }
    else
        pos2 = pos1;
    LeaveCriticalSection(&This->parent->crst);

    if(cappos) *cappos = pos1;
    if(readpos) *readpos = pos2;

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetFormat(IDirectSoundCaptureBuffer8 *iface, WAVEFORMATEX *wfx, DWORD allocated, DWORD *written)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    HRESULT hr = DS_OK;
    UINT size;

    TRACE("(%p)->(%p, %lu, %p)\n", iface, wfx, allocated, written);

    if(!wfx && !written)
    {
        WARN("Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    size = sizeof(This->format.Format) + This->format.Format.cbSize;
    if(wfx)
    {
        if(allocated < size)
            hr = DSERR_INVALIDPARAM;
        else
            memcpy(wfx, &This->format.Format, size);
    }
    if(written)
        *written = size;

    return hr;
}

static HRESULT WINAPI DSCBuffer_GetStatus(IDirectSoundCaptureBuffer8 *iface, DWORD *status)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);

    TRACE("(%p)->(%p)\n", iface, status);

    if (!status)
        return DSERR_INVALIDPARAM;
    EnterCriticalSection(&This->parent->crst);
    *status = 0;
    if (This->playing)
    {
        *status |= DSCBSTATUS_CAPTURING;
        if (This->looping)
            *status |= DSCBSTATUS_LOOPING;
    }
    LeaveCriticalSection(&This->parent->crst);

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Initialize(IDirectSoundCaptureBuffer8 *iface, IDirectSoundCapture *parent, const DSCBUFFERDESC *desc)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    WAVEFORMATEX *format;
    ALenum buf_format = -1;

    TRACE("(%p)->(%p, %p)\n", iface, parent, desc);

    if(This->device)
        return DSERR_ALREADYINITIALIZED;

    if (!desc->lpwfxFormat)
        return DSERR_INVALIDPARAM;

    format = desc->lpwfxFormat;
    if(format->nChannels <= 0 || format->nChannels > 2)
    {
        WARN("Invalid Channels %d\n", format->nChannels);
        return DSERR_INVALIDPARAM;
    }
    if(format->wBitsPerSample <= 0 || (format->wBitsPerSample%8) != 0)
    {
        WARN("Invalid BitsPerSample %d\n", format->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(format->nBlockAlign != format->nChannels*format->wBitsPerSample/8)
    {
        WARN("Invalid BlockAlign %d (expected %u = %u*%u/8)\n",
             format->nBlockAlign, format->nChannels*format->wBitsPerSample/8,
             format->nChannels, format->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN("Invalid sample rate %lu\n", format->nSamplesPerSec);
        return DSERR_INVALIDPARAM;
    }
    if(format->nAvgBytesPerSec != format->nSamplesPerSec*format->nBlockAlign)
    {
        WARN("Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
             format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
             format->nSamplesPerSec, format->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }

    if(format->wFormatTag == WAVE_FORMAT_PCM)
    {
        if(format->nChannels == 1)
        {
            switch(format->wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_MONO8; break;
                case 16: buf_format = AL_FORMAT_MONO16; break;
                default:
                    WARN("Unsupported bpp %u\n", format->wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        else if(format->nChannels == 2)
        {
            switch(format->wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_STEREO8; break;
                case 16: buf_format = AL_FORMAT_STEREO16; break;
                default:
                    WARN("Unsupported bpp %u\n", format->wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        else
            WARN("Unsupported channels: %d\n", format->nChannels);

        This->format.Format = *format;
        This->format.Format.cbSize = 0;
    }
    else if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE *wfe;

        if(format->cbSize < sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX))
            return DSERR_INVALIDPARAM;
        else if(format->cbSize > sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX) &&
                format->cbSize != sizeof(WAVEFORMATEXTENSIBLE))
            return DSERR_CONTROLUNAVAIL;

        wfe = CONTAINING_RECORD(format, WAVEFORMATEXTENSIBLE, Format);
        if(!IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
            return DSERR_BADFORMAT;
        if(wfe->Samples.wValidBitsPerSample &&
           wfe->Samples.wValidBitsPerSample != wfe->Format.wBitsPerSample)
            return DSERR_BADFORMAT;

        if(wfe->Format.nChannels == 1 && wfe->dwChannelMask == SPEAKER_FRONT_CENTER)
        {
            switch(wfe->Format.wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_MONO8; break;
                case 16: buf_format = AL_FORMAT_MONO16; break;
                default:
                    WARN("Unsupported bpp %u\n", wfe->Format.wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        else if(wfe->Format.nChannels == 2 && wfe->dwChannelMask == (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT))
        {
            switch(wfe->Format.wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_STEREO8; break;
                case 16: buf_format = AL_FORMAT_STEREO16; break;
                default:
                    WARN("Unsupported bpp %u\n", wfe->Format.wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        else
            WARN("Unsupported channels: %d -- 0x%08lu\n", wfe->Format.nChannels, wfe->dwChannelMask);

        This->format = *wfe;
        This->format.Format.cbSize = sizeof(This->format) - sizeof(This->format.Format);
        This->format.Samples.wValidBitsPerSample = This->format.Format.wBitsPerSample;
    }
    else
        WARN("Unhandled formattag %x\n", format->wFormatTag);

    if(buf_format <= 0)
    {
        WARN("Could not get OpenAL format\n");
        return DSERR_INVALIDPARAM;
    }

    if(desc->dwBufferBytes < This->format.Format.nBlockAlign ||
       (desc->dwBufferBytes%This->format.Format.nBlockAlign) != 0)
    {
        WARN("Invalid BufferBytes (%lu %% %d)\n", desc->dwBufferBytes,
             This->format.Format.nBlockAlign);
        return DSERR_INVALIDPARAM;
    }


    This->buf_size = desc->dwBufferBytes;
    This->buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, This->buf_size);
    if(!This->buf)
    {
        WARN("Out of memory\n");
        return DSERR_INVALIDPARAM;
    }

    This->device = alcCaptureOpenDevice(This->parent->device_name,
        This->format.Format.nSamplesPerSec, buf_format,
        This->format.Format.nSamplesPerSec / FAKE_REFRESH_COUNT * 2
    );
    if(!This->device)
    {
        ERR("Couldn't open device %s 0x%x@%lu, reason: %04x\n", This->parent->device_name,
            buf_format, This->format.Format.nSamplesPerSec, alcGetError(NULL));
        return DSERR_INVALIDPARAM;
    }

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Lock(IDirectSoundCaptureBuffer8 *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    DWORD remain;

    TRACE("(%p)->(%lu, %lu, %p, %p, %p, %p, %#lx)\n", iface, ofs, bytes, ptr1, len1, ptr2, len2, flags);

    if(!ptr1 || !len1)
    {
        WARN("Invalid pointer/len %p %p\n", ptr1, len1);
        return DSERR_INVALIDPARAM;
    }

    *ptr1 = NULL;
    *len1 = 0;
    if(ptr2) *ptr2 = NULL;
    if(len2) *len2 = 0;

    if(ofs >= This->buf_size)
    {
        WARN("Invalid ofs %lu\n", ofs);
        return DSERR_INVALIDPARAM;
    }

    if((flags&DSCBLOCK_ENTIREBUFFER))
        bytes = This->buf_size;
    else if(bytes > This->buf_size)
    {
        WARN("Invalid size %lu\n", bytes);
        return DSERR_INVALIDPARAM;
    }

    if(InterlockedExchange(&This->locked, TRUE) == TRUE)
    {
        WARN("Already locked\n");
        return DSERR_INVALIDPARAM;
    }

    if(ofs + bytes >= This->buf_size)
    {
        *len1 = This->buf_size - ofs;
        remain = bytes - *len1;
    }
    else
    {
        *len1 = bytes;
        remain = 0;
    }
    *ptr1 = This->buf + ofs;

    if(ptr2 && len2 && remain)
    {
        *ptr2 = This->buf;
        *len2 = remain;
    }

    return DS_OK;
}

static HRESULT WINAPI DSCBuffer_Start(IDirectSoundCaptureBuffer8 *iface, DWORD flags)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);

    TRACE("(%p)->(%08lx)\n", iface, flags);

    EnterCriticalSection(&This->parent->crst);
    if(!This->playing)
    {
        DSCBuffer_starttimer(This);
        This->playing = 1;
        alcCaptureStart(This->device);
    }
    This->looping |= !!(flags & DSCBSTART_LOOPING);
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Stop(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->parent->crst);
    if(This->playing)
    {
        DWORD i;
        for(i = 0;i < This->nnotify;++i)
        {
            if(This->notify[i].dwOffset == DSCBPN_OFFSET_STOP)
                SetEvent(This->notify[i].hEventNotify);
        }

        This->playing = This->looping = 0;
        alcCaptureStop(This->device);
    }
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Unlock(IDirectSoundCaptureBuffer8 *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    DWORD_PTR ofs1, ofs2;
    DWORD_PTR boundary = (DWORD_PTR)This->buf;

    TRACE("(%p)->(%p, %lu, %p, %lu)\n", iface, ptr1, len1, ptr2, len2);

    if(InterlockedExchange(&This->locked, FALSE) == FALSE)
    {
        WARN("Not locked\n");
        return DSERR_INVALIDPARAM;
    }

    /* Make sure offset is between boundary and boundary + bufsize */
    ofs1 = (DWORD_PTR)ptr1;
    ofs2 = (DWORD_PTR)ptr2;
    if(ofs1 < boundary)
        return DSERR_INVALIDPARAM;
    if(ofs2 && ofs2 != boundary)
        return DSERR_INVALIDPARAM;

    ofs1 -= boundary;
    ofs2 = 0;
    if(This->buf_size-ofs1 < len1 || len2 > ofs1)
        return DSERR_INVALIDPARAM;

    return DS_OK;
}

static HRESULT WINAPI DSCBuffer_GetObjectInPath(IDirectSoundCaptureBuffer8 *iface, REFGUID guid, DWORD num, REFGUID riid, void **ppv)
{
    FIXME("(%p)->(%s, %lu, %s, %p) stub\n", iface, debugstr_guid(guid), num, debugstr_guid(riid), ppv);
    return E_NOTIMPL;
}

static HRESULT WINAPI DSCBuffer_GetFXStatus(IDirectSoundCaptureBuffer8 *iface, DWORD count, DWORD *status)
{
    FIXME("(%p)->(%lu, %p) stub\n", iface, count, status);
    return E_NOTIMPL;
}

static IDirectSoundCaptureBuffer8Vtbl DSCBuffer_Vtbl =
{
    DSCBuffer_QueryInterface,
    DSCBuffer_AddRef,
    DSCBuffer_Release,
    DSCBuffer_GetCaps,
    DSCBuffer_GetCurrentPosition,
    DSCBuffer_GetFormat,
    DSCBuffer_GetStatus,
    DSCBuffer_Initialize,
    DSCBuffer_Lock,
    DSCBuffer_Start,
    DSCBuffer_Stop,
    DSCBuffer_Unlock,
    DSCBuffer_GetObjectInPath,
    DSCBuffer_GetFXStatus
};

static inline DSCBuffer *impl_from_IDirectSoundNotify(IDirectSoundNotify *iface)
{
    return CONTAINING_RECORD(iface, DSCBuffer, IDirectSoundNotify_iface);
}

static HRESULT WINAPI DSCBufferNot_QueryInterface(IDirectSoundNotify *iface, REFIID riid, void **ppv)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    return IDirectSoundCaptureBuffer_QueryInterface(&This->IDirectSoundCaptureBuffer8_iface, riid, ppv);
}

static ULONG WINAPI DSCBufferNot_AddRef(IDirectSoundNotify *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->not_ref);
    TRACE("new refcount %ld\n", ret);
    return ret;
}

static ULONG WINAPI DSCBufferNot_Release(IDirectSoundNotify *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->not_ref);
    TRACE("new refcount %ld\n", ret);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSCBuffer_Destroy(This);

    return ret;
}

static HRESULT WINAPI DSCBufferNot_SetNotificationPositions(IDirectSoundNotify *iface, DWORD count, const DSBPOSITIONNOTIFY *notifications)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    DSBPOSITIONNOTIFY *nots;
    HRESULT hr;
    DWORD state;

    TRACE("(%p)->(%lu, %p))\n", iface, count, notifications);

    EnterCriticalSection(&This->parent->crst);
    hr = DSERR_INVALIDPARAM;
    if (count && !notifications)
        goto out;

    hr = DSCBuffer_GetStatus(&This->IDirectSoundCaptureBuffer8_iface, &state);
    if(FAILED(hr)) goto out;

    hr = DSERR_INVALIDCALL;
    if (state & DSCBSTATUS_CAPTURING)
        goto out;

    if (!count)
    {
        HeapFree(GetProcessHeap(), 0, This->notify);
        This->notify = 0;
        This->nnotify = 0;
    }
    else
    {
        DWORD i;
        hr = DSERR_INVALIDPARAM;
        for (i = 0; i < count; ++i)
        {
            if (notifications[i].dwOffset >= This->buf_size
                && notifications[i].dwOffset != DSCBPN_OFFSET_STOP)
                goto out;
        }
        hr = E_OUTOFMEMORY;
        nots = HeapAlloc(GetProcessHeap(), 0, count*sizeof(*nots));
        if (!nots)
            goto out;
        memcpy(nots, notifications, count*sizeof(*nots));
        HeapFree(GetProcessHeap(), 0, This->notify);
        This->notify = nots;
        This->nnotify = count;
        hr = S_OK;
    }

out:
    LeaveCriticalSection(&This->parent->crst);
    return hr;
}

static IDirectSoundNotifyVtbl DSCNot_Vtbl =
{
    DSCBufferNot_QueryInterface,
    DSCBufferNot_AddRef,
    DSCBufferNot_Release,
    DSCBufferNot_SetNotificationPositions
};


static inline DSCImpl *impl_from_IDirectSoundCapture(IDirectSoundCapture *iface)
{
    return CONTAINING_RECORD(iface, DSCImpl, IDirectSoundCapture_iface);
}

static inline DSCImpl *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, DSCImpl, IUnknown_iface);
}

static HRESULT WINAPI DSCImpl_QueryInterface(IDirectSoundCapture *iface, REFIID riid, void **ppv);
static ULONG WINAPI DSCImpl_AddRef(IDirectSoundCapture *iface);
static ULONG WINAPI DSCImpl_Release(IDirectSoundCapture *iface);

HRESULT DSOUND_CaptureCreate(REFIID riid, void **cap)
{
    HRESULT hr;

    hr = DSOUND_CaptureCreate8(&IID_IDirectSoundCapture, cap);
    if(SUCCEEDED(hr))
    {
        DSCImpl *impl = impl_from_IDirectSoundCapture(*cap);
        impl->is_8 = FALSE;

        if(!IsEqualIID(riid, &IID_IDirectSoundCapture))
        {
            hr = DSCImpl_QueryInterface(&impl->IDirectSoundCapture_iface, riid, cap);
            DSCImpl_Release(&impl->IDirectSoundCapture_iface);
        }
    }
    return hr;
}

HRESULT DSOUND_CaptureCreate8(REFIID riid, void **cap)
{
    DSCImpl *This;

    *cap = NULL;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This) return DSERR_OUTOFMEMORY;

    This->IDirectSoundCapture_iface.lpVtbl = &DSC_Vtbl;
    This->IUnknown_iface.lpVtbl = &DSC_Unknown_Vtbl;

    This->is_8 = TRUE;

    InitializeCriticalSection(&This->crst);

    if(FAILED(DSCImpl_QueryInterface(&This->IDirectSoundCapture_iface, riid, cap)))
    {
        DSCImpl_Destroy(This);
        return E_NOINTERFACE;
    }
    return S_OK;
}

static void DSCImpl_Destroy(DSCImpl *This)
{
    EnterCriticalSection(&This->crst);
    if (This->buf)
        DSCBuffer_Destroy(This->buf);
    LeaveCriticalSection(&This->crst);

    HeapFree(GetProcessHeap(), 0, This->device_name);

    DeleteCriticalSection(&This->crst);

    HeapFree(GetProcessHeap(), 0, This);
}


static HRESULT WINAPI DSCImpl_IUnknown_QueryInterface(IUnknown *iface, REFIID riid, void **ppobj)
{
    DSCImpl *This = impl_from_IUnknown(iface);
    return DSCImpl_QueryInterface(&This->IDirectSoundCapture_iface, riid, ppobj);
}

static ULONG WINAPI DSCImpl_IUnknown_AddRef(IUnknown *iface)
{
    DSCImpl *This = impl_from_IUnknown(iface);
    ULONG ref;

    InterlockedIncrement(&(This->allref));
    ref = InterlockedIncrement(&(This->unkref));
    TRACE("(%p) ref was %lu\n", This, ref - 1);

    return ref;
}

static ULONG WINAPI DSCImpl_IUnknown_Release(IUnknown *iface)
{
    DSCImpl *This = impl_from_IUnknown(iface);
    ULONG ref = InterlockedDecrement(&(This->unkref));
    TRACE("(%p) ref was %lu\n", This, ref + 1);
    if(InterlockedDecrement(&(This->allref)) == 0)
        DSCImpl_Destroy(This);
    return ref;
}

static IUnknownVtbl DSC_Unknown_Vtbl = {
    DSCImpl_IUnknown_QueryInterface,
    DSCImpl_IUnknown_AddRef,
    DSCImpl_IUnknown_Release
};


static HRESULT WINAPI DSCImpl_QueryInterface(IDirectSoundCapture *iface, REFIID riid, void **ppv)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IDirectSoundCapture))
        *ppv = &This->IDirectSoundCapture_iface;
    else if(IsEqualIID(riid, &IID_IUnknown))
        *ppv = &This->IUnknown_iface;
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI DSCImpl_AddRef(IDirectSoundCapture *iface)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    LONG ref;

    InterlockedIncrement(&(This->allref));
    ref = InterlockedIncrement(&This->dscref);
    TRACE("Reference count incremented to %li\n", ref);

    return ref;
}

static ULONG WINAPI DSCImpl_Release(IDirectSoundCapture *iface)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->dscref);
    TRACE("Reference count decremented to %li\n", ref);
    if(InterlockedDecrement(&This->allref) == 0)
        DSCImpl_Destroy(This);

    return ref;
}

static HRESULT WINAPI DSCImpl_CreateCaptureBuffer(IDirectSoundCapture *iface, const DSCBUFFERDESC *desc, IDirectSoundCaptureBuffer **ppv, IUnknown *unk)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    DSCBuffer *buffer;
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p)\n", iface, desc, ppv, unk);

    if(unk)
    {
        WARN("Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }

    if(!desc || desc->dwSize < sizeof(DSCBUFFERDESC1))
    {
        WARN("Passed invalid description %p %lu\n", desc, desc?desc->dwSize:0);
        return DSERR_INVALIDPARAM;
    }
    if(!ppv)
    {
        WARN("Passed null pointer\n");
        return DSERR_INVALIDPARAM;
    }
    *ppv = NULL;

    EnterCriticalSection(&This->crst);
    if(!This->device_name)
    {
        hr = DSERR_UNINITIALIZED;
        WARN("Not initialized\n");
        goto out;
    }
    if(This->buf)
    {
        hr = DSERR_ALLOCATED;
        WARN("Capture buffer already allocated\n");
        goto out;
    }

    hr = DSCBuffer_Create(&buffer, This);
    if(SUCCEEDED(hr))
    {
        hr = DSCBuffer_Initialize(&buffer->IDirectSoundCaptureBuffer8_iface, iface, desc);
        if(FAILED(hr))
            DSCBuffer_Release(&buffer->IDirectSoundCaptureBuffer8_iface);
        else
        {
            This->buf = buffer;
            *ppv = (IDirectSoundCaptureBuffer*)&buffer->IDirectSoundCaptureBuffer8_iface;
        }
    }

out:
    LeaveCriticalSection(&This->crst);
    return hr;
}

static HRESULT WINAPI DSCImpl_GetCaps(IDirectSoundCapture *iface, DSCCAPS *caps)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);

    TRACE("(%p)->(%p)\n", iface, caps);

    if(!This->device_name)
    {
        WARN("Not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!caps) {
        WARN("Caps is null\n");
        return DSERR_INVALIDPARAM;
    }
    if(caps->dwSize < sizeof(*caps)) {
        WARN("Invalid size %ld\n", caps->dwSize);
        return DSERR_INVALIDPARAM;
    }

    caps->dwFlags = 0;
    /* Support all WAVE_FORMAT formats specified in mmsystem.h */
    caps->dwFormats = 0x000fffff;
    caps->dwChannels = 2;

    return DS_OK;
}

static HRESULT WINAPI DSCImpl_Initialize(IDirectSoundCapture *iface, const GUID *devguid)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    OLECHAR *guid_str = NULL;
    HRESULT hr;
    GUID guid;
    int len;

    TRACE("(%p)->(%p)\n", iface, devguid);

    if(!openal_loaded)
    {
        ERR("OpenAL not loaded!\n");
        return DSERR_NODRIVER;
    }

    if(This->device_name)
    {
        WARN("Already initialized\n");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!devguid || IsEqualGUID(devguid, &GUID_NULL))
        devguid = &DSDEVID_DefaultCapture;
    else if(IsEqualGUID(devguid, &DSDEVID_DefaultPlayback) ||
            IsEqualGUID(devguid, &DSDEVID_DefaultVoicePlayback))
        return DSERR_NODRIVER;

    hr = DSOAL_GetDeviceID(devguid, &guid);
    if(FAILED(hr)) return hr;

    devguid = &guid;

    hr = StringFromCLSID(devguid, &guid_str);
    if(FAILED(hr))
    {
        ERR("Failed to convert GUID to string\n");
        return hr;
    }

    EnterCriticalSection(&This->crst);
    EnterCriticalSection(&openal_crst);

    hr = E_OUTOFMEMORY;
    len = WideCharToMultiByte(CP_UTF8, 0, guid_str, -1, NULL, 0, NULL, NULL);
    if(len <= 0)
    {
        ERR("Failed to convert GUID to string\n");
        goto out;
    }

    This->device_name = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len+1);
    if(!This->device_name)
    {
        WARN("Out of memory to duplicate string \"%ls\"\n", guid_str);
        goto out;
    }
    WideCharToMultiByte(CP_UTF8, 0, guid_str, -1, This->device_name, len, NULL, NULL);

    hr = S_OK;
out:
    LeaveCriticalSection(&openal_crst);
    LeaveCriticalSection(&This->crst);
    if(guid_str)
        CoTaskMemFree(guid_str);
    guid_str = NULL;
    return hr;
}

static IDirectSoundCaptureVtbl DSC_Vtbl =
{
    DSCImpl_QueryInterface,
    DSCImpl_AddRef,
    DSCImpl_Release,
    DSCImpl_CreateCaptureBuffer,
    DSCImpl_GetCaps,
    DSCImpl_Initialize
};
