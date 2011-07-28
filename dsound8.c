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
#include "mmsystem.h"
#include "winternl.h"
#include "mmddk.h"
#include "wine/debug.h"
#include "dsound.h"

#include "dsound_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsound);

#else

#define WINVER 0x6100
#include <windows.h>
#include <dsound.h>

#include "dsound_private.h"

#ifndef DSSPEAKER_7POINT1
#define DSSPEAKER_7POINT1       7
#endif

#endif

static DS8Impl **devicelist;
static UINT devicelistsize;

static const IDirectSound8Vtbl DS8_Vtbl;

HRESULT DSOUND_Create(REFIID riid, LPVOID *ds)
{
    HRESULT hr;

    if(IsEqualIID(riid, &IID_IDirectSound8))
        return E_NOINTERFACE;
    hr = DSOUND_Create8(riid, ds);
    if(hr == S_OK)
        ((DS8Impl*)*ds)->is_8 = FALSE;
    return hr;
}

static void DS8Impl_Destroy(DS8Impl *This);

static const WCHAR speakerconfigkey[] = {
    'S','Y','S','T','E','M','\\',
    'C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t','\\',
    'C','o','n','t','r','o','l','\\',
    'M','e','d','i','a','R','e','s','o','u','r','c','e','s','\\',
    'D','i','r','e','c','t','S','o','u','n','d','\\',
    'S','p','e','a','k','e','r',' ','C','o','n','f','i','g','u','r','a','t','i','o','n',0
};

static const WCHAR speakerconfig[] = {
    'S','p','e','a','k','e','r',' ','C','o','n','f','i','g','u','r','a','t','i','o','n',0
};

HRESULT DSOUND_Create8(REFIID riid, LPVOID *ds)
{
    DS8Impl *This;
    HKEY regkey;
    HRESULT hr;

    *ds = NULL;
    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if(!This)
        return E_OUTOFMEMORY;
    This->is_8 = TRUE;
    This->IDirectSound8_iface.lpVtbl = &DS8_Vtbl;
    This->speaker_config = DSSPEAKER_COMBINED(DSSPEAKER_5POINT1, DSSPEAKER_GEOMETRY_WIDE);

    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE, speakerconfigkey, 0, KEY_READ, &regkey) == ERROR_SUCCESS)
    {
        DWORD type, conf, confsize = sizeof(DWORD);

        if(RegQueryValueExW(regkey, speakerconfig, NULL, &type, (BYTE*)&conf, &confsize) == ERROR_SUCCESS)
        {
            if(type == REG_DWORD)
                This->speaker_config = conf;
        }

        RegCloseKey(regkey);
    }
    /*RegGetValueW(HKEY_LOCAL_MACHINE, speakerconfigkey, speakerconfig, RRF_RT_REG_DWORD, NULL, &This->speaker_config, NULL);*/

    hr = IUnknown_QueryInterface(&This->IDirectSound8_iface, riid, ds);
    if(FAILED(hr))
        DS8Impl_Destroy(This);
    else
    {
        void *temp;

        EnterCriticalSection(&openal_crst);
        if(devicelist)
            temp = HeapReAlloc(GetProcessHeap(), 0, devicelist, sizeof(*devicelist)*(devicelistsize+1));
        else
            temp = HeapAlloc(GetProcessHeap(), 0, sizeof(*devicelist)*(devicelistsize+1));
        if(temp)
        {
            devicelist = temp;
            devicelist[devicelistsize++] = This;
        }
        LeaveCriticalSection(&openal_crst);
    }
    return hr;
}

static void DS8Impl_Destroy(DS8Impl *This)
{
    UINT i;

    EnterCriticalSection(&openal_crst);
    for(i = 0;i < devicelistsize;i++)
    {
        if(devicelist[i] == This)
        {
            devicelist[i] = devicelist[--devicelistsize];
            if(devicelistsize == 0)
            {
                HeapFree(GetProcessHeap(), 0, devicelist);
                devicelist = NULL;
            }
            break;
        }
    }

    if(This->deviceref && InterlockedDecrement(This->deviceref) == 0)
    {
        if(This->primary)
            DS8Primary_Destroy(This->primary);
        if(This->device)
            alcCloseDevice(This->device);

        HeapFree(GetProcessHeap(), 0, This->deviceref);
    }
    else if(This->deviceref && This->primary->parent == This)
    {
        /* If the primary is referencing this as its parent, update it to
         * reference another handle for the device */
        for(i = 0;i < devicelistsize;i++)
        {
            if(devicelist[i]->primary == This->primary)
            {
                This->primary->parent = devicelist[i];
                break;
            }
        }
    }
    LeaveCriticalSection(&openal_crst);

    HeapFree(GetProcessHeap(), 0, This);
}

