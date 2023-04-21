#ifndef DSFULLDUPLEX_H
#define DSFULLDUPLEX_H

#include <atomic>

#include <dsound.h>

#include "comptr.h"


class DSFullDuplex final : IDirectSoundFullDuplex {
    std::atomic<ULONG> mTotalRef{1u}, mFdRef{1u};

public:
    DSFullDuplex();
    ~DSFullDuplex();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(const GUID *captureGuid, const GUID *renderGuid, const DSCBUFFERDESC *dscBufferDesc, const DSBUFFERDESC *dsBufferDesc, HWND hwnd, DWORD level, IDirectSoundCaptureBuffer8 **dsCaptureBuffer8, IDirectSoundBuffer8 **dsBuffer8) noexcept override;

    template<typename T> [[nodiscard]]
    T as() noexcept { return static_cast<T>(this); }

    static ComPtr<DSFullDuplex> Create();
};

#endif // DSFULLDUPLEX_H
