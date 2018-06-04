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

#include <stdarg.h>

#include <windows.h>
#include <dsound.h>

#include "dsound_private.h"

#ifndef DSCBPN_OFFSET_STOP
#define DSCBPN_OFFSET_STOP          0xffffffff
#endif

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);


typedef struct DSCImpl DSCImpl;
typedef struct DSCBuffer DSCBuffer;

struct DSCImpl {
    /* IDirectSoundCapture and IDirectSoundCapture8 are aliases */
    IDirectSoundCapture IDirectSoundCapture_iface;
    LONG ref;

    BOOL is_8;

    ALCchar *device;
    DSCBuffer *buf;

    CRITICAL_SECTION crst;
};

struct DSCBuffer {
    IDirectSoundCaptureBuffer8 IDirectSoundCaptureBuffer8_iface;
    IDirectSoundNotify IDirectSoundNotify_iface;
    LONG ref, not_ref;
    LONG all_ref;

    DSCImpl *parent;
    ALCdevice *dev;

    DWORD buf_size;
    BYTE *buf;

    LONG locked;

    WAVEFORMATEXTENSIBLE format;

    DSBPOSITIONNOTIFY *notify;
    DWORD nnotify;

    UINT timer_id;
    DWORD timer_res;
    HANDLE thread_hdl;
    DWORD thread_id;

    DWORD pos;
    BOOL playing, looping;
};

static const IDirectSoundCaptureVtbl DSC_Vtbl;
static const IDirectSoundCaptureBuffer8Vtbl DSCBuffer_Vtbl;
static const IDirectSoundNotifyVtbl DSCNot_Vtbl;

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
                TRACE("Triggering notification %"LONGFMT"u (%"LONGFMT"u) from buffer %p\n", i, ofs, buf);
                SetEvent(event);
            }
            continue;
        }

        /* Normal case */
        if(ofs >= lastpos && ofs < curpos)
        {
            TRACE("Triggering notification %"LONGFMT"u (%"LONGFMT"u) from buffer %p\n", i, ofs, buf);
            SetEvent(event);
        }
    }
}

static DWORD CALLBACK DSCBuffer_thread(void *param)
{
    DSCImpl *This = param;
    DSCBuffer *buf;
    ALCint avail;
    MSG msg;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while(GetMessageA(&msg, NULL, 0, 0))
    {
        if(msg.message != WM_USER)
            continue;

        avail = 0;
        buf = This->buf;
        alcGetIntegerv(buf->dev, ALC_CAPTURE_SAMPLES, 1, &avail);
        if(avail == 0 || !buf->playing)
            continue;

        EnterCriticalSection(&This->crst);
    more_samples:
        avail *= buf->format.Format.nBlockAlign;
        if(avail + buf->pos > buf->buf_size)
            avail = buf->buf_size - buf->pos;

        alcCaptureSamples(buf->dev, buf->buf + buf->pos, avail/buf->format.Format.nBlockAlign);
        trigger_notifies(buf, buf->pos, buf->pos + avail);
        buf->pos += avail;

        if(buf->pos == buf->buf_size)
        {
            buf->pos = 0;
            if(!buf->looping)
                IDirectSoundCaptureBuffer8_Stop(&buf->IDirectSoundCaptureBuffer8_iface);
            else
            {
                alcGetIntegerv(buf->dev, ALC_CAPTURE_SAMPLES, 1, &avail);
                if(avail) goto more_samples;
            }
        }

        LeaveCriticalSection(&This->crst);
    }

    return 0;
}


static void CALLBACK DSCBuffer_timer(UINT timerID, UINT msg, DWORD_PTR dwUser,
                                     DWORD_PTR dw1, DWORD_PTR dw2)
{
    (void)timerID;
    (void)msg;
    (void)dw1;
    (void)dw2;
    PostThreadMessageA(dwUser, WM_USER, 0, 0);
}