static inline DS8Impl *impl_from_IDirectSound8(IDirectSound8 *iface)
{
    return CONTAINING_RECORD(iface, DS8Impl, IDirectSound8_iface);
}

static HRESULT WINAPI DS8_QueryInterface(IDirectSound8 *iface, REFIID riid, LPVOID *ppv)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);

    TRACE("(%p)->(%s, %p)\n", iface, debugstr_guid(riid), ppv);

    *ppv = NULL;
    if(IsEqualIID(riid, &IID_IUnknown) ||
       IsEqualIID(riid, &IID_IDirectSound))
        *ppv = &This->IDirectSound8_iface;
    else if((IsEqualIID(riid, &IID_IDirectSound8)))
    {
        if(This->is_8)
            *ppv = &This->IDirectSound8_iface;
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

static ULONG WINAPI DS8_AddRef(IDirectSound8 *iface)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    LONG ref;

    ref = InterlockedIncrement(&This->ref);
    TRACE("Reference count incremented to %d\n", ref);

    return ref;
}

static ULONG WINAPI DS8_Release(IDirectSound8 *iface)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    LONG ref;

    ref = InterlockedDecrement(&This->ref);
    TRACE("Reference count decremented to %d\n", ref);
    if(ref == 0)
        DS8Impl_Destroy(This);

    return ref;
}

static HRESULT WINAPI DS8_CreateSoundBuffer(IDirectSound8 *iface, LPCDSBUFFERDESC desc, LPLPDIRECTSOUNDBUFFER buf, IUnknown *pUnkOuter)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
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
        WARN("Invalid buffer %p/%u\n", desc, desc?desc->dwSize:0);
        return DSERR_INVALIDPARAM;
    }
    if(desc->dwSize >= sizeof(DSBUFFERDESC))
    {
        if(!(desc->dwFlags&DSBCAPS_CTRL3D))
        {
            if(!IsEqualGUID(&desc->guid3DAlgorithm, &GUID_NULL))
            {
                WARN("Invalid 3D algorithm GUID specified for non-3D buffer: %s\n", debugstr_guid(&desc->guid3DAlgorithm));
                return DSERR_INVALIDPARAM;
            }
        }
        else
            TRACE("Requested 3D algorithm GUID: %s\n", debugstr_guid(&desc->guid3DAlgorithm));
    }

    /* OpenAL doesn't support playing with 3d and panning at same time.. */
    if((desc->dwFlags&(DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN)) == (DSBCAPS_CTRL3D|DSBCAPS_CTRLPAN))
    {
        if(!This->is_8)
            ERR("Cannot create buffers with 3D and panning control\n");
        else
            WARN("Cannot create buffers with 3D and panning control\n");
        return DSERR_INVALIDPARAM;
    }

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else if((desc->dwFlags&DSBCAPS_PRIMARYBUFFER))
    {
        IDirectSoundBuffer *prim = &This->primary->IDirectSoundBuffer_iface;

        hr = S_OK;
        if(IDirectSoundBuffer_AddRef(prim) == 1)
        {
            hr = IDirectSoundBuffer_Initialize(prim, (IDirectSound*)&This->IDirectSound8_iface, desc);
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
        DS8Buffer *dsb;

        hr = DS8Buffer_Create(&dsb, This->primary, NULL);
        if(SUCCEEDED(hr))
        {
            hr = IDirectSoundBuffer8_Initialize(&dsb->IDirectSoundBuffer8_iface, (IDirectSound*)&This->IDirectSound8_iface, desc);
            if(FAILED(hr))
                IDirectSoundBuffer8_Release(&dsb->IDirectSoundBuffer8_iface);
            else
            {
                dsb->bufferlost = (This->prio_level == DSSCL_WRITEPRIMARY);
                *buf = (IDirectSoundBuffer*)&dsb->IDirectSoundBuffer8_iface;
            }
        }
    }
    LeaveCriticalSection(&openal_crst);

    TRACE("%08x\n", hr);
    return hr;
}

