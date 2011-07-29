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

#ifdef __WINESRC__

#define COBJMACROS
#define NONAMELESSSTRUCT
#define NONAMELESSUNION
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winnls.h"
#include "winreg.h"
#include "mmsystem.h"
#include "winternl.h"
#include "mmddk.h"
#include "wine/debug.h"
#include "dsound.h"

#include "dsound_private.h"

#include "ks.h"
#include "ksmedia.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsound);

#else

#define WINVER 0x0600
#include <windows.h>
#include <dsound.h>

#include "dsound_private.h"

#ifndef DSCBPN_OFFSET_STOP
#define DSCBPN_OFFSET_STOP          0xffffffff
#endif

DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM, 0x00000001, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);

#endif

/* IDirectSoundCapture and IDirectSoundCapture8 are aliases */
HRESULT DSOUND_CaptureCreate(REFIID riid, IDirectSoundCapture **cap)
{
    return DSOUND_CaptureCreate8(riid, cap);
}

typedef struct DSCBuffer DSCBuffer;

typedef struct DSCImpl
{
    const IDirectSoundCaptureVtbl *lpVtbl;
    LONG ref;

    ALCchar *device;
    DSCBuffer *buf;
    UINT timer_id;
    DWORD timer_res;
    CRITICAL_SECTION crst;
} DSCImpl;

struct DSCBuffer
{
    const IDirectSoundCaptureBuffer8Vtbl *lpVtbl;
    const IDirectSoundNotifyVtbl *lpNotVtbl;
    LONG ref, not_ref;
    DSCImpl *parent;
    ALCdevice *dev;
    DWORD buf_size;
    BYTE *buf;
    WAVEFORMATEX *format;
    DSBPOSITIONNOTIFY *notify;
    DWORD nnotify;

    DWORD pos;
    BOOL playing, looping;
};

static const IDirectSoundCaptureVtbl DSC_Vtbl;
static const IDirectSoundCaptureBuffer8Vtbl DSCBuffer_Vtbl;
static const IDirectSoundNotifyVtbl DSCNot_Vtbl;

static void DSCImpl_Destroy(DSCImpl *This);

static HRESULT DSCBuffer_Create(DSCBuffer **buf)
{
    DSCBuffer *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;
    This->lpVtbl = &DSCBuffer_Vtbl;
    This->lpNotVtbl = &DSCNot_Vtbl;
    This->ref = 1;
    *buf = This;
    return S_OK;
}

static void trigger_notifies(DSCBuffer *buf, DWORD lastpos, DWORD curpos)
{
    DWORD i;
    if (lastpos == curpos)
        return;
    for (i = 0; i < buf->nnotify; ++i)
    {
        DSBPOSITIONNOTIFY *not = &buf->notify[i];
        HANDLE event = not->hEventNotify;
        DWORD ofs = not->dwOffset;

        if (ofs == DSCBPN_OFFSET_STOP)
            continue;

        /* Wraparound case */
        if (curpos < lastpos)
        {
            if (ofs < curpos
                || ofs >= lastpos)
                SetEvent(event);
            continue;
        }

        /* Normal case */
        if (ofs >= lastpos
            && ofs < curpos)
            SetEvent(event);
    }
}

