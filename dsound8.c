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
#include <ks.h>
#include <ksmedia.h>

#include "dsound_private.h"

#ifndef DSSPEAKER_7POINT1
#define DSSPEAKER_7POINT1       7
#endif


static DWORD CALLBACK DSShare_thread(void *dwUser)
{
    DeviceShare *share = (DeviceShare*)dwUser;
    BYTE *scratch_mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2048);
    ALsizei i;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    TRACE("Shared device (%p) message loop start\n", share);
    while(WaitForSingleObject(share->timer_evt, INFINITE) == WAIT_OBJECT_0 && !share->quit_now)
    {
        EnterCriticalSection(&share->crst);
        setALContext(share->ctx);

        for(i = 0;i < share->nprimaries;++i)
        {
            DSPrimary_triggernots(share->primaries[i]);
            if(!HAS_EXTENSION(share, SOFTX_MAP_BUFFER))
                DSPrimary_streamfeeder(share->primaries[i], scratch_mem);
        }

        popALContext();
        LeaveCriticalSection(&share->crst);
    }
    TRACE("Shared device (%p) message loop quit\n", share);

    HeapFree(GetProcessHeap(), 0, scratch_mem);
    scratch_mem = NULL;

    if(local_contexts)
    {
        set_context(NULL);
        TlsSetValue(TlsThreadPtr, NULL);
    }

    return 0;
}

static void CALLBACK DSShare_timer(void *arg, BOOLEAN unused)
{
    (void)unused;
    SetEvent((HANDLE)arg);
}

static void DSShare_starttimer(DeviceShare *share)
{
    DWORD triggertime;

    if(share->queue_timer)
        return;

    triggertime = 1000 / share->refresh * 2 / 3;
    TRACE("Calling timer every %lu ms for %d refreshes per second\n",
          triggertime, share->refresh);

    CreateTimerQueueTimer(&share->queue_timer, NULL, DSShare_timer, share->timer_evt,
                          triggertime, triggertime, WT_EXECUTEINTIMERTHREAD);
}



static DeviceShare **sharelist;
static UINT sharelistsize;

static void DSShare_Destroy(DeviceShare *share)
{
    UINT i;

    EnterCriticalSection(&openal_crst);
    for(i = 0;i < sharelistsize;i++)
    {
        if(sharelist[i] == share)
        {
            sharelist[i] = sharelist[--sharelistsize];
            if(sharelistsize == 0)
            {
                HeapFree(GetProcessHeap(), 0, sharelist);
                sharelist = NULL;
            }
            break;
        }
    }
    LeaveCriticalSection(&openal_crst);

    if(share->queue_timer)
        DeleteTimerQueueTimer(NULL, share->queue_timer, INVALID_HANDLE_VALUE);
    share->queue_timer = NULL;

    if(share->thread_hdl)
    {
        InterlockedExchange(&share->quit_now, TRUE);
        SetEvent(share->timer_evt);

        if(WaitForSingleObject(share->thread_hdl, 1000) != WAIT_OBJECT_0)
            ERR("Thread wait timed out\n");

        CloseHandle(share->thread_hdl);
        share->thread_hdl = NULL;
    }

    if(share->timer_evt)
        CloseHandle(share->timer_evt);
    share->timer_evt = NULL;

    if(share->ctx)
    {
        /* Calling setALContext is not appropriate here, since we *have* to
         * unset the context before destroying it
         */
        EnterCriticalSection(&openal_crst);
        set_context(share->ctx);

        share->sources.maxhw_alloc = share->sources.maxsw_alloc = 0;

        set_context(NULL);
        TlsSetValue(TlsThreadPtr, NULL);
        alcDestroyContext(share->ctx);
        share->ctx = NULL;
        LeaveCriticalSection(&openal_crst);
    }

    if(share->device)
        alcCloseDevice(share->device);
    share->device = NULL;

    DeleteCriticalSection(&share->crst);

    HeapFree(GetProcessHeap(), 0, share->primaries);
    HeapFree(GetProcessHeap(), 0, share);

    TRACE("Closed shared device %p\n", share);
}