static HRESULT WINAPI DS8_GetCaps(IDirectSound8 *iface, LPDSCAPS caps)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;
    TRACE("\n");

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else if(!caps || caps->dwSize < sizeof(*caps))
        hr = DSERR_INVALIDPARAM;
    else
    {
        LONG count = This->primary->max_sources;

        setALContext(This->primary->ctx);
        caps->dwFlags = DSCAPS_CONTINUOUSRATE |
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
            caps->dwMaxHw3DStreamingBuffers = count;
        count -= This->primary->nbuffers;
        if(count < 0)
        {
            ERR("How did the count drop below 0?\n");
            count = 0;
        }
        caps->dwFreeHwMixingAllBuffers =
            caps->dwFreeHwMixingStaticBuffers =
            caps->dwFreeHwMixingStreamingBuffers =
            caps->dwFreeHw3DAllBuffers =
            caps->dwFreeHw3DStaticBuffers =
            caps->dwFreeHw3DStreamingBuffers = count;
        caps->dwTotalHwMemBytes =
            caps->dwFreeHwMemBytes = 64 * 1024 * 1024;
        caps->dwMaxContigFreeHwMemBytes = caps->dwFreeHwMemBytes;
        caps->dwUnlockTransferRateHwBuffers = 4096;
        caps->dwPlayCpuOverheadSwBuffers = 0;
        popALContext();
    }
    LeaveCriticalSection(&openal_crst);

    return hr;
}
static HRESULT WINAPI DS8_DuplicateSoundBuffer(IDirectSound8 *iface, IDirectSoundBuffer *in, IDirectSoundBuffer **out)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p, %p)\n", iface, in, out);

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else if(!in || !out)
        hr = DSERR_INVALIDPARAM;
    else
    {
        DWORD i;
        DSBCAPS caps;
        DS8Buffer *buf;

        *out = NULL;

        for(i = 0; i < This->primary->nbuffers; ++i)
        {
            if(This->primary->buffers[i] == (DS8Buffer*)in)
                break;
        }
        if(i == This->primary->nbuffers)
        {
            hr = DSERR_INVALIDPARAM;
            WARN("Buffer %p not found\n", in);
            goto out;
        }

        caps.dwSize = sizeof(caps);
        hr = IDirectSoundBuffer_GetCaps(in, &caps);
        if(SUCCEEDED(hr) && (caps.dwFlags&DSBCAPS_CTRLFX))
        {
            WARN("Cannot duplicate buffer %p, which has DSBCAPS_CTRLFX\n", in);
            hr = DSERR_INVALIDPARAM;
        }
        if(SUCCEEDED(hr))
            hr = DS8Buffer_Create(&buf, This->primary, (DS8Buffer*)in);
        if(SUCCEEDED(hr))
        {
            *out = (IDirectSoundBuffer*)&buf->IDirectSoundBuffer8_iface;
            hr = IDirectSoundBuffer_Initialize(*out, NULL, NULL);
        }
        if(SUCCEEDED(hr))
        {
            DWORD savelost = ((DS8Buffer*)in)->bufferlost;
            ((DS8Buffer*)in)->bufferlost = 0;
            /* According to MSDN volume isn't copied
             */
            if((caps.dwFlags&DSBCAPS_CTRLPAN))
            {
                LONG pan;
                if(SUCCEEDED(IDirectSoundBuffer_GetPan(in, &pan)))
                    IDirectSoundBuffer_SetPan(*out, pan);
            }
            if((caps.dwFlags&DSBCAPS_CTRLFREQUENCY))
            {
                DWORD freq;
                if(SUCCEEDED(IDirectSoundBuffer_GetFrequency(in, &freq)))
                    IDirectSoundBuffer_SetFrequency(*out, freq);
            }
            if((caps.dwFlags&DSBCAPS_CTRL3D))
            {
                IDirectSound3DBuffer *buf3d;
                DS3DBUFFER DS3DBuffer;
                HRESULT subhr;

                subhr = IDirectSound_QueryInterface(in, &IID_IDirectSound3DBuffer, (void**)&buf3d);
                if(SUCCEEDED(subhr))
                {
                    DS3DBuffer.dwSize = sizeof(DS3DBuffer);
                    subhr = IDirectSound3DBuffer_GetAllParameters(buf3d, &DS3DBuffer);
                    IDirectSound3DBuffer_Release(buf3d);
                }
                if(SUCCEEDED(subhr))
                    subhr = IDirectSoundBuffer_QueryInterface(*out, &IID_IDirectSound3DBuffer, (void**)&buf3d);
                if(SUCCEEDED(subhr))
                {
                    subhr = IDirectSound3DBuffer_SetAllParameters(buf3d, &DS3DBuffer, DS3D_IMMEDIATE);
                    IDirectSound3DBuffer_Release(buf3d);
                }
            }
            ((DS8Buffer*)in)->bufferlost = buf->bufferlost = savelost;
        }
        if(FAILED(hr))
        {
            if(*out)
                IDirectSoundBuffer_Release(*out);
            *out = NULL;
            goto out;
        }
    }
