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

#include "windows.h"
#include "dsound.h"
#include "mmsystem.h"
#include "ks.h"
#include <devpropdef.h>

#include "dsound_private.h"


#ifndef DS_INCOMPLETE
#define DS_INCOMPLETE                   ((HRESULT)0x08780020)
#endif

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 3
#endif


/* TODO: when bufferlost is set, return from all calls except initialize with
 * DSERR_BUFFERLOST
 */
static IDirectSoundBuffer8Vtbl DSBuffer_Vtbl;
static IDirectSound3DBufferVtbl DSBuffer3d_Vtbl;
static IDirectSoundNotifyVtbl DSBufferNot_Vtbl;
static IKsPropertySetVtbl DSBufferProp_Vtbl;


static inline DSBuffer *impl_from_IDirectSoundBuffer8(IDirectSoundBuffer8 *iface)
{
    return CONTAINING_RECORD(iface, DSBuffer, IDirectSoundBuffer8_iface);
}

static inline DSBuffer *impl_from_IDirectSoundBuffer(IDirectSoundBuffer *iface)
{
    return CONTAINING_RECORD(iface, DSBuffer, IDirectSoundBuffer8_iface);
}

static inline DSBuffer *impl_from_IDirectSound3DBuffer(IDirectSound3DBuffer *iface)
{
    return CONTAINING_RECORD(iface, DSBuffer, IDirectSound3DBuffer_iface);
}

static inline DSBuffer *impl_from_IDirectSoundNotify(IDirectSoundNotify *iface)
{
    return CONTAINING_RECORD(iface, DSBuffer, IDirectSoundNotify_iface);
}

static inline DSBuffer *impl_from_IKsPropertySet(IKsPropertySet *iface)
{
    return CONTAINING_RECORD(iface, DSBuffer, IKsPropertySet_iface);
}


/* Should be called with critsect held and context set.. */
static void DSBuffer_addnotify(DSBuffer *buf)
{
    DSPrimary *prim = buf->primary;
    DSBuffer **list;
    DWORD i;

    list = prim->notifies;
    for(i = 0; i < prim->nnotifies; ++i)
    {
        if(buf == list[i])
        {
            ERR("Buffer %p already in notification list\n", buf);
            return;
        }
    }
    if(prim->nnotifies == prim->sizenotifies)
    {
        list = HeapReAlloc(GetProcessHeap(), 0, list, (prim->nnotifies + 1) * sizeof(*list));
        if(!list) return;
        prim->sizenotifies++;
    }
    list[prim->nnotifies++] = buf;
    prim->notifies = list;
}


static const char *get_fmtstr_PCM(const DSPrimary *prim, const WAVEFORMATEX *format, WAVEFORMATEXTENSIBLE *out)
{
    out->Format = *format;
    out->Format.cbSize = 0;

    if(out->Format.nChannels != 1 && out->Format.nChannels != 2 &&
       !HAS_EXTENSION(prim->share, EXT_MCFORMATS))
    {
        WARN("Multi-channel not available\n");
        return NULL;
    }

    if(format->wBitsPerSample == 8)
    {
        switch(format->nChannels)
        {
        case 1: return "AL_FORMAT_MONO8";
        case 2: return "AL_FORMAT_STEREO8";
        case 4: return "AL_FORMAT_QUAD8";
        case 6: return "AL_FORMAT_51CHN8";
        case 7: return "AL_FORMAT_61CHN8";
        case 8: return "AL_FORMAT_71CHN8";
        }
    }
    else if(format->wBitsPerSample == 16)
    {
        switch(format->nChannels)
        {
        case 1: return "AL_FORMAT_MONO16";
        case 2: return "AL_FORMAT_STEREO16";
        case 4: return "AL_FORMAT_QUAD16";
        case 6: return "AL_FORMAT_51CHN16";
        case 7: return "AL_FORMAT_61CHN16";
        case 8: return "AL_FORMAT_71CHN16";
        }
    }

    FIXME("Could not get OpenAL format (%d-bit, %d channels)\n",
          format->wBitsPerSample, format->nChannels);
    return NULL;
}

static const char *get_fmtstr_FLOAT(const DSPrimary *prim, const WAVEFORMATEX *format, WAVEFORMATEXTENSIBLE *out)
{
    out->Format = *format;
    out->Format.cbSize = 0;

    if(out->Format.nChannels != 1 && out->Format.nChannels != 2 &&
       !HAS_EXTENSION(prim->share, EXT_MCFORMATS))
    {
        WARN("Multi-channel not available\n");
        return NULL;
    }

    if(format->wBitsPerSample == 32 && HAS_EXTENSION(prim->share, EXT_FLOAT32))
    {
        switch(format->nChannels)
        {
        case 1: return "AL_FORMAT_MONO_FLOAT32";
        case 2: return "AL_FORMAT_STEREO_FLOAT32";
        case 4: return "AL_FORMAT_QUAD32";
        case 6: return "AL_FORMAT_51CHN32";
        case 7: return "AL_FORMAT_61CHN32";
        case 8: return "AL_FORMAT_71CHN32";
        }
    }

    FIXME("Could not get OpenAL format (%d-bit, %d channels)\n",
          format->wBitsPerSample, format->nChannels);
    return NULL;
}

/* Speaker configs */
#define MONO SPEAKER_FRONT_CENTER
#define STEREO (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT)
#define REAR (SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define QUAD (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X5DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT)
#define X6DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_CENTER|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)
#define X7DOT1 (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT|SPEAKER_FRONT_CENTER|SPEAKER_LOW_FREQUENCY|SPEAKER_BACK_LEFT|SPEAKER_BACK_RIGHT|SPEAKER_SIDE_LEFT|SPEAKER_SIDE_RIGHT)

static const char *get_fmtstr_EXT(const DSPrimary *prim, const WAVEFORMATEX *format, WAVEFORMATEXTENSIBLE *out)
{
    *out = *CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format);
    out->Format.cbSize = sizeof(*out) - sizeof(out->Format);

    if(!out->Samples.wValidBitsPerSample)
        out->Samples.wValidBitsPerSample = out->Format.wBitsPerSample;
    else if(out->Samples.wValidBitsPerSample > out->Format.wBitsPerSample)
    {
        WARN("Invalid ValidBitsPerSample (%u > %u)\n", out->Samples.wValidBitsPerSample, out->Format.wBitsPerSample);
        return NULL;
    }
    else if(out->Samples.wValidBitsPerSample < out->Format.wBitsPerSample)
    {
        FIXME("Padded samples not supported (%u of %u)\n", out->Samples.wValidBitsPerSample, out->Format.wBitsPerSample);
        return NULL;
    }

    if(out->dwChannelMask != MONO && out->dwChannelMask != STEREO &&
       !HAS_EXTENSION(prim->share, EXT_MCFORMATS))
    {
        WARN("Multi-channel not available\n");
        return NULL;
    }

    if(IsEqualGUID(&out->SubFormat, &KSDATAFORMAT_SUBTYPE_PCM))
    {
        if(out->Samples.wValidBitsPerSample == 8)
        {
            switch(out->dwChannelMask)
            {
            case   MONO: return "AL_FORMAT_MONO8";
            case STEREO: return "AL_FORMAT_STEREO8";
            case   REAR: return "AL_FORMAT_REAR8";
            case   QUAD: return "AL_FORMAT_QUAD8";
            case X5DOT1: return "AL_FORMAT_51CHN8";
            case X6DOT1: return "AL_FORMAT_61CHN8";
            case X7DOT1: return "AL_FORMAT_71CHN8";
            }
        }
        else if(out->Samples.wValidBitsPerSample == 16)
        {
            switch(out->dwChannelMask)
            {
            case   MONO: return "AL_FORMAT_MONO16";
            case STEREO: return "AL_FORMAT_STEREO16";
            case   REAR: return "AL_FORMAT_REAR16";
            case   QUAD: return "AL_FORMAT_QUAD16";
            case X5DOT1: return "AL_FORMAT_51CHN16";
            case X6DOT1: return "AL_FORMAT_61CHN16";
            case X7DOT1: return "AL_FORMAT_71CHN16";
            }
        }

        FIXME("Could not get OpenAL PCM format (%d-bit, channelmask %#lx)\n",
              out->Samples.wValidBitsPerSample, out->dwChannelMask);
        return NULL;
    }
    else if(IsEqualGUID(&out->SubFormat, &KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
            HAS_EXTENSION(prim->share, EXT_FLOAT32))
    {
        if(out->Samples.wValidBitsPerSample == 32)
        {
            switch(out->dwChannelMask)
            {
            case   MONO: return "AL_FORMAT_MONO_FLOAT32";
            case STEREO: return "AL_FORMAT_STEREO_FLOAT32";
            case   REAR: return "AL_FORMAT_REAR32";
            case   QUAD: return "AL_FORMAT_QUAD32";
            case X5DOT1: return "AL_FORMAT_51CHN32";
            case X6DOT1: return "AL_FORMAT_61CHN32";
            case X7DOT1: return "AL_FORMAT_71CHN32";
            }
        }
        else
        {
            WARN("Invalid float bits: %u\n", out->Samples.wValidBitsPerSample);
            return NULL;
        }

        FIXME("Could not get OpenAL float format (%d-bit, channelmask %#lx)\n",
              out->Samples.wValidBitsPerSample, out->dwChannelMask);
        return NULL;
    }
    else if(!IsEqualGUID(&out->SubFormat, &GUID_NULL))
        ERR("Unhandled extensible format: %s\n", debugstr_guid(&out->SubFormat));
    return NULL;
}