static void DSCBuffer_starttimer(DSCBuffer *This)
{
    TIMECAPS time;
    ALint refresh = FAKE_REFRESH_COUNT;
    DWORD triggertime, res = DS_TIME_RES;

    if(This->timer_id)
        return;

    timeGetDevCaps(&time, sizeof(TIMECAPS));
    triggertime = 1000 / refresh;
    if (triggertime < time.wPeriodMin)
        triggertime = time.wPeriodMin;
    TRACE("Calling timer every %"LONGFMT"u ms for %i refreshes per second\n", triggertime, refresh);
    if (res < time.wPeriodMin)
        res = time.wPeriodMin;
    if (timeBeginPeriod(res) == TIMERR_NOCANDO)
        WARN("Could not set minimum resolution, don't expect sound\n");
    This->timer_res = res;
    This->timer_id = timeSetEvent(triggertime, res, DSCBuffer_timer, This->thread_id, TIME_PERIODIC|TIME_KILL_SYNCHRONOUS);
}

static HRESULT DSCBuffer_Create(DSCBuffer **buf, DSCImpl *parent)
{
    DSCBuffer *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This) return E_OUTOFMEMORY;

    This->IDirectSoundCaptureBuffer8_iface.lpVtbl = (IDirectSoundCaptureBuffer8Vtbl*)&DSCBuffer_Vtbl;
    This->IDirectSoundNotify_iface.lpVtbl = (IDirectSoundNotifyVtbl*)&DSCNot_Vtbl;

    This->all_ref = This->ref = 1;

    This->parent = parent;

    This->thread_hdl = CreateThread(NULL, 0, DSCBuffer_thread, This->parent, 0, &This->thread_id);
    if(This->thread_hdl == NULL)
    {
        HeapFree(GetProcessHeap(), 0, This);
        return DSERR_OUTOFMEMORY;
    }

    *buf = This;
    return S_OK;
}

static void DSCBuffer_Destroy(DSCBuffer *This)
{
    if(This->timer_id)
    {
        timeKillEvent(This->timer_id);
        timeEndPeriod(This->timer_res);
    }
    if(This->thread_hdl)
    {
        PostThreadMessageA(This->thread_id, WM_QUIT, 0, 0);
        if(WaitForSingleObject(This->thread_hdl, 1000) != WAIT_OBJECT_0)
            ERR("Thread wait timed out");
        CloseHandle(This->thread_hdl);
    }

    if(This->dev)
    {
        if(This->playing)
            alcCaptureStop(This->dev);
        alcCaptureCloseDevice(This->dev);
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
    TRACE("Reference count incremented to %"LONGFMT"i\n", ref);

    return ref;
}

static ULONG WINAPI DSCBuffer_Release(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %"LONGFMT"i\n", ref);
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

    TRACE("(%p)->(%p, %"LONGFMT"u, %p)\n", iface, wfx, allocated, written);

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

    if(This->dev)
        return DSERR_ALREADYINITIALIZED;

    if (!desc->lpwfxFormat)
        return DSERR_INVALIDPARAM;

    format = desc->lpwfxFormat;
    if(format->nChannels > 2)
    {
        WARN("nChannels > 2 not supported for recording\n");
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

        memcpy(&This->format.Format, format, sizeof(This->format.Format));
        This->format.Format.nBlockAlign = This->format.Format.wBitsPerSample * This->format.Format.nChannels / 8;
        This->format.Format.nAvgBytesPerSec = This->format.Format.nSamplesPerSec * This->format.Format.nBlockAlign;
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
            WARN("Unsupported channels: %d -- 0x%08"LONGFMT"u\n", wfe->Format.nChannels, wfe->dwChannelMask);

        memcpy(&This->format, wfe, sizeof(This->format));
        This->format.Format.cbSize = sizeof(This->format) - sizeof(This->format.Format);
        This->format.Format.nBlockAlign = This->format.Format.wBitsPerSample * This->format.Format.nChannels / 8;
        This->format.Format.nAvgBytesPerSec = This->format.Format.nSamplesPerSec * This->format.Format.nBlockAlign;
    }
    else
        WARN("Unhandled formattag %x\n", format->wFormatTag);

    if(buf_format <= 0)
    {
        WARN("Could not get OpenAL format\n");
        return DSERR_INVALIDPARAM;
    }

    This->buf_size = desc->dwBufferBytes;
    This->buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, This->buf_size);
    if(!This->buf)
    {
        WARN("Out of memory\n");
        return DSERR_INVALIDPARAM;
    }

    This->dev = alcCaptureOpenDevice(This->parent->device, This->format.Format.nSamplesPerSec, buf_format, This->format.Format.nSamplesPerSec / FAKE_REFRESH_COUNT * 2);
    if(!This->dev)
    {
        ERR("Couldn't open device %s 0x%x@%"LONGFMT"u, reason: %04x\n", This->parent->device, buf_format, This->format.Format.nSamplesPerSec, alcGetError(NULL));
        return DSERR_INVALIDPARAM;
    }

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Lock(IDirectSoundCaptureBuffer8 *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    DWORD remain;

    TRACE("(%p)->(%"LONGFMT"u, %"LONGFMT"u, %p, %p, %p, %p, %#"LONGFMT"x)\n", iface, ofs, bytes, ptr1, len1, ptr2, len2, flags);

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
        WARN("Invalid ofs %"LONGFMT"u\n", ofs);
        return DSERR_INVALIDPARAM;
    }

    if((flags&DSCBLOCK_ENTIREBUFFER))
        bytes = This->buf_size;
    else if(bytes > This->buf_size)
    {
        WARN("Invalid size %"LONGFMT"u\n", bytes);
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

    TRACE("(%p)->(%08"LONGFMT"x)\n", iface, flags);

    EnterCriticalSection(&This->parent->crst);
    if(!This->playing)
    {
        DSCBuffer_starttimer(This);
        This->playing = 1;
        alcCaptureStart(This->dev);
    }
    This->looping = !!(flags & DSCBSTART_LOOPING);
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
        alcCaptureStop(This->dev);
    }
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Unlock(IDirectSoundCaptureBuffer8 *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DSCBuffer *This = impl_from_IDirectSoundCaptureBuffer8(iface);
    DWORD_PTR ofs1, ofs2;
    DWORD_PTR boundary = (DWORD_PTR)This->buf;

    TRACE("(%p)->(%p, %"LONGFMT"u, %p, %"LONGFMT"u)\n", iface, ptr1, len1, ptr2, len2);

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
    FIXME("(%p)->(%s, %"LONGFMT"u, %s, %p) stub\n", iface, debugstr_guid(guid), num, debugstr_guid(riid), ppv);
    return E_NOTIMPL;
}

static HRESULT WINAPI DSCBuffer_GetFXStatus(IDirectSoundCaptureBuffer8 *iface, DWORD count, DWORD *status)
{
    FIXME("(%p)->(%"LONGFMT"u, %p) stub\n", iface, count, status);
    return E_NOTIMPL;
}

static const IDirectSoundCaptureBuffer8Vtbl DSCBuffer_Vtbl =
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
    return IDirectSoundCaptureBuffer_QueryInterface((IDirectSoundCaptureBuffer*)This, riid, ppv);
}

