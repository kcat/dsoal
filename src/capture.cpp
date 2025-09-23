#include "capture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "guidprinter.h"
#include "logging.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

#ifndef DSCBPN_OFFSET_STOP
#define DSCBPN_OFFSET_STOP          0xffffffff
#endif

template<typename T>
struct CoTaskMemDeleter {
    void operator()(T *mem) const { CoTaskMemFree(mem); }
};
template<typename T>
using CoTaskMemPtr = std::unique_ptr<T,CoTaskMemDeleter<T>>;


class DSCBuffer final : IDirectSoundCaptureBuffer8 {
    explicit DSCBuffer(DSCapture &parent, bool is8) : mIs8{is8}, mParent{parent} { }

    class Notify final : IDirectSoundNotify {
        auto impl_from_base() noexcept
        {
#ifdef __GNUC__
            _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wcast-align\"")
#endif
            return CONTAINING_RECORD(this, DSCBuffer, mNotify);
#ifdef __GNUC__
            _Pragma("GCC diagnostic pop")
#endif
        }

    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
        ULONG STDMETHODCALLTYPE AddRef() noexcept override;
        ULONG STDMETHODCALLTYPE Release() noexcept override;
        HRESULT STDMETHODCALLTYPE SetNotificationPositions(DWORD numNotifies, const DSBPOSITIONNOTIFY *notifies) noexcept override;

        Notify() = default;
        Notify(const Notify&) = delete;
        Notify& operator=(const Notify&) = delete;

        template<typename T>
        T as() noexcept { return static_cast<T>(this); }
    };
    Notify mNotify;

    std::atomic<ULONG> mTotalRef{1u}, mDsRef{1u}, mNotRef{0u};

    bool mIs8{};
    bool mCapturing{};

    DSCapture &mParent;
    std::thread mCaptureThread;
    WAVEFORMATEXTENSIBLE mWaveFmt{};
    ALCdevice *mDevice{};
    std::vector<DSBPOSITIONNOTIFY> mNotifies;
    std::vector<std::byte> mBuffer;
    std::atomic<DWORD> mWritePos{0u};
    std::atomic<bool> mLocked{false};
    std::atomic<bool> mQuitNow{false};

public:
    ~DSCBuffer()
    {
        if(mCaptureThread.joinable())
        {
            mQuitNow = true;
            mCaptureThread.join();
        }
        if(mDevice)
            alcCaptureCloseDevice(mDevice);
    }

    DSCBuffer(const DSCBuffer&) = delete;
    DSCBuffer& operator=(const DSCBuffer&) = delete;

    void captureThread();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCBCAPS lpDSCBCaps) noexcept override;
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD lpdwCapturePosition,LPDWORD lpdwReadPosition) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated, LPDWORD lpdwSizeWritten) noexcept override;
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD lpdwStatus) noexcept override;
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUNDCAPTURE lpDSC, LPCDSCBUFFERDESC lpcDSCBDesc) noexcept override;
    HRESULT STDMETHODCALLTYPE Lock(DWORD dwReadCusor, DWORD dwReadBytes, LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2, LPDWORD lpdwAudioBytes2, DWORD dwFlags) noexcept override;
    HRESULT STDMETHODCALLTYPE Start(DWORD dwFlags) noexcept override;
    HRESULT STDMETHODCALLTYPE Stop() noexcept override;
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1, LPVOID lpvAudioPtr2, DWORD dwAudioBytes2) noexcept override;
    HRESULT STDMETHODCALLTYPE GetObjectInPath(REFGUID rguidObject, DWORD dwIndex, REFGUID rguidInterface, LPVOID *ppObject) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFXStatus(DWORD dwFXCount, LPDWORD pdwFXStatus) noexcept override;

    void finalize() noexcept
    {
        if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1) [[unlikely]]
            delete this;
    }

    template<typename T>
    T as() noexcept { return static_cast<T>(this); }

    static auto Create(DSCapture &parent, bool is8) -> ComPtr<DSCBuffer>
    { return ComPtr<DSCBuffer>{new DSCBuffer{parent, is8}}; }
};

