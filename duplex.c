/*              DirectSoundFullDuplex
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 1998 Rob Riggs
 * Copyright 2000-2001 TransGaming Technologies, Inc.
 * Copyright 2005 Robert Reif
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


/*****************************************************************************
 * IDirectSoundFullDuplex implementation structure
 */
typedef struct IDirectSoundFullDuplexImpl
{
    IDirectSoundFullDuplex IDirectSoundFullDuplex_iface;
    IUnknown               IUnknown_iface;
    IDirectSound8          IDirectSound8_iface;
    IDirectSoundCapture    IDirectSoundCapture_iface;
    LONG ref, unkref, ds8ref, dscref;
    LONG all_ref;

    /* IDirectSoundFullDuplexImpl fields */
    IDirectSound8       *renderer_device;
    IDirectSoundCapture *capture_device;
} IDirectSoundFullDuplexImpl;


static void DSOUND_FullDuplexDestroy(IDirectSoundFullDuplexImpl *This)
{
    if (This->capture_device)
        IDirectSoundCapture_Release(This->capture_device);
    if (This->renderer_device)
        IDirectSound_Release(This->renderer_device);
    HeapFree(GetProcessHeap(), 0, This);
    TRACE("(%p) released\n", This);
}

/*******************************************************************************
 * IUnknown
 */
static inline IDirectSoundFullDuplexImpl *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, IDirectSoundFullDuplexImpl, IUnknown_iface);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IUnknown_QueryInterface(
    IUnknown *iface, REFIID riid, void **ppobj)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IUnknown(iface);
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppobj);
    return IDirectSoundFullDuplex_QueryInterface(&This->IDirectSoundFullDuplex_iface, riid, ppobj);
}

static ULONG WINAPI IDirectSoundFullDuplex_IUnknown_AddRef(IUnknown *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IUnknown(iface);
    ULONG ref;

    InterlockedIncrement(&(This->all_ref));
    ref = InterlockedIncrement(&(This->unkref));
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI IDirectSoundFullDuplex_IUnknown_Release(IUnknown *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IUnknown(iface);
    ULONG ref = InterlockedDecrement(&(This->unkref));
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&(This->all_ref)) == 0)
        DSOUND_FullDuplexDestroy(This);
    return ref;
}

static const IUnknownVtbl DirectSoundFullDuplex_Unknown_Vtbl = {
    IDirectSoundFullDuplex_IUnknown_QueryInterface,
    IDirectSoundFullDuplex_IUnknown_AddRef,
    IDirectSoundFullDuplex_IUnknown_Release
};

/*******************************************************************************
 * IDirectSoundFullDuplex_IDirectSound8
 */
static inline IDirectSoundFullDuplexImpl *impl_from_IDirectSound8(IDirectSound8 *iface)
{
    return CONTAINING_RECORD(iface, IDirectSoundFullDuplexImpl, IDirectSound8_iface);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_QueryInterface(
    IDirectSound8 *iface, REFIID riid, void **ppobj)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppobj);
    return IDirectSoundFullDuplex_QueryInterface(&This->IDirectSoundFullDuplex_iface, riid, ppobj);
}