static ULONG WINAPI DSCBufferNot_AddRef(IDirectSoundNotify *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->not_ref);
    TRACE("new refcount %"LONGFMT"d\n", ret);
    return ret;
}

static ULONG WINAPI DSCBufferNot_Release(IDirectSoundNotify *iface)
{
    DSCBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->not_ref);
    TRACE("new refcount %"LONGFMT"d\n", ret);
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

    TRACE("(%p)->(%"LONGFMT"u, %p))\n", iface, count, notifications);

    EnterCriticalSection(&This->parent->crst);
    hr = DSERR_INVALIDPARAM;
    if (count && !notifications)
        goto out;

    hr = DSCBuffer_GetStatus(&This->IDirectSoundCaptureBuffer8_iface, &state);
    if (FAILED(hr))
        goto out;

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

static const IDirectSoundNotifyVtbl DSCNot_Vtbl =
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
            hr = IDirectSoundCapture_QueryInterface(&impl->IDirectSoundCapture_iface, riid, cap);
            IDirectSoundCapture_Release(&impl->IDirectSoundCapture_iface);
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

    This->IDirectSoundCapture_iface.lpVtbl = (IDirectSoundCaptureVtbl*)&DSC_Vtbl;

    This->is_8 = TRUE;

    InitializeCriticalSection(&This->crst);

    if(FAILED(IDirectSoundCapture_QueryInterface(&This->IDirectSoundCapture_iface, riid, cap)))
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

    HeapFree(GetProcessHeap(), 0, This->device);

    DeleteCriticalSection(&This->crst);

    HeapFree(GetProcessHeap(), 0, This);
}