static HRESULT DSShare_Create(REFIID guid, DeviceShare **out)
{
    static const struct {
        const char extname[64];
        int extenum;
    } extensions[MAX_EXTENSIONS] = {
        { "EAX5.0", EXT_EAX },
        { "AL_EXT_FLOAT32",   EXT_FLOAT32 },
        { "AL_EXT_MCFORMATS", EXT_MCFORMATS },
        { "AL_SOFT_deferred_updates",  SOFT_DEFERRED_UPDATES },
        { "AL_SOFT_source_spatialize", SOFT_SOURCE_SPATIALIZE },
        { "AL_SOFTX_map_buffer",       SOFTX_MAP_BUFFER },
    };
    OLECHAR *guid_str = NULL;
    ALchar drv_name[64];
    DeviceShare *share;
    IMMDevice *mmdev;
    ALCint attrs[7];
    void *temp;
    HRESULT hr, cohr;
    ALsizei i;

    share = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*share));
    if(!share) return DSERR_OUTOFMEMORY;
    share->ref = 1;
    share->refresh = FAKE_REFRESH_COUNT;
    share->speaker_config = DSSPEAKER_7POINT1_SURROUND;
    share->vm_managermode = DSPROPERTY_VMANAGER_MODE_DEFAULT;

    TRACE("Creating shared device %p\n", share);

    cohr = get_mmdevice(eRender, guid, &mmdev);
    if(!mmdev)
        hr = DSERR_INVALIDPARAM;
    else
    {
        IPropertyStore *store;

        hr = IMMDevice_OpenPropertyStore(mmdev, STGM_READ, &store);
        if(FAILED(hr))
            WARN("IMMDevice_OpenPropertyStore failed: %08lx\n", hr);
        else
        {
            ULONG phys_speakers = 0;
            PROPVARIANT pv;

            PropVariantInit(&pv);

            hr = IPropertyStore_GetValue(store, &PKEY_AudioEndpoint_PhysicalSpeakers, &pv);
            if(FAILED(hr))
                WARN("IPropertyStore_GetValue failed: %08lx\n", hr);
            else if(pv.vt != VT_UI4)
                WARN("PKEY_AudioEndpoint_PhysicalSpeakers is not a ULONG: 0x%04x\n", pv.vt);
            else
            {
                phys_speakers = pv.ulVal;

#define BIT_MATCH(v, b) (((v)&(b)) == (b))
                if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_7POINT1))
                    share->speaker_config = DSSPEAKER_7POINT1;
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_7POINT1_SURROUND))
                    share->speaker_config = DSSPEAKER_7POINT1_SURROUND;
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_5POINT1))
                    share->speaker_config = DSSPEAKER_5POINT1_BACK;
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_5POINT1_SURROUND))
                    share->speaker_config = DSSPEAKER_5POINT1_SURROUND;
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_QUAD))
                    share->speaker_config = DSSPEAKER_QUAD;
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_STEREO))
                    share->speaker_config = DSSPEAKER_COMBINED(DSSPEAKER_STEREO, DSSPEAKER_GEOMETRY_WIDE);
                else if(BIT_MATCH(phys_speakers, KSAUDIO_SPEAKER_MONO))
                    share->speaker_config = DSSPEAKER_MONO;
                else
                    FIXME("Unhandled physical speaker layout: 0x%08lx\n", phys_speakers);