static void DSData_Release(DSData *This);
static HRESULT DSData_Create(DSData **ppv, const DSBUFFERDESC *desc, DSPrimary *prim)
{
    HRESULT hr = DSERR_INVALIDPARAM;
    const WAVEFORMATEX *format;
    const char *fmt_str = NULL;
    DSData *pBuffer;
    DWORD buf_size;

    format = desc->lpwfxFormat;
    TRACE("Requested buffer format:\n"
          "    FormatTag      = 0x%04x\n"
          "    Channels       = %d\n"
          "    SamplesPerSec  = %lu\n"
          "    AvgBytesPerSec = %lu\n"
          "    BlockAlign     = %d\n"
          "    BitsPerSample  = %d\n",
          format->wFormatTag, format->nChannels,
          format->nSamplesPerSec, format->nAvgBytesPerSec,
          format->nBlockAlign, format->wBitsPerSample);

    if(format->nChannels <= 0)
    {
        WARN("Invalid Channels %d\n", format->nChannels);
        return DSERR_INVALIDPARAM;
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN("Invalid SamplesPerSec %lu\n", format->nSamplesPerSec);
        return DSERR_INVALIDPARAM;
    }
    if(format->nBlockAlign <= 0)
    {
        WARN("Invalid BlockAlign %d\n", format->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }
    if(format->wBitsPerSample == 0 || (format->wBitsPerSample%8) != 0)
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
    /* HACK: Some games provide an incorrect value here and expect to work.
     * This is clearly not supposed to succeed with just anything, but until
     * the amount of leeway allowed is discovered, be very lenient.
     */
    if(format->nAvgBytesPerSec == 0)
    {
        WARN("Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
             format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
             format->nSamplesPerSec, format->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }
    if(format->nAvgBytesPerSec != format->nBlockAlign*format->nSamplesPerSec)
        WARN("Unexpected AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
             format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
             format->nSamplesPerSec, format->nBlockAlign);

    if((desc->dwFlags&(DSBCAPS_LOCSOFTWARE|DSBCAPS_LOCHARDWARE)) == (DSBCAPS_LOCSOFTWARE|DSBCAPS_LOCHARDWARE))
    {
        WARN("Hardware and software location requested\n");
        return DSERR_INVALIDPARAM;
    }

    buf_size  = desc->dwBufferBytes + format->nBlockAlign - 1;
    buf_size -= buf_size%format->nBlockAlign;
    if(buf_size < DSBSIZE_MIN) return DSERR_BUFFERTOOSMALL;
    if(buf_size > DSBSIZE_MAX) return DSERR_INVALIDPARAM;

    /* Generate a new buffer. Supporting the DSBCAPS_LOC* flags properly
     * will need the EAX-RAM extension. Currently, we just tell the app it
     * gets what it wanted. */
    if(!HAS_EXTENSION(prim->share, SOFTX_MAP_BUFFER))
        pBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pBuffer)+buf_size);
    else
        pBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pBuffer));
    if(!pBuffer) return E_OUTOFMEMORY;
    pBuffer->ref = 1;
    pBuffer->primary = prim;

    pBuffer->dsbflags = desc->dwFlags;
    pBuffer->buf_size = buf_size;

    if(format->wFormatTag == WAVE_FORMAT_PCM)
        fmt_str = get_fmtstr_PCM(prim, format, &pBuffer->format);
    else if(format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        fmt_str = get_fmtstr_FLOAT(prim, format, &pBuffer->format);
    else if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE *wfe;

        if(format->cbSize != sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX) &&
            format->cbSize != sizeof(WAVEFORMATEXTENSIBLE))
            goto fail;

        wfe = CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format);
        TRACE("Extensible values:\n"
                "    Samples     = %d\n"
                "    ChannelMask = 0x%lx\n"
                "    SubFormat   = %s\n",
                wfe->Samples.wReserved, wfe->dwChannelMask,
                debugstr_guid(&wfe->SubFormat));

        fmt_str = get_fmtstr_EXT(prim, format, &pBuffer->format);
    }
    else
        ERR("Unhandled formattag 0x%04x\n", format->wFormatTag);
    if(!fmt_str) goto fail;

    alGetError();
    pBuffer->buf_format = alGetEnumValue(fmt_str);
    if(alGetError() != AL_NO_ERROR || pBuffer->buf_format == 0 ||
       pBuffer->buf_format == -1)
    {
        WARN("Could not get OpenAL format from %s\n", fmt_str);
        goto fail;
    }

    hr = E_OUTOFMEMORY;
    if(!HAS_EXTENSION(prim->share, SOFTX_MAP_BUFFER))
    {
        pBuffer->data = (BYTE*)(pBuffer+1);

        alGenBuffers(1, &pBuffer->bid);
        checkALError();
    }
    else
    {
        const ALbitfieldSOFT map_bits = AL_MAP_READ_BIT_SOFT | AL_MAP_WRITE_BIT_SOFT |
                                        AL_MAP_PERSISTENT_BIT_SOFT;
        alGenBuffers(1, &pBuffer->bid);
        alBufferStorageSOFT(pBuffer->bid, pBuffer->buf_format, NULL, pBuffer->buf_size,
                            pBuffer->format.Format.nSamplesPerSec, map_bits);
        pBuffer->data = alMapBufferSOFT(pBuffer->bid, 0, pBuffer->buf_size, map_bits);
        checkALError();

        if(!pBuffer->data) goto fail;
    }

    *ppv = pBuffer;
    return S_OK;

fail:
    DSData_Release(pBuffer);
    return hr;
}

static void DSData_AddRef(DSData *data)
{
    InterlockedIncrement(&data->ref);
}

/* This function is always called with the device lock held */
static void DSData_Release(DSData *This)
{
    if(InterlockedDecrement(&This->ref)) return;

    TRACE("Deleting %p\n", This);
    if(This->bid)
    {
        DSPrimary *prim = This->primary;
        if(HAS_EXTENSION(prim->share, SOFTX_MAP_BUFFER))
            alUnmapBufferSOFT(This->bid);
        alDeleteBuffers(1, &This->bid);
        checkALError();
    }
    HeapFree(GetProcessHeap(), 0, This);
}


HRESULT DSBuffer_Create(DSBuffer **ppv, DSPrimary *prim, IDirectSoundBuffer *orig)
{
    DSBuffer *This = NULL;
    DWORD i;

    *ppv = NULL;
    EnterCriticalSection(&prim->share->crst);
    for(i = 0;i < prim->NumBufferGroups;++i)
    {
        if(prim->BufferGroups[i].FreeBuffers)
        {
            int idx = CTZ64(prim->BufferGroups[i].FreeBuffers);
            This = prim->BufferGroups[i].Buffers + idx;
            memset(This, 0, sizeof(*This));
            prim->BufferGroups[i].FreeBuffers &= ~(U64(1) << idx);
            break;
        }
    }
    if(!This)
    {
        struct DSBufferGroup *grp = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            (prim->NumBufferGroups+1)*sizeof(prim->BufferGroups[0]));
        if(grp)
        {
            for(i = 0;i < prim->NumBufferGroups;i++)
                grp[i] = prim->BufferGroups[i];
            grp[i].FreeBuffers = ~U64(0);
            grp[i].Buffers = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       64*sizeof(grp[0].Buffers[0]));
            if(!grp[i].Buffers)
            {
                HeapFree(GetProcessHeap(), 0, grp);
                grp = NULL;
            }
            else
            {
                HeapFree(GetProcessHeap(), 0, prim->BufferGroups);
                prim->BufferGroups = grp;
                prim->NumBufferGroups++;

                This = prim->BufferGroups[i].Buffers + 0;
                memset(This, 0, sizeof(*This));
                prim->BufferGroups[i].FreeBuffers &= ~(U64(1) << 0);
            }
        }
    }
    LeaveCriticalSection(&prim->share->crst);
    if(!This)
    {
        WARN("Out of memory allocating buffers\n");
        return DSERR_OUTOFMEMORY;
    }

    This->IDirectSoundBuffer8_iface.lpVtbl = &DSBuffer_Vtbl;
    This->IDirectSound3DBuffer_iface.lpVtbl = &DSBuffer3d_Vtbl;
    This->IDirectSoundNotify_iface.lpVtbl = &DSBufferNot_Vtbl;
    This->IKsPropertySet_iface.lpVtbl = &DSBufferProp_Vtbl;

    This->share = prim->share;
    This->primary = prim;
    This->ctx = prim->ctx;

    This->current.vol = 0;
    This->current.pan = 0;
    This->current.frequency = 0;
    This->current.ds3d.dwSize = sizeof(This->current.ds3d);
    This->current.ds3d.vPosition.x = 0.0f;
    This->current.ds3d.vPosition.y = 0.0f;
    This->current.ds3d.vPosition.z = 0.0f;
    This->current.ds3d.vVelocity.x = 0.0f;
    This->current.ds3d.vVelocity.y = 0.0f;
    This->current.ds3d.vVelocity.z = 0.0f;
    This->current.ds3d.dwInsideConeAngle = DS3D_DEFAULTCONEANGLE;
    This->current.ds3d.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
    This->current.ds3d.vConeOrientation.x = 0.0f;
    This->current.ds3d.vConeOrientation.y = 0.0f;
    This->current.ds3d.vConeOrientation.z = 1.0f;
    This->current.ds3d.lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;
    This->current.ds3d.flMinDistance = DS3D_DEFAULTMINDISTANCE;
    This->current.ds3d.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
    This->current.ds3d.dwMode = DS3DMODE_NORMAL;

    if(orig)
    {
        DSBuffer *org = impl_from_IDirectSoundBuffer(orig);
        DSData *data = org->buffer;

        if(org->bufferlost)
        {
            DSBuffer_Destroy(This);
            return DSERR_BUFFERLOST;
        }
        DSData_AddRef(data);
        This->buffer = data;

        /* According to MSDN, volume isn't copied. */
        if((data->dsbflags&DSBCAPS_CTRLPAN))
            This->current.pan = org->current.pan;
        if((data->dsbflags&DSBCAPS_CTRLFREQUENCY))
            This->current.frequency = org->current.frequency;
        if((data->dsbflags&DSBCAPS_CTRL3D))
            This->current.ds3d = org->current.ds3d;
    }

    This->deferred.ds3d = This->current.ds3d;

    This->vm_voicepriority = (DWORD)-1;
    
    *ppv = This;
    return DS_OK;
}

void DSBuffer_Destroy(DSBuffer *This)
{
    DSPrimary *prim = This->primary;
    DWORD i;

    if(!prim) return;
    TRACE("Destroying %p\n", This);

    EnterCriticalSection(&prim->share->crst);
    /* Remove from list, if in list */
    for(i = 0;i < prim->nnotifies;++i)
    {
        if(This == prim->notifies[i])
        {
            prim->notifies[i] = prim->notifies[--prim->nnotifies];
            break;
        }
    }

    setALContext(This->ctx);
    if(This->source)
    {
        DeviceShare *share = This->share;

        alDeleteSources(1, &This->source);
        This->source = 0;
        checkALError();

        if(This->loc_status == DSBSTATUS_LOCHARDWARE)
            share->sources.availhw_num += 1;
        else
            share->sources.availsw_num += 1;
    }
    if(This->stream_bids[0])
        alDeleteBuffers(QBUFFERS, This->stream_bids);

    if(This->buffer)
        DSData_Release(This->buffer);

    popALContext();

    HeapFree(GetProcessHeap(), 0, This->notify);

    for(i = 0;i < prim->NumBufferGroups;++i)
    {
        DWORD_PTR idx = This - prim->BufferGroups[i].Buffers;
        if(idx < 64)
        {
            prim->BufferGroups[i].FreeBuffers |= U64(1) << idx;
            This = NULL;
            break;
        }
    }
    LeaveCriticalSection(&prim->share->crst);
}