static void CALLBACK DSCBuffer_timer(UINT timerID, UINT msg, DWORD_PTR dwUser,
                                     DWORD_PTR dw1, DWORD_PTR dw2)
{
    DSCImpl *This = (DSCImpl*)dwUser;
    DSCBuffer *buf;
    ALint avail = 0;

    EnterCriticalSection(&This->crst);
    buf = This->buf;
    if (!buf || !buf->dev || !buf->playing)
        goto out;

    alcGetIntegerv(buf->dev, ALC_CAPTURE_SAMPLES, 1, &avail);
    if (avail)
    {
        avail *= buf->format->nBlockAlign;
        if (avail + buf->pos > buf->buf_size)
            avail = buf->buf_size - buf->pos;

        alcCaptureSamples(buf->dev, buf->buf + buf->pos, avail/buf->format->nBlockAlign);
        trigger_notifies(buf, buf->pos, buf->pos + avail);
        buf->pos += avail;

        if (buf->pos == buf->buf_size)
        {
            buf->pos = 0;
            if (!buf->looping)
                IDirectSoundCaptureBuffer_Stop((IDirectSoundCaptureBuffer*)buf);
            else
            {
                avail = 0;
                alcGetIntegerv(buf->dev, ALC_CAPTURE_SAMPLES, 1, &avail);
                avail *= buf->format->nBlockAlign;
                if (avail >= buf->buf_size)
                {
                    ERR("TOO MUCH AVAIL: %u/%u\n", avail, buf->buf_size);
                    avail = buf->buf_size;
                }

                if (avail)
                {
                    alcCaptureSamples(buf->dev, buf->buf + buf->pos, avail/buf->format->nBlockAlign);
                    trigger_notifies(buf, buf->pos, buf->pos + avail);
                    buf->pos += avail;
                }
            }
        }
    }

out:
    LeaveCriticalSection(&This->crst);
    return;
}