#undef BIT_MATCH
            }

            /* If the device has a stereo layout, check the formfactor to see
             * if it's really headphones/headset.
             */
            if(DSSPEAKER_CONFIG(share->speaker_config) == DSSPEAKER_STEREO)
            {
                hr = IPropertyStore_GetValue(store, &PKEY_AudioEndpoint_FormFactor, &pv);
                if(FAILED(hr))
                    WARN("IPropertyStore_GetValue failed: %08lx\n", hr);
                else if(pv.vt != VT_UI4)
                    WARN("PKEY_AudioEndpoint_FormFactor is not a ULONG: 0x%04x\n", pv.vt);
                else
                {
                    if(pv.ulVal == Headphones || pv.ulVal == Headset)
                        share->speaker_config = DSSPEAKER_HEADPHONE;
                }
            }

            TRACE("Got speaker config %d:%d from physical speakers 0x%08lx\n",
                  DSSPEAKER_GEOMETRY(share->speaker_config),
                  DSSPEAKER_CONFIG(share->speaker_config), phys_speakers);

            PropVariantClear(&pv);
            IPropertyStore_Release(store);
        }

        release_mmdevice(mmdev, cohr);
        mmdev = NULL;
    }

    InitializeCriticalSection(&share->crst);

    hr = StringFromCLSID(guid, &guid_str);
    if(FAILED(hr))
    {
        ERR("Failed to convert GUID to string\n");
        goto fail;
    }
    WideCharToMultiByte(CP_UTF8, 0, guid_str, -1, drv_name, sizeof(drv_name), NULL, NULL);
    drv_name[sizeof(drv_name)-1] = 0;
    CoTaskMemFree(guid_str);
    guid_str = NULL;

    hr = DSERR_NODRIVER;
    share->device = alcOpenDevice(drv_name);
    if(!share->device)
    {
        alcGetError(NULL);
        WARN("Couldn't open device \"%s\"\n", drv_name);
        goto fail;
    }
    TRACE("Opened AL device: %s\n",
          alcIsExtensionPresent(share->device, "ALC_ENUMERATE_ALL_EXT") ?
          alcGetString(share->device, ALC_ALL_DEVICES_SPECIFIER) :
          alcGetString(share->device, ALC_DEVICE_SPECIFIER));

    i = 0;
    attrs[i++] = ALC_MONO_SOURCES;
    attrs[i++] = MAX_SOURCES;
    attrs[i++] = ALC_STEREO_SOURCES;
    attrs[i++] = 0;
    attrs[i++] = 0;
    share->ctx = alcCreateContext(share->device, attrs);
    if(!share->ctx)
    {
        ALCenum err = alcGetError(share->device);
        ERR("Could not create context (0x%x)!\n", err);
        goto fail;
    }

    share->guid = *guid;

    setALContext(share->ctx);
    alcGetIntegerv(share->device, ALC_REFRESH, 1, &share->refresh);
    checkALCError(share->device);

    for(i = 0;i < MAX_EXTENSIONS;i++)
    {
        if((strncmp(extensions[i].extname, "ALC", 3) == 0) ?
           alcIsExtensionPresent(share->device, extensions[i].extname) :
           alIsExtensionPresent(extensions[i].extname))
        {
            TRACE("Found %s\n", extensions[i].extname);
            BITFIELD_SET(share->Exts, extensions[i].extenum);
        }
    }

    alcGetIntegerv(share->device, ALC_MONO_SOURCES, 1, &attrs[0]);
    alcGetIntegerv(share->device, ALC_STEREO_SOURCES, 1, &attrs[1]);
    checkALCError(share->device);
    share->sources.maxhw_alloc = (DWORD)attrs[0] + (DWORD)attrs[1];
    popALContext();

    hr = E_OUTOFMEMORY;
    if(share->sources.maxhw_alloc > MAX_SOURCES)
        share->sources.maxhw_alloc = MAX_SOURCES;
    else if(share->sources.maxhw_alloc < 128)
    {
        ERR("Could only allocate %lu sources (minimum 128 required)\n",
            share->sources.maxhw_alloc);
        goto fail;
    }

    if(share->sources.maxhw_alloc > MAX_HWBUFFERS)
    {
        share->sources.maxsw_alloc = share->sources.maxhw_alloc - MAX_HWBUFFERS;
        share->sources.maxhw_alloc = MAX_HWBUFFERS;
    }
    else if(share->sources.maxhw_alloc > MAX_HWBUFFERS/2)
    {
        share->sources.maxsw_alloc = share->sources.maxhw_alloc - MAX_HWBUFFERS/2;
        share->sources.maxhw_alloc = MAX_HWBUFFERS/2;
    }
    else
    {
        share->sources.maxsw_alloc = share->sources.maxhw_alloc - MAX_HWBUFFERS/4;
        share->sources.maxhw_alloc = MAX_HWBUFFERS/4;
    }
    share->sources.availhw_num = share->sources.maxhw_alloc;
    share->sources.availsw_num = share->sources.maxsw_alloc;
    TRACE("Allocated %lu hardware sources and %lu software sources\n",
          share->sources.maxhw_alloc, share->sources.maxsw_alloc);

    if(sharelist)
        temp = HeapReAlloc(GetProcessHeap(), 0, sharelist, sizeof(*sharelist)*(sharelistsize+1));
    else
        temp = HeapAlloc(GetProcessHeap(), 0, sizeof(*sharelist)*(sharelistsize+1));
    if(temp)
    {
        sharelist = temp;
        sharelist[sharelistsize++] = share;
    }

    hr = E_FAIL;

    share->quit_now = FALSE;
    share->timer_evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if(!share->timer_evt) goto fail;

    share->queue_timer = NULL;

    share->thread_hdl = CreateThread(NULL, 0, DSShare_thread, share, 0, &share->thread_id);
    if(!share->thread_hdl) goto fail;

    DSShare_starttimer(share);

    *out = share;
    return DS_OK;

fail:
    DSShare_Destroy(share);
    return hr;
}

static ULONG DSShare_AddRef(DeviceShare *share)
{
    ULONG ref = InterlockedIncrement(&share->ref);
    return ref;
}

static ULONG DSShare_Release(DeviceShare *share)
{
    ULONG ref = InterlockedDecrement(&share->ref);
    if(ref == 0) DSShare_Destroy(share);
    return ref;
}


static IDirectSound8Vtbl DS8_Vtbl;
static IDirectSoundVtbl DS_Vtbl;
static IUnknownVtbl DS8_Unknown_Vtbl;