HRESULT DSBuffer_GetInterface(DSBuffer *buf, REFIID riid, void **ppv)
{
    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IDirectSoundBuffer))
        *ppv = &buf->IDirectSoundBuffer8_iface;
    else if(IsEqualIID(riid, &IID_IDirectSoundBuffer8))
    {
        if(buf->primary->parent->is_8)
            *ppv = &buf->IDirectSoundBuffer8_iface;
    }
    else if(IsEqualIID(riid, &IID_IDirectSound3DBuffer))
    {
        if((buf->buffer->dsbflags&DSBCAPS_CTRL3D))
            *ppv = &buf->IDirectSound3DBuffer_iface;
    }
    else if(IsEqualIID(riid, &IID_IDirectSoundNotify))
    {
        if((buf->buffer->dsbflags&DSBCAPS_CTRLPOSITIONNOTIFY))
            *ppv = &buf->IDirectSoundNotify_iface;
    }
    else if(IsEqualIID(riid, &IID_IKsPropertySet))
        *ppv = &buf->IKsPropertySet_iface;
    else if(IsEqualIID(riid, &IID_IUnknown))
        *ppv = &buf->IDirectSoundBuffer8_iface;
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static HRESULT DSBuffer_SetLoc(DSBuffer *buf, DWORD loc_status)
{
    DeviceShare *share = buf->share;
    DSData *data = buf->buffer;

    if((loc_status && buf->loc_status == loc_status) || (!loc_status && buf->loc_status))
        return DS_OK;

    /* If we have a source, we're changing location, so return the source we
     * have to get a new one.
     */
    if(buf->source)
    {
        alDeleteSources(1, &buf->source);
        buf->source = 0;
        checkALError();

        if(buf->loc_status == DSBSTATUS_LOCHARDWARE)
            share->sources.availhw_num += 1;
        else
            share->sources.availsw_num += 1;
    }
    buf->loc_status = 0;

    if(!loc_status)
    {
        if(share->sources.availhw_num)
            loc_status = DSBSTATUS_LOCHARDWARE;
        else if(share->sources.availsw_num)
            loc_status = DSBSTATUS_LOCSOFTWARE;
    }

    if((loc_status == DSBSTATUS_LOCHARDWARE && !share->sources.availhw_num) ||
       (loc_status == DSBSTATUS_LOCSOFTWARE && !share->sources.availsw_num) ||
       !loc_status)
    {
        ERR("Out of %s sources\n",
            (loc_status == DSBSTATUS_LOCHARDWARE) ? "hardware" :
            (loc_status == DSBSTATUS_LOCSOFTWARE) ? "software" : "any"
        );
        return DSERR_ALLOCATED;
    }

    if(loc_status == DSBSTATUS_LOCHARDWARE)
        share->sources.availhw_num -= 1;
    else
        share->sources.availsw_num -= 1;
    alGenSources(1, &buf->source);
    alSourcef(buf->source, AL_GAIN, mB_to_gain((float)buf->current.vol));
    alSourcef(buf->source, AL_PITCH,
        buf->current.frequency ? (float)buf->current.frequency/data->format.Format.nSamplesPerSec
                               : 1.0f);
    checkALError();

    /* TODO: Don't set EAX parameters or connect to effect slots for software
     * buffers. Need to check if EAX buffer properties are still tracked, or if
     * they're lost/reset when leaving hardware.
     *
     * Alternatively, we can just allow it and say software processing supports
     * EAX too. Depends if apps may get upset over that.
     */

    if((data->dsbflags&DSBCAPS_CTRL3D))
    {
        const DSPrimary *prim = buf->primary;
        const ALuint source = buf->source;
        const DS3DBUFFER *params = &buf->current.ds3d;

        alSource3f(source, AL_POSITION, params->vPosition.x, params->vPosition.y,
                                       -params->vPosition.z);
        alSource3f(source, AL_VELOCITY, params->vVelocity.x, params->vVelocity.y,
                                       -params->vVelocity.z);
        alSourcei(source, AL_CONE_INNER_ANGLE, params->dwInsideConeAngle);
        alSourcei(source, AL_CONE_OUTER_ANGLE, params->dwOutsideConeAngle);
        alSource3f(source, AL_DIRECTION, params->vConeOrientation.x,
                                         params->vConeOrientation.y,
                                        -params->vConeOrientation.z);
        alSourcef(source, AL_CONE_OUTER_GAIN, mB_to_gain((float)params->lConeOutsideVolume));
        alSourcef(source, AL_REFERENCE_DISTANCE, params->flMinDistance);
        alSourcef(source, AL_MAX_DISTANCE, params->flMaxDistance);
        if(HAS_EXTENSION(share, SOFT_SOURCE_SPATIALIZE))
            alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT,
                (params->dwMode==DS3DMODE_DISABLE) ? AL_FALSE : AL_TRUE
            );
        alSourcei(source, AL_SOURCE_RELATIVE,
            (params->dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE
        );

        alSourcef(source, AL_ROLLOFF_FACTOR, prim->current.ds3d.flRolloffFactor);
        checkALError();
    }
    else
    {
        const ALuint source = buf->source;
        const ALfloat x = (ALfloat)(buf->current.pan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) -
                          0.5f;

        alSource3f(source, AL_POSITION, x, 0.0f, -sqrtf(1.0f - x*x));
        alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSource3f(source, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
        alSourcef(source, AL_CONE_OUTER_GAIN, 1.0f);
        alSourcef(source, AL_REFERENCE_DISTANCE, 1.0f);
        alSourcef(source, AL_MAX_DISTANCE, 1000.0f);
        alSourcef(source, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcef(source, AL_DOPPLER_FACTOR, 0.0f);
        alSourcei(source, AL_CONE_INNER_ANGLE, 360);
        alSourcei(source, AL_CONE_OUTER_ANGLE, 360);
        alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
        if(HAS_EXTENSION(share, SOFT_SOURCE_SPATIALIZE))
        {
            /* Set to auto so panning works for mono, and multi-channel works
             * as expected.
             */
            alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT, AL_AUTO_SOFT);
        }
        if(HAS_EXTENSION(share, EXT_EAX))
        {
            static const GUID NullSlots[EAX_MAX_ACTIVE_FXSLOTS] = { { 0 } };
            EAXSet(&EAXPROPERTYID_EAX40_Source, EAXSOURCE_ACTIVEFXSLOTID, source, (void*)NullSlots,
                sizeof(NullSlots));
        }
        checkALError();
    }

    buf->loc_status = loc_status;
    return DS_OK;
}


static HRESULT WINAPI DSBuffer_QueryInterface(IDirectSoundBuffer8 *iface, REFIID riid, void **ppv)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSBuffer_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DSBuffer_AddRef(IDirectSoundBuffer8 *iface)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSBuffer_Release(IDirectSoundBuffer8 *iface)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->ref);
    TRACE("(%p) ref %lu\n", iface, ret);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSBuffer_Destroy(This);

    return ret;
}

static HRESULT WINAPI DSBuffer_GetCaps(IDirectSoundBuffer8 *iface, DSBCAPS *caps)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DSData *data;

    TRACE("(%p)->(%p)\n", iface, caps);

    if(!caps || caps->dwSize < sizeof(*caps))
    {
        WARN("Invalid DSBCAPS (%p, %lu)\n", caps, (caps ? caps->dwSize : 0));
        return DSERR_INVALIDPARAM;
    }
    data = This->buffer;

    caps->dwFlags = data->dsbflags;
    if(!(data->dsbflags&DSBCAPS_LOCDEFER))
    {
        if(This->loc_status == DSBSTATUS_LOCHARDWARE)
            caps->dwFlags |= DSBCAPS_LOCHARDWARE;
        else if(This->loc_status == DSBSTATUS_LOCSOFTWARE)
            caps->dwFlags |= DSBCAPS_LOCSOFTWARE;
    }
    caps->dwBufferBytes = data->buf_size;
    caps->dwUnlockTransferRate = 4096;
    caps->dwPlayCpuOverhead = 0;

    return S_OK;
}

HRESULT WINAPI DSBuffer_GetCurrentPosition(IDirectSoundBuffer8 *iface, DWORD *playpos, DWORD *curpos)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    ALsizei writecursor, pos;
    DSData *data;

    TRACE("(%p)->(%p, %p)\n", iface, playpos, curpos);

    data = This->buffer;
    if(This->segsize != 0)
    {
        ALint queued = QBUFFERS;
        ALint status = AL_INITIAL;
        ALint ofs = 0;

        EnterCriticalSection(&This->share->crst);

        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alGetSourcei(This->source, AL_BUFFERS_QUEUED, &queued);
            alGetSourcei(This->source, AL_BYTE_OFFSET, &ofs);
            alGetSourcei(This->source, AL_SOURCE_STATE, &status);
            checkALError();
            popALContext();
        }

        if(status == AL_STOPPED)
            pos = This->segsize*queued + This->queue_base;
        else
            pos = ofs + This->queue_base;
        if(pos >= data->buf_size)
        {
            if(This->islooping)
                pos %= data->buf_size;
            else if(This->isplaying)
            {
                pos = data->buf_size;
                alSourceStop(This->source);
                alSourcei(This->source, AL_BUFFER, 0);
                This->curidx = 0;
                This->isplaying = FALSE;
            }
        }
        if(This->isplaying)
            writecursor = (This->segsize*QBUFFERS + pos) % data->buf_size;
        else
            writecursor = pos % data->buf_size;

        LeaveCriticalSection(&This->share->crst);
    }
    else
    {
        const WAVEFORMATEX *format = &data->format.Format;
        ALint status = AL_INITIAL;
        ALint ofs = 0;

        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alGetSourcei(This->source, AL_BYTE_OFFSET, &ofs);
            alGetSourcei(This->source, AL_SOURCE_STATE, &status);
            checkALError();
            popALContext();
        }

        if(status == AL_PLAYING)
        {
            pos = ofs;
            writecursor = format->nSamplesPerSec / This->primary->refresh;
            writecursor *= format->nBlockAlign;
        }
        else
        {
            /* AL_STOPPED means the source naturally reached its end, where
             * DirectSound's position should be at the end (OpenAL reports 0
             * for stopped sources). The Stop method correlates to pausing,
             * which would put the source into an AL_PAUSED state and correctly
             * hold its current position. AL_INITIAL means the buffer hasn't
             * been played since last changing location.
             */
            switch(status)
            {
                case AL_STOPPED: pos = data->buf_size; break;
                case AL_PAUSED: pos = ofs; break;
                case AL_INITIAL: pos = This->lastpos; break;
                default: pos = 0;
            }
            writecursor = 0;
        }
        writecursor = (writecursor + pos) % data->buf_size;
    }
    TRACE("%p Play pos = %u, write pos = %u\n", This, pos, writecursor);

    if(pos > data->buf_size)
    {
        ERR("playpos > buf_size\n");
        pos %= data->buf_size;
    }
    if(writecursor >= data->buf_size)
    {
        ERR("writepos >= buf_size\n");
        writecursor %= data->buf_size;
    }

    if(playpos) *playpos = pos;
    if(curpos)  *curpos = writecursor;

    return S_OK;
}