static void DSCBuffer_starttimer(DSCImpl *prim)
{
    TIMECAPS time;
    ALint refresh = FAKE_REFRESH_COUNT;
    DWORD triggertime, res = DS_TIME_RES;

    if (prim->timer_id)
        return;

    timeGetDevCaps(&time, sizeof(TIMECAPS));
    triggertime = 1000 / refresh;
    if (triggertime < time.wPeriodMin)
        triggertime = time.wPeriodMin;
    TRACE("Calling timer every %u ms for %i refreshes per second\n", triggertime, refresh);
    if (res < time.wPeriodMin)
        res = time.wPeriodMin;
    if (timeBeginPeriod(res) == TIMERR_NOCANDO)
        WARN("Could not set minimum resolution, don't expect sound\n");
    prim->timer_res = res;
    prim->timer_id = timeSetEvent(triggertime, res, DSCBuffer_timer, (DWORD_PTR)prim, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
}

static void DSCBuffer_Destroy(DSCBuffer *This)
{
    if (This->dev)
    {
        if (This->playing)
            alcCaptureStop(This->dev);
        alcCaptureCloseDevice(This->dev);
    }
    if (This->parent)
        This->parent->buf = NULL;
    HeapFree(GetProcessHeap(), 0, This->notify);
    HeapFree(GetProcessHeap(), 0, This->format);
    HeapFree(GetProcessHeap(), 0, This->buf);
    HeapFree(GetProcessHeap(), 0, This);
}

static HRESULT WINAPI DSCBuffer_QueryInterface(IDirectSoundCaptureBuffer8 *iface, REFIID riid, void **ppv)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    if (!ppv)
        return E_POINTER;
    TRACE("(%p)->(%s,%p)\n", This, debugstr_guid(riid), ppv);
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IDirectSoundNotify))
        *ppv = &This->lpNotVtbl;
    else if (IsEqualIID(riid, &IID_IUnknown)
             || IsEqualIID(riid, &IID_IDirectSoundCaptureBuffer)
             || IsEqualIID(riid, &IID_IDirectSoundCaptureBuffer8))
        *ppv = This;
    if (!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI DSCBuffer_AddRef(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    LONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("Reference count incremented to %i\n", ref);
    return ref;
}

static ULONG WINAPI DSCBuffer_Release(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    CRITICAL_SECTION *crst = &This->parent->crst;
    LONG ref;
    EnterCriticalSection(crst);
    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %i\n", ref);
    if (!ref && !This->not_ref)
        DSCBuffer_Destroy(This);
    LeaveCriticalSection(crst);
    return ref;
}

static HRESULT WINAPI DSCBuffer_GetCaps(IDirectSoundCaptureBuffer8 *iface, DSCBCAPS *caps)
{
    DSCBuffer *This = (DSCBuffer*)iface;

    if (!caps || caps->dwSize < sizeof(*caps))
        return DSERR_INVALIDPARAM;
    caps->dwSize = sizeof(*caps);
    caps->dwFlags = 0;
    caps->dwBufferBytes = This->buf_size;
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetCurrentPosition(IDirectSoundCaptureBuffer8 *iface, DWORD *cappos, DWORD *readpos)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    DWORD pos1, pos2;
    EnterCriticalSection(&This->parent->crst);
    pos1 = This->pos;
    if (This->playing)
    {
        pos2 = This->format->nSamplesPerSec / 100;
        pos2 *= This->format->nBlockAlign;
        pos2 += pos1;
        if (!This->looping && pos2 > This->buf_size)
            pos2 = 0;
        else
            pos2 %= This->buf_size;
    }
    else
        pos2 = pos1;
    pos2 %= This->buf_size;
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetFormat(IDirectSoundCaptureBuffer8 *iface, WAVEFORMATEX *wfx, DWORD size, DWORD *written)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    TRACE("(%p,%p,%u,%p)\n", This, wfx, size, written);

    if (size > sizeof(WAVEFORMATEX) + This->format->cbSize)
        size = sizeof(WAVEFORMATEX) + This->format->cbSize;

    if (wfx)
    {
        CopyMemory(wfx, This->format, size);
        if (written)
            *written = size;
    } else if (written)
        *written = sizeof(WAVEFORMATEX) + This->format->cbSize;
    else
        return DSERR_INVALIDPARAM;

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetStatus(IDirectSoundCaptureBuffer8 *iface, DWORD *status)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    TRACE("(%p)->(%p)\n", This, status);

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
    DSCBuffer *This = (DSCBuffer*)iface;
    WAVEFORMATEX *format;
    ALenum buf_format = -1;

    if (This->parent)
        return DSERR_ALREADYINITIALIZED;
    This->parent = (DSCImpl*)parent;
    if (!desc->lpwfxFormat)
        return DSERR_INVALIDPARAM;

    format = desc->lpwfxFormat;
    if (format->nChannels > 2)
    {
        WARN("nChannels > 2 not supported for recording\n");
        return DSERR_INVALIDPARAM;
    }

    if (!This->format)
        This->format = HeapAlloc(GetProcessHeap(), 0, sizeof(WAVEFORMATEXTENSIBLE));
    if (!This->format)
        return DSERR_OUTOFMEMORY;

    if (format->wFormatTag == WAVE_FORMAT_PCM
        || format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            WAVEFORMATEXTENSIBLE *wfe = (WAVEFORMATEXTENSIBLE*)format;
            if (format->cbSize < sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX))
                return DSERR_INVALIDPARAM;
            else if (format->cbSize > sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)
                && format->cbSize != sizeof(WAVEFORMATEXTENSIBLE))
                return DSERR_CONTROLUNAVAIL;
            else if (!IsEqualGUID(&wfe->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
                return DSERR_BADFORMAT;
        }

        if (format->nChannels == 1)
        {
            switch (format->wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_MONO8; break;
                case 16: buf_format = AL_FORMAT_MONO16; break;
                default:
                    WARN("Unsupported bpp %u\n", format->wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        else if (format->nChannels == 2)
        {
            switch (format->wBitsPerSample)
            {
                case 8: buf_format = AL_FORMAT_STEREO8; break;
                case 16: buf_format = AL_FORMAT_STEREO16; break;
                default:
                    WARN("Unsupported bpp %u\n", format->wBitsPerSample);
                    return DSERR_BADFORMAT;
            }
        }
        memcpy(This->format, format, sizeof(*format) + format->cbSize);
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            This->format->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        else
            This->format->cbSize = 0;
        This->format->nBlockAlign = This->format->wBitsPerSample * This->format->nChannels / 8;
        This->format->nAvgBytesPerSec = This->format->nSamplesPerSec * This->format->nBlockAlign;
    }
    else if (format->wFormatTag)
        WARN("Unhandled formattag %x\n", format->wFormatTag);

    This->buf_size = desc->dwBufferBytes;
    if(buf_format <= 0)
    {
        WARN("Could not get OpenAL format\n");
        return DSERR_INVALIDPARAM;
    }
    This->dev = alcCaptureOpenDevice(This->parent->device, This->format->nSamplesPerSec, buf_format, This->buf_size / This->format->nBlockAlign);
    if (!This->dev)
    {
        ERR("couldn't open device %s %x@%u, reason: %04x\n", This->parent->device, buf_format, This->format->nSamplesPerSec, alcGetError(NULL));
        return DSERR_INVALIDPARAM;
    }
    This->buf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, This->buf_size);
    if (!This->buf)
    {
        alcCaptureCloseDevice(This->dev);
        WARN("Out of memory\n");
        return DSERR_INVALIDPARAM;
    }

    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Lock(IDirectSoundCaptureBuffer8 *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    HRESULT hr;
    DWORD remain;
    TRACE("(%p)->(%u, %u, %p, %p, %p, %p, %#x)\n", This, ofs, bytes, ptr1, len1, ptr2, len2, flags);

    EnterCriticalSection(&This->parent->crst);
    hr = DSERR_INVALIDPARAM;
    if (ptr1)
        *ptr1 = NULL;
    if (len1)
        *len1 = 0;
    if (ptr2)
        *ptr2 = NULL;
    if (len2)
        *len2 = 0;
    if (ofs >= This->buf_size)
    {
        WARN("Invalid ofs %u\n", ofs);
        goto out;
    }
    if (!ptr1 || !len1)
    {
        WARN("Invalid pointer/len %p %p\n", ptr1, len1);
        goto out;
    }
    if (flags & DSCBLOCK_ENTIREBUFFER)
        bytes = This->buf_size;
    if (bytes > This->buf_size)
    {
        WARN("Invalid size %u\n", bytes);
        goto out;
    }

    if (ofs + bytes >= This->buf_size)
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

    if (ptr2 && len2 && remain)
    {
        *ptr2 = This->buf;
        *len2 = remain;
    }
    hr = S_OK;

out:
    LeaveCriticalSection(&This->parent->crst);
    return hr;
}

static HRESULT WINAPI DSCBuffer_Start(IDirectSoundCaptureBuffer8 *iface, DWORD flags)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    TRACE("(%p)->(%08x)\n", This, flags);

    EnterCriticalSection(&This->parent->crst);
    if (!This->playing)
    {
        DSCBuffer_starttimer(This->parent);
        This->playing = 1;
        alcCaptureStart(This->dev);
    }
    This->looping = !!(flags & DSCBSTART_LOOPING);
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Stop(IDirectSoundCaptureBuffer8 *iface)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    TRACE("(%p)\n", This);

    EnterCriticalSection(&This->parent->crst);
    if (This->playing)
    {
        DWORD i;

        for (i = 0; i < This->nnotify; ++i)
            if (This->notify[i].dwOffset == DSCBPN_OFFSET_STOP)
            {
                SetEvent(This->notify[i].hEventNotify);
                break;
            }
        This->playing = This->looping = 0;
        alcCaptureStop(This->dev);
    }
    LeaveCriticalSection(&This->parent->crst);
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_Unlock(IDirectSoundCaptureBuffer8 *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    TRACE("(%p)->(%p,%u,%p,%u)\n", This, ptr1, len2, ptr2, len2);

    if (!ptr1)
        return DSERR_INVALIDPARAM;
    return S_OK;
}

static HRESULT WINAPI DSCBuffer_GetObjectInPath(IDirectSoundCaptureBuffer8 *iface, REFGUID guid, DWORD num, REFGUID riid, void **ppv)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    FIXME("(%p)->(%s %u %s %p) stub\n", This, debugstr_guid(guid), num, debugstr_guid(riid), ppv);
    return E_NOTIMPL;
}

static HRESULT WINAPI DSCBuffer_GetFXStatus(IDirectSoundCaptureBuffer8 *iface, DWORD count, DWORD *status)
{
    DSCBuffer *This = (DSCBuffer*)iface;
    FIXME("(%p)->(%u %p) stub\n", This, count, status);
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

static DSCBuffer *get_this_from_not(IDirectSoundNotify *iface)
{
    return (DSCBuffer*)((char*)iface - offsetof(DSCBuffer,lpNotVtbl));
}

static HRESULT WINAPI DSCBufferNot_QueryInterface(IDirectSoundNotify *iface, REFIID riid, void **ppv)
{
    DSCBuffer *This = get_this_from_not(iface);
    return IDirectSoundCaptureBuffer_QueryInterface((IDirectSoundCaptureBuffer*)This, riid, ppv);
}

static ULONG WINAPI DSCBufferNot_AddRef(IDirectSoundNotify *iface)
{
    DSCBuffer *This = get_this_from_not(iface);
    LONG ret;

    ret = InterlockedIncrement(&This->not_ref);
    TRACE("new refcount %d\n", ret);
    return ret;
}

static ULONG WINAPI DSCBufferNot_Release(IDirectSoundNotify *iface)
{
    DSCBuffer *This = get_this_from_not(iface);
    CRITICAL_SECTION *crst = &This->parent->crst;
    LONG ret;

    EnterCriticalSection(crst);
    ret = InterlockedDecrement(&This->not_ref);
    TRACE("new refcount %d\n", ret);
    if (!ret && !This->ref)
        DSCBuffer_Destroy(This);
    LeaveCriticalSection(crst);
    return ret;
}

static HRESULT WINAPI DSCBufferNot_SetNotificationPositions(IDirectSoundNotify *iface, DWORD count, const DSBPOSITIONNOTIFY *notifications)
{
    DSCBuffer *This = get_this_from_not(iface);
    DSBPOSITIONNOTIFY *nots;
    HRESULT hr;
    DWORD state;

    EnterCriticalSection(&This->parent->crst);
    hr = DSERR_INVALIDPARAM;
    if (count && !notifications)
        goto out;

    hr = DSCBuffer_GetStatus((IDirectSoundCaptureBuffer8*)This, &state);
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

HRESULT DSOUND_CaptureCreate8(REFIID riid, IDirectSoundCapture8 **cap)
{
    DSCImpl *This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    *cap = NULL;
    if (!This)
        return DSERR_OUTOFMEMORY;
    This->lpVtbl = &DSC_Vtbl;

    InitializeCriticalSection(&This->crst);
    This->crst.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": DSCImpl.crst");
    if (FAILED(IUnknown_QueryInterface((IUnknown*)This, riid, (void**)cap)))
    {
        DSCImpl_Destroy(This);
        return E_NOINTERFACE;
    }
    return S_OK;
}

static void DSCImpl_Destroy(DSCImpl *This)
{
    if (This->timer_id)
    {
        timeKillEvent(This->timer_id);
        timeEndPeriod(This->timer_res);
    }
    EnterCriticalSection(&This->crst);
    if (This->buf)
        DSCBuffer_Destroy(This->buf);
    LeaveCriticalSection(&This->crst);
    HeapFree(GetProcessHeap(), 0, This->device);
    This->crst.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&This->crst);
    HeapFree(GetProcessHeap(), 0, This);
}

static HRESULT WINAPI DSCImpl_QueryInterface(IDirectSoundCapture *iface, REFIID riid, void **ppv)
{
    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown)
        || IsEqualIID(riid, &IID_IDirectSoundCapture))
    {
        *ppv = iface;
    }
    if (!*ppv)
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI DSCImpl_AddRef(IDirectSoundCapture8 *iface)
{
    DSCImpl *This = (DSCImpl*)iface;
    LONG ref;
    ref = InterlockedIncrement(&This->ref);
    TRACE("Reference count incremented to %i\n", ref);
    return ref;
}

static ULONG WINAPI DSCImpl_Release(IDirectSoundCapture8 *iface)
{
    DSCImpl *This = (DSCImpl*)iface;
    LONG ref;
    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %i\n", ref);
    if (!ref)
        DSCImpl_Destroy(This);
    return ref;
}

static HRESULT WINAPI DSCImpl_CreateCaptureBuffer(IDirectSoundCapture8 *iface, const DSCBUFFERDESC *desc, IDirectSoundCaptureBuffer **ppv, IUnknown *unk)
{
    DSCImpl *This = (DSCImpl*)iface;
    HRESULT hr;
    TRACE("(%p)->(%p,%p,%p)\n", This, desc, ppv, unk);

    if (unk)
    {
        WARN("Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }

    if (!desc || desc->dwSize < sizeof(DSCBUFFERDESC1))
    {
        WARN("Passed invalid description %p %u\n", desc, desc?desc->dwSize:0);
        return DSERR_INVALIDPARAM;
    }
    if (!ppv)
    {
        WARN("Passed null pointer\n");
        return DSERR_INVALIDPARAM;
    }
    *ppv = NULL;

    EnterCriticalSection(&This->crst);
    if (!This->device)
    {
        hr = DSERR_UNINITIALIZED;
        WARN("Not initialized\n");
        goto out;
    }
    if (This->buf)
    {
        hr = DSERR_ALLOCATED;
        WARN("Capture buffer already allocated\n");
        goto out;
    }

    hr = DSCBuffer_Create(&This->buf);
    if (SUCCEEDED(hr))
    {
        hr = IDirectSoundCaptureBuffer_Initialize((IDirectSoundCaptureBuffer*)This->buf, iface, desc);
        if (FAILED(hr))
        {
            DSCBuffer_Destroy(This->buf);
            This->buf = NULL;
        }
    }
    *ppv = (IDirectSoundCaptureBuffer*)This->buf;
out:
    LeaveCriticalSection(&This->crst);
    return hr;
}

static HRESULT WINAPI DSCImpl_GetCaps(IDirectSoundCapture8 *iface, DSCCAPS *caps)
{
    DSCImpl *This = (DSCImpl*)iface;
    TRACE("(%p,%p)\n", This, caps);

    if (!This->device) {
        WARN("Not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if (!caps) {
        WARN("Caps is null\n");
        return DSERR_INVALIDPARAM;
    }

    if (caps->dwSize < sizeof(*caps)) {
        WARN("Invalid size %d\n", caps->dwSize);
        return DSERR_INVALIDPARAM;
    }

    caps->dwFlags = 0;
    /* Support all WAVE_FORMAT formats specified in mmsystem.h */
    caps->dwFormats = 0x000fffff;
    caps->dwChannels = 2;

    return DS_OK;
}

static HRESULT WINAPI DSCImpl_Initialize(IDirectSoundCapture8 *iface, const GUID *devguid)
{
    DSCImpl *This = (DSCImpl*)iface;
    HRESULT hr;
    const ALCchar *devs, *drv_name;
    GUID guid;
    UINT n;
    TRACE("(%p,%p)\n", This, devguid);

    if (!openal_loaded)
    {
        ERR("OpenAL not loaded!\n");
        return DSERR_NODRIVER;
    }

    if (This->device) {
        WARN("Already initialized\n");
        return DSERR_ALREADYINITIALIZED;
    }

    if (!devguid)
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
    if (memcmp(devguid, &DSOUND_capture_guid, sizeof(GUID)-1)
        || !devs || !*devs)
    {
        WARN("No driver found\n");
        goto out;
    }

    if (n)
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
    if (!This->device)
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