static HRESULT DSDevice_Create(BOOL is8, REFIID riid, LPVOID *ds);
static void DSDevice_Destroy(DSDevice *This);
static HRESULT DSDevice_GetInterface(DSDevice *This, REFIID riid, LPVOID *ppv);

/*******************************************************************************
 * IUnknown
 */
static inline DSDevice *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, DSDevice, IUnknown_iface);
}

static HRESULT WINAPI DSDevice_IUnknown_QueryInterface(IUnknown *iface, REFIID riid, void **ppobj)
{
    DSDevice *This = impl_from_IUnknown(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppobj);
    return DSDevice_GetInterface(This, riid, ppobj);
}

static ULONG WINAPI DSDevice_IUnknown_AddRef(IUnknown *iface)
{
    DSDevice *This = impl_from_IUnknown(iface);
    ULONG ref;

    InterlockedIncrement(&(This->ref));
    ref = InterlockedIncrement(&(This->unkref));
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI DSDevice_IUnknown_Release(IUnknown *iface)
{
    DSDevice *This = impl_from_IUnknown(iface);
    ULONG ref = InterlockedDecrement(&(This->unkref));
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&(This->ref)) == 0)
        DSDevice_Destroy(This);
    return ref;
}

static IUnknownVtbl DS8_Unknown_Vtbl = {
    DSDevice_IUnknown_QueryInterface,
    DSDevice_IUnknown_AddRef,
    DSDevice_IUnknown_Release
};


static inline DSDevice *impl_from_IDirectSound8(IDirectSound8 *iface)
{
    return CONTAINING_RECORD(iface, DSDevice, IDirectSound8_iface);
}

static inline DSDevice *impl_from_IDirectSound(IDirectSound *iface)
{
    return CONTAINING_RECORD(iface, DSDevice, IDirectSound_iface);
}


HRESULT DSOUND_Create(REFIID riid, void **ds)
{ return DSDevice_Create(FALSE, riid, ds); }

HRESULT DSOUND_Create8(REFIID riid, LPVOID *ds)
{ return DSDevice_Create(TRUE, riid, ds); }

static HRESULT DSDevice_Create(BOOL is8, REFIID riid, LPVOID *ds)
{
    DSDevice *This;
    HRESULT hr;

    *ds = NULL;
    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This) return DSERR_OUTOFMEMORY;

    TRACE("Creating device instance %p\n", This);
    This->IDirectSound8_iface.lpVtbl = &DS8_Vtbl;
    This->IDirectSound_iface.lpVtbl = &DS_Vtbl;
    This->IUnknown_iface.lpVtbl = &DS8_Unknown_Vtbl;

    This->is_8 = is8;

    hr = DSDevice_GetInterface(This, riid, ds);
    if(FAILED(hr)) DSDevice_Destroy(This);
    return hr;
}

static void DSDevice_Destroy(DSDevice *This)
{
    DeviceShare *share = This->share;

    TRACE("Destroying device instance %p\n", This);
    if(share)
    {
        ALsizei i;

        EnterCriticalSection(&share->crst);

        for(i = 0;i < share->nprimaries;++i)
        {
            if(share->primaries[i] == &This->primary)
            {
                share->nprimaries -= 1;
                share->primaries[i] = share->primaries[share->nprimaries];
                break;
            }
        }

        LeaveCriticalSection(&share->crst);
    }

    DSPrimary_Clear(&This->primary);
    if(This->share)
        DSShare_Release(This->share);
    This->share = NULL;

    HeapFree(GetProcessHeap(), 0, This);
}

static HRESULT DSDevice_GetInterface(DSDevice *This, REFIID riid, LPVOID *ppv)
{
    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown))
        *ppv = &This->IUnknown_iface;
    else if(IsEqualIID(riid, &IID_IDirectSound8))
    {
        if(This->is_8)
            *ppv = &This->IDirectSound8_iface;
    }
    else if(IsEqualIID(riid, &IID_IDirectSound))
        *ppv = &This->IDirectSound_iface;
    else
        FIXME("Unhandled GUID: %s\n", debugstr_guid(riid));

    if(*ppv)
    {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    return E_NOINTERFACE;
}


static HRESULT WINAPI DS8_QueryInterface(IDirectSound8 *iface, REFIID riid, LPVOID *ppv)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSDevice_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DS8_AddRef(IDirectSound8 *iface)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    LONG ref;

    InterlockedIncrement(&This->ref);
    ref = InterlockedIncrement(&This->dsref);
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI DS8_Release(IDirectSound8 *iface)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->dsref);
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&This->ref) == 0)
        DSDevice_Destroy(This);

    return ref;
}