static ULONG WINAPI IDirectSoundFullDuplex_IDirectSound8_AddRef(IDirectSound8 *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    ULONG ref;

    InterlockedIncrement(&(This->all_ref));
    ref = InterlockedIncrement(&(This->ds8ref));
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI IDirectSoundFullDuplex_IDirectSound8_Release(IDirectSound8 *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    ULONG ref = InterlockedDecrement(&(This->ds8ref));
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&(This->all_ref)) == 0)
        DSOUND_FullDuplexDestroy(This);
    return ref;
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_CreateSoundBuffer(
    IDirectSound8 *iface, LPCDSBUFFERDESC dsbd, IDirectSoundBuffer **ppdsb,
    IUnknown *lpunk)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p,%p,%p,%p)\n",This,dsbd,ppdsb,lpunk);
    return IDirectSound8_CreateSoundBuffer(This->renderer_device,dsbd,ppdsb,lpunk);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_GetCaps(
    IDirectSound8 *iface, DSCAPS *lpDSCaps)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p,%p)\n",This,lpDSCaps);
    return IDirectSound8_GetCaps(This->renderer_device, lpDSCaps);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_DuplicateSoundBuffer(
    IDirectSound8 *iface, IDirectSoundBuffer *psb, IDirectSoundBuffer **ppdsb)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p,%p,%p)\n",This,psb,ppdsb);
    return IDirectSound8_DuplicateSoundBuffer(This->renderer_device,psb,ppdsb);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_SetCooperativeLevel(
    IDirectSound8 *iface, HWND hwnd, DWORD level)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p,%p,%lu)\n",This,hwnd,level);
    return IDirectSound8_SetCooperativeLevel(This->renderer_device,hwnd,level);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_Compact(IDirectSound8 *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p)\n", This);
    return IDirectSound8_Compact(This->renderer_device);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_GetSpeakerConfig(
    IDirectSound8 *iface, DWORD *lpdwSpeakerConfig)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p, %p)\n", This, lpdwSpeakerConfig);
    return IDirectSound8_GetSpeakerConfig(This->renderer_device,lpdwSpeakerConfig);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_SetSpeakerConfig(
    IDirectSound8 *iface, DWORD config)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p,0x%08lx)\n",This,config);
    return IDirectSound8_SetSpeakerConfig(This->renderer_device,config);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_Initialize(
    IDirectSound8 *iface, LPCGUID lpcGuid)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p, %s)\n", This, debugstr_guid(lpcGuid));
    return IDirectSound8_Initialize(This->renderer_device,lpcGuid);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSound8_VerifyCertification(
    IDirectSound8 *iface, DWORD *cert)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSound8(iface);
    TRACE("(%p, %p)\n", This, cert);
    return IDirectSound8_VerifyCertification(This->renderer_device,cert);
}

static IDirectSound8Vtbl DirectSoundFullDuplex_DirectSound8_Vtbl = {
    IDirectSoundFullDuplex_IDirectSound8_QueryInterface,
    IDirectSoundFullDuplex_IDirectSound8_AddRef,
    IDirectSoundFullDuplex_IDirectSound8_Release,
    IDirectSoundFullDuplex_IDirectSound8_CreateSoundBuffer,
    IDirectSoundFullDuplex_IDirectSound8_GetCaps,
    IDirectSoundFullDuplex_IDirectSound8_DuplicateSoundBuffer,
    IDirectSoundFullDuplex_IDirectSound8_SetCooperativeLevel,
    IDirectSoundFullDuplex_IDirectSound8_Compact,
    IDirectSoundFullDuplex_IDirectSound8_GetSpeakerConfig,
    IDirectSoundFullDuplex_IDirectSound8_SetSpeakerConfig,
    IDirectSoundFullDuplex_IDirectSound8_Initialize,
    IDirectSoundFullDuplex_IDirectSound8_VerifyCertification
};

/*******************************************************************************
 * IDirectSoundFullDuplex_IDirectSoundCapture
 */
static inline IDirectSoundFullDuplexImpl *impl_from_IDirectSoundCapture(IDirectSoundCapture *iface)
{
    return CONTAINING_RECORD(iface, IDirectSoundFullDuplexImpl, IDirectSoundCapture_iface);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_QueryInterface(
    IDirectSoundCapture *iface, REFIID riid, void **ppobj)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    TRACE("(%p, %s, %p)\n", This, debugstr_guid(riid), ppobj);
    return IDirectSoundFullDuplex_QueryInterface(&This->IDirectSoundFullDuplex_iface, riid, ppobj);
}

static ULONG WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_AddRef(IDirectSoundCapture *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    ULONG ref;

    InterlockedIncrement(&(This->all_ref));
    ref = InterlockedIncrement(&(This->dscref));
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_Release(IDirectSoundCapture *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    ULONG ref = InterlockedDecrement(&(This->dscref));
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&(This->all_ref)) == 0)
        DSOUND_FullDuplexDestroy(This);
    return ref;
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_CreateCaptureBuffer(
    IDirectSoundCapture *iface, LPCDSCBUFFERDESC lpcDSCBufferDesc,
    IDirectSoundCaptureBuffer **lplpDSCaptureBuffer, IUnknown *pUnk)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    TRACE("(%p, %p, %p, %p)\n", This, lpcDSCBufferDesc, lplpDSCaptureBuffer, pUnk);
    return IDirectSoundCapture_CreateCaptureBuffer(This->capture_device,lpcDSCBufferDesc,lplpDSCaptureBuffer,pUnk);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_GetCaps(
    IDirectSoundCapture *iface, LPDSCCAPS lpDSCCaps)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    TRACE("(%p, %p)\n", This, lpDSCCaps);
    return IDirectSoundCapture_GetCaps(This->capture_device, lpDSCCaps);
}

