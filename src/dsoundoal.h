#ifndef DSOUNDOAL_H
#define DSOUNDOAL_H

#include <atomic>
#include <dsound.h>

#include "comptr.h"


class DSound8OAL final : IDirectSound8 {
    DSound8OAL();
    ~DSound8OAL();

public:
    /*** IUnknown methods ***/
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    /*** IDirectSound8 methods ***/
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC lpcDSBufferDesc, LPLPDIRECTSOUNDBUFFER lplpDirectSoundBuffer, IUnknown *pUnkOuter) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS lpDSCaps) noexcept override;
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER lpDsbOriginal, LPLPDIRECTSOUNDBUFFER lplpDsbDuplicate) noexcept override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hwnd, DWORD dwLevel) noexcept override;
    HRESULT STDMETHODCALLTYPE Compact() noexcept override;
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD lpdwSpeakerConfig) noexcept override;
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD dwSpeakerConfig) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID lpcGuid) noexcept override;
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD pdwCertified) noexcept override;

    std::atomic<ULONG> mRef{1u};

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }
    template<typename T>
    T as() const noexcept { return static_cast<T>(this); }

    static ComPtr<DSound8OAL> Create();
};

#endif // DSOUNDOAL_H