static HRESULT WINAPI DS8_CreateSoundBuffer(IDirectSound8 *iface, LPCDSBUFFERDESC desc, LPLPDIRECTSOUNDBUFFER buf, IUnknown *pUnkOuter)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p)\n", iface, desc, buf, pUnkOuter);

    if(!buf)
    {
        WARN("buf is null\n");
        return DSERR_INVALIDPARAM;
    }
    *buf = NULL;

    if(pUnkOuter)
    {
        WARN("Aggregation isn't supported\n");
        return DSERR_NOAGGREGATION;
    }
    if(!desc || desc->dwSize < sizeof(DSBUFFERDESC1))
    {
        WARN("Invalid buffer %p/%lu\n", desc, desc?desc->dwSize:0);
        return DSERR_INVALIDPARAM;
    }

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    TRACE("Requested buffer:\n"
          "    Size        = %lu\n"
          "    Flags       = 0x%08lx\n"
          "    BufferBytes = %lu\n",
          desc->dwSize, desc->dwFlags, desc->dwBufferBytes);

    if(desc->dwSize >= sizeof(DSBUFFERDESC))
    {
        if(!(desc->dwFlags&DSBCAPS_CTRL3D))
        {
            if(!IsEqualGUID(&desc->guid3DAlgorithm, &GUID_NULL))
            {
                /* Not fatal. Some apps pass unknown values here. */
                WARN("Unknown 3D algorithm GUID specified for non-3D buffer: %s\n",
                    debugstr_guid(&desc->guid3DAlgorithm));
            }
        }
        else
            TRACE("Requested 3D algorithm GUID: %s\n", debugstr_guid(&desc->guid3DAlgorithm));
    }

    /* OpenAL doesn't support playing with 3d and panning at same time.. */
    if((desc->dwFlags&(DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN)) == (DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN))
    {
        if(!This->is_8)
        {
            static int once = 0;
            if(!once++)
                FIXME("Buffers with 3D and panning control ignore panning\n");
        }
        else
        {
            WARN("Cannot create buffers with 3D and panning control\n");
            return DSERR_INVALIDPARAM;
        }
    }

    EnterCriticalSection(&This->share->crst);
    if((desc->dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        IDirectSoundBuffer *prim = &This->primary.IDirectSoundBuffer_iface;

        hr = S_OK;
        if(IDirectSoundBuffer_AddRef(prim) == 1)
        {
            hr = DSPrimary_Initialize(prim, &This->IDirectSound_iface, desc);
            if(FAILED(hr))
            {
                IDirectSoundBuffer_Release(prim);
                prim = NULL;
            }
        }
        *buf = prim;
    }
    else
    {
        DSBuffer *dsb;

        hr = DSBuffer_Create(&dsb, &This->primary, NULL);
        if(SUCCEEDED(hr))
        {
            hr = DSBuffer_Initialize(&dsb->IDirectSoundBuffer8_iface, &This->IDirectSound_iface, desc);
            if(SUCCEEDED(hr))
            {
                dsb->bufferlost = (This->prio_level == DSSCL_WRITEPRIMARY);
                hr = DSBuffer_GetInterface(dsb, &IID_IDirectSoundBuffer, (void**)buf);
            }
            if(FAILED(hr))
                DSBuffer_Destroy(dsb);
        }
    }
    LeaveCriticalSection(&This->share->crst);

    TRACE("%08lx\n", hr);
    return hr;
}