static HRESULT WINAPI IDirectSoundFullDuplex_IDirectSoundCapture_Initialize(
    IDirectSoundCapture *iface, LPCGUID lpcGUID)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundCapture(iface);
    TRACE("(%p, %s)\n", This, debugstr_guid(lpcGUID));
    return IDirectSoundCapture_Initialize(This->capture_device,lpcGUID);
}

static IDirectSoundCaptureVtbl DirectSoundFullDuplex_DirectSoundCapture_Vtbl = {
    IDirectSoundFullDuplex_IDirectSoundCapture_QueryInterface,
    IDirectSoundFullDuplex_IDirectSoundCapture_AddRef,
    IDirectSoundFullDuplex_IDirectSoundCapture_Release,
    IDirectSoundFullDuplex_IDirectSoundCapture_CreateCaptureBuffer,
    IDirectSoundFullDuplex_IDirectSoundCapture_GetCaps,
    IDirectSoundFullDuplex_IDirectSoundCapture_Initialize
};

/***************************************************************************
 * IDirectSoundFullDuplexImpl
 */
static inline IDirectSoundFullDuplexImpl *impl_from_IDirectSoundFullDuplex(IDirectSoundFullDuplex *iface)
{
    return CONTAINING_RECORD(iface, IDirectSoundFullDuplexImpl, IDirectSoundFullDuplex_iface);
}