out:
    LeaveCriticalSection(&openal_crst);
    return hr;
}

static HRESULT WINAPI DS8_SetCooperativeLevel(IDirectSound8 *iface, HWND hwnd, DWORD level)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p, %u)\n", iface, hwnd, level);

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else if(level > DSSCL_WRITEPRIMARY || level < DSSCL_NORMAL)
        hr = E_INVALIDARG;
    else if(level == DSSCL_WRITEPRIMARY && (This->prio_level != DSSCL_WRITEPRIMARY))
    {
        DWORD i, state;

        for(i = 0; i < This->primary->nbuffers; ++i)
        {
            DS8Buffer *buf = This->primary->buffers[i];
            if(FAILED(IDirectSoundBuffer_GetStatus(&buf->IDirectSoundBuffer8_iface, &state)) ||
               (state&DSBSTATUS_PLAYING))
            {
                WARN("DSSCL_WRITEPRIMARY set with playing buffers!\n");
                hr = DSERR_INVALIDCALL;
                goto out;
            }
            /* Mark buffer as lost */
            buf->bufferlost = 1;
        }
        if(This->primary->write_emu)
        {
            ERR("Why was there a write_emu?\n");
            /* Delete it */
            IDirectSoundBuffer8_Release(This->primary->write_emu);
            This->primary->write_emu = NULL;
        }
        if(This->primary->flags)
        {
            /* Primary has open references.. create write_emu */
            DSBUFFERDESC desc;
            DS8Buffer *emu;

            memset(&desc, 0, sizeof(desc));
            desc.dwSize = sizeof(desc);
            desc.dwFlags = DSBCAPS_LOCHARDWARE | (This->primary->flags&DSBCAPS_CTRLPAN);
            desc.dwBufferBytes = This->primary->buf_size;
            desc.lpwfxFormat = &This->primary->format.Format;

            hr = DS8Buffer_Create(&emu, This->primary, NULL);
            if(SUCCEEDED(hr))
            {
                This->primary->write_emu = &emu->IDirectSoundBuffer8_iface;
                hr = IDirectSoundBuffer8_Initialize(This->primary->write_emu, (IDirectSound*)&This->IDirectSound8_iface, &desc);
                if(FAILED(hr))
                {
                    IDirectSoundBuffer8_Release(This->primary->write_emu);
                    This->primary->write_emu = NULL;
                }
            }
        }
    }
    else if(This->prio_level == DSSCL_WRITEPRIMARY && level != DSSCL_WRITEPRIMARY && This->primary->write_emu)
    {
        TRACE("Nuking write_emu\n");
        /* Delete it */
        IDirectSoundBuffer8_Release(This->primary->write_emu);
        This->primary->write_emu = NULL;
    }
    if(SUCCEEDED(hr))
        This->prio_level = level;
out:
    LeaveCriticalSection(&openal_crst);

    return hr;
}

static HRESULT WINAPI DS8_Compact(IDirectSound8 *iface)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->()\n", iface);

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else if(This->prio_level < DSSCL_PRIORITY)
        hr = DSERR_PRIOLEVELNEEDED;
    LeaveCriticalSection(&openal_crst);

    return hr;
}

static HRESULT WINAPI DS8_GetSpeakerConfig(IDirectSound8 *iface, DWORD *config)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr = S_OK;

    TRACE("(%p)->(%p)\n", iface, config);

    if(!config)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else
        *config = This->speaker_config;
    LeaveCriticalSection(&openal_crst);

    return hr;
}