static HRESULT WINAPI DSCImpl_QueryInterface(IDirectSoundCapture *iface, REFIID riid, void **ppv)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown) ||
       IsEqualIID(riid, &IID_IDirectSoundCapture))
        *ppv = &This->IDirectSoundCapture_iface;
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

    ref = InterlockedIncrement(&This->ref);
    TRACE("Reference count incremented to %"LONGFMT"i\n", ref);

    return ref;
}

static ULONG WINAPI DSCImpl_Release(IDirectSoundCapture *iface)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %"LONGFMT"i\n", ref);
    if(!ref)
        DSCImpl_Destroy(This);

    return ref;
}

static HRESULT WINAPI DSCImpl_CreateCaptureBuffer(IDirectSoundCapture *iface, const DSCBUFFERDESC *desc, IDirectSoundCaptureBuffer **ppv, IUnknown *unk)
{
    DSCImpl *This = impl_from_IDirectSoundCapture(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p)\n", iface, desc, ppv, unk);

    if(unk)
    {
        WARN("Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }

    if(!desc || desc->dwSize < sizeof(DSCBUFFERDESC1))
    {
        WARN("Passed invalid description %p %"LONGFMT"u\n", desc, desc?desc->dwSize:0);
        return DSERR_INVALIDPARAM;
    }
    if(!ppv)
    {
        WARN("Passed null pointer\n");
        return DSERR_INVALIDPARAM;
    }
    *ppv = NULL;

    EnterCriticalSection(&This->crst);
    if(!This->device)
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

    hr = DSCBuffer_Create(&This->buf, This);
    if(SUCCEEDED(hr))
    {
        hr = IDirectSoundCaptureBuffer8_Initialize(&This->buf->IDirectSoundCaptureBuffer8_iface, iface, desc);
        if(SUCCEEDED(hr))
            *ppv = (IDirectSoundCaptureBuffer*)&This->buf->IDirectSoundCaptureBuffer8_iface;
        else
        {
            DSCBuffer_Destroy(This->buf);
            This->buf = NULL;
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

    if(!This->device) {
        WARN("Not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!caps) {
        WARN("Caps is null\n");
        return DSERR_INVALIDPARAM;
    }
    if(caps->dwSize < sizeof(*caps)) {
        WARN("Invalid size %"LONGFMT"d\n", caps->dwSize);
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
    HRESULT hr;
    const ALCchar *devs, *drv_name;
    GUID guid;
    UINT n;

    TRACE("(%p)->(%p)\n", iface, devguid);

    if(!openal_loaded)
    {
        ERR("OpenAL not loaded!\n");
        return DSERR_NODRIVER;
    }

    if(This->device) {
        WARN("Already initialized\n");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!devguid || IsEqualGUID(devguid, &GUID_NULL))
        devguid = &DSDEVID_DefaultCapture;

    hr = GetDeviceID(devguid, &guid);
    if (FAILED(hr))
        return DSERR_INVALIDPARAM;
    devguid = &guid;

    EnterCriticalSection(&This->crst);
    EnterCriticalSection(&openal_crst);
    devs = DSOUND_getcapturedevicestrings();
    n = guid.Data4[7];

    hr = DSERR_NODRIVER;
    if(memcmp(devguid, &DSOUND_capture_guid, sizeof(GUID)-1) ||
       !devs || !*devs)
    {
        WARN("No driver found\n");
        goto out;
    }

    if(n)
    {
        const ALCchar *str = devs;
        while (n--)
        {
            str += strlen(str) + 1;
            if (!*str)
            {
                WARN("No driver string found\n");
                goto out;
            }
        }
        drv_name = str;
    }
    else
        drv_name = devs;

    This->device = HeapAlloc(GetProcessHeap(), 0, strlen(drv_name)+1);
    if(!This->device)
    {
        WARN("Out of memory to allocate %s\n", drv_name);
        goto out;
    }
    strcpy(This->device, drv_name);

    hr = S_OK;
out:
    LeaveCriticalSection(&openal_crst);
    LeaveCriticalSection(&This->crst);
    return hr;
}

static const IDirectSoundCaptureVtbl DSC_Vtbl =
{
    DSCImpl_QueryInterface,
    DSCImpl_AddRef,
    DSCImpl_Release,
    DSCImpl_CreateCaptureBuffer,
    DSCImpl_GetCaps,
    DSCImpl_Initialize
};