static HRESULT WINAPI IDirectSoundFullDuplexImpl_QueryInterface(
    IDirectSoundFullDuplex *iface, REFIID riid, void **ppobj)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundFullDuplex(iface);
    TRACE( "(%p,%s,%p)\n", This, debugstr_guid(riid), ppobj );

    if (ppobj == NULL) {
        WARN("invalid parameter\n");
        return E_INVALIDARG;
    }

    *ppobj = NULL;

    if (IsEqualIID(riid, &IID_IUnknown)) {
        *ppobj = &This->IUnknown_iface;
        IUnknown_AddRef((IUnknown*)*ppobj);
        return S_OK;
    } else if (IsEqualIID(riid, &IID_IDirectSoundFullDuplex)) {
        *ppobj = &This->IDirectSoundFullDuplex_iface;
        IUnknown_AddRef((IUnknown*)*ppobj);
        return S_OK;
    } else if (IsEqualIID(riid, &IID_IDirectSound) ||
               IsEqualIID(riid, &IID_IDirectSound8)) {
        if(This->renderer_device != NULL)
        {
            *ppobj = &This->IDirectSound8_iface;
            IUnknown_AddRef((IUnknown*)*ppobj);
            return S_OK;
        }
    } else if (IsEqualIID(riid, &IID_IDirectSoundCapture)) {
        if(This->capture_device != NULL)
        {
            *ppobj = &This->IDirectSoundCapture_iface;
            IUnknown_AddRef((IUnknown*)*ppobj);
            return S_OK;
        }
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI IDirectSoundFullDuplexImpl_AddRef(IDirectSoundFullDuplex *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundFullDuplex(iface);
    ULONG ref;

    InterlockedIncrement(&(This->all_ref));
    ref = InterlockedIncrement(&(This->ref));
    TRACE("(%p) ref %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI IDirectSoundFullDuplexImpl_Release(IDirectSoundFullDuplex *iface)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundFullDuplex(iface);
    ULONG ref = InterlockedDecrement(&(This->ref));
    TRACE("(%p) ref %lu\n", iface, ref);
    if(InterlockedDecrement(&(This->all_ref)) == 0)
        DSOUND_FullDuplexDestroy(This);
    return ref;
}

static HRESULT WINAPI IDirectSoundFullDuplexImpl_Initialize(
    IDirectSoundFullDuplex *iface,
    LPCGUID pCaptureGuid, LPCGUID pRendererGuid,
    LPCDSCBUFFERDESC lpDscBufferDesc, LPCDSBUFFERDESC lpDsBufferDesc,
    HWND hWnd, DWORD dwLevel,
    IDirectSoundCaptureBuffer8 **lplpDirectSoundCaptureBuffer8,
    IDirectSoundBuffer8 **lplpDirectSoundBuffer8)
{
    IDirectSoundFullDuplexImpl *This = impl_from_IDirectSoundFullDuplex(iface);
    IDirectSoundCaptureBuffer *capbuffer;
    IDirectSoundBuffer *buffer;
    void *ptr;
    HRESULT hr;

    TRACE("(%p,%s,%s,%p,%p,%p,%lx,%p,%p)\n", This,
        debugstr_guid(pCaptureGuid), debugstr_guid(pRendererGuid),
        lpDscBufferDesc, lpDsBufferDesc, hWnd, dwLevel,
        lplpDirectSoundCaptureBuffer8, lplpDirectSoundBuffer8);

    if(!lplpDirectSoundBuffer8 || !lplpDirectSoundCaptureBuffer8)
    {
        WARN("NULL output pointers\n");
        return DSERR_INVALIDPARAM;
    }

    *lplpDirectSoundCaptureBuffer8 = NULL;
    *lplpDirectSoundBuffer8 = NULL;

    if(This->renderer_device != NULL || This->capture_device != NULL)
    {
        WARN("already initialized\n");
        return DSERR_ALREADYINITIALIZED;
    }

    hr = DSOUND_Create8(&IID_IDirectSound8, &ptr);
    if(SUCCEEDED(hr))
    {
        This->renderer_device = ptr;
        hr = IDirectSound8_Initialize(This->renderer_device, pRendererGuid);
    }
    if(hr != DS_OK)
    {
        WARN("DirectSoundDevice_Initialize() failed\n");
        return hr;
    }

    IDirectSound8_SetCooperativeLevel(This->renderer_device, hWnd, dwLevel);

    hr = IDirectSound8_CreateSoundBuffer(This->renderer_device, lpDsBufferDesc,
                                         &buffer, NULL);
    if(SUCCEEDED(hr))
    {
        hr = IDirectSoundBuffer_QueryInterface(buffer, &IID_IDirectSoundBuffer8, &ptr);
        IDirectSoundBuffer_Release(buffer);
    }
    if(hr != DS_OK)
    {
        WARN("IDirectSoundBufferImpl_Create() failed\n");
        return hr;
    }
    *lplpDirectSoundBuffer8 = ptr;

    hr = DSOUND_CaptureCreate8(&IID_IDirectSoundCapture, &ptr);
    if(SUCCEEDED(hr))
    {
        This->capture_device = ptr;
        hr = IDirectSoundCapture_Initialize(This->capture_device, pCaptureGuid);
    }
    if(hr != DS_OK)
    {
        WARN("DirectSoundCaptureDevice_Initialize() failed\n");
        return hr;
    }

    hr = IDirectSoundCapture_CreateCaptureBuffer(This->capture_device, lpDscBufferDesc,
                                                 &capbuffer, NULL);
    if(SUCCEEDED(hr))
    {
        hr = IDirectSoundCaptureBuffer_QueryInterface(capbuffer, &IID_IDirectSoundCaptureBuffer8, &ptr);
        IDirectSoundCaptureBuffer_Release(capbuffer);
    }
    if(hr != DS_OK)
    {
        WARN("IDirectSoundCaptureBufferImpl_Create() failed\n");
        return hr;
    }
    *lplpDirectSoundCaptureBuffer8 = ptr;

    return hr;
}

static IDirectSoundFullDuplexVtbl dsfdvt = {
    /* IUnknown methods */
    IDirectSoundFullDuplexImpl_QueryInterface,
    IDirectSoundFullDuplexImpl_AddRef,
    IDirectSoundFullDuplexImpl_Release,

    /* IDirectSoundFullDuplex methods */
    IDirectSoundFullDuplexImpl_Initialize
};

HRESULT DSOUND_FullDuplexCreate(
    REFIID riid, void **ppDSFD)
{
    IDirectSoundFullDuplexImpl *This = NULL;
    HRESULT hr;

    TRACE("(%s, %p)\n", debugstr_guid(riid), ppDSFD);

    if(ppDSFD == NULL)
    {
        WARN("invalid parameter: ppDSFD == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSFD = NULL;

    if(!IsEqualIID(riid, &IID_IUnknown) &&
       !IsEqualIID(riid, &IID_IDirectSoundFullDuplex))
        return E_NOINTERFACE;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDirectSoundFullDuplexImpl));
    if(This == NULL)
    {
        WARN("out of memory\n");
        return DSERR_OUTOFMEMORY;
    }

    This->IDirectSoundFullDuplex_iface.lpVtbl = &dsfdvt;
    This->IUnknown_iface.lpVtbl = &DirectSoundFullDuplex_Unknown_Vtbl;
    This->IDirectSound8_iface.lpVtbl = &DirectSoundFullDuplex_DirectSound8_Vtbl;
    This->IDirectSoundCapture_iface.lpVtbl = &DirectSoundFullDuplex_DirectSoundCapture_Vtbl;

    This->all_ref = 0;
    This->ref = 0;
    This->unkref = 0;
    This->ds8ref = 0;
    This->dscref = 0;

    This->capture_device = NULL;
    This->renderer_device = NULL;

    hr = IDirectSoundFullDuplexImpl_QueryInterface(&This->IDirectSoundFullDuplex_iface, riid, ppDSFD);
    if(FAILED(hr)) DSOUND_FullDuplexDestroy(This);
    return hr;
}

/***************************************************************************
 * DirectSoundFullDuplexCreate [DSOUND.10]
 *
 * Create and initialize a DirectSoundFullDuplex interface.
 *
 * PARAMS
 *    pcGuidCaptureDevice [I] Address of sound capture device GUID.
 *    pcGuidRenderDevice  [I] Address of sound render device GUID.
 *    pcDSCBufferDesc     [I] Address of capture buffer description.
 *    pcDSBufferDesc      [I] Address of  render buffer description.
 *    hWnd                [I] Handle to application window.
 *    dwLevel             [I] Cooperative level.
 *    ppDSFD              [O] Address where full duplex interface returned.
 *    ppDSCBuffer8        [0] Address where capture buffer interface returned.
 *    ppDSBuffer8         [0] Address where render buffer interface returned.
 *    pUnkOuter           [I] Must be NULL.
 *
 * RETURNS
 *    Success: DS_OK
 *    Failure: DSERR_NOAGGREGATION, DSERR_ALLOCATED, DSERR_INVALIDPARAM,
 *             DSERR_OUTOFMEMORY DSERR_INVALIDCALL DSERR_NODRIVER
 */
HRESULT WINAPI
DSOAL_DirectSoundFullDuplexCreate(
    LPCGUID pcGuidCaptureDevice, LPCGUID pcGuidRenderDevice,
    LPCDSCBUFFERDESC pcDSCBufferDesc, LPCDSBUFFERDESC pcDSBufferDesc,
    HWND hWnd, DWORD dwLevel,
    IDirectSoundFullDuplex **ppDSFD,
    IDirectSoundCaptureBuffer8 **ppDSCBuffer8,
    IDirectSoundBuffer8 **ppDSBuffer8,
    IUnknown *pUnkOuter)
{
    void *iface = NULL;
    HRESULT hres;

    TRACE("(%s,%s,%p,%p,%p,%lx,%p,%p,%p,%p)\n",
        debugstr_guid(pcGuidCaptureDevice), debugstr_guid(pcGuidRenderDevice),
        pcDSCBufferDesc, pcDSBufferDesc, hWnd, dwLevel, ppDSFD, ppDSCBuffer8,
        ppDSBuffer8, pUnkOuter);

    if(ppDSFD == NULL)
    {
        WARN("invalid parameter: ppDSFD == NULL\n");
        return DSERR_INVALIDPARAM;
    }
    *ppDSFD = NULL;

    if(pUnkOuter)
    {
        WARN("pUnkOuter != 0\n");
        return DSERR_NOAGGREGATION;
    }

    if(pcDSCBufferDesc == NULL)
    {
        WARN("invalid parameter: pcDSCBufferDesc == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(pcDSBufferDesc == NULL)
    {
        WARN("invalid parameter: pcDSBufferDesc == NULL\n");
        return DSERR_INVALIDPARAM;
    }


    if(ppDSCBuffer8 == NULL)
    {
        WARN("invalid parameter: ppDSCBuffer8 == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    if(ppDSBuffer8 == NULL)
    {
        WARN("invalid parameter: ppDSBuffer8 == NULL\n");
        return DSERR_INVALIDPARAM;
    }

    hres = DSOUND_FullDuplexCreate(&IID_IDirectSoundFullDuplex, &iface);
    if(FAILED(hres)) return hres;

    *ppDSFD = iface;
    hres = IDirectSoundFullDuplexImpl_Initialize(*ppDSFD,
                                                 pcGuidCaptureDevice,
                                                 pcGuidRenderDevice,
                                                 pcDSCBufferDesc,
                                                 pcDSBufferDesc,
                                                 hWnd, dwLevel,
                                                 ppDSCBuffer8,
                                                 ppDSBuffer8);
    if(hres != DS_OK)
    {
        IDirectSoundFullDuplex_Release(*ppDSFD);
        WARN("IDirectSoundFullDuplexImpl_Initialize failed\n");
        *ppDSFD = NULL;
    }

    return hres;
}