static HRESULT WINAPI DSBuffer_GetFormat(IDirectSoundBuffer8 *iface, WAVEFORMATEX *wfx, DWORD allocated, DWORD *written)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr = S_OK;
    UINT size;

    TRACE("(%p)->(%p, %lu, %p)\n", iface, wfx, allocated, written);

    if(!wfx && !written)
    {
        WARN("Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    size = sizeof(This->buffer->format.Format) + This->buffer->format.Format.cbSize;
    if(wfx)
    {
        if(allocated < size)
            hr = DSERR_INVALIDPARAM;
        else
            memcpy(wfx, &This->buffer->format.Format, size);
    }
    if(written)
        *written = size;

    return hr;
}

static HRESULT WINAPI DSBuffer_GetVolume(IDirectSoundBuffer8 *iface, LONG *vol)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, vol);

    if(!vol)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSERR_CONTROLUNAVAIL;
    if(!(This->buffer->dsbflags&DSBCAPS_CTRLVOLUME))
        WARN("Volume control not set\n");
    else
    {
        *vol = This->current.vol;
        hr = DS_OK;
    }

    return hr;
}

static HRESULT WINAPI DSBuffer_GetPan(IDirectSoundBuffer8 *iface, LONG *pan)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, pan);

    if(!pan)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSERR_CONTROLUNAVAIL;
    if(!(This->buffer->dsbflags&DSBCAPS_CTRLPAN))
        WARN("Panning control not set\n");
    else
    {
        *pan = This->current.pan;
        hr = DS_OK;
    }

    return hr;
}

static HRESULT WINAPI DSBuffer_GetFrequency(IDirectSoundBuffer8 *iface, DWORD *freq)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, freq);

    if(!freq)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSERR_CONTROLUNAVAIL;
    if(!(This->buffer->dsbflags&DSBCAPS_CTRLFREQUENCY))
        WARN("Frequency control not set\n");
    else
    {
        *freq = This->current.frequency;
        hr = DS_OK;
    }

    return hr;
}

HRESULT WINAPI DSBuffer_GetStatus(IDirectSoundBuffer8 *iface, DWORD *status)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    ALint state, looping;

    TRACE("(%p)->(%p)\n", iface, status);

    if(!status)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }
    *status = 0;

    if(This->segsize == 0)
    {
        state = AL_INITIAL;
        looping = AL_FALSE;
        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alGetSourcei(This->source, AL_SOURCE_STATE, &state);
            alGetSourcei(This->source, AL_LOOPING, &looping);
            checkALError();
            popALContext();
        }
    }
    else
    {
        EnterCriticalSection(&This->share->crst);
        state = This->isplaying ? AL_PLAYING : AL_PAUSED;
        looping = This->islooping;
        LeaveCriticalSection(&This->share->crst);
    }

    if((This->buffer->dsbflags&DSBCAPS_LOCDEFER))
        *status |= This->loc_status;
    if(state == AL_PLAYING)
        *status |= DSBSTATUS_PLAYING | (looping ? DSBSTATUS_LOOPING : 0);

    TRACE("%p status = 0x%08lx\n", This, *status);
    return S_OK;
}

HRESULT WINAPI DSBuffer_Initialize(IDirectSoundBuffer8 *iface, IDirectSound *ds, const DSBUFFERDESC *desc)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DSPrimary *prim;
    DSData *data;
    HRESULT hr;

    TRACE("(%p)->(%p, %p)\n", iface, ds, desc);

    EnterCriticalSection(&This->share->crst);
    setALContext(This->ctx);

    hr = DSERR_ALREADYINITIALIZED;
    if(This->init_done) goto out;

    prim = This->primary;
    if(!This->buffer)
    {
        hr = DSERR_INVALIDPARAM;
        if(!desc)
        {
            WARN("Missing DSound buffer description\n");
            goto out;
        }
        if(!desc->lpwfxFormat)
        {
            WARN("Missing buffer format (%p)\n", This);
            goto out;
        }
        if((desc->dwFlags&DSBCAPS_CTRL3D) && desc->lpwfxFormat->nChannels != 1)
        {
            if(prim->parent->is_8)
            {
                /* DirectSoundBuffer8 objects aren't allowed non-mono 3D
                 * buffers */
                WARN("Can't create multi-channel 3D buffers\n");
                goto out;
            }
            else
            {
                static int once = 0;
                if(!once++)
                    ERR("Multi-channel 3D sounds are not spatialized\n");
            }
        }
        if((desc->dwFlags&DSBCAPS_CTRLPAN) && desc->lpwfxFormat->nChannels != 1)
        {
            static int once = 0;
            if(!once++)
                ERR("Panning for multi-channel buffers is not supported\n");
        }

        hr = DSData_Create(&This->buffer, desc, prim);
        if(FAILED(hr)) goto out;

        data = This->buffer;
        if(data->format.Format.wBitsPerSample == 8)
            memset(data->data, 0x80, data->buf_size);
        else
            memset(data->data, 0x00, data->buf_size);
    }

    data = This->buffer;
    if(!(data->dsbflags&DSBCAPS_STATIC) && !HAS_EXTENSION(This->share, SOFTX_MAP_BUFFER))
    {
        This->segsize = (data->format.Format.nAvgBytesPerSec+prim->refresh-1) / prim->refresh;
        This->segsize = clampI(This->segsize, data->format.Format.nBlockAlign, 2048);
        This->segsize += data->format.Format.nBlockAlign - 1;
        This->segsize -= This->segsize%data->format.Format.nBlockAlign;

        alGenBuffers(QBUFFERS, This->stream_bids);
        checkALError();
    }
    if(!(data->dsbflags&DSBCAPS_CTRL3D))
    {
        /* Non-3D sources aren't distance attenuated. */
        This->current.ds3d.dwMode = DS3DMODE_DISABLE;
    }
    if(!This->current.frequency)
        This->current.frequency = data->format.Format.nSamplesPerSec;

    hr = DS_OK;
    if(!(data->dsbflags&DSBCAPS_LOCDEFER))
    {
        DWORD loc = 0;
        if((data->dsbflags&DSBCAPS_LOCHARDWARE)) loc = DSBSTATUS_LOCHARDWARE;
        else if((data->dsbflags&DSBCAPS_LOCSOFTWARE)) loc = DSBSTATUS_LOCSOFTWARE;
        hr = DSBuffer_SetLoc(This, loc);
    }
out:
    This->init_done = SUCCEEDED(hr);

    popALContext();
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSBuffer_Lock(IDirectSoundBuffer8 *iface, DWORD ofs, DWORD bytes, void **ptr1, DWORD *len1, void **ptr2, DWORD *len2, DWORD flags)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DWORD remain;

    TRACE("(%p)->(%lu, %lu, %p, %p, %p, %p, 0x%lx)\n", This, ofs, bytes, ptr1, len1, ptr2, len2, flags);

    if(!ptr1 || !len1)
    {
        WARN("Invalid pointer/len %p %p\n", ptr1, len1);
        return DSERR_INVALIDPARAM;
    }

    *ptr1 = NULL;
    *len1 = 0;
    if(ptr2) *ptr2 = NULL;
    if(len2) *len2 = 0;

    if((flags&DSBLOCK_FROMWRITECURSOR))
        DSBuffer_GetCurrentPosition(iface, NULL, &ofs);
    else if(ofs >= (DWORD)This->buffer->buf_size)
    {
        WARN("Invalid ofs %lu\n", ofs);
        return DSERR_INVALIDPARAM;
    }
    if((flags&DSBLOCK_ENTIREBUFFER))
        bytes = This->buffer->buf_size;
    else if(bytes > (DWORD)This->buffer->buf_size)
    {
        WARN("Invalid size %lu\n", bytes);
        return DSERR_INVALIDPARAM;
    }

    if(InterlockedExchange(&This->buffer->locked, TRUE) == TRUE)
    {
        WARN("Already locked\n");
        return DSERR_INVALIDPARAM;
    }

    *ptr1 = This->buffer->data + ofs;
    if(bytes >= (DWORD)This->buffer->buf_size-ofs)
    {
        *len1 = This->buffer->buf_size - ofs;
        remain = bytes - *len1;
    }
    else
    {
        *len1 = bytes;
        remain = 0;
    }

    if(ptr2 && len2 && remain)
    {
        *ptr2 = This->buffer->data;
        *len2 = remain;
    }

    return DS_OK;
}