static HRESULT WINAPI DS8_GetCaps(IDirectSound8 *iface, LPDSCAPS caps)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    struct DSBufferGroup *bufgroup, *endgroup;
    DWORD free_bufs;

    TRACE("(%p)->(%p)\n", iface, caps);

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!caps || caps->dwSize < sizeof(*caps))
    {
        WARN("Invalid DSCAPS (%p, %lu)\n", caps, (caps?caps->dwSize:0));
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);

    free_bufs = This->share->sources.maxhw_alloc;
    bufgroup = This->primary.BufferGroups;
    endgroup = bufgroup + This->primary.NumBufferGroups;
    for(;free_bufs && bufgroup != endgroup;++bufgroup)
    {
        DWORD64 usemask = ~bufgroup->FreeBuffers;
        while(usemask)
        {
            int idx = CTZ64(usemask);
            DSBuffer *buf = bufgroup->Buffers + idx;
            usemask &= ~(U64(1) << idx);

            if(buf->loc_status == DSBSTATUS_LOCHARDWARE)
            {
                if(!--free_bufs)
                    break;
            }
        }
    }

    caps->dwFlags = DSCAPS_CONTINUOUSRATE | DSCAPS_CERTIFIED |
                    DSCAPS_PRIMARY16BIT | DSCAPS_PRIMARYSTEREO |
                    DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARYMONO |
                    DSCAPS_SECONDARY16BIT | DSCAPS_SECONDARY8BIT |
                    DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO;
    caps->dwPrimaryBuffers = 1;
    caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
    caps->dwMaxHwMixingAllBuffers =
        caps->dwMaxHwMixingStaticBuffers =
        caps->dwMaxHwMixingStreamingBuffers =
        caps->dwMaxHw3DAllBuffers =
        caps->dwMaxHw3DStaticBuffers =
        caps->dwMaxHw3DStreamingBuffers = This->share->sources.maxhw_alloc;
    caps->dwFreeHwMixingAllBuffers =
        caps->dwFreeHwMixingStaticBuffers =
        caps->dwFreeHwMixingStreamingBuffers =
        caps->dwFreeHw3DAllBuffers =
        caps->dwFreeHw3DStaticBuffers =
        caps->dwFreeHw3DStreamingBuffers = free_bufs;
    caps->dwTotalHwMemBytes =
        caps->dwFreeHwMemBytes = 64 * 1024 * 1024;
    caps->dwMaxContigFreeHwMemBytes = caps->dwFreeHwMemBytes;
    caps->dwUnlockTransferRateHwBuffers = 4096;
    caps->dwPlayCpuOverheadSwBuffers = 0;

    LeaveCriticalSection(&This->share->crst);

    return DS_OK;
}
static HRESULT WINAPI DS8_DuplicateSoundBuffer(IDirectSound8 *iface, IDirectSoundBuffer *in, IDirectSoundBuffer **out)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    DSBuffer *buf = NULL;
    DSBCAPS caps;
    HRESULT hr;

    TRACE("(%p)->(%p, %p)\n", iface, in, out);

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(!in || !out)
    {
        WARN("Invalid pointer: in = %p, out = %p\n", in, out);
        return DSERR_INVALIDPARAM;
    }
    *out = NULL;

    caps.dwSize = sizeof(caps);
    hr = IDirectSoundBuffer_GetCaps(in, &caps);
    if(SUCCEEDED(hr) && (caps.dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        WARN("Cannot duplicate buffer %p, which has DSBCAPS_PRIMARYBUFFER\n", in);
        hr = DSERR_INVALIDPARAM;
    }
    if(SUCCEEDED(hr) && (caps.dwFlags&DSBCAPS_CTRLFX))
    {
        WARN("Cannot duplicate buffer %p, which has DSBCAPS_CTRLFX\n", in);
        hr = DSERR_INVALIDPARAM;
    }
    if(SUCCEEDED(hr))
        hr = DSBuffer_Create(&buf, &This->primary, in);
    if(SUCCEEDED(hr))
    {
        hr = DSBuffer_Initialize(&buf->IDirectSoundBuffer8_iface, NULL, NULL);
        if(SUCCEEDED(hr))
            hr = DSBuffer_GetInterface(buf, &IID_IDirectSoundBuffer, (void**)out);
        if(FAILED(hr))
            DSBuffer_Destroy(buf);
    }

    return hr;
}