#define CLASS_PREFIX "DSCBuffer::"
#define PREFIX CLASS_PREFIX "captureThread "
void DSCBuffer::captureThread()
{
    auto devlock = mParent.getUniqueLock();
    while(!mQuitNow.load(std::memory_order_acquire))
    {
        devlock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        devlock.lock();

        auto avails = ALCint{};
        alcGetIntegerv(mDevice, ALC_CAPTURE_SAMPLES, 1, &avails);
        if(avails < 0)
        {
            ERR("Invalid capture sample count: {}", avails);
            continue;
        }

        auto availframes = ds::saturate_cast<ALCuint>(avails);
        if(availframes == 0) continue;

        auto writepos = mWritePos.load(std::memory_order_relaxed);
        const auto oldpos = writepos;
        while(availframes > 0)
        {
            const auto toread = std::min<size_t>(availframes,
                (mBuffer.size() - writepos) / mWaveFmt.Format.nBlockAlign);

            alcCaptureSamples(mDevice, &mBuffer[writepos], ds::saturate_cast<ALCsizei>(toread));

            availframes -= ds::saturate_cast<ALCuint>(toread);
            writepos += ds::saturate_cast<DWORD>(toread) * mWaveFmt.Format.nBlockAlign;
            if(writepos == mBuffer.size()) writepos = 0;
        }

        std::ranges::for_each(mNotifies, [oldpos,writepos](const DSBPOSITIONNOTIFY &notify)
        {
            if(oldpos > writepos)
            {
                if(notify.dwOffset != static_cast<DWORD>(DSCBPN_OFFSET_STOP)
                    && (notify.dwOffset >= oldpos || notify.dwOffset < writepos))
                    SetEvent(notify.hEventNotify);
            }
            else if(notify.dwOffset >= oldpos && notify.dwOffset < writepos)
                SetEvent(notify.hEventNotify);
        });
        mWritePos.store(writepos, std::memory_order_release);
    }
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSCBuffer::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
    *ppvObject = nullptr;

    if(riid == IID_IUnknown)
    {
        AddRef();
        *ppvObject = as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCaptureBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundCaptureBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCaptureBuffer8)
    {
        if(!mIs8)
        {
            WARN("Requesting IDirectSoundCaptureBuffer8 iface for non-DS8 object");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSoundCaptureBuffer8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundNotify)
    {
        mNotify.AddRef();
        *ppvObject = mNotify.as<IDirectSoundNotify*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCBuffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCBuffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    finalize();
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetCaps(LPDSCBCAPS lpDSCBCaps) noexcept
{
    TRACE("({})->({})", voidp{this}, voidp{lpDSCBCaps});

    if(!lpDSCBCaps || lpDSCBCaps->dwSize < sizeof(*lpDSCBCaps))
    {
        WARN("Bad caps: {}, {}", voidp{lpDSCBCaps}, lpDSCBCaps ? lpDSCBCaps->dwSize : 0ul);
        return DSERR_INVALIDPARAM;
    }

    lpDSCBCaps->dwSize = sizeof(*lpDSCBCaps);
    lpDSCBCaps->dwFlags = 0;
    lpDSCBCaps->dwBufferBytes = ds::saturate_cast<DWORD>(mBuffer.size());
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCurrentPosition "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetCurrentPosition(LPDWORD lpdwCapturePosition,
    LPDWORD lpdwReadPosition) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{lpdwCapturePosition}, voidp{lpdwReadPosition});

    auto cappos = mWritePos.load(std::memory_order_acquire);
    const auto readpos = cappos;
    if(mCapturing)
    {
        /* 10ms write-ahead (area between [readpos...cappos] is going to be
         * overwritten as new samples come in).
         */
        cappos += mWaveFmt.Format.nSamplesPerSec / 100 * mWaveFmt.Format.nBlockAlign;
        cappos %= mBuffer.size();
    }

    DEBUG(" pos = {}, read pos = {}", cappos, readpos);
    if(lpdwCapturePosition) *lpdwCapturePosition = cappos;
    if(lpdwReadPosition) *lpdwReadPosition = readpos;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFormat "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetFormat(LPWAVEFORMATEX lpwfxFormat, DWORD dwSizeAllocated,
    LPDWORD lpdwSizeWritten) noexcept
{
    TRACE("({})->({}, {}, {})", voidp{this}, voidp{lpwfxFormat}, dwSizeAllocated,
        voidp{lpdwSizeWritten});

    if(!lpwfxFormat && !lpdwSizeWritten)
    {
        WARN("Cannot report format of format size");
        return DSERR_INVALIDPARAM;
    }

    const auto size = DWORD{sizeof(mWaveFmt.Format)} + mWaveFmt.Format.cbSize;
    if(lpwfxFormat)
    {
        if(dwSizeAllocated < size)
            return DSERR_INVALIDPARAM;
        std::memcpy(lpwfxFormat, &mWaveFmt.Format, size);
    }
    if(lpdwSizeWritten)
        *lpdwSizeWritten = size;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetStatus "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetStatus(LPDWORD lpdwStatus) noexcept
{
    TRACE("({})->({})", voidp{this}, voidp{lpdwStatus});

    if(!lpdwStatus)
    {
        WARN("Null out pointer");
        return DSERR_INVALIDPARAM;
    }

    if(!mCapturing)
        *lpdwStatus = 0;
    else
        *lpdwStatus = DSCBSTATUS_CAPTURING | DSCBSTATUS_LOOPING;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSCBuffer::Initialize(LPDIRECTSOUNDCAPTURE lpDSC,
    LPCDSCBUFFERDESC lpcDSCBDesc) noexcept
{
    TRACE("({})->({}, {})", voidp{this}, voidp{lpDSC}, cvoidp{lpcDSCBDesc});

    TRACE("Requested buffer:\n"
        "    Size        = {}\n"
        "    Flags       = 0x{:08x}\n"
        "    BufferBytes = {}\n"
        "    Reserved    = {}\n"
        "    wfxFormat   = {}\n"
        "    FXCount     = {}\n"
        "    DSCFXDesc   = {}",
        lpcDSCBDesc->dwSize, lpcDSCBDesc->dwFlags, lpcDSCBDesc->dwBufferBytes,
        lpcDSCBDesc->dwReserved, cvoidp{lpcDSCBDesc->lpwfxFormat}, lpcDSCBDesc->dwFXCount,
        cvoidp{lpcDSCBDesc->lpDSCFXDesc});

    auto lock = mParent.getLockGuard();
    if(mDevice)
        return DSERR_ALREADYINITIALIZED;

    if(lpcDSCBDesc->dwFXCount > 0)
    {
        WARN("Capture effects not supported");
        return DSERR_INVALIDPARAM;
    }

    if(!lpcDSCBDesc->lpwfxFormat)
        return DSERR_INVALIDPARAM;

    auto *format = lpcDSCBDesc->lpwfxFormat;
    if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        if(format->cbSize < sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX))
            return DSERR_INVALIDPARAM;
        if(format->cbSize > sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)
            && format->cbSize != sizeof(WAVEFORMATEXTENSIBLE))
            return DSERR_CONTROLUNAVAIL;

        auto *wfe = CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format);
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
        TRACE("Requested capture format:\n"
            "    FormatTag          = 0x{:04x}\n"
            "    Channels           = {}\n"
            "    SamplesPerSec      = {}\n"
            "    AvgBytesPerSec     = {}\n"
            "    BlockAlign         = {}\n"
            "    BitsPerSample      = {}\n"
            "    Size               = {}\n"
            "    ValidBitsPerSample = {}\n"
            "    ChannelMask        = 0x{:08x}\n"
            "    SubFormat          = {}",
            wfe->Format.wFormatTag, wfe->Format.nChannels, wfe->Format.nSamplesPerSec,
            wfe->Format.nAvgBytesPerSec, wfe->Format.nBlockAlign, wfe->Format.wBitsPerSample,
            wfe->Format.cbSize, wfe->Samples.wValidBitsPerSample, wfe->dwChannelMask,
            FmtidPrinter{wfe->SubFormat}.c_str());
        /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
    }
    else
    {
        TRACE("Requested capture format:\n"
            "    FormatTag          = 0x{:04x}\n"
            "    Channels           = {}\n"
            "    SamplesPerSec      = {}\n"
            "    AvgBytesPerSec     = {}\n"
            "    BlockAlign         = {}\n"
            "    BitsPerSample      = {}\n"
            "    Size               = {}",
            format->wFormatTag, format->nChannels, format->nSamplesPerSec,
            format->nAvgBytesPerSec, format->nBlockAlign, format->wBitsPerSample,
            format->cbSize);
    }

    if(format->nChannels < 1 || format->nChannels > 2)
    {
        WARN("Invalid Channels {}", format->nChannels);
        return DSERR_INVALIDPARAM;
    }
    if(format->wBitsPerSample <= 0 || (format->wBitsPerSample%8) != 0)
    {
        WARN("Invalid BitsPerSample {}", format->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(format->nBlockAlign != format->nChannels*format->wBitsPerSample/8)
    {
        WARN("Invalid BlockAlign {} (expected {} = {}*{}/8)", format->nBlockAlign,
            format->nChannels*format->wBitsPerSample/8, format->nChannels, format->wBitsPerSample);
        return DSERR_INVALIDPARAM;
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN("Invalid sample rate {}", format->nSamplesPerSec);
        return DSERR_INVALIDPARAM;
    }
    if(format->nAvgBytesPerSec != format->nSamplesPerSec*format->nBlockAlign)
    {
        WARN("Invalid AvgBytesPerSec {} (expected {} = {}*{})", format->nAvgBytesPerSec,
            format->nSamplesPerSec*format->nBlockAlign, format->nSamplesPerSec,
            format->nBlockAlign);
        return DSERR_INVALIDPARAM;
    }

    auto alformat = ALenum{AL_NONE};
    if(format->wFormatTag == WAVE_FORMAT_PCM)
    {
        if(format->nChannels == 1)
        {
            switch(format->wBitsPerSample)
            {
            case 8: alformat = AL_FORMAT_MONO8; break;
            case 16: alformat = AL_FORMAT_MONO16; break;
            default:
                WARN("Unsupported bpp {}", format->wBitsPerSample);
                return DSERR_BADFORMAT;
            }
        }
        else if(format->nChannels == 2)
        {
            switch(format->wBitsPerSample)
            {
            case 8: alformat = AL_FORMAT_STEREO8; break;
            case 16: alformat = AL_FORMAT_STEREO16; break;
            default:
                WARN("Unsupported bpp {}", format->wBitsPerSample);
                return DSERR_BADFORMAT;
            }
        }
        else
        {
            WARN("Unsupported channels: {}", format->nChannels);
            return DSERR_BADFORMAT;
        }

        mWaveFmt = {};
        mWaveFmt.Format = *format;
        mWaveFmt.Format.cbSize = 0;
    }
    else if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
        auto *wfe = CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format);
        if(wfe->SubFormat != KSDATAFORMAT_SUBTYPE_PCM)
            return DSERR_BADFORMAT;
        if(wfe->Samples.wValidBitsPerSample
            && wfe->Samples.wValidBitsPerSample != wfe->Format.wBitsPerSample)
            return DSERR_BADFORMAT;

        if(wfe->Format.nChannels == 1 && wfe->dwChannelMask == SPEAKER_FRONT_CENTER)
        {
            switch(wfe->Format.wBitsPerSample)
            {
            case 8: alformat = AL_FORMAT_MONO8; break;
            case 16: alformat = AL_FORMAT_MONO16; break;
            default:
                WARN("Unsupported bpp {}", wfe->Format.wBitsPerSample);
                return DSERR_BADFORMAT;
            }
        }
        else if(wfe->Format.nChannels == 2 && wfe->dwChannelMask == (SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT))
        {
            switch(wfe->Format.wBitsPerSample)
            {
            case 8: alformat = AL_FORMAT_STEREO8; break;
            case 16: alformat = AL_FORMAT_STEREO16; break;
            default:
                WARN("Unsupported bpp {}", wfe->Format.wBitsPerSample);
                return DSERR_BADFORMAT;
            }
        }
        else
        {
            WARN("Unsupported channels: {}, 0x{:08x}", wfe->Format.nChannels, wfe->dwChannelMask);
            return DSERR_BADFORMAT;
        }

        mWaveFmt = *wfe;
        mWaveFmt.Format.cbSize = sizeof(mWaveFmt) - sizeof(mWaveFmt.Format);
        mWaveFmt.Samples.wValidBitsPerSample = mWaveFmt.Format.wBitsPerSample;
        /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */
    }
    else
    {
        WARN("Unhandled formattag %x\n", format->wFormatTag);
        return DSERR_BADFORMAT;
    }

    if(!alformat)
    {
        WARN("Could not get OpenAL format");
        return DSERR_BADFORMAT;
    }

    if(lpcDSCBDesc->dwBufferBytes < mWaveFmt.Format.nBlockAlign
        || lpcDSCBDesc->dwBufferBytes > DWORD{std::numeric_limits<ALCsizei>::max()}
        || (lpcDSCBDesc->dwBufferBytes%mWaveFmt.Format.nBlockAlign) != 0)
    {
        WARN("Invalid BufferBytes ({} % {})", lpcDSCBDesc->dwBufferBytes,
            mWaveFmt.Format.nBlockAlign);
        return DSERR_INVALIDPARAM;
    }

    try {
        mBuffer.resize(lpcDSCBDesc->dwBufferBytes);
    }
    catch(std::exception &e) {
        ERR("Exception creating buffer: {}", e.what());
        return DSERR_OUTOFMEMORY;
    }

    mDevice = alcCaptureOpenDevice(mParent.getName().c_str(), mWaveFmt.Format.nSamplesPerSec,
        alformat, static_cast<ALCsizei>(lpcDSCBDesc->dwBufferBytes/mWaveFmt.Format.nBlockAlign));
    if(!mDevice)
    {
        ERR("Couldn't open device {} {:#x}@{}, reason: 0x{:04x}", mParent.getName(), alformat,
            mWaveFmt.Format.nSamplesPerSec, alcGetError(nullptr));
        return DSERR_INVALIDPARAM;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Lock "
HRESULT STDMETHODCALLTYPE DSCBuffer::Lock(DWORD dwReadCusor, DWORD dwReadBytes,
    LPVOID *lplpvAudioPtr1, LPDWORD lpdwAudioBytes1, LPVOID *lplpvAudioPtr2,
    LPDWORD lpdwAudioBytes2, DWORD dwFlags) noexcept
{
    DEBUG("({})->({}, {}, {}, {}, {}, {}, {:#x})", voidp{this}, dwReadCusor, dwReadBytes,
        voidp{lplpvAudioPtr1}, voidp{lpdwAudioBytes1}, voidp{lplpvAudioPtr2},
        voidp{lpdwAudioBytes2}, dwFlags);

    if(lplpvAudioPtr1) *lplpvAudioPtr1 = nullptr;
    if(lpdwAudioBytes1) *lpdwAudioBytes1 = 0;
    if(lplpvAudioPtr2) *lplpvAudioPtr2 = nullptr;
    if(lpdwAudioBytes2) *lpdwAudioBytes2 = 0;
    if(!lplpvAudioPtr1 || !lpdwAudioBytes1)
    {
        WARN("Invalid pointer/len {} {}", voidp{lplpvAudioPtr1}, voidp{lpdwAudioBytes1});
        return DSERR_INVALIDPARAM;
    }

    if(dwReadCusor > mBuffer.size())
    {
        WARN("Invalid read pos: {} > {}", dwReadCusor, mBuffer.size());
        return DSERR_INVALIDPARAM;
    }

    if((dwFlags&DSCBLOCK_ENTIREBUFFER))
        dwReadBytes = ds::saturate_cast<DWORD>(mBuffer.size());
    else if(dwReadBytes > mBuffer.size())
    {
        WARN("Invalid size: {} > {}", dwReadBytes, mBuffer.size());
        return DSERR_INVALIDPARAM;
    }

    if(mLocked.exchange(true, std::memory_order_relaxed))
    {
        WARN("Already locked");
        return DSERR_INVALIDPARAM;
    }

    auto remain = DWORD{};
    if(dwReadCusor > mBuffer.size() - dwReadBytes)
    {
        *lpdwAudioBytes1 = ds::saturate_cast<DWORD>(mBuffer.size()) - dwReadCusor;
        remain = dwReadBytes - *lpdwAudioBytes1;
    }
    else
        *lpdwAudioBytes1 = dwReadBytes;
    *lplpvAudioPtr1 = std::to_address(mBuffer.begin() + ptrdiff_t(dwReadCusor));

    if(lplpvAudioPtr2 && lpdwAudioBytes2 && remain)
    {
        *lplpvAudioPtr2 = mBuffer.data();
        *lpdwAudioBytes2 = remain;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Start "
HRESULT STDMETHODCALLTYPE DSCBuffer::Start(DWORD dwFlags) noexcept
{
    TRACE("({})->({:#x})", voidp{this}, dwFlags);

    if(!(dwFlags&DSCBSTART_LOOPING))
    {
        FIXME("Non-looping capture not currently supported");
        return DSERR_INVALIDPARAM;
    }

    auto lock = mParent.getLockGuard();
    if(!mCapturing)
    {
        mQuitNow.store(false, std::memory_order_release);
        mCaptureThread = std::thread{&DSCBuffer::captureThread, this};
        alcCaptureStart(mDevice);
        mCapturing = true;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Stop "
HRESULT STDMETHODCALLTYPE DSCBuffer::Stop() noexcept
{
    TRACE("({})->()", voidp{this});

    auto lock = mParent.getUniqueLock();
    if(mCapturing)
    {
        alcCaptureStop(mDevice);
        lock.unlock();
        mQuitNow.store(true, std::memory_order_release);
        mCaptureThread.join();
        lock.lock();

        std::ranges::for_each(mNotifies, [](const DSBPOSITIONNOTIFY &notify)
        {
            if(notify.dwOffset == static_cast<DWORD>(DSCBPN_OFFSET_STOP))
                SetEvent(notify.hEventNotify);
        });
        mCapturing = false;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Unlock "
HRESULT STDMETHODCALLTYPE DSCBuffer::Unlock(LPVOID lpvAudioPtr1, DWORD dwAudioBytes1,
    LPVOID lpvAudioPtr2, DWORD dwAudioBytes2) noexcept
{
    DEBUG("({})->({}, {}, {}, {})", voidp{this}, voidp{lpvAudioPtr1}, dwAudioBytes1,
        voidp{lpvAudioPtr2}, dwAudioBytes2);

    if(!mLocked.exchange(false, std::memory_order_relaxed))
    {
        WARN("Not locked");
        return DSERR_INVALIDPARAM;
    }

    auto const ofs1 = static_cast<uintptr_t>(static_cast<std::byte*>(lpvAudioPtr1)-mBuffer.data());
    auto const ofs2 = static_cast<uintptr_t>(lpvAudioPtr2
        ? static_cast<std::byte*>(lpvAudioPtr2)-mBuffer.data() : 0);
    if(ofs1 > mBuffer.size() || mBuffer.size()-ofs1 < dwAudioBytes1 || dwAudioBytes2 > ofs1)
        return DSERR_INVALIDPARAM;
    if(ofs2 != 0)
        return DSERR_INVALIDPARAM;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetObjectInPath "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetObjectInPath(REFGUID rguidObject, DWORD dwIndex,
    REFGUID rguidInterface, LPVOID *ppObject) noexcept
{
    FIXME("({})->({}, {}, {}, {})", voidp{this}, GuidPrinter{rguidObject}.c_str(), dwIndex,
        GuidPrinter{rguidInterface}.c_str(), voidp{ppObject});

    if(!ppObject)
        return DSERR_INVALIDPARAM;
    *ppObject = nullptr;

    return DSERR_CONTROLUNAVAIL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFXStatus "
HRESULT STDMETHODCALLTYPE DSCBuffer::GetFXStatus(DWORD dwFXCount, LPDWORD pdwFXStatus) noexcept
{
    TRACE("({})->({}, {})", voidp{this}, dwFXCount, voidp{pdwFXStatus});

    if(dwFXCount > 0)
    {
        WARN("Querying too many effects");
        return DSERR_INVALIDPARAM;
    }

    /* 0 effects is fine, that's allowed when initializing. */
    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX

#define CLASS_PREFIX "DSCBuffer::Notify::"
HRESULT STDMETHODCALLTYPE DSCBuffer::Notify::QueryInterface(REFIID riid, void** ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCBuffer::Notify::AddRef() noexcept
{
    auto *self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mNotRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCBuffer::Notify::Release() noexcept
{
    auto *self = impl_from_base();
    const auto ret = self->mNotRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetNotificationPositions "
HRESULT STDMETHODCALLTYPE DSCBuffer::Notify::SetNotificationPositions(DWORD numNotifies,
    const DSBPOSITIONNOTIFY *notifies) noexcept
{
    TRACE("({})->({}, {})", voidp{this}, numNotifies, cvoidp{notifies});

    if(!notifies && numNotifies > 0)
        return DSERR_INVALIDPARAM;

    auto *self = impl_from_base();
    auto lock = self->mParent.getLockGuard();

    if(self->mCapturing)
    {
        WARN("Setting notifications while capturing");
        return DSERR_INVALIDCALL;
    }

    auto newnots = std::vector<DSBPOSITIONNOTIFY>{};
    const auto notifyspan = std::span{notifies, numNotifies};
    if(!notifyspan.empty())
    {
        const auto invalidNotify = std::ranges::find_if_not(notifyspan,
            [self](const DSBPOSITIONNOTIFY &notify) noexcept -> bool
        {
            DEBUG(" offset = {}, event = {}", notify.dwOffset, voidp{notify.hEventNotify});
            return notify.dwOffset < self->mBuffer.size()
                || notify.dwOffset == static_cast<DWORD>(DSCBPN_OFFSET_STOP);
        });
        if(invalidNotify != notifyspan.end())
        {
            WARN("Out of range ({}: {} >= {})", std::distance(notifyspan.begin(), invalidNotify),
                invalidNotify->dwOffset, self->mBuffer.size());
            return DSERR_INVALIDPARAM;
        }
        newnots.assign(notifyspan.begin(), notifyspan.end());

        std::ranges::stable_sort(newnots, std::less{}, &DSBPOSITIONNOTIFY::dwOffset);
    }
    std::swap(self->mNotifies, newnots);

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX

} // namespace

#define CLASS_PREFIX "DSCapture::"
ComPtr<DSCapture> DSCapture::Create(bool is8)
{
    return ComPtr<DSCapture>{new DSCapture{is8}};
}

DSCapture::DSCapture(bool is8) : mIs8{is8} { }
DSCapture::~DSCapture() = default;


#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE DSCapture::QueryInterface(REFIID riid, void **ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundCapture)
    {
        AddRef();
        *ppvObject = as<IDirectSoundCapture*>();
        return S_OK;
    }

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCapture::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCapture::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    finalize();
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "CreateCaptureBuffer "
HRESULT STDMETHODCALLTYPE DSCapture::CreateCaptureBuffer(const DSCBUFFERDESC *dscBufferDesc,
    IDirectSoundCaptureBuffer **dsCaptureBuffer, IUnknown *outer) noexcept
{
    TRACE("({})->({}, {}, {})", voidp{this}, cvoidp{dscBufferDesc}, voidp{dsCaptureBuffer},
        voidp{outer});

    if(!dsCaptureBuffer)
    {
        WARN("dsCaptureBuffer is null");
        return DSERR_INVALIDPARAM;
    }
    *dsCaptureBuffer = nullptr;

    if(outer)
    {
        WARN("Aggregation isn't supported");
        return DSERR_NOAGGREGATION;
    }
    if(!dscBufferDesc || dscBufferDesc->dwSize < sizeof(DSCBUFFERDESC1))
    {
        WARN("Invalid DSCBUFFERDESC ({}, {})", cvoidp{dscBufferDesc},
            dscBufferDesc ? dscBufferDesc->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    auto bufdesc = DSCBUFFERDESC{};
    std::memcpy(&bufdesc, dscBufferDesc, std::min<DWORD>(sizeof(bufdesc), dscBufferDesc->dwSize));
    bufdesc.dwSize = sizeof(bufdesc);

    try {
        auto dscbuf = DSCBuffer::Create(*this, mIs8);
        if(auto hr = dscbuf->Initialize(this, &bufdesc); FAILED(hr))
            return hr;

        *dsCaptureBuffer = dscbuf.release()->as<IDirectSoundCaptureBuffer*>();
    }
    catch(std::exception &e) {
        ERR("Exception creating buffer: {}", e.what());
        return E_FAIL;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE DSCapture::GetCaps(DSCCAPS *dscCaps) noexcept
{
    TRACE("({})->({})", voidp{this}, voidp{dscCaps});

    auto dlock = std::unique_lock{mMutex};
    if(mDeviceName.empty())
    {
        WARN("Device not initialized");
        return DSERR_UNINITIALIZED;
    }

    if(!dscCaps)
    {
        WARN("Caps is null");
        return DSERR_INVALIDPARAM;
    }
    if(dscCaps->dwSize < sizeof(*dscCaps))
    {
        WARN("Invalid size: {}", dscCaps->dwSize);
        return DSERR_INVALIDPARAM;
    }

    dscCaps->dwFlags = DSCCAPS_CERTIFIED;
    /* Support all WAVE_FORMAT formats specified in mmsystem.h */
    dscCaps->dwFormats = 0x000fffff;
    dscCaps->dwChannels = 2;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE DSCapture::Initialize(const GUID *guid) noexcept
{
    TRACE("({})->({})", voidp{this}, DevidPrinter{guid}.c_str());

    auto dlock = std::unique_lock{mMutex};
    if(!mDeviceName.empty())
    {
        WARN("Already initialized");
        return DSERR_ALREADYINITIALIZED;
    }

    if(!guid || *guid == GUID_NULL)
        guid = &DSDEVID_DefaultCapture;
    else if(*guid == DSDEVID_DefaultPlayback || *guid == DSDEVID_DefaultVoicePlayback)
        return DSERR_NODRIVER;

    auto devguid = GUID{};
    if(const auto hr = GetDeviceID(*guid, devguid); FAILED(hr))
        return hr;

    mDeviceName = std::invoke([&devguid]() -> std::string
    {
        try {
            auto guid_str = CoTaskMemPtr<OLECHAR>{};
            const auto hr = StringFromCLSID(devguid, ds::out_ptr(guid_str));
            if(SUCCEEDED(hr)) return wstr_to_utf8(guid_str.get());

            ERR("StringFromCLSID failed: {:#x}", as_unsigned(hr));
        }
        catch(std::exception &e) {
            ERR("Exception converting GUID to string: {}", e.what());
        }
        return std::string{};
    });
    if(mDeviceName.empty())
        return DSERR_OUTOFMEMORY;

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX


#define CLASS_PREFIX "DSCapture::Unknown::"
HRESULT STDMETHODCALLTYPE DSCapture::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE DSCapture::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE DSCapture::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    self->finalize();
    return ret;
}
#undef PREFIX
#undef CLASS_PREFIX