static HRESULT WINAPI DSBuffer_Play(IDirectSoundBuffer8 *iface, DWORD res1, DWORD prio, DWORD flags)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    ALint state = AL_STOPPED;
    DSData *data;
    HRESULT hr;

    TRACE("(%p)->(%lu, %lu, %lu)\n", iface, res1, prio, flags);

    EnterCriticalSection(&This->share->crst);
    setALContext(This->ctx);

    hr = DSERR_BUFFERLOST;
    if(This->bufferlost)
    {
        WARN("Buffer %p lost\n", This);
        goto out;
    }

    data = This->buffer;
    if((data->dsbflags&DSBCAPS_LOCDEFER))
    {
        DWORD loc = 0;

        hr = DSERR_INVALIDPARAM;
        if((flags&(DSBPLAY_LOCSOFTWARE|DSBPLAY_LOCHARDWARE)) == (DSBPLAY_LOCSOFTWARE|DSBPLAY_LOCHARDWARE))
        {
            WARN("Both hardware and software specified\n");
            goto out;
        }

        if((flags&DSBPLAY_LOCHARDWARE)) loc = DSBSTATUS_LOCHARDWARE;
        else if((flags&DSBPLAY_LOCSOFTWARE)) loc = DSBSTATUS_LOCSOFTWARE;

        if(loc && This->loc_status && loc != This->loc_status)
        {
            if(This->segsize != 0)
            {
                if(This->isplaying)
                    state = AL_PLAYING;
            }
            else
            {
                alGetSourcei(This->source, AL_SOURCE_STATE, &state);
                checkALError();
            }

            if(state == AL_PLAYING)
            {
                ERR("Attemping to change location on playing buffer\n");
                goto out;
            }
        }

        hr = DSBuffer_SetLoc(This, loc);
        if(FAILED(hr)) goto out;
    }
    else if(prio)
    {
        ERR("Invalid priority set for non-deferred buffer %p, %lu!\n", This->buffer, prio);
        hr = DSERR_INVALIDPARAM;
        goto out;
    }

    if(This->segsize != 0)
    {
        This->islooping = !!(flags&DSBPLAY_LOOPING);
        if(This->isplaying) state = AL_PLAYING;
    }
    else
    {
        alSourcei(This->source, AL_LOOPING, (flags&DSBPLAY_LOOPING) ? AL_TRUE : AL_FALSE);
        alGetSourcei(This->source, AL_SOURCE_STATE, &state);
        checkALError();
    }

    hr = S_OK;
    if(state == AL_PLAYING)
        goto out;

    if(This->segsize == 0)
    {
        if(state == AL_INITIAL)
        {
            alSourcei(This->source, AL_BUFFER, data->bid);
            alSourcei(This->source, AL_BYTE_OFFSET, This->lastpos % data->buf_size);
        }
        alSourcePlay(This->source);
    }
    else
    {
        alSourceRewind(This->source);
        alSourcei(This->source, AL_BUFFER, 0);
        This->queue_base = This->data_offset % data->buf_size;
        This->curidx = 0;
    }
    if(alGetError() != AL_NO_ERROR)
    {
        ERR("Couldn't start source\n");
        hr = DSERR_GENERIC;
        goto out;
    }
    This->isplaying = TRUE;

    if(This->nnotify)
        DSBuffer_addnotify(This);

out:
    popALContext();
    LeaveCriticalSection(&This->share->crst);
    return hr;
}

static HRESULT WINAPI DSBuffer_SetCurrentPosition(IDirectSoundBuffer8 *iface, DWORD pos)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DSData *data;

    TRACE("(%p)->(%lu)\n", iface, pos);

    data = This->buffer;
    if(pos >= (DWORD)data->buf_size)
        return DSERR_INVALIDPARAM;
    pos -= pos%data->format.Format.nBlockAlign;

    EnterCriticalSection(&This->share->crst);

    if(This->segsize != 0)
    {
        if(This->isplaying)
        {
            setALContext(This->ctx);
            /* Perform a flush, so the next timer update will restart at the
             * proper position */
            alSourceRewind(This->source);
            alSourcei(This->source, AL_BUFFER, 0);
            checkALError();
            popALContext();
        }
        This->queue_base = This->data_offset = pos;
        This->curidx = 0;
    }
    else
    {
        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alSourcei(This->source, AL_BYTE_OFFSET, pos);
            checkALError();
            popALContext();
        }
    }
    This->lastpos = pos;

    LeaveCriticalSection(&This->share->crst);
    return DS_OK;
}

static HRESULT WINAPI DSBuffer_SetFormat(IDirectSoundBuffer8 *iface, const WAVEFORMATEX *wfx)
{
    /* This call only works on primary buffers */
    WARN("(%p)->(%p)\n", iface, wfx);
    return DSERR_INVALIDCALL;
}

static HRESULT WINAPI DSBuffer_SetVolume(IDirectSoundBuffer8 *iface, LONG vol)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%ld)\n", iface, vol);

    if(vol > DSBVOLUME_MAX || vol < DSBVOLUME_MIN)
    {
        WARN("Invalid volume (%ld)\n", vol);
        return DSERR_INVALIDPARAM;
    }

    if(!(This->buffer->dsbflags&DSBCAPS_CTRLVOLUME))
        hr = DSERR_CONTROLUNAVAIL;
    else
    {
        This->current.vol = vol;
        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alSourcef(This->source, AL_GAIN, mB_to_gain((float)vol));
            popALContext();
        }
    }

    return hr;
}

static HRESULT WINAPI DSBuffer_SetPan(IDirectSoundBuffer8 *iface, LONG pan)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%ld)\n", iface, pan);

    if(pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
    {
        WARN("invalid parameter: pan = %ld\n", pan);
        return DSERR_INVALIDPARAM;
    }

    if(!(This->buffer->dsbflags&DSBCAPS_CTRLPAN))
        hr = DSERR_CONTROLUNAVAIL;
    else
    {
        This->current.pan = pan;
        if(LIKELY(This->source && !(This->buffer->dsbflags&DSBCAPS_CTRL3D)))
        {
            ALfloat pos[3];
            pos[0] = (ALfloat)(pan-DSBPAN_LEFT)/(ALfloat)(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f;
            pos[1] = 0.0f;
            /* NOTE: Strict movement along the X plane can cause the sound to
             * jump between left and right sharply. Using a curved path helps
             * smooth it out.
             */
            pos[2] = -sqrtf(1.0f - pos[0]*pos[0]);

            setALContext(This->ctx);
            alSourcefv(This->source, AL_POSITION, pos);
            checkALError();
            popALContext();
        }
    }

    return hr;
}

static HRESULT WINAPI DSBuffer_SetFrequency(IDirectSoundBuffer8 *iface, DWORD freq)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DSData *data;
    HRESULT hr = S_OK;

    TRACE("(%p)->(%lu)\n", iface, freq);

    if(freq != 0 && (freq < DSBFREQUENCY_MIN || freq > DSBFREQUENCY_MAX))
    {
        WARN("invalid parameter: freq = %lu\n", freq);
        return DSERR_INVALIDPARAM;
    }

    data = This->buffer;
    if(!(data->dsbflags&DSBCAPS_CTRLFREQUENCY))
        hr = DSERR_CONTROLUNAVAIL;
    else
    {
        This->current.frequency = freq ? freq : data->format.Format.nSamplesPerSec;
        if(LIKELY(This->source))
        {
            setALContext(This->ctx);
            alSourcef(This->source, AL_PITCH,
                This->current.frequency / (ALfloat)data->format.Format.nSamplesPerSec
            );
            checkALError();
            popALContext();
        }
    }

    return hr;
}

static HRESULT WINAPI DSBuffer_Stop(IDirectSoundBuffer8 *iface)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->share->crst);
    if(LIKELY(This->source))
    {
        const ALuint source = This->source;
        ALint state, ofs;

        setALContext(This->ctx);
        alSourcePause(source);
        alGetSourcei(source, AL_BYTE_OFFSET, &ofs);
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        checkALError();

        This->isplaying = FALSE;
        if(This->nnotify)
            DSPrimary_triggernots(This->primary);
        /* Ensure the notification's last tracked position is updated, as well
         * as the queue offsets for streaming sources.
         */
        if(This->segsize == 0)
            This->lastpos = (state == AL_STOPPED) ? This->buffer->buf_size : ofs;
        else
        {
            DSData *data = This->buffer;
            ALint done = 0;

            alGetSourcei(This->source, AL_BUFFERS_PROCESSED, &done);
            This->queue_base += This->segsize*done + ofs;
            if(This->queue_base >= data->buf_size)
            {
                if(This->islooping)
                    This->queue_base %= data->buf_size;
                else
                    This->queue_base = data->buf_size;
            }
            This->lastpos = This->queue_base;

            alSourceRewind(This->source);
            alSourcei(This->source, AL_BUFFER, 0);
            checkALError();

            This->curidx = 0;
            This->data_offset = This->lastpos % data->buf_size;
        }
        This->islooping = FALSE;
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer_Unlock(IDirectSoundBuffer8 *iface, void *ptr1, DWORD len1, void *ptr2, DWORD len2)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    DSData *buf = This->buffer;
    DWORD bufsize = buf->buf_size;
    DWORD_PTR ofs1, ofs2;
    DWORD_PTR boundary = (DWORD_PTR)buf->data;
    HRESULT hr;

    TRACE("(%p)->(%p, %lu, %p, %lu)\n", iface, ptr1, len1, ptr2, len2);

    if(InterlockedExchange(&buf->locked, FALSE) == FALSE)
    {
        WARN("Not locked\n");
        return DSERR_INVALIDPARAM;
    }

    hr = DSERR_INVALIDPARAM;
    /* Make sure offset is between boundary and boundary + bufsize */
    ofs1 = (DWORD_PTR)ptr1;
    ofs2 = (DWORD_PTR)ptr2;
    if(ofs1 < boundary || (ofs2 && ofs2 != boundary))
        goto out;
    ofs1 -= boundary;
    ofs2 = 0;
    if(bufsize-ofs1 < len1 || len2 > ofs1)
        goto out;
    if(!ptr2)
        len2 = 0;

    hr = DS_OK;
    if(!len1 && !len2)
        goto out;

    if(HAS_EXTENSION(This->share, SOFTX_MAP_BUFFER))
    {
        setALContext(This->ctx);
        alFlushMappedBufferSOFT(buf->bid, 0, buf->buf_size);
        checkALError();
        popALContext();
    }
    else if(This->segsize == 0)
    {
        setALContext(This->ctx);
        alBufferData(buf->bid, buf->buf_format, buf->data, buf->buf_size,
                     buf->format.Format.nSamplesPerSec);
        checkALError();
        popALContext();
    }

out:
    if(hr != S_OK)
        WARN("Invalid parameters (%p,%lu) (%p,%lu,%p,%lu)\n", (void*)boundary, bufsize,
            ptr1, len1, ptr2, len2);
    return hr;
}