static HRESULT WINAPI DS8_SetCooperativeLevel(IDirectSound8 *iface, HWND hwnd, DWORD level)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p, %lu)\n", iface, hwnd, level);

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    if(level > DSSCL_WRITEPRIMARY || level < DSSCL_NORMAL)
    {
        WARN("Invalid coop level: %lu\n", level);
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&This->share->crst);
    if(level == DSSCL_WRITEPRIMARY && (This->prio_level != DSSCL_WRITEPRIMARY))
    {
        struct DSBufferGroup *bufgroup = This->primary.BufferGroups;
        DWORD i, state;

        if(This->primary.write_emu)
        {
            ERR("Why was there a write_emu?\n");
            /* Delete it */
            IDirectSoundBuffer_Release(This->primary.write_emu);
            This->primary.write_emu = NULL;
        }

        for(i = 0;i < This->primary.NumBufferGroups;++i)
        {
            DWORD64 usemask = ~bufgroup[i].FreeBuffers;
            while(usemask)
            {
                int idx = CTZ64(usemask);
                DSBuffer *buf = bufgroup[i].Buffers + idx;
                usemask &= ~(U64(1) << idx);

                if(FAILED(DSBuffer_GetStatus(&buf->IDirectSoundBuffer8_iface, &state)) ||
                   (state&DSBSTATUS_PLAYING))
                {
                    WARN("DSSCL_WRITEPRIMARY set with playing buffers!\n");
                    hr = DSERR_INVALIDCALL;
                    goto out;
                }
                /* Mark buffer as lost */
                buf->bufferlost = 1;
            }
        }

        if(This->primary.flags)
        {
            /* Primary has open references.. create write_emu */
            DSBUFFERDESC desc;
            DSBuffer *emu;

            memset(&desc, 0, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = DSBCAPS_LOCHARDWARE | (This->primary.flags&DSBCAPS_CTRLPAN);
            desc.dwBufferBytes = This->primary.buf_size;
            desc.lpwfxFormat = &This->primary.format.Format;

            hr = DSBuffer_Create(&emu, &This->primary, NULL);
            if(SUCCEEDED(hr))
            {
                hr = DSBuffer_Initialize(&emu->IDirectSoundBuffer8_iface,
                                         &This->IDirectSound_iface, &desc);
                if(SUCCEEDED(hr))
                    hr = DSBuffer_GetInterface(emu, &IID_IDirectSoundBuffer,
                                               (void**)&This->primary.write_emu);
                if(FAILED(hr))
                    DSBuffer_Destroy(emu);
            }
        }
    }
    else if(This->prio_level == DSSCL_WRITEPRIMARY && level != DSSCL_WRITEPRIMARY)
    {
        /* Delete it */
        TRACE("Nuking write_emu\n");
        if(This->primary.write_emu)
            IDirectSoundBuffer_Release(This->primary.write_emu);
        This->primary.write_emu = NULL;
    }
    if(SUCCEEDED(hr))
        This->prio_level = level;
out:
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DS8_Compact(IDirectSound8 *iface)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    EnterCriticalSection(&This->share->crst);
    if(This->prio_level < DSSCL_PRIORITY)
    {
        WARN("Coop level not high enough (%lu)\n", This->prio_level);
        hr = DSERR_PRIOLEVELNEEDED;
    }
    LeaveCriticalSection(&This->share->crst);

    return hr;
}

static HRESULT WINAPI DS8_GetSpeakerConfig(IDirectSound8 *iface, DWORD *config)
{
    DSDevice *This = impl_from_IDirectSound8(iface);

    TRACE("(%p)->(%p)\n", iface, config);

    if(!config)
        return DSERR_INVALIDPARAM;
    *config = 0;

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    *config = This->share->speaker_config;

    return DS_OK;
}

static HRESULT WINAPI DS8_SetSpeakerConfig(IDirectSound8 *iface, DWORD config)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    DWORD geo, speaker;

    TRACE("(%p)->(0x%08lx)\n", iface, config);

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    geo = DSSPEAKER_GEOMETRY(config);
    speaker = DSSPEAKER_CONFIG(config);

    if(geo && (geo < DSSPEAKER_GEOMETRY_MIN || geo > DSSPEAKER_GEOMETRY_MAX))
    {
        WARN("Invalid speaker angle %lu\n", geo);
        return DSERR_INVALIDPARAM;
    }
    if(speaker < DSSPEAKER_HEADPHONE || speaker > DSSPEAKER_7POINT1)
    {
        WARN("Invalid speaker config %lu\n", speaker);
        return DSERR_INVALIDPARAM;
    }

    /* No-op on Vista+. */
    return DS_OK;
}

static HRESULT WINAPI DS8_Initialize(IDirectSound8 *iface, const GUID *devguid)
{
    DSDevice *This = impl_from_IDirectSound8(iface);
    HRESULT hr;
    GUID guid;
    UINT n;

    TRACE("(%p)->(%s)\n", iface, debugstr_guid(devguid));

    if(!openal_loaded)
        return DSERR_NODRIVER;

    if(This->share)
    {
        WARN("Device already initialized\n");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!devguid || IsEqualGUID(devguid, &GUID_NULL))
        devguid = &DSDEVID_DefaultPlayback;
    else if(IsEqualGUID(devguid, &DSDEVID_DefaultCapture) ||
            IsEqualGUID(devguid, &DSDEVID_DefaultVoiceCapture))
        return DSERR_NODRIVER;

    hr = DSOAL_GetDeviceID(devguid, &guid);
    if(FAILED(hr)) return hr;

    EnterCriticalSection(&openal_crst);

    TRACE("Searching shared devices for %s\n", debugstr_guid(&guid));
    for(n = 0;n < sharelistsize;n++)
    {
        if(IsEqualGUID(&sharelist[n]->guid, &guid))
        {
            TRACE("Matched shared device %p\n", sharelist[n]);

            DSShare_AddRef(sharelist[n]);
            This->share = sharelist[n];
            break;
        }
    }

    if(!This->share)
        hr = DSShare_Create(&guid, &This->share);
    if(SUCCEEDED(hr))
    {
        This->device = This->share->device;
        hr = DSPrimary_PreInit(&This->primary, This);
    }

    if(SUCCEEDED(hr))
    {
        DeviceShare *share = This->share;
        DSPrimary **prims;

        EnterCriticalSection(&share->crst);

        prims = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                          (share->nprimaries+1) * sizeof(*prims));
        if(!prims)
            hr = DSERR_OUTOFMEMORY;
        else
        {
            ALsizei i;
            for(i = 0;i < share->nprimaries;++i)
                prims[i] = share->primaries[i];
            prims[i] = &This->primary;

            HeapFree(GetProcessHeap(), 0, share->primaries);
            share->primaries = prims;
            share->nprimaries += 1;
        }

        LeaveCriticalSection(&share->crst);
    }

    if(FAILED(hr))
    {
        if(This->share)
            DSShare_Release(This->share);
        This->share = NULL;
    }

    LeaveCriticalSection(&openal_crst);
    return hr;
}