static HRESULT WINAPI DS8_SetSpeakerConfig(IDirectSound8 *iface, DWORD config)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr;

    TRACE("(%p)->(0x%08x)\n", iface, config);

    EnterCriticalSection(&openal_crst);
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else
    {
        HKEY key;
        DWORD geo, speaker;

        geo = DSSPEAKER_GEOMETRY(config);
        speaker = DSSPEAKER_CONFIG(config);

        hr = DSERR_INVALIDPARAM;
        if(geo && (geo < DSSPEAKER_GEOMETRY_MIN || geo > DSSPEAKER_GEOMETRY_MAX))
        {
            WARN("Invalid speaker angle %u\n", geo);
            goto out;
        }
        if(speaker < DSSPEAKER_HEADPHONE || speaker > DSSPEAKER_7POINT1)
        {
            WARN("Invalid speaker config %u\n", speaker);
            goto out;
        }

        hr = DSERR_GENERIC;
        if(!RegCreateKeyExW(HKEY_LOCAL_MACHINE, speakerconfigkey, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL))
        {
            RegSetValueExW(key, speakerconfig, 0, REG_DWORD, (const BYTE*)&config, sizeof(DWORD));
            This->speaker_config = config;
            RegCloseKey(key);
            hr = S_OK;
        }
    }
out:
    LeaveCriticalSection(&openal_crst);

    return hr;
}

static HRESULT WINAPI DS8_Initialize(IDirectSound8 *iface, const GUID *devguid)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    const ALCchar *drv_name;
    HRESULT hr;
    UINT n;

    TRACE("(%p)->(%s)\n", iface, debugstr_guid(devguid));

    if(!openal_loaded)
        return DSERR_NODRIVER;

    if(!devguid)
        devguid = &DSDEVID_DefaultPlayback;

    EnterCriticalSection(&openal_crst);

    hr = DSERR_ALREADYINITIALIZED;
    if(This->device)
        goto out;

    hr = GetDeviceID(devguid, &This->guid);
    if(FAILED(hr))
        goto out;

    for(n = 0;n < devicelistsize;n++)
    {
        if(devicelist[n]->device && devicelist[n]->is_8 == This->is_8 &&
           IsEqualGUID(&devicelist[n]->guid, &This->guid))
        {
            TRACE("Matched already open device %p\n", devicelist[n]);

            This->device = devicelist[n]->device;
            This->primary = devicelist[n]->primary;
            This->deviceref = devicelist[n]->deviceref;
            InterlockedIncrement(This->deviceref);

            hr = DS_OK;
            goto out;
        }
    }

    if(!This->deviceref)
    {
        hr = DSERR_OUTOFMEMORY;
        if(!(This->deviceref=HeapAlloc(GetProcessHeap(), 0, sizeof(LONG))))
            goto out;
        This->deviceref[0] = 1;
    }

    hr = DSERR_NODRIVER;
    if(!(drv_name=DSOUND_getdevicestrings()) ||
       memcmp(&This->guid, &DSOUND_renderer_guid, sizeof(GUID)-1) != 0)
    {
        WARN("No device found\n");
        goto out;
    }

    n = This->guid.Data4[7];
    while(*drv_name && n--)
        drv_name += strlen(drv_name) + 1;
    if(!*drv_name)
    {
        WARN("No device string found\n");
        goto out;
    }

    This->device = alcOpenDevice(drv_name);
    if(!This->device)
    {
        alcGetError(NULL);
        WARN("Couldn't open device \"%s\"\n", drv_name);
        goto out;
    }
    TRACE("Opened device: %s\n", alcGetString(This->device, ALC_DEVICE_SPECIFIER));

    hr = DS8Primary_Create(&This->primary, This);
    if(FAILED(hr))
    {
        alcCloseDevice(This->device);
        This->device = NULL;
    }

out:
    LeaveCriticalSection(&openal_crst);

    return hr;
}

/* I, Maarten Lankhorst, hereby declare this driver certified
 * What this means.. ? An extra bit set
 */
static HRESULT WINAPI DS8_VerifyCertification(IDirectSound8 *iface, DWORD *certified)
{
    DS8Impl *This = impl_from_IDirectSound8(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", iface, certified);

    if(!certified)
        return DSERR_INVALIDPARAM;

    EnterCriticalSection(&openal_crst);
    hr = S_OK;
    if(!This->device)
        hr = DSERR_UNINITIALIZED;
    else
        *certified = DS_CERTIFIED;
    LeaveCriticalSection(&openal_crst);

    return hr;
}

static const IDirectSound8Vtbl DS8_Vtbl =
{
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