static HRESULT WINAPI DSBuffer_Restore(IDirectSoundBuffer8 *iface)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&This->share->crst);
    if(This->primary->parent->prio_level < DSSCL_WRITEPRIMARY ||
       (IDirectSoundBuffer*)&This->IDirectSoundBuffer8_iface == This->primary->write_emu)
    {
        This->bufferlost = 0;
        hr = S_OK;
    }
    else
        hr = DSERR_BUFFERLOST;
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSBuffer_SetFX(IDirectSoundBuffer8 *iface, DWORD fxcount, DSEFFECTDESC *desc, DWORD *rescodes)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    ALenum state = AL_INITIAL;
    DSData *data;
    HRESULT hr;
    DWORD i;

    TRACE("(%p)->(%lu, %p, %p)\n", This, fxcount, desc, rescodes);

    data = This->buffer;
    if(!(data->dsbflags&DSBCAPS_CTRLFX))
    {
        WARN("FX control not set\n");
        return DSERR_CONTROLUNAVAIL;
    }

    if(data->locked)
    {
        WARN("Buffer is locked\n");
        return DSERR_INVALIDCALL;
    }

    EnterCriticalSection(&This->share->crst);

    setALContext(This->ctx);
    if(LIKELY(This->source))
    {
        alGetSourcei(This->source, AL_SOURCE_STATE, &state);
        checkALError();
    }
    popALContext();
    if(This->segsize != 0 && state != AL_PLAYING)
        state = This->isplaying ? AL_PLAYING : AL_PAUSED;
    if(state == AL_PLAYING)
    {
        WARN("Buffer is playing\n");
        hr = DSERR_INVALIDCALL;
        goto done;
    }

    hr = DSERR_INVALIDPARAM;
    if(fxcount == 0)
    {
        if(desc || rescodes)
        {
            WARN("Non-NULL desc and/or result pointer specified with no effects.\n");
            goto done;
        }

        /* No effects; we can handle that */
        hr = DS_OK;
        goto done;
    }

    if(!desc || !rescodes)
    {
        WARN("NULL desc and/or result pointer specified.\n");
        goto done;
    }

    /* We don't (currently) handle DSound effects */
    for(i = 0;i < fxcount;++i)
    {
        FIXME("Cannot handle effect: %s\n", debugstr_guid(&desc[i].guidDSFXClass));
        rescodes[i] = DSFXR_FAILED;
    }
    hr = DS_INCOMPLETE;