/* I, Maarten Lankhorst, hereby declare this driver certified
 * What this means.. ? An extra bit set
 */
static HRESULT WINAPI DS8_VerifyCertification(IDirectSound8 *iface, DWORD *certified)
{
    DSDevice *This = impl_from_IDirectSound8(iface);

    TRACE("(%p)->(%p)\n", iface, certified);

    if(!certified)
        return DSERR_INVALIDPARAM;
    *certified = 0;

    if(!This->share)
    {
        WARN("Device not initialized\n");
        return DSERR_UNINITIALIZED;
    }

    *certified = DS_CERTIFIED;

    return DS_OK;
}

static IDirectSound8Vtbl DS8_Vtbl = {
    DS8_QueryInterface,
    DS8_AddRef,
    DS8_Release,
    DS8_CreateSoundBuffer,
    DS8_GetCaps,
    DS8_DuplicateSoundBuffer,
    DS8_SetCooperativeLevel,
    DS8_Compact,
    DS8_GetSpeakerConfig,
    DS8_SetSpeakerConfig,
    DS8_Initialize,
    DS8_VerifyCertification
};


static HRESULT WINAPI DS_QueryInterface(IDirectSound *iface, REFIID riid, LPVOID *ppv)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);
    return DSDevice_GetInterface(This, riid, ppv);
}

static ULONG WINAPI DS_AddRef(IDirectSound *iface)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_AddRef(&This->IDirectSound8_iface);
}

static ULONG WINAPI DS_Release(IDirectSound *iface)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_Release(&This->IDirectSound8_iface);
}

static HRESULT WINAPI DS_CreateSoundBuffer(IDirectSound *iface, LPCDSBUFFERDESC desc, LPLPDIRECTSOUNDBUFFER buf, IUnknown *pUnkOuter)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_CreateSoundBuffer(&This->IDirectSound8_iface, desc, buf, pUnkOuter);
}

static HRESULT WINAPI DS_GetCaps(IDirectSound *iface, LPDSCAPS caps)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_GetCaps(&This->IDirectSound8_iface, caps);
}
static HRESULT WINAPI DS_DuplicateSoundBuffer(IDirectSound *iface, IDirectSoundBuffer *in, IDirectSoundBuffer **out)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_DuplicateSoundBuffer(&This->IDirectSound8_iface, in, out);
}

static HRESULT WINAPI DS_SetCooperativeLevel(IDirectSound *iface, HWND hwnd, DWORD level)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_SetCooperativeLevel(&This->IDirectSound8_iface, hwnd, level);
}

static HRESULT WINAPI DS_Compact(IDirectSound *iface)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_Compact(&This->IDirectSound8_iface);
}

static HRESULT WINAPI DS_GetSpeakerConfig(IDirectSound *iface, DWORD *config)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_GetSpeakerConfig(&This->IDirectSound8_iface, config);
}

static HRESULT WINAPI DS_SetSpeakerConfig(IDirectSound *iface, DWORD config)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_SetSpeakerConfig(&This->IDirectSound8_iface, config);
}

static HRESULT WINAPI DS_Initialize(IDirectSound *iface, const GUID *devguid)
{
    DSDevice *This = impl_from_IDirectSound(iface);
    return DS8_Initialize(&This->IDirectSound8_iface, devguid);
}

static IDirectSoundVtbl DS_Vtbl = {
    DS_QueryInterface,
    DS_AddRef,
    DS_Release,
    DS_CreateSoundBuffer,
    DS_GetCaps,
    DS_DuplicateSoundBuffer,
    DS_SetCooperativeLevel,
    DS_Compact,
    DS_GetSpeakerConfig,
    DS_SetSpeakerConfig,
    DS_Initialize
};