done:
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSBuffer_AcquireResources(IDirectSoundBuffer8 *iface, DWORD flags, DWORD fxcount, DWORD *rescodes)
{
    DSBuffer *This = impl_from_IDirectSoundBuffer8(iface);
    HRESULT hr;

    TRACE("(%p)->(%lu, %lu, %p)\n", This, flags, fxcount, rescodes);

    /* effects aren't supported at the moment.. */
    if(fxcount != 0 || rescodes)
    {
        WARN("Non-zero effect count and/or result pointer specified with no effects.\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    setALContext(This->ctx);

    hr = DS_OK;
    if((This->buffer->dsbflags&DSBCAPS_LOCDEFER))
    {
        DWORD loc = 0;

        hr = DSERR_INVALIDPARAM;
        if((flags&(DSBPLAY_LOCSOFTWARE|DSBPLAY_LOCHARDWARE)) == (DSBPLAY_LOCSOFTWARE|DSBPLAY_LOCHARDWARE))
        {
            WARN("Both hardware and software specified\n");
            goto out;
        }

        if((flags&DSBPLAY_LOCHARDWARE)) loc = DSBSTATUS_LOCHARDWARE;
        else if((flags&DSBPLAY_LOCSOFTWARE)) loc = DSBSTATUS_LOCSOFTWARE;

        if(loc && This->loc_status && loc != This->loc_status)
        {
            ALint state = AL_INITIAL;

            if(This->segsize != 0)
            {
                if(This->isplaying)
                    state = AL_PLAYING;
            }
            else
            {
                alGetSourcei(This->source, AL_SOURCE_STATE, &state);
                checkALError();
            }

            if(state == AL_PLAYING)
            {
                ERR("Attemping to change location on playing buffer\n");
                goto out;
            }
        }

        hr = DSBuffer_SetLoc(This, loc);
    }

out:
    popALContext();
    LeaveCriticalSection(&This->share->crst);
    return hr;
}

static HRESULT WINAPI DSBuffer_GetObjectInPath(IDirectSoundBuffer8 *iface, REFGUID guid, DWORD idx, REFGUID rguidiface, void **ppv)
{
    FIXME("(%p)->(%s, %lu, %s, %p) : stub!\n", iface, debugstr_guid(guid), idx, debugstr_guid(rguidiface), ppv);
    if(ppv) *ppv = NULL;
    return E_NOTIMPL;
}

static IDirectSoundBuffer8Vtbl DSBuffer_Vtbl = {
    DSBuffer_QueryInterface,
    DSBuffer_AddRef,
    DSBuffer_Release,
    DSBuffer_GetCaps,
    DSBuffer_GetCurrentPosition,
    DSBuffer_GetFormat,
    DSBuffer_GetVolume,
    DSBuffer_GetPan,
    DSBuffer_GetFrequency,
    DSBuffer_GetStatus,
    DSBuffer_Initialize,
    DSBuffer_Lock,
    DSBuffer_Play,
    DSBuffer_SetCurrentPosition,
    DSBuffer_SetFormat,
    DSBuffer_SetVolume,
    DSBuffer_SetPan,
    DSBuffer_SetFrequency,
    DSBuffer_Stop,
    DSBuffer_Unlock,
    DSBuffer_Restore,
    DSBuffer_SetFX,
    DSBuffer_AcquireResources,
    DSBuffer_GetObjectInPath
};


void DSBuffer_SetParams(DSBuffer *This, const DS3DBUFFER *params, LONG flags)
{
    const ALuint source = This->source;
    union BufferParamFlags dirty = { flags };

    /* Copy deferred parameters first. */
    if(dirty.bit.pos)
        This->current.ds3d.vPosition = params->vPosition;
    if(dirty.bit.vel)
        This->current.ds3d.vVelocity = params->vVelocity;
    if(dirty.bit.cone_angles)
    {
        This->current.ds3d.dwInsideConeAngle = params->dwInsideConeAngle;
        This->current.ds3d.dwOutsideConeAngle = params->dwOutsideConeAngle;
    }
    if(dirty.bit.cone_orient)
        This->current.ds3d.vConeOrientation = params->vConeOrientation;
    if(dirty.bit.cone_outsidevolume)
        This->current.ds3d.lConeOutsideVolume = params->lConeOutsideVolume;
    if(dirty.bit.min_distance)
        This->current.ds3d.flMinDistance = params->flMinDistance;
    if(dirty.bit.max_distance)
        This->current.ds3d.flMaxDistance = params->flMaxDistance;
    if(dirty.bit.mode)
        This->current.ds3d.dwMode = params->dwMode;

    /* Now apply what's changed to OpenAL. */
    if(UNLIKELY(!source)) return;

    if(dirty.bit.pos)
        alSource3f(source, AL_POSITION, params->vPosition.x, params->vPosition.y,
                                       -params->vPosition.z);
    if(dirty.bit.vel)
        alSource3f(source, AL_VELOCITY, params->vVelocity.x, params->vVelocity.y,
                                       -params->vVelocity.z);
    if(dirty.bit.cone_angles)
    {
        alSourcei(source, AL_CONE_INNER_ANGLE, params->dwInsideConeAngle);
        alSourcei(source, AL_CONE_OUTER_ANGLE, params->dwOutsideConeAngle);
    }
    if(dirty.bit.cone_orient)
        alSource3f(source, AL_DIRECTION, params->vConeOrientation.x,
                                         params->vConeOrientation.y,
                                        -params->vConeOrientation.z);
    if(dirty.bit.cone_outsidevolume)
        alSourcef(source, AL_CONE_OUTER_GAIN, mB_to_gain((float)params->lConeOutsideVolume));
    if(dirty.bit.min_distance)
        alSourcef(source, AL_REFERENCE_DISTANCE, params->flMinDistance);
    if(dirty.bit.max_distance)
        alSourcef(source, AL_MAX_DISTANCE, params->flMaxDistance);
    if(dirty.bit.mode)
    {
        if(HAS_EXTENSION(This->share, SOFT_SOURCE_SPATIALIZE))
            alSourcei(source, AL_SOURCE_SPATIALIZE_SOFT,
                (params->dwMode==DS3DMODE_DISABLE) ? AL_FALSE : AL_TRUE
            );
        alSourcei(source, AL_SOURCE_RELATIVE,
            (params->dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE
        );
    }
}

static HRESULT WINAPI DSBuffer3D_QueryInterface(IDirectSound3DBuffer *iface, REFIID riid, void **ppv)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSBuffer_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DSBuffer3D_AddRef(IDirectSound3DBuffer *iface)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->ds3d_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSBuffer3D_Release(IDirectSound3DBuffer *iface)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->ds3d_ref);
    TRACE("(%p) ref %lu\n", iface, ret);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSBuffer_Destroy(This);

    return ret;
}

static HRESULT WINAPI DSBuffer3D_GetConeAngles(IDirectSound3DBuffer *iface, DWORD *pdwInsideConeAngle, DWORD *pdwOutsideConeAngle)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p, %p)\n", This, pdwInsideConeAngle, pdwOutsideConeAngle);
    if(!pdwInsideConeAngle || !pdwOutsideConeAngle)
    {
        WARN("Invalid pointers (%p, %p)\n", pdwInsideConeAngle, pdwOutsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *pdwInsideConeAngle = This->current.ds3d.dwInsideConeAngle;
    *pdwOutsideConeAngle = This->current.ds3d.dwOutsideConeAngle;
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetConeOrientation(IDirectSound3DBuffer *iface, D3DVECTOR *orient)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, orient);
    if(!orient)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *orient = This->current.ds3d.vConeOrientation;
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetConeOutsideVolume(IDirectSound3DBuffer *iface, LONG *vol)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, vol);
    if(!vol)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    *vol = This->current.ds3d.lConeOutsideVolume;
    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetMaxDistance(IDirectSound3DBuffer *iface, D3DVALUE *maxdist)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, maxdist);
    if(!maxdist)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    *maxdist = This->current.ds3d.flMaxDistance;
    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetMinDistance(IDirectSound3DBuffer *iface, D3DVALUE *mindist)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, mindist);
    if(!mindist)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    *mindist = This->current.ds3d.flMinDistance;
    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetMode(IDirectSound3DBuffer *iface, DWORD *mode)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, mode);
    if(!mode)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    *mode = This->current.ds3d.dwMode;
    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetPosition(IDirectSound3DBuffer *iface, D3DVECTOR *pos)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, pos);
    if(!pos)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *pos = This->current.ds3d.vPosition;
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetVelocity(IDirectSound3DBuffer *iface, D3DVECTOR *vel)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", This, vel);
    if(!vel)
    {
        WARN("Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    *vel = This->current.ds3d.vVelocity;
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_GetAllParameters(IDirectSound3DBuffer *iface, DS3DBUFFER *ds3dbuffer)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%p)\n", iface, ds3dbuffer);

    if(!ds3dbuffer || ds3dbuffer->dwSize < sizeof(*ds3dbuffer))
    {
        WARN("Invalid parameters %p %lu\n", ds3dbuffer, ds3dbuffer ? ds3dbuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    ds3dbuffer->vPosition = This->current.ds3d.vPosition;
    ds3dbuffer->vVelocity = This->current.ds3d.vVelocity;
    ds3dbuffer->dwInsideConeAngle = This->current.ds3d.dwInsideConeAngle;
    ds3dbuffer->dwOutsideConeAngle = This->current.ds3d.dwOutsideConeAngle;
    ds3dbuffer->vConeOrientation = This->current.ds3d.vConeOrientation;
    ds3dbuffer->lConeOutsideVolume = This->current.ds3d.lConeOutsideVolume;
    ds3dbuffer->flMinDistance = This->current.ds3d.flMinDistance;
    ds3dbuffer->flMaxDistance = This->current.ds3d.flMaxDistance;
    ds3dbuffer->dwMode = This->current.ds3d.dwMode;
    LeaveCriticalSection(&This->share->crst);

    return DS_OK;
}

static HRESULT WINAPI DSBuffer3D_SetConeAngles(IDirectSound3DBuffer *iface, DWORD dwInsideConeAngle, DWORD dwOutsideConeAngle, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%lu, %lu, %lu)\n", This, dwInsideConeAngle, dwOutsideConeAngle, apply);
    if(dwInsideConeAngle > DS3D_MAXCONEANGLE || dwOutsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN("Invalid cone angles (%lu, %lu)\n", dwInsideConeAngle, dwOutsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.dwInsideConeAngle = dwInsideConeAngle;
        This->deferred.ds3d.dwOutsideConeAngle = dwOutsideConeAngle;
        This->dirty.bit.cone_angles = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.dwInsideConeAngle = dwInsideConeAngle;
        This->current.ds3d.dwOutsideConeAngle = dwOutsideConeAngle;
        if(LIKELY(This->source))
        {
            alSourcei(This->source, AL_CONE_INNER_ANGLE, dwInsideConeAngle);
            alSourcei(This->source, AL_CONE_OUTER_ANGLE, dwOutsideConeAngle);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetConeOrientation(IDirectSound3DBuffer *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%f, %f, %f, %lu)\n", This, x, y, z, apply);

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.vConeOrientation.x = x;
        This->deferred.ds3d.vConeOrientation.y = y;
        This->deferred.ds3d.vConeOrientation.z = z;
        This->dirty.bit.cone_orient = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.vConeOrientation.x = x;
        This->current.ds3d.vConeOrientation.y = y;
        This->current.ds3d.vConeOrientation.z = z;
        if(LIKELY(This->source))
        {
            alSource3f(This->source, AL_DIRECTION, x, y, -z);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetConeOutsideVolume(IDirectSound3DBuffer *iface, LONG vol, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%ld, %lu)\n", This, vol, apply);
    if(vol < DSBVOLUME_MIN || vol > DSBVOLUME_MAX)
    {
        WARN("Invalid volume (%ld)\n", vol);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.lConeOutsideVolume = vol;
        This->dirty.bit.cone_outsidevolume = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.lConeOutsideVolume = vol;
        if(LIKELY(This->source))
        {
            alSourcef(This->source, AL_CONE_OUTER_GAIN, mB_to_gain((float)vol));
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetMaxDistance(IDirectSound3DBuffer *iface, D3DVALUE maxdist, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%f, %lu)\n", This, maxdist, apply);
    if(maxdist < 0.0f)
    {
        WARN("Invalid max distance (%f)\n", maxdist);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.flMaxDistance = maxdist;
        This->dirty.bit.max_distance = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.flMaxDistance = maxdist;
        if(LIKELY(This->source))
        {
            alSourcef(This->source, AL_MAX_DISTANCE, maxdist);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetMinDistance(IDirectSound3DBuffer *iface, D3DVALUE mindist, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%f, %lu)\n", This, mindist, apply);
    if(mindist < 0.0f)
    {
        WARN("Invalid min distance (%f)\n", mindist);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.flMinDistance = mindist;
        This->dirty.bit.min_distance = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.flMinDistance = mindist;
        if(LIKELY(This->source))
        {
            alSourcef(This->source, AL_REFERENCE_DISTANCE, mindist);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetMode(IDirectSound3DBuffer *iface, DWORD mode, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%lu, %lu)\n", This, mode, apply);
    if(mode != DS3DMODE_NORMAL && mode != DS3DMODE_HEADRELATIVE &&
       mode != DS3DMODE_DISABLE)
    {
        WARN("Invalid mode (%lu)\n", mode);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(apply == DS3D_DEFERRED)
    {
        This->deferred.ds3d.dwMode = mode;
        This->dirty.bit.mode = 1;
    }
    else
    {
        setALContext(This->ctx);
        This->current.ds3d.dwMode = mode;
        if(LIKELY(This->source))
        {
            if(HAS_EXTENSION(This->share, SOFT_SOURCE_SPATIALIZE))
                alSourcei(This->source, AL_SOURCE_SPATIALIZE_SOFT,
                          (mode==DS3DMODE_DISABLE) ? AL_FALSE : AL_TRUE);
            alSourcei(This->source, AL_SOURCE_RELATIVE,
                      (mode != DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetPosition(IDirectSound3DBuffer *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%f, %f, %f, %lu)\n", This, x, y, z, apply);

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
        if(LIKELY(This->source))
        {
            alSource3f(This->source, AL_POSITION, x, y, -z);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetVelocity(IDirectSound3DBuffer *iface, D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);

    TRACE("(%p)->(%f, %f, %f, %lu)\n", This, x, y, z, apply);

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
        if(LIKELY(This->source))
        {
            alSource3f(This->source, AL_VELOCITY, x, y, -z);
            checkALError();
        }
        popALContext();
    }
    LeaveCriticalSection(&This->share->crst);

    return S_OK;
}

static HRESULT WINAPI DSBuffer3D_SetAllParameters(IDirectSound3DBuffer *iface, const DS3DBUFFER *ds3dbuffer, DWORD apply)
{
    DSBuffer *This = impl_from_IDirectSound3DBuffer(iface);
    TRACE("(%p)->(%p, %lu)\n", This, ds3dbuffer, apply);

    if(!ds3dbuffer || ds3dbuffer->dwSize < sizeof(*ds3dbuffer))
    {
        WARN("Invalid DS3DBUFFER (%p, %lu)\n", ds3dbuffer, ds3dbuffer ? ds3dbuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dbuffer->dwInsideConeAngle > DS3D_MAXCONEANGLE ||
       ds3dbuffer->dwOutsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN("Invalid cone angles (%lu, %lu)\n",
             ds3dbuffer->dwInsideConeAngle, ds3dbuffer->dwOutsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dbuffer->lConeOutsideVolume > DSBVOLUME_MAX ||
       ds3dbuffer->lConeOutsideVolume < DSBVOLUME_MIN)
    {
        WARN("Invalid cone outside volume (%ld)\n", ds3dbuffer->lConeOutsideVolume);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dbuffer->flMaxDistance < 0.0f)
    {
        WARN("Invalid max distance (%f)\n", ds3dbuffer->flMaxDistance);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dbuffer->flMinDistance < 0.0f)
    {
        WARN("Invalid min distance (%f)\n", ds3dbuffer->flMinDistance);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dbuffer->dwMode != DS3DMODE_NORMAL &&
       ds3dbuffer->dwMode != DS3DMODE_HEADRELATIVE &&
       ds3dbuffer->dwMode != DS3DMODE_DISABLE)
    {
        WARN("Invalid mode (%lu)\n", ds3dbuffer->dwMode);
        return DSERR_INVALIDPARAM;
    }

    if(apply == DS3D_DEFERRED)
    {
        EnterCriticalSection(&This->share->crst);
        This->deferred.ds3d = *ds3dbuffer;
        This->deferred.ds3d.dwSize = sizeof(This->deferred.ds3d);
        This->dirty.bit.pos = 1;
        This->dirty.bit.vel = 1;
        This->dirty.bit.cone_angles = 1;
        This->dirty.bit.cone_orient = 1;
        This->dirty.bit.cone_outsidevolume = 1;
        This->dirty.bit.min_distance = 1;
        This->dirty.bit.max_distance = 1;
        This->dirty.bit.mode = 1;
        LeaveCriticalSection(&This->share->crst);
    }
    else
    {
        union BufferParamFlags dirty = { 0 };
        dirty.bit.pos = 1;
        dirty.bit.vel = 1;
        dirty.bit.cone_angles = 1;
        dirty.bit.cone_orient = 1;
        dirty.bit.cone_outsidevolume = 1;
        dirty.bit.min_distance = 1;
        dirty.bit.max_distance = 1;
        dirty.bit.mode = 1;

        EnterCriticalSection(&This->share->crst);
        setALContext(This->ctx);
        DSBuffer_SetParams(This, ds3dbuffer, dirty.flags);
        checkALError();
        popALContext();
        LeaveCriticalSection(&This->share->crst);
    }

    return S_OK;
}

static IDirectSound3DBufferVtbl DSBuffer3d_Vtbl =
{
    DSBuffer3D_QueryInterface,
    DSBuffer3D_AddRef,
    DSBuffer3D_Release,
    DSBuffer3D_GetAllParameters,
    DSBuffer3D_GetConeAngles,
    DSBuffer3D_GetConeOrientation,
    DSBuffer3D_GetConeOutsideVolume,
    DSBuffer3D_GetMaxDistance,
    DSBuffer3D_GetMinDistance,
    DSBuffer3D_GetMode,
    DSBuffer3D_GetPosition,
    DSBuffer3D_GetVelocity,
    DSBuffer3D_SetAllParameters,
    DSBuffer3D_SetConeAngles,
    DSBuffer3D_SetConeOrientation,
    DSBuffer3D_SetConeOutsideVolume,
    DSBuffer3D_SetMaxDistance,
    DSBuffer3D_SetMinDistance,
    DSBuffer3D_SetMode,
    DSBuffer3D_SetPosition,
    DSBuffer3D_SetVelocity
};


static HRESULT WINAPI DSBufferNot_QueryInterface(IDirectSoundNotify *iface, REFIID riid, void **ppv)
{
    DSBuffer *This = impl_from_IDirectSoundNotify(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSBuffer_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DSBufferNot_AddRef(IDirectSoundNotify *iface)
{
    DSBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->not_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSBufferNot_Release(IDirectSoundNotify *iface)
{
    DSBuffer *This = impl_from_IDirectSoundNotify(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->not_ref);
    TRACE("(%p) ref %lu\n", iface, ret);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSBuffer_Destroy(This);

    return ret;
}

static HRESULT WINAPI DSBufferNot_SetNotificationPositions(IDirectSoundNotify *iface, DWORD count, const DSBPOSITIONNOTIFY *notifications)
{
    DSBuffer *This = impl_from_IDirectSoundNotify(iface);
    DSBPOSITIONNOTIFY *nots;
    DWORD state;
    HRESULT hr;

    TRACE("(%p)->(%lu, %p))\n", iface, count, notifications);

    EnterCriticalSection(&This->share->crst);
    hr = DSERR_INVALIDPARAM;
    if(count && !notifications)
        goto out;

    hr = DSBuffer_GetStatus(&This->IDirectSoundBuffer8_iface, &state);
    if(FAILED(hr)) goto out;

    hr = DSERR_INVALIDCALL;
    if((state&DSBSTATUS_PLAYING))
        goto out;

    if(!count)
    {
        HeapFree(GetProcessHeap(), 0, This->notify);
        This->notify = 0;
        This->nnotify = 0;
        hr = S_OK;
    }
    else
    {
        DWORD i;

        hr = DSERR_INVALIDPARAM;
        for(i = 0;i < count;++i)
        {
            if(notifications[i].dwOffset >= (DWORD)This->buffer->buf_size &&
               notifications[i].dwOffset != (DWORD)DSBPN_OFFSETSTOP)
                goto out;
        }

        hr = E_OUTOFMEMORY;
        nots = HeapAlloc(GetProcessHeap(), 0, count*sizeof(*nots));
        if(!nots) goto out;
        memcpy(nots, notifications, count*sizeof(*nots));

        HeapFree(GetProcessHeap(), 0, This->notify);
        This->notify = nots;
        This->nnotify = count;

        hr = S_OK;
    }

out:
    LeaveCriticalSection(&This->share->crst);
    return hr;
}

static IDirectSoundNotifyVtbl DSBufferNot_Vtbl =
{
    DSBufferNot_QueryInterface,
    DSBufferNot_AddRef,
    DSBufferNot_Release,
    DSBufferNot_SetNotificationPositions
};


static const char *debug_bufferprop(const GUID *guid)
{
#define HANDLE_ID(id) if(IsEqualGUID(guid, &(id))) return #id
    HANDLE_ID(EAXPROPERTYID_EAX40_Source);
    HANDLE_ID(DSPROPSETID_EAX30_BufferProperties);
    HANDLE_ID(DSPROPSETID_EAX20_BufferProperties);
    HANDLE_ID(EAXPROPERTYID_EAX40_FXSlot0);
    HANDLE_ID(EAXPROPERTYID_EAX40_FXSlot1);
    HANDLE_ID(EAXPROPERTYID_EAX40_FXSlot2);
    HANDLE_ID(EAXPROPERTYID_EAX40_FXSlot3);
    HANDLE_ID(DSPROPSETID_EAX30_ListenerProperties);
    HANDLE_ID(DSPROPSETID_EAX20_ListenerProperties);
    HANDLE_ID(EAXPROPERTYID_EAX40_Context);
    HANDLE_ID(DSPROPSETID_EAX10_BufferProperties);
    HANDLE_ID(DSPROPSETID_EAX10_ListenerProperties);
    HANDLE_ID(DSPROPSETID_VoiceManager);
    HANDLE_ID(DSPROPSETID_ZOOMFX_BufferProperties);
    HANDLE_ID(DSPROPSETID_I3DL2_ListenerProperties);
    HANDLE_ID(DSPROPSETID_I3DL2_BufferProperties);
#undef HANDLE_ID
    return debugstr_guid(guid);
}

static HRESULT WINAPI DSBufferProp_QueryInterface(IKsPropertySet *iface, REFIID riid, void **ppv)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSBuffer_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DSBufferProp_AddRef(IKsPropertySet *iface)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    InterlockedIncrement(&This->all_ref);
    ret = InterlockedIncrement(&This->prop_ref);
    TRACE("(%p) ref %lu\n", iface, ret);

    return ret;
}

static ULONG WINAPI DSBufferProp_Release(IKsPropertySet *iface)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    LONG ret;

    ret = InterlockedDecrement(&This->prop_ref);
    TRACE("(%p) ref %lu\n", iface, ret);
    if(InterlockedDecrement(&This->all_ref) == 0)
        DSBuffer_Destroy(This);

    return ret;
}

/* NOTE: Due to some apparent quirks in DSound, the listener properties are
         handled through secondary buffers. */
static HRESULT WINAPI DSBufferProp_Get(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData,
  ULONG *pcbReturned)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    HRESULT hr = E_PROP_ID_UNSUPPORTED;
    ALenum err;

    TRACE("(%p)->(%s, 0x%lx, %p, %lu, %p, %lu, %p)\n", iface, debug_bufferprop(guidPropSet),
          dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData, pcbReturned);

    if(!pcbReturned)
        return E_POINTER;
    *pcbReturned = 0;

    if(cbPropData > 0 && !pPropData)
    {
        WARN("pPropData is NULL with cbPropData > 0\n");
        return E_POINTER;
    }

    EnterCriticalSection(&This->share->crst);
    if(IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Source)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_BufferProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_BufferProperties)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot0)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot1)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot2)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot3)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_ListenerProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Context)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_BufferProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_ListenerProperties))
    {
        err = EAXGet(guidPropSet, dwPropID, This->source, pPropData, cbPropData);
        if(err != AL_NO_ERROR) hr = E_FAIL;
        else hr = DS_OK;
    }
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_VoiceManager))
        hr = VoiceMan_Get(This, dwPropID, pPropData, cbPropData, pcbReturned);
    else
        FIXME("Unhandled propset: %s\n", debug_bufferprop(guidPropSet));
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSBufferProp_Set(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  LPVOID pInstanceData, ULONG cbInstanceData,
  LPVOID pPropData, ULONG cbPropData)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    HRESULT hr = E_PROP_ID_UNSUPPORTED;

    TRACE("(%p)->(%s, 0x%lx, %p, %lu, %p, %lu)\n", iface, debug_bufferprop(guidPropSet),
          dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData);

    if(cbPropData > 0 && !pPropData)
    {
        WARN("pPropData is NULL with cbPropData > 0\n");
        return E_POINTER;
    }

    EnterCriticalSection(&This->share->crst);
    if(IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Source)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_BufferProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_BufferProperties)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot0)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot1)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot2)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot3)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_ListenerProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Context)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_BufferProperties)
        || IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_ListenerProperties))
    {
        DSPrimary *prim = This->primary;
        BOOL immediate = !(dwPropID&0x80000000ul);
        ALenum err;

        setALContext(prim->ctx);

        /* If deferred settings are being committed, defer OpenAL updates so
         * both the EAX and standard properties get batched together.
         * CommitDeferredSettings will apply and process updates.
         */
        if(immediate) alDeferUpdatesSOFT();
        err = EAXSet(guidPropSet, dwPropID, This->source, pPropData, cbPropData);
        if(err != AL_NO_ERROR) hr = E_FAIL;
        else hr = DS_OK;

        if(immediate)
        {
            if(hr == DS_OK)
            {
                /* CommitDeferredSettings will call alProcessUpdatesSOFT. */
                DSPrimary3D_CommitDeferredSettings(&prim->IDirectSound3DListener_iface);
            }
            else
                alProcessUpdatesSOFT();
        }

        popALContext();
    }
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_VoiceManager))
    {
        hr = VoiceMan_Set(This, dwPropID, pPropData, cbPropData);
    }
    else
        FIXME("Unhandled propset: %s\n", debug_bufferprop(guidPropSet));
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DSBufferProp_QuerySupport(IKsPropertySet *iface,
  REFGUID guidPropSet, ULONG dwPropID,
  ULONG *pTypeSupport)
{
    DSBuffer *This = impl_from_IKsPropertySet(iface);
    HRESULT hr = E_PROP_ID_UNSUPPORTED;

    TRACE("(%p)->(%s, 0x%lx, %p)\n", iface, debug_bufferprop(guidPropSet), dwPropID,
          pTypeSupport);

    if(!pTypeSupport)
        return E_POINTER;
    *pTypeSupport = 0;

    EnterCriticalSection(&This->share->crst);
    if(IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Source))
        hr = EAX4Source_Query(This, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_BufferProperties))
        hr = EAX3Buffer_Query(This, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_BufferProperties))
        hr = EAX2Buffer_Query(This, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot0)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot1)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot2)
        || IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_FXSlot3))
        hr = EAX4Slot_Query(This->primary, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX30_ListenerProperties))
        hr = EAX3_Query(This->primary, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX20_ListenerProperties))
        hr = EAX2_Query(This->primary, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &EAXPROPERTYID_EAX40_Context))
        hr = EAX4Context_Query(This->primary, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_ListenerProperties))
        hr = EAX1_Query(This->primary, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_EAX10_BufferProperties))
        hr = EAX1Buffer_Query(This, dwPropID, pTypeSupport);
    else if(IsEqualIID(guidPropSet, &DSPROPSETID_VoiceManager))
        hr = VoiceMan_Query(This, dwPropID, pTypeSupport);
    else
        FIXME("Unhandled propset: %s (propid: %lu)\n", debug_bufferprop(guidPropSet), dwPropID);
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static IKsPropertySetVtbl DSBufferProp_Vtbl =
{
    DSBufferProp_QueryInterface,
    DSBufferProp_AddRef,
    DSBufferProp_Release,
    DSBufferProp_Get,
    DSBufferProp_Set,
    DSBufferProp_QuerySupport
};
