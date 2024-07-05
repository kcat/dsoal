#include "buffer.h"

#include <bit>
#include <cinttypes>
#include <optional>
#include <span>

#include <ks.h>
#include <ksmedia.h>
#include <vfwmsgs.h>

#include "dsoal.h"
#include "dsoundoal.h"
#include "eax.h"
#include "guidprinter.h"
#include "logging.h"
#include "primarybuffer.h"
#include "vmanager.h"


namespace {

using voidp = void*;
using cvoidp = const void*;

ALenum ConvertFormat(WAVEFORMATEXTENSIBLE &dst, const WAVEFORMATEX &src,
    const std::bitset<ExtensionCount> exts) noexcept
{
    TRACE("ConvertFormat Requested buffer format:\n"
          "    FormatTag      = 0x%04x\n"
          "    Channels       = %d\n"
          "    SamplesPerSec  = %lu\n"
          "    AvgBytesPerSec = %lu\n"
          "    BlockAlign     = %d\n"
          "    BitsPerSample  = %d\n"
          "    Size           = %d\n",
        src.wFormatTag, src.nChannels, src.nSamplesPerSec, src.nAvgBytesPerSec, src.nBlockAlign,
        src.wBitsPerSample, src.cbSize);

    dst = {};
    dst.Format = src;
    dst.Format.cbSize = 0;

    enum { UInt8, Int16, Float32 } sampleType{};
    switch(dst.Format.wFormatTag)
    {
    case WAVE_FORMAT_PCM:
        switch(dst.Format.wBitsPerSample)
        {
        case 8: sampleType = UInt8; break;
        case 16: sampleType = Int16; break;
        default:
            FIXME("ConvertFormat %u-bit integer samples not supported\n",
                dst.Format.wBitsPerSample);
            return AL_NONE;
        }
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        if(dst.Format.wBitsPerSample == 32 && exts.test(EXT_FLOAT32))
            sampleType = Float32;
        else
        {
            FIXME("ConvertFormat %u-bit floating point samples not supported\n",
                dst.Format.wBitsPerSample);
            return AL_NONE;
        }
        break;
    default:
        FIXME("ConvertFormat Format 0x%04x samples not supported\n", dst.Format.wFormatTag);
        return AL_NONE;
    }

    switch(sampleType)
    {
    case UInt8:
        switch(dst.Format.nChannels)
        {
        case 1: return AL_FORMAT_MONO8;
        case 2: return AL_FORMAT_STEREO8;
        }
        break;
    case Int16:
        switch(dst.Format.nChannels)
        {
        case 1: return AL_FORMAT_MONO16;
        case 2: return AL_FORMAT_STEREO16;
        }
        break;
    case Float32:
        switch(dst.Format.nChannels)
        {
        case 1: return AL_FORMAT_MONO_FLOAT32;
        case 2: return AL_FORMAT_STEREO_FLOAT32;
        }
        break;
    }

    FIXME("ConvertFormat Could not get OpenAL format (0x%04x, %d-bit, %d channels)\n",
        dst.Format.wFormatTag, dst.Format.wBitsPerSample, dst.Format.nChannels);
    return AL_NONE;
}

ALenum ConvertFormat(WAVEFORMATEXTENSIBLE &dst, const WAVEFORMATEXTENSIBLE &src,
    const std::bitset<ExtensionCount> exts) noexcept
{
    TRACE("ConvertFormat Requested buffer format:\n"
          "    FormatTag          = 0x%04x\n"
          "    Channels           = %u\n"
          "    SamplesPerSec      = %lu\n"
          "    AvgBytesPerSec     = %lu\n"
          "    BlockAlign         = %u\n"
          "    BitsPerSample      = %u\n"
          "    Size               = %u\n"
          "    ValidBitsPerSample = %u\n"
          "    ChannelMask        = 0x%08lx\n"
          "    SubFormat          = %s\n",
        src.Format.wFormatTag, src.Format.nChannels, src.Format.nSamplesPerSec,
        src.Format.nAvgBytesPerSec, src.Format.nBlockAlign, src.Format.wBitsPerSample,
        src.Format.cbSize, src.Samples.wValidBitsPerSample, src.dwChannelMask,
        FmtidPrinter{src.SubFormat}.c_str());

    dst = src;
    dst.Format.cbSize = sizeof(dst) - sizeof(dst.Format);

    if(!dst.Samples.wValidBitsPerSample)
        dst.Samples.wValidBitsPerSample = dst.Format.wBitsPerSample;
    else if(dst.Samples.wValidBitsPerSample != dst.Format.wBitsPerSample)
    {
        WARN("ConvertFormat Padded sample formats not supported (%d-bit total, %d-bit valid)\n",
            dst.Format.wBitsPerSample, dst.Samples.wValidBitsPerSample);
        return AL_NONE;
    }

    auto unsupported_format = [dst]
    {
        FIXME("ConvertFormat Unsupported channel configuration (%u channels, 0x%08lx)\n",
              dst.Format.nChannels, dst.dwChannelMask);
        return AL_NONE;
    };
    enum { Mono, Stereo, Quad, X51, X71 } channelConfig{};
    switch(dst.dwChannelMask)
    {
    case KSAUDIO_SPEAKER_MONO:
        if(dst.Format.nChannels == 1) channelConfig = Mono;
        else return unsupported_format();
        break;
    case KSAUDIO_SPEAKER_STEREO:
        if(dst.Format.nChannels == 2) channelConfig = Stereo;
        else return unsupported_format();
        break;
    case KSAUDIO_SPEAKER_QUAD:
        if(dst.Format.nChannels == 4 && exts.test(EXT_MCFORMATS)) channelConfig = Quad;
        else return unsupported_format();
        break;
    case KSAUDIO_SPEAKER_5POINT1_BACK:
    case KSAUDIO_SPEAKER_5POINT1_SURROUND:
        if(dst.Format.nChannels == 6 && exts.test(EXT_MCFORMATS)) channelConfig = X51;
        else return unsupported_format();
        break;
    case KSAUDIO_SPEAKER_7POINT1_SURROUND:
        if(dst.Format.nChannels == 8 && exts.test(EXT_MCFORMATS)) channelConfig = X71;
        else return unsupported_format();
        break;

    case 0:
        if(dst.Format.nChannels == 1) channelConfig = Mono;
        else if(dst.Format.nChannels == 2) channelConfig = Stereo;
        else return unsupported_format();
        break;

    default:
        return unsupported_format();
    }

    enum { UInt8, Int16, Float32 } sampleType{};
    if(dst.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
    {
        switch(dst.Format.wBitsPerSample)
        {
        case 8: sampleType = UInt8; break;
        case 16: sampleType = Int16; break;
        default:
            FIXME("ConvertFormat %u-bit integer samples not supported\n",
                dst.Format.wBitsPerSample);
            return AL_NONE;
        }
    }
    else if(dst.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
    {
        if(dst.Format.wBitsPerSample == 32 && exts.test(EXT_FLOAT32))
            sampleType = Float32;
        else
        {
            FIXME("ConvertFormat %u-bit floating point samples not supported\n",
                dst.Format.wBitsPerSample);
            return AL_NONE;
        }
    }
    else
    {
        FIXME("ConvertFormat Unsupported sample subformat %s\n",
            GuidPrinter{dst.SubFormat}.c_str());
        return AL_NONE;
    }

    switch(sampleType)
    {
    case UInt8:
        switch(channelConfig)
        {
        case Mono: return AL_FORMAT_MONO8;
        case Stereo: return AL_FORMAT_STEREO8;
        case Quad: return AL_FORMAT_QUAD8;
        case X51: return AL_FORMAT_51CHN8;
        case X71: return AL_FORMAT_71CHN8;
        }
        break;
    case Int16:
        switch(channelConfig)
        {
        case Mono: return AL_FORMAT_MONO16;
        case Stereo: return AL_FORMAT_STEREO16;
        case Quad: return AL_FORMAT_QUAD16;
        case X51: return AL_FORMAT_51CHN16;
        case X71: return AL_FORMAT_71CHN16;
        }
        break;
    case Float32:
        switch(channelConfig)
        {
        case Mono: return AL_FORMAT_MONO_FLOAT32;
        case Stereo: return AL_FORMAT_STEREO_FLOAT32;
        case Quad: return AL_FORMAT_QUAD32;
        case X51: return AL_FORMAT_51CHN32;
        case X71: return AL_FORMAT_71CHN32;
        }
        break;
    }

    FIXME("ConvertFormat Could not get OpenAL format (%d-bit, %d channels, %s)\n",
        dst.Format.wBitsPerSample, dst.Format.nChannels, GuidPrinter{dst.SubFormat}.c_str());
    return AL_NONE;
}

} // namespace

#define PREFIX "SharedBuffer::"
SharedBuffer::~SharedBuffer()
{
    if(mAlBuffer != 0)
        alDeleteBuffers(1, &mAlBuffer);
}

ds::expected<ComPtr<SharedBuffer>,HRESULT> SharedBuffer::Create(const DSBUFFERDESC &bufferDesc,
    const std::bitset<ExtensionCount> exts) noexcept
{
    const WAVEFORMATEX *format{bufferDesc.lpwfxFormat};

    if(format->nChannels <= 0)
    {
        WARN(PREFIX "Create Invalid Channels %d\n", format->nChannels);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN(PREFIX "Create Invalid SamplesPerSec %lu\n", format->nSamplesPerSec);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign <= 0)
    {
        WARN(PREFIX "Create Invalid BlockAlign %d\n", format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->wBitsPerSample == 0 || (format->wBitsPerSample%8) != 0)
    {
        WARN(PREFIX "Create Invalid BitsPerSample %d\n", format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign != format->nChannels*format->wBitsPerSample/8)
    {
        WARN(PREFIX "Create Invalid BlockAlign %d (expected %u = %u*%u/8)\n",
             format->nBlockAlign, format->nChannels*format->wBitsPerSample/8,
             format->nChannels, format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    /* HACK: Some games provide an incorrect value here and expect to work.
     * This is clearly not supposed to succeed with just anything, but until
     * the amount of leeway allowed is discovered, be very lenient.
     */
    if(format->nAvgBytesPerSec == 0)
    {
        WARN(PREFIX "Create Invalid AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
            format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
            format->nSamplesPerSec, format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nAvgBytesPerSec != format->nBlockAlign*format->nSamplesPerSec)
        WARN(PREFIX "Create Unexpected AvgBytesPerSec %lu (expected %lu = %lu*%u)\n",
            format->nAvgBytesPerSec, format->nSamplesPerSec*format->nBlockAlign,
            format->nSamplesPerSec, format->nBlockAlign);

    static constexpr DWORD LocFlags{DSBCAPS_LOCSOFTWARE | DSBCAPS_LOCHARDWARE};
    if((bufferDesc.dwFlags&LocFlags) == LocFlags)
    {
        WARN(PREFIX "Create Hardware and software location requested\n");
        return ds::unexpected(DSERR_INVALIDPARAM);
    }

    /* Round the buffer size up to the next black alignment. */
    DWORD bufSize{bufferDesc.dwBufferBytes + format->nBlockAlign - 1};
    bufSize -= bufSize%format->nBlockAlign;
    if(bufSize < DSBSIZE_MIN) return ds::unexpected(DSERR_BUFFERTOOSMALL);
    if(bufSize > DSBSIZE_MAX) return ds::unexpected(DSERR_INVALIDPARAM);

    /* Over-allocate the shared buffer, combining it with the sample storage. */
    auto shared = [bufSize] {
        try { return ComPtr<SharedBuffer>{new(ExtraBytes(bufSize)) SharedBuffer{}}; }
        catch(...) { return ComPtr<SharedBuffer>{}; }
    }();
    if(!shared)
        return ds::unexpected(DSERR_OUTOFMEMORY);
    shared->mData = {reinterpret_cast<char*>(shared.get() + 1), bufSize};
    shared->mFlags = bufferDesc.dwFlags;

    if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        static constexpr WORD ExtExtraSize{sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)};
        if(format->cbSize < ExtExtraSize)
        {
            WARN(PREFIX "Create EXTENSIBLE size too small (%u, expected %u)\n", format->cbSize,
                ExtExtraSize);
            return ds::unexpected(DSERR_INVALIDPARAM);
        }
        auto wfe{CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format)};
        shared->mAlFormat = ConvertFormat(shared->mWfxFormat, *wfe, exts);
    }
    else
        shared->mAlFormat = ConvertFormat(shared->mWfxFormat, *format, exts);
    if(!shared->mAlFormat) return ds::unexpected(DSERR_INVALIDPARAM);

    alGenBuffers(1, &shared->mAlBuffer);
    alBufferDataStatic(shared->mAlBuffer, shared->mAlFormat, shared->mData.data(),
        static_cast<ALsizei>(shared->mData.size()),
        static_cast<ALsizei>(shared->mWfxFormat.Format.nSamplesPerSec));
    alGetError();

    return shared;
}
#undef PREFIX

#define PREFIX "Buffer::"
Buffer::Buffer(DSound8OAL &parent, bool is8, IDirectSoundBuffer *original) noexcept
    : mParent{parent}, mContext{parent.getShared().mContext}, mMutex{parent.getMutex()}, mIs8{is8}
{
    mImmediate.dwSize = sizeof(mImmediate);
    mImmediate.vPosition.x = 0.0f;
    mImmediate.vPosition.y = 0.0f;
    mImmediate.vPosition.z = 0.0f;
    mImmediate.vVelocity.x = 0.0f;
    mImmediate.vVelocity.y = 0.0f;
    mImmediate.vVelocity.z = 0.0f;
    mImmediate.dwInsideConeAngle = DS3D_DEFAULTCONEANGLE;
    mImmediate.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
    mImmediate.vConeOrientation.x = 0.0f;
    mImmediate.vConeOrientation.y = 0.0f;
    mImmediate.vConeOrientation.z = 1.0f;
    mImmediate.lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;
    mImmediate.flMinDistance = DS3D_DEFAULTMINDISTANCE;
    mImmediate.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
    mImmediate.dwMode = DS3DMODE_NORMAL;

    if(original)
    {
        /* MinGW headers do not have IDirectSoundBuffer8 inherit from
         * IDirectSoundBuffer, which MSVC apparently does. Reverse the cast
         * done for Buffer -> IDirectSoundBuffer.
         */
        auto upcast = [](auto *orig)
        {
            if constexpr(std::is_base_of_v<std::remove_pointer_t<decltype(orig)>,Buffer>)
                return static_cast<Buffer*>(orig);
            else
                return static_cast<Buffer*>(std::bit_cast<IDirectSoundBuffer8*>(orig));
        };
        Buffer *orig{upcast(original)};
        mBuffer = orig->mBuffer;

        /* According to MSDN, volume isn't copied. */
        if((mBuffer->mFlags&DSBCAPS_CTRLPAN))
            mPan = orig->mPan;
        if((mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
            mFrequency = orig->mFrequency;
        if((mBuffer->mFlags&DSBCAPS_CTRL3D))
            mImmediate = orig->mImmediate;
    }
    mDeferred = mImmediate;
}

Buffer::~Buffer()
{
    ALSection alsection{mContext};

    if(mSource != 0)
    {
        alDeleteSources(1, &mSource);
        alGetError();
        mSource = 0;
        if(mLocStatus == LocStatus::Hardware)
            mParent.getShared().decHwSources();
        else if(mLocStatus == LocStatus::Software)
            mParent.getShared().decSwSources();
    }

    if(mBuffer)
    {
        if((mBuffer->mFlags&DSBCAPS_CTRL3D))
            mParent.remove3dBuffer(this);
        mBuffer = nullptr;
    }
}


bool Buffer::updateNotify() noexcept
{
    ALint state{}, ioffset{};
    alGetSourcei(mSource, AL_BYTE_OFFSET, &ioffset);
    alGetSourcei(mSource, AL_SOURCE_STATE, &state);

    if(state == AL_STOPPED)
    {
        /* If the source is AL_STOPPED, it reached the end naturally, so all
         * notifies since the last position have been hit, along with
         * DSBPN_OFFSETSTOP (-1 or max unsigned value).
         */
        for(auto &notify : mNotifies)
        {
            if(notify.dwOffset >= mLastPos)
                SetEvent(notify.hEventNotify);
        }
        mLastPos = mBuffer->mData.size();
        return false;
    }

    const DWORD offset{static_cast<DWORD>(ioffset)};
    if(offset == mLastPos) return true;

    if(offset > mLastPos)
    {
        for(auto &notify : mNotifies)
        {
            if(notify.dwOffset >= mLastPos && notify.dwOffset < offset)
                SetEvent(notify.hEventNotify);
        }
    }
    else
    {
        for(auto &notify : mNotifies)
        {
            if((notify.dwOffset >= mLastPos || notify.dwOffset < offset)
                && notify.dwOffset != static_cast<DWORD>(DSBPN_OFFSETSTOP))
                SetEvent(notify.hEventNotify);
        }
    }
    mLastPos = offset;

    return true;
}


HRESULT Buffer::setLocation(LocStatus locStatus) noexcept
{
    if(locStatus != LocStatus::Any && mLocStatus == locStatus)
        return DS_OK;
    if(locStatus == LocStatus::Any && mLocStatus != LocStatus::None)
        return DS_OK;

    /* If we have a source, we're changing location, so return the source we
     * have to get a new one.
     */
    if(mSource != 0)
    {
        alDeleteSources(1, &mSource);
        mSource = 0;
        alGetError();

        if(mLocStatus == LocStatus::Hardware)
            mParent.getShared().decHwSources();
        else
            mParent.getShared().decSwSources();
    }
    mLocStatus = LocStatus::None;

    bool ok{false};
    if(locStatus != LocStatus::Software)
    {
        ok = mParent.getShared().incHwSources();
        if(ok) locStatus = LocStatus::Hardware;
    }
    if(locStatus != LocStatus::Hardware && !ok)
    {
        ok = mParent.getShared().incSwSources();
        if(ok) locStatus = LocStatus::Software;
    }
    if(!ok)
    {
        ERR("Out of %s sources\n",
            (locStatus == LocStatus::Hardware) ? "hardware" :
            (locStatus == LocStatus::Software) ? "software" : "any"
        );
        return DSERR_ALLOCATED;
    }

    alGenSources(1, &mSource);
    alSourcef(mSource, AL_GAIN, mB_to_gain(static_cast<float>(mVolume)));
    alSourcef(mSource, AL_PITCH, (mFrequency == 0) ? 1.0f :
        static_cast<float>(mFrequency)/static_cast<float>(mBuffer->mWfxFormat.Format.nSamplesPerSec));

    if((mBuffer->mFlags&DSBCAPS_CTRL3D))
    {
        if(mImmediate.dwMode == DS3DMODE_DISABLE)
        {
            const float x{static_cast<float>(mPan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};
            alSource3f(mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
            alSource3f(mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            alSource3f(mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
            alSourcef(mSource, AL_ROLLOFF_FACTOR, 0.0f);
        }
        else
        {
            alSource3f(mSource, AL_POSITION, mImmediate.vPosition.x, mImmediate.vPosition.y,
                -mImmediate.vPosition.z);
            alSource3f(mSource, AL_VELOCITY, mImmediate.vVelocity.x, mImmediate.vVelocity.y,
                -mImmediate.vVelocity.z);
            alSource3f(mSource, AL_DIRECTION, mImmediate.vConeOrientation.x,
                mImmediate.vConeOrientation.y, -mImmediate.vConeOrientation.z);
            alSourcef(mSource, AL_ROLLOFF_FACTOR, mParent.getPrimary().getCurrentRolloffFactor());
        }
        alSourcei(mSource, AL_SOURCE_RELATIVE,
            (mImmediate.dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
        alSourcef(mSource, AL_CONE_INNER_ANGLE, static_cast<float>(mImmediate.dwInsideConeAngle));
        alSourcef(mSource, AL_CONE_OUTER_ANGLE, static_cast<float>(mImmediate.dwOutsideConeAngle));
        alSourcef(mSource, AL_CONE_OUTER_GAIN,
            mB_to_gain(static_cast<float>(mImmediate.lConeOutsideVolume)));
        alSourcef(mSource, AL_REFERENCE_DISTANCE, mImmediate.flMinDistance);
        alSourcef(mSource, AL_MAX_DISTANCE, mImmediate.flMaxDistance);
    }
    else
    {
        const ALfloat x{static_cast<ALfloat>(mPan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};

        alSource3f(mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
        alSourcef(mSource, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(mSource, AL_SOURCE_RELATIVE, AL_TRUE);
        if(mParent.haveExtension(EXT_EAX))
        {
            static std::array<GUID,EAX40_MAX_ACTIVE_FXSLOTS> NullSlots{};
            EAXSet(&EAXPROPERTYID_EAX40_Source, EAXSOURCE_ACTIVEFXSLOTID, mSource,
                NullSlots.data(), NullSlots.size()*sizeof(GUID));
        }
    }
    alGetError();

    mLocStatus = locStatus;
    return DS_OK;
}


HRESULT STDMETHODCALLTYPE Buffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG(PREFIX "QueryInterface (%p)->(%s, %p)\n", voidp{this}, IidPrinter{riid}.c_str(),
        voidp{ppvObject});

    *ppvObject = nullptr;
    if(riid == IID_IUnknown)
    {
        mUnknownIface.AddRef();
        *ppvObject = mUnknownIface.as<IUnknown*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundBuffer)
    {
        AddRef();
        *ppvObject = as<IDirectSoundBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundBuffer8)
    {
        if(!mIs8)
        {
            WARN(PREFIX "QueryInterface Requesting IDirectSoundBuffer8 iface for non-DS8 object\n");
            return E_NOINTERFACE;
        }
        AddRef();
        *ppvObject = as<IDirectSoundBuffer8*>();
        return S_OK;
    }
    if(riid == IID_IDirectSound3DBuffer)
    {
        if(!(mBuffer->mFlags&DSBCAPS_CTRL3D))
        {
            WARN(PREFIX "QueryInterface Requesting IDirectSound3DBuffer iface without DSBCAPS_CTRL3D\n");
            return E_NOINTERFACE;
        }
        mBuffer3D.AddRef();
        *ppvObject = mBuffer3D.as<IDirectSound3DBuffer*>();
        return S_OK;
    }
    if(riid == IID_IDirectSoundNotify)
    {
        if(!(mBuffer->mFlags&DSBCAPS_CTRLPOSITIONNOTIFY))
        {
            WARN(PREFIX "QueryInterface Requesting IDirectSoundNotify iface without DSBCAPS_CTRLPOSITIONNOTIFY\n");
            return E_NOINTERFACE;
        }
        mNotify.AddRef();
        *ppvObject = mNotify.as<IDirectSoundNotify*>();
        return S_OK;
    }
    if(riid == IID_IKsPropertySet)
    {
        mProp.AddRef();
        *ppvObject = mProp.as<IKsPropertySet*>();
        return S_OK;
    }

    FIXME(PREFIX "QueryInterface Unhandled GUID: %s\n", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Buffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        mParent.dispose(this);
    return ret;
}


HRESULT STDMETHODCALLTYPE Buffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    DEBUG(PREFIX "GetCaps (%p)->(%p)\n", voidp{this}, voidp{bufferCaps});

    if(!bufferCaps || bufferCaps->dwSize < sizeof(*bufferCaps))
    {
        WARN(PREFIX "GetCaps Invalid DSBCAPS (%p, %lu)\n", voidp{bufferCaps},
            (bufferCaps ? bufferCaps->dwSize : 0));
        return DSERR_INVALIDPARAM;
    }

    bufferCaps->dwFlags = mBuffer->mFlags;
    if(!(mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        if(mLocStatus == LocStatus::Hardware)
            bufferCaps->dwFlags |= DSBCAPS_LOCHARDWARE;
        else if(mLocStatus == LocStatus::Software)
            bufferCaps->dwFlags |= DSBCAPS_LOCSOFTWARE;
    }
    bufferCaps->dwBufferBytes = static_cast<DWORD>(mBuffer->mData.size());
    bufferCaps->dwUnlockTransferRate = 4096;
    bufferCaps->dwPlayCpuOverhead = 0;

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    DEBUG(PREFIX "GetCurrentPosition (%p)->(%p, %p)\n", voidp{this}, voidp{playCursor},
        voidp{writeCursor});

    ALint status{AL_INITIAL};
    ALint ofs{0};

    if(mSource != 0)
    {
        ALSection alsection{mContext};
        alGetSourcei(mSource, AL_BYTE_OFFSET, &ofs);
        alGetSourcei(mSource, AL_SOURCE_STATE, &status);
        alGetError();
    }

    auto &format = mBuffer->mWfxFormat.Format;
    DWORD pos, writecursor;
    if(status == AL_PLAYING)
    {
        pos = static_cast<ALuint>(ofs);
        writecursor = format.nSamplesPerSec / (1000 / 20);
        writecursor *= format.nBlockAlign;
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
        case AL_STOPPED: pos = static_cast<DWORD>(mBuffer->mData.size()); break;
        case AL_PAUSED: pos = static_cast<ALuint>(ofs); break;
        case AL_INITIAL: pos = mLastPos; break;
        default: pos = 0;
        }
        writecursor = 0;
    }

    if(pos > mBuffer->mData.size())
    {
        ERR(PREFIX "GetCurrentPosition playpos > buf_size\n");
        pos %= mBuffer->mData.size();
    }
    writecursor = (writecursor+pos) % mBuffer->mData.size();

    DEBUG(PREFIX "GetCurrentPosition pos = %lu, write pos = %lu\n", pos, writecursor);

    if(playCursor) *playCursor = pos;
    if(writeCursor)  *writeCursor = writecursor;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    DEBUG(PREFIX "GetFormat (%p)->(%p, %lu, %p)\n", voidp{this}, voidp{wfx},
        sizeAllocated, voidp{sizeWritten});

    if(!wfx && !sizeWritten)
    {
        WARN(PREFIX "GetFormat Cannot report format or format size\n");
        return DSERR_INVALIDPARAM;
    }

    const DWORD size{sizeof(mBuffer->mWfxFormat.Format) + mBuffer->mWfxFormat.Format.cbSize};
    if(sizeWritten)
        *sizeWritten = size;
    if(wfx)
    {
        if(sizeAllocated < size)
            return DSERR_INVALIDPARAM;
        std::memcpy(wfx, &mBuffer->mWfxFormat.Format, size);
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetVolume(LONG *volume) noexcept
{
    DEBUG(PREFIX "GetVolume (%p)->(%p)\n", voidp{this}, voidp{volume});

    if(!volume)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    *volume = mVolume;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetPan(LONG *pan) noexcept
{
    DEBUG(PREFIX "GetPan (%p)->(%p)\n", voidp{this}, voidp{pan});

    if(!pan)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    *pan = mPan;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetFrequency(DWORD *frequency) noexcept
{
    FIXME(PREFIX "GetFrequency (%p)->(%p)\n", voidp{this}, voidp{frequency});

    if(!frequency)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    *frequency = mFrequency;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::GetStatus(DWORD *status) noexcept
{
    DEBUG(PREFIX "GetStatus (%p)->(%p)\n", voidp{this}, voidp{status});

    if(!status)
        return DSERR_INVALIDPARAM;
    *status = 0;

    ALint state{AL_INITIAL};
    ALint looping{AL_FALSE};
    if(mSource != 0)
    {
        ALSection alsection{mContext};
        alGetSourcei(mSource, AL_SOURCE_STATE, &state);
        alGetSourcei(mSource, AL_LOOPING, &looping);
        alGetError();
    }

    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
        *status |= static_cast<DWORD>(mLocStatus);
    if(state == AL_PLAYING)
        *status |= DSBSTATUS_PLAYING | (looping ? DSBSTATUS_LOOPING : 0);

    DEBUG(PREFIX "GetStatus status = 0x%08lx\n", *status);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    DEBUG(PREFIX "Initialize (%p)->(%p, %p)\n", voidp{this}, voidp{directSound},
        cvoidp{dsBufferDesc});

    std::unique_lock lock{mMutex};
    if(mIsInitialized) return DSERR_ALREADYINITIALIZED;

    ALSection alsection{mContext};
    if(!mBuffer)
    {
        if(!dsBufferDesc)
        {
            WARN(PREFIX "Initialize Missing buffer description\n");
            return DSERR_INVALIDPARAM;
        }
        if(!dsBufferDesc->lpwfxFormat)
        {
            WARN(PREFIX "Initialize Missing buffer format\n");
            return DSERR_INVALIDPARAM;
        }
        if((dsBufferDesc->dwFlags&DSBCAPS_CTRL3D) && dsBufferDesc->lpwfxFormat->nChannels != 1)
        {
            if(mIs8)
            {
                /* DirectSoundBuffer8 objects aren't allowed non-mono 3D
                 * buffers.
                 */
                WARN(PREFIX "Initialize Can't create multi-channel 3D buffers\n");
                return DSERR_INVALIDPARAM;
            }

            static bool once{};
            if(!once)
            {
                once = true;
                ERR(PREFIX "Initialize Multi-channel 3D sounds are not spatialized\n");
            }
        }
        if((dsBufferDesc->dwFlags&DSBCAPS_CTRLPAN) && dsBufferDesc->lpwfxFormat->nChannels != 1)
        {
            static bool once{false};
            if(!once)
            {
                once = true;
                ERR(PREFIX "Initialize Panning for multi-channel buffers is not supported\n");
            }
        }

        auto shared = SharedBuffer::Create(*dsBufferDesc, mParent.getExtensions());
        if(!shared) return shared.error();
        mBuffer = std::move(shared.value());
    }

    if(!mFrequency)
        mFrequency = mBuffer->mWfxFormat.Format.nSamplesPerSec;

    if((mBuffer->mFlags&DSBCAPS_CTRL3D))
        mParent.add3dBuffer(this);

    HRESULT hr{DS_OK};
    if(!(mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        LocStatus locStatus{LocStatus::Any};
        if((mBuffer->mFlags&DSBCAPS_LOCHARDWARE)) locStatus = LocStatus::Hardware;
        else if((mBuffer->mFlags&DSBCAPS_LOCSOFTWARE)) locStatus = LocStatus::Software;
        hr = setLocation(locStatus);
    }
    mIsInitialized = SUCCEEDED(hr);

    return hr;
}

HRESULT STDMETHODCALLTYPE Buffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    DEBUG(PREFIX "Lock (%p)->(%lu, %lu, %p, %p, %p, %p, %lu)\n", voidp{this}, offset, bytes,
        voidp{audioPtr1}, voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);

    if(!audioPtr1 || !audioBytes1)
    {
        WARN(PREFIX "Lock Invalid pointer/len %p %p\n", voidp{audioPtr1}, voidp{audioBytes1});
        return DSERR_INVALIDPARAM;
    }

    *audioPtr1 = nullptr;
    *audioBytes1 = 0;
    if(audioPtr2) *audioPtr2 = nullptr;
    if(audioBytes2) *audioBytes2 = 0;

    if((flags&DSBLOCK_FROMWRITECURSOR))
        GetCurrentPosition(nullptr, &offset);
    else if(offset >= mBuffer->mData.size())
    {
        WARN(PREFIX "Lock Invalid offset %lu\n", offset);
        return DSERR_INVALIDPARAM;
    }
    if((flags&DSBLOCK_ENTIREBUFFER))
        bytes = static_cast<DWORD>(mBuffer->mData.size());
    else if(bytes > mBuffer->mData.size())
    {
        WARN(PREFIX "Lock Invalid size %lu\n", bytes);
        return DSERR_INVALIDPARAM;
    }

    if(mLocked.exchange(true, std::memory_order_relaxed))
    {
        WARN(PREFIX "Lock Already locked\n");
        return DSERR_INVALIDPARAM;
    }

    DWORD remain{};
    *audioPtr1 = std::to_address(mBuffer->mData.begin() + ptrdiff_t(offset));
    if(bytes > mBuffer->mData.size()-offset)
    {
        *audioBytes1 = static_cast<DWORD>(mBuffer->mData.size() - offset);
        remain = bytes - *audioBytes1;
    }
    else
    {
        *audioBytes1 = bytes;
        remain = 0;
    }

    if(audioPtr2 && audioBytes2 && remain)
    {
        *audioPtr2 = mBuffer->mData.data();
        *audioBytes2 = remain;
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Play(DWORD reserved1, DWORD priority, DWORD flags) noexcept
{
    DEBUG(PREFIX "Play (%p)->(%lu, %lu, %lu)\n", voidp{this}, reserved1, priority, flags);

    std::unique_lock lock{mMutex};
    if(mBufferLost) UNLIKELY
    {
        WARN(PREFIX "Play Buffer lost\n");
        return DSERR_BUFFERLOST;
    }

    ALSection alsection{mContext};
    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        LocStatus loc{LocStatus::Any};

        static constexpr DWORD LocFlags{DSBPLAY_LOCSOFTWARE | DSBPLAY_LOCHARDWARE};
        if((flags&LocFlags) == LocFlags)
        {
            WARN(PREFIX "Play Both hardware and software specified\n");
            return DSERR_INVALIDPARAM;
        }

        if((flags&DSBPLAY_LOCHARDWARE)) loc = LocStatus::Hardware;
        else if((flags&DSBPLAY_LOCSOFTWARE)) loc = LocStatus::Software;

        if(loc != LocStatus::Any && mLocStatus != LocStatus::None && loc != mLocStatus)
        {
            ALint state{};
            alGetSourcei(mSource, AL_SOURCE_STATE, &state);
            alGetError();

            if(state == AL_PLAYING)
            {
                FIXME(PREFIX "Play Attemping to change location on playing buffer\n");
                return DSERR_INVALIDPARAM;
            }
        }

        HRESULT hr{setLocation(loc)};
        if(FAILED(hr)) return hr;
    }
    else if(priority != 0)
    {
        ERR(PREFIX "Play Invalid priority for non-deferred buffer, %lu.\n", priority);
        return DSERR_INVALIDPARAM;
    }

    ALint state{};
    alSourcei(mSource, AL_LOOPING, (flags&DSBPLAY_LOOPING) ? AL_TRUE : AL_FALSE);
    alGetSourcei(mSource, AL_SOURCE_STATE, &state);
    alGetError();

    if(state == AL_PLAYING)
        return DS_OK;

    mLastPos %= mBuffer->mData.size();
    if(state == AL_INITIAL)
    {
        alSourcei(mSource, AL_BUFFER, static_cast<ALint>(mBuffer->mAlBuffer));
        alSourcei(mSource, AL_BYTE_OFFSET, static_cast<ALint>(mLastPos));
    }
    alSourcePlay(mSource);
    if(alGetError() != AL_NO_ERROR)
    {
        ERR(PREFIX "Play Couldn't start source\n");
        return DSERR_GENERIC;
    }

    if((mBuffer->mFlags&DSBCAPS_CTRLPOSITIONNOTIFY) && !mNotifies.empty())
        mParent.addNotifyBuffer(this);

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    FIXME("Buffer::SetCurrentPosition (%p)->(%lu)\n", voidp{this}, newPosition);

    if(newPosition >= mBuffer->mData.size())
        return DSERR_INVALIDPARAM;
    newPosition -= newPosition % mBuffer->mWfxFormat.Format.nBlockAlign;

    std::unique_lock lock{mMutex};
    if(mSource != 0)
    {
        ALSection alsection{mContext};
        alSourcei(mSource, AL_BYTE_OFFSET, static_cast<ALint>(newPosition));
        alGetError();
    }
    mLastPos = newPosition;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFormat(const WAVEFORMATEX *wfx) noexcept
{
    DEBUG("Buffer::SetFormat (%p)->(%p)\n", voidp{this}, cvoidp{wfx});
    return DSERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Buffer::SetVolume(LONG volume) noexcept
{
    DEBUG(PREFIX "SetVolume (%p)->(%ld)\n", voidp{this}, volume);

    if(volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
    {
        WARN(PREFIX "SetVolume Invalid volume (%ld)\n", volume);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    std::unique_lock lock{mMutex};
    mVolume = volume;
    if(mSource != 0) LIKELY
    {
        ALSection alsection{mContext};
        alSourcef(mSource, AL_GAIN, mB_to_gain(static_cast<float>(volume)));
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetPan(LONG pan) noexcept
{
    DEBUG(PREFIX "SetPan (%p)->(%ld)\n", voidp{this}, pan);

    if(pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
    {
        WARN(PREFIX "SetPan Invalid parameter: %ld\n", pan);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    std::unique_lock lock{mMutex};
    mPan = pan;
    if((!(mBuffer->mFlags&DSBCAPS_CTRL3D) || mImmediate.dwMode == DS3DMODE_DISABLE)
        && mSource != 0) LIKELY
    {
        ALSection alsection{mContext};
        /* NOTE: Strict movement along the X plane can cause the sound to jump
         * between left and right sharply. Using a curved path helps smooth it
         * out.
         */
        const float x{static_cast<float>(pan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};
        alSource3f(mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFrequency(DWORD frequency) noexcept
{
    DEBUG(PREFIX "SetFrequency (%p)->(%lu)\n", voidp{this}, frequency);

    if(frequency != 0 && (frequency < DSBFREQUENCY_MIN || frequency > DSBFREQUENCY_MAX))
    {
        WARN(PREFIX "SetFrequency Invalid parameter: %lu\n", frequency);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    std::unique_lock lock{mMutex};
    mFrequency = frequency ? frequency : mBuffer->mWfxFormat.Format.nSamplesPerSec;
    if(mSource != 0)
    {
        ALSection alsection{mContext};
        const float pitch{static_cast<float>(mFrequency) /
            static_cast<float>(mBuffer->mWfxFormat.Format.nSamplesPerSec)};
        alSourcef(mSource, AL_PITCH, pitch);
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Stop() noexcept
{
    DEBUG(PREFIX "Stop (%p)->()\n", voidp{this});

    std::unique_lock lock{mMutex};
    if(mSource == 0) UNLIKELY
        return DS_OK;

    ALSection alsection{mContext};
    ALint ofs, state;
    alSourcePause(mSource);
    alGetSourcei(mSource, AL_BYTE_OFFSET, &ofs);
    alGetSourcei(mSource, AL_SOURCE_STATE, &state);
    alGetError();

    if((mBuffer->mFlags&DSBCAPS_CTRLPOSITIONNOTIFY))
    {
        mParent.triggerNotifies();
        mParent.removeNotifyBuffer(this);
    }

    /* Ensure the notification's last tracked position is updated. */
    mLastPos = (state == AL_STOPPED) ? static_cast<DWORD>(mBuffer->mData.size())
        : static_cast<DWORD>(ofs);

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    DEBUG(PREFIX "Unlock (%p)->(%p, %lu, %p, %lu)\n", voidp{this}, audioPtr1, audioBytes1,
        audioPtr2, audioBytes2);

    if(!mLocked.exchange(false, std::memory_order_relaxed))
    {
        WARN(PREFIX "Unlock Not locked\n");
        return DSERR_INVALIDPARAM;
    }

    /* Make sure offset is between boundary and boundary + bufsize */
    auto ofs1 = static_cast<uintptr_t>(static_cast<char*>(audioPtr1) - mBuffer->mData.data());
    auto ofs2 = static_cast<uintptr_t>(audioPtr2 ?
        static_cast<char*>(audioPtr2) - mBuffer->mData.data() : 0);
    if(ofs1 >= mBuffer->mData.size() || mBuffer->mData.size()-ofs1 < audioBytes1 || ofs2 != 0
        || audioBytes2 > ofs1)
    {
        WARN(PREFIX "Unlock Invalid parameters (%p,%zu) (%p,%lu,%p,%lu)\n",
            voidp{mBuffer->mData.data()}, mBuffer->mData.size(), audioPtr1, audioBytes1, audioPtr2,
            audioBytes2);
        return DSERR_INVALIDPARAM;
    }

    /* NOTE: The data was written directly to the buffer, so there's nothing to
     * transfer.
     */

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Restore() noexcept
{
    DEBUG("Buffer::Restore (%p)->()\n", voidp{this});

    std::unique_lock lock{mMutex};
    if(mParent.getPriorityLevel() == DSSCL_WRITEPRIMARY)
        return DSERR_BUFFERLOST;

    mBufferLost = false;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::SetFX(DWORD effectsCount, DSEFFECTDESC *dsFXDesc, DWORD *resultCodes) noexcept
{
    FIXME("Buffer::SetFX (%p)->(%lu, %p, %p)\n", voidp{this}, effectsCount, voidp{dsFXDesc},
        voidp{resultCodes});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::AcquireResources(DWORD flags, DWORD effectsCount, DWORD *resultCodes) noexcept
{
    FIXME("Buffer::AcquireResources (%p)->(%lu, %lu, %p)\n", voidp{this}, flags, effectsCount,
        voidp{resultCodes});
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE Buffer::GetObjectInPath(REFGUID objectId, DWORD index, REFGUID interfaceId, void **ppObject) noexcept
{
    FIXME("Buffer::GetObjectInPath (%p)->(%s, %lu, %s, %p)\n", voidp{this},
        GuidPrinter{objectId}.c_str(), index, GuidPrinter{interfaceId}.c_str(), voidp{ppObject});
    return E_NOTIMPL;
}


void Buffer::setParams(const DS3DBUFFER &params, const std::bitset<FlagCount> flags)
{
    /* Copy deferred parameters first. */
    if(flags.test(Position))
        mImmediate.vPosition = params.vPosition;
    if(flags.test(Velocity))
        mImmediate.vVelocity = params.vVelocity;
    if(flags.test(ConeAngles))
    {
        mImmediate.dwInsideConeAngle = params.dwInsideConeAngle;
        mImmediate.dwOutsideConeAngle = params.dwOutsideConeAngle;
    }
    if(flags.test(ConeOrientation))
        mImmediate.vConeOrientation = params.vConeOrientation;
    if(flags.test(ConeVolume))
        mImmediate.lConeOutsideVolume = params.lConeOutsideVolume;
    if(flags.test(MinDistance))
        mImmediate.flMinDistance = params.flMinDistance;
    if(flags.test(MaxDistance))
        mImmediate.flMaxDistance = params.flMaxDistance;
    if(flags.test(Mode))
        mImmediate.dwMode = params.dwMode;

    /* Now apply what's changed to OpenAL. */
    if(!mSource) UNLIKELY return;

    if(mImmediate.dwMode != DS3DMODE_DISABLE)
    {
        if(flags.test(Position))
            alSource3f(mSource, AL_POSITION, params.vPosition.x, params.vPosition.y,
                -params.vPosition.z);
        if(flags.test(Velocity))
            alSource3f(mSource, AL_VELOCITY, params.vVelocity.x, params.vVelocity.y,
                -params.vVelocity.z);
        if(flags.test(ConeOrientation))
            alSource3f(mSource, AL_DIRECTION, params.vConeOrientation.x, params.vConeOrientation.y,
                -params.vConeOrientation.z);
    }
    if(flags.test(ConeAngles))
    {
        alSourcei(mSource, AL_CONE_INNER_ANGLE, static_cast<ALint>(params.dwInsideConeAngle));
        alSourcei(mSource, AL_CONE_OUTER_ANGLE, static_cast<ALint>(params.dwOutsideConeAngle));
    }
    if(flags.test(ConeVolume))
        alSourcef(mSource, AL_CONE_OUTER_GAIN,
            mB_to_gain(static_cast<float>(params.lConeOutsideVolume)));
    if(flags.test(MinDistance))
        alSourcef(mSource, AL_REFERENCE_DISTANCE, params.flMinDistance);
    if(flags.test(MaxDistance))
        alSourcef(mSource, AL_MAX_DISTANCE, params.flMaxDistance);
    if(flags.test(Mode))
    {
        if(params.dwMode == DS3DMODE_DISABLE)
        {
            const float x{static_cast<float>(mPan-DSBPAN_LEFT)/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};
            alSource3f(mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
            alSource3f(mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            alSource3f(mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
            alSourcef(mSource, AL_ROLLOFF_FACTOR, 0.0f);
        }
        else
        {
            alSource3f(mSource, AL_POSITION, mImmediate.vPosition.x, mImmediate.vPosition.y,
                -mImmediate.vPosition.z);
            alSource3f(mSource, AL_VELOCITY, mImmediate.vVelocity.x, mImmediate.vVelocity.y,
                -mImmediate.vVelocity.z);
            alSource3f(mSource, AL_DIRECTION, mImmediate.vConeOrientation.x,
                mImmediate.vConeOrientation.y, -mImmediate.vConeOrientation.z);
            alSourcef(mSource, AL_ROLLOFF_FACTOR, mParent.getPrimary().getCurrentRolloffFactor());
        }
        alSourcei(mSource, AL_SOURCE_RELATIVE,
            (params.dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
    }
}
#undef PREFIX


/*** IDirectSound3DBuffer interface. ***/
#define PREFIX "Buffer3D::"
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Buffer3D::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDs3dRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Buffer3D::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDs3dRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetAllParameters(DS3DBUFFER *ds3dBuffer) noexcept
{
    DEBUG(PREFIX "GetAllParameters (%p)->(%p)\n", voidp{this}, voidp{ds3dBuffer});

    if(!ds3dBuffer || ds3dBuffer->dwSize < sizeof(*ds3dBuffer))
    {
        WARN(PREFIX "GetAllParameters Invalid parameters %p %lu\n", voidp{ds3dBuffer},
            ds3dBuffer ? ds3dBuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    ds3dBuffer->vPosition = self->mImmediate.vPosition;
    ds3dBuffer->vVelocity = self->mImmediate.vVelocity;
    ds3dBuffer->dwInsideConeAngle = self->mImmediate.dwInsideConeAngle;
    ds3dBuffer->dwOutsideConeAngle = self->mImmediate.dwOutsideConeAngle;
    ds3dBuffer->vConeOrientation = self->mImmediate.vConeOrientation;
    ds3dBuffer->lConeOutsideVolume = self->mImmediate.lConeOutsideVolume;
    ds3dBuffer->flMinDistance = self->mImmediate.flMinDistance;
    ds3dBuffer->flMaxDistance = self->mImmediate.flMaxDistance;
    ds3dBuffer->dwMode = self->mImmediate.dwMode;

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeAngles(DWORD *insideConeAngle, DWORD *outsideConeAngle) noexcept
{
    DEBUG(PREFIX "GetConeAngles (%p)->(%p, %p)\n", voidp{this}, voidp{insideConeAngle},
        voidp{outsideConeAngle});

    if(!insideConeAngle || !outsideConeAngle)
    {
        WARN(PREFIX "GetConeAngles Invalid pointers (%p, %p)\n", voidp{insideConeAngle},
            voidp{outsideConeAngle});
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *insideConeAngle = self->mImmediate.dwInsideConeAngle;
    *outsideConeAngle = self->mImmediate.dwOutsideConeAngle;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOrientation(D3DVECTOR *orientation) noexcept
{
    DEBUG(PREFIX "GetConeOrientation (%p)->(%p)\n", voidp{this}, voidp{orientation});

    if(!orientation)
    {
        WARN(PREFIX "GetConeOrientation Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *orientation = self->mImmediate.vConeOrientation;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOutsideVolume(LONG *coneOutsideVolume) noexcept
{
    DEBUG(PREFIX "GetConeOutsideVolume (%p)->(%p)\n", voidp{this}, voidp{coneOutsideVolume});

    if(!coneOutsideVolume)
    {
        WARN(PREFIX "GetConeOutsideVolume Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *coneOutsideVolume = self->mImmediate.lConeOutsideVolume;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMaxDistance(D3DVALUE *maxDistance) noexcept
{
    DEBUG(PREFIX "GetMaxDistance (%p)->(%p)\n", voidp{this}, voidp{maxDistance});

    if(!maxDistance)
    {
        WARN(PREFIX "GetMaxDistance Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *maxDistance = self->mImmediate.flMaxDistance;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMinDistance(D3DVALUE *minDistance) noexcept
{
    DEBUG(PREFIX "GetMinDistance (%p)->(%p)\n", voidp{this}, voidp{minDistance});

    if(!minDistance)
    {
        WARN(PREFIX "GetMinDistance Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *minDistance = self->mImmediate.flMinDistance;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMode(DWORD *mode) noexcept
{
    DEBUG(PREFIX "GetMode (%p)->(%p)\n", voidp{this}, voidp{mode});

    if(!mode)
    {
        WARN(PREFIX "GetMode Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *mode = self->mImmediate.dwMode;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetPosition(D3DVECTOR *position) noexcept
{
    DEBUG(PREFIX "GetPosition (%p)->(%p)\n", voidp{this}, voidp{position});

    if(!position)
    {
        WARN(PREFIX "GetPosition Invalid pointer\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *position = self->mImmediate.vPosition;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetVelocity(D3DVECTOR *velocity) noexcept
{
    DEBUG(PREFIX "GetVelocity (%p)->(%p)\n", voidp{this}, voidp{velocity});

    if(!velocity)
    {
        WARN(PREFIX "GetVelocity Invalid pointer)\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    *velocity = self->mImmediate.vVelocity;
    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetAllParameters(const DS3DBUFFER *ds3dBuffer, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetAllParameters (%p)->(%p, %lu)\n", voidp{this}, cvoidp{ds3dBuffer}, apply);

    if(!ds3dBuffer || ds3dBuffer->dwSize < sizeof(*ds3dBuffer))
    {
        WARN(PREFIX "SetAllParameters Invalid DS3DBUFFER (%p, %lu)\n", cvoidp{ds3dBuffer},
            ds3dBuffer ? ds3dBuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->dwInsideConeAngle > DS3D_MAXCONEANGLE
        || ds3dBuffer->dwOutsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN(PREFIX "SetAllParameters Invalid cone angles (%lu, %lu)\n",
            ds3dBuffer->dwInsideConeAngle, ds3dBuffer->dwOutsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->lConeOutsideVolume > DSBVOLUME_MAX
        || ds3dBuffer->lConeOutsideVolume < DSBVOLUME_MIN)
    {
        WARN(PREFIX "SetAllParameters Invalid cone outside volume (%ld)\n",
            ds3dBuffer->lConeOutsideVolume);
        return DSERR_INVALIDPARAM;
    }

    if(!(ds3dBuffer->flMinDistance >= 0.0f))
    {
        WARN(PREFIX "SetAllParameters Invalid min distance (%f)\n", ds3dBuffer->flMinDistance);
        return DSERR_INVALIDPARAM;
    }

    if(!(ds3dBuffer->flMaxDistance >= 0.0f))
    {
        WARN(PREFIX "SetAllParameters Invalid max distance (%f)\n", ds3dBuffer->flMaxDistance);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->dwMode != DS3DMODE_NORMAL && ds3dBuffer->dwMode != DS3DMODE_HEADRELATIVE
        && ds3dBuffer->dwMode != DS3DMODE_DISABLE)
    {
        WARN(PREFIX "SetAllParameters Invalid mode (%lu)\n", ds3dBuffer->dwMode);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred = *ds3dBuffer;
        self->mDeferred.dwSize = sizeof(self->mDeferred);
        self->mDirty.set();
    }
    else
    {
        ALSection alsection{self->mContext};
        self->setParams(*ds3dBuffer, ~0ull);
        alGetError();
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeAngles(DWORD insideConeAngle, DWORD outsideConeAngle, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetConeAngles (%p)->(%lu, %lu, %lu)\n", voidp{this}, insideConeAngle,
        outsideConeAngle, apply);

    if(insideConeAngle > DS3D_MAXCONEANGLE || outsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN(PREFIX "SetConeAngles Invalid cone angles (%lu, %lu)\n", insideConeAngle,
            outsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.dwInsideConeAngle = insideConeAngle;
        self->mDeferred.dwOutsideConeAngle = outsideConeAngle;
        self->mDirty.set(ConeAngles);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.dwInsideConeAngle = insideConeAngle;
        self->mImmediate.dwOutsideConeAngle = outsideConeAngle;

        if(self->mSource != 0)
        {
            alSourcei(self->mSource, AL_CONE_INNER_ANGLE, static_cast<ALint>(insideConeAngle));
            alSourcei(self->mSource, AL_CONE_OUTER_ANGLE, static_cast<ALint>(outsideConeAngle));
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetConeOrientation (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vConeOrientation.x = x;
        self->mDeferred.vConeOrientation.y = y;
        self->mDeferred.vConeOrientation.z = z;
        self->mDirty.set(ConeOrientation);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.vConeOrientation.x = x;
        self->mImmediate.vConeOrientation.y = y;
        self->mImmediate.vConeOrientation.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3f(self->mSource, AL_DIRECTION, x, y, -z);
            alGetError();
        }
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOutsideVolume(LONG coneOutsideVolume, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetConeOutsideVolume (%p)->(%ld, %lu)\n", voidp{this}, coneOutsideVolume, apply);

    if(coneOutsideVolume > DSBVOLUME_MAX || coneOutsideVolume < DSBVOLUME_MIN)
    {
        WARN(PREFIX "SetConeOutsideVolume Invalid cone outside volume (%ld)\n", coneOutsideVolume);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.lConeOutsideVolume = coneOutsideVolume;
        self->mDirty.set(ConeVolume);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.lConeOutsideVolume = coneOutsideVolume;

        if(self->mSource != 0)
            alSourcef(self->mSource, AL_CONE_OUTER_GAIN,
                mB_to_gain(static_cast<float>(coneOutsideVolume)));
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMaxDistance(D3DVALUE maxDistance, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetMaxDistance (%p)->(%f, %lu)\n", voidp{this}, maxDistance, apply);

    if(!(maxDistance >= 0.0f))
    {
        WARN(PREFIX "SetMaxDistance Invalid max distance (%f)\n", maxDistance);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flMaxDistance = maxDistance;
        self->mDirty.set(MaxDistance);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.flMaxDistance = maxDistance;

        if(self->mSource != 0)
            alSourcef(self->mSource, AL_MAX_DISTANCE, maxDistance);
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMinDistance(D3DVALUE minDistance, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetMinDistace (%p)->(%f, %lu)\n", voidp{this}, minDistance, apply);

    if(!(minDistance >= 0.0f))
    {
        WARN(PREFIX "SetMinDistance Invalid min distance (%f)\n", minDistance);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flMinDistance = minDistance;
        self->mDirty.set(MinDistance);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.flMinDistance = minDistance;

        if(self->mSource != 0)
            alSourcef(self->mSource, AL_REFERENCE_DISTANCE, minDistance);
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMode(DWORD mode, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetMode (%p)->(%lu, %lu)\n", voidp{this}, mode, apply);

    if(mode != DS3DMODE_NORMAL && mode != DS3DMODE_HEADRELATIVE && mode != DS3DMODE_DISABLE)
    {
        WARN(PREFIX "SetMode Invalid mode (%lu)\n", mode);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.dwMode = mode;
        self->mDirty.set(Mode);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.dwMode = mode;

        if(self->mSource != 0)
        {
            if(mode == DS3DMODE_DISABLE)
            {
                const float pandiff{static_cast<float>(self->mPan - DSBPAN_LEFT)};
                const float x{pandiff/(DSBPAN_RIGHT-DSBPAN_LEFT) - 0.5f};

                alSource3f(self->mSource, AL_POSITION, x, 0.0f, -std::sqrt(1.0f - x*x));
                alSource3f(self->mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
                alSource3f(self->mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
                alSourcef(self->mSource, AL_ROLLOFF_FACTOR, 0.0f);
            }
            else
            {
                alSource3f(self->mSource, AL_POSITION, self->mImmediate.vPosition.x,
                    self->mImmediate.vPosition.y, -self->mImmediate.vPosition.z);
                alSource3f(self->mSource, AL_VELOCITY, self->mImmediate.vVelocity.x,
                    self->mImmediate.vVelocity.y, -self->mImmediate.vVelocity.z);
                alSource3f(self->mSource, AL_DIRECTION, self->mImmediate.vConeOrientation.x,
                    self->mImmediate.vConeOrientation.y, -self->mImmediate.vConeOrientation.z);
                alSourcef(self->mSource, AL_ROLLOFF_FACTOR,
                    self->mParent.getPrimary().getCurrentRolloffFactor());
            }
            alSourcei(self->mSource, AL_SOURCE_RELATIVE,
                (mode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
            alGetError();
        }
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetPosition (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vPosition.x = x;
        self->mDeferred.vPosition.y = y;
        self->mDeferred.vPosition.z = z;
        self->mDirty.set(Position);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.vPosition.x = x;
        self->mImmediate.vPosition.y = y;
        self->mImmediate.vPosition.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3f(self->mSource, AL_POSITION, x, y, -z);
            alGetError();
        }
    }

    return DS_OK;
}

HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG(PREFIX "SetVelocity (%p)->(%f, %f, %f, %lu)\n", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vVelocity.x = x;
        self->mDeferred.vVelocity.y = y;
        self->mDeferred.vVelocity.z = z;
        self->mDirty.set(Velocity);
    }
    else
    {
        ALSection alsection{self->mContext};
        self->mImmediate.vVelocity.x = x;
        self->mImmediate.vVelocity.y = y;
        self->mImmediate.vVelocity.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3f(self->mSource, AL_VELOCITY, x, y, -z);
            alGetError();
        }
    }

    return DS_OK;
}
#undef PREFIX


/*** IKsPropertySet interface. ***/
#define PREFIX "BufferProp::"
HRESULT STDMETHODCALLTYPE Buffer::Prop::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Prop::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mPropRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Prop::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mPropRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    DEBUG(PREFIX "Get (%p)->(%s, 0x%lx, %p, %lu, %p, %lu, %p)\n", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData, voidp{pcbReturned});

    if(!pcbReturned)
        return E_POINTER;
    *pcbReturned = 0;

    if(cbPropData > 0 && !pPropData)
    {
        WARN(PREFIX "Get pPropData is null with cbPropData > 0\n");
        return E_POINTER;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(guidPropSet == EAXPROPERTYID_EAX40_Source
        || guidPropSet == DSPROPSETID_EAX30_BufferProperties
        || guidPropSet == DSPROPSETID_EAX20_BufferProperties
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot0
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot1
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot2
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot3
        || guidPropSet == DSPROPSETID_EAX30_ListenerProperties
        || guidPropSet == DSPROPSETID_EAX20_ListenerProperties
        || guidPropSet == EAXPROPERTYID_EAX40_Context
        || guidPropSet == DSPROPSETID_EAX10_ListenerProperties
        || guidPropSet == DSPROPSETID_EAX10_BufferProperties)
    {
        if(self->mParent.haveExtension(EXT_EAX))
        {
            ALSection alsection{self->mContext};
            const ALenum err{EAXGet(&guidPropSet, dwPropID, self->mSource, pPropData, cbPropData)};
            if(err != AL_NO_ERROR)
                return E_FAIL;
            /* Not sure what to do here. OpenAL EAX doesn't return the amount
             * of data written, and determining how much should have been
             * written (at least for EAX4) will require tracking the FXSlot
             * state since different effects will write different amounts for
             * the same PropID.
             */
            *pcbReturned = cbPropData;
            return DS_OK;
        }
    }
    else if(guidPropSet == DSPROPSETID_VoiceManager)
    {
        switch(dwPropID)
        {
        case DSPROPERTY_VMANAGER_MODE:
            if(cbPropData >= sizeof(DWORD))
            {
                *static_cast<DWORD*>(pPropData) = self->mBuffer->mVoiceMode;
                *pcbReturned = sizeof(DWORD);
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;

        case DSPROPERTY_VMANAGER_PRIORITY:
            if(cbPropData >= sizeof(DWORD))
            {
                *static_cast<DWORD*>(pPropData) = self->mVmPriority;
                *pcbReturned = sizeof(DWORD);
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;

        case DSPROPERTY_VMANAGER_STATE:
            if(cbPropData >= sizeof(DWORD))
            {
                ALint state{};
                if(self->mSource != 0)
                {
                    ALSection alsection{self->mContext};
                    alGetSourcei(self->mSource, AL_SOURCE_STATE, &state);
                }

                /* FIXME: Probably not accurate. */
                if(state == AL_PLAYING)
                    *static_cast<DWORD*>(pPropData) = DSPROPERTY_VMANAGER_STATE_PLAYING3DHW;
                else
                    *static_cast<DWORD*>(pPropData) = DSPROPERTY_VMANAGER_STATE_SILENT;
                *pcbReturned = sizeof(DWORD);
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
        }

        FIXME(PREFIX "Get Unhandled VoiceManager propid: 0x%08lx\n", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    return E_PROP_ID_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    DEBUG(PREFIX "Set (%p)->(%s, 0x%lx, %p, %lu, %p, %lu)\n", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, pInstanceData, cbInstanceData, pPropData,
        cbPropData);

    if(cbPropData > 0 && !pPropData)
    {
        WARN(PREFIX "Set pPropData is null with cbPropData > 0\n");
        return E_POINTER;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(guidPropSet == EAXPROPERTYID_EAX40_Source
        || guidPropSet == DSPROPSETID_EAX30_BufferProperties
        || guidPropSet == DSPROPSETID_EAX20_BufferProperties
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot0
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot1
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot2
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot3
        || guidPropSet == DSPROPSETID_EAX30_ListenerProperties
        || guidPropSet == DSPROPSETID_EAX20_ListenerProperties
        || guidPropSet == EAXPROPERTYID_EAX40_Context
        || guidPropSet == DSPROPSETID_EAX10_ListenerProperties
        || guidPropSet == DSPROPSETID_EAX10_BufferProperties)
    {
        if(self->mParent.haveExtension(EXT_EAX))
        {
            ALSection alsection{self->mContext};
            const bool immediate{!(dwPropID&0x80000000u)};

            if(immediate)
                alcSuspendContext(self->mContext);

            const ALenum err{EAXSet(&guidPropSet, dwPropID, self->mSource, pPropData, cbPropData)};
            if(immediate)
            {
                /* FIXME: Commit DSound settings regardless? alcProcessContext
                 * will commit deferred EAX settings, which we can't avoid.
                 */
                if(err == AL_NO_ERROR)
                    self->mParent.getPrimary().commit();
                alcProcessContext(self->mContext);
            }
            if(err != AL_NO_ERROR)
                return E_FAIL;
            return DS_OK;
        }
    }
    else if(guidPropSet == DSPROPSETID_VoiceManager)
    {
        switch(dwPropID)
        {
        case DSPROPERTY_VMANAGER_MODE:
            if(cbPropData >= sizeof(DWORD))
            {
                if(const DWORD mode{*static_cast<DWORD*>(pPropData)}; mode < VMANAGER_MODE_MAX)
                {
                    TRACE(PREFIX "Set DSPROPERTY_VMANAGER_MODE: %lu\n", mode);
                    self->mBuffer->mVoiceMode = static_cast<VmMode>(mode);
                    return DS_OK;
                }
            }
            return DSERR_INVALIDPARAM;

        case DSPROPERTY_VMANAGER_PRIORITY:
            if(cbPropData >= sizeof(DWORD))
            {
                const DWORD prio{*static_cast<DWORD*>(pPropData)};
                TRACE(PREFIX "Set DSPROPERTY_VMANAGER_PRIORITY: %lu\n", prio);
                self->mVmPriority = prio;
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
        }

        FIXME(PREFIX "Set Unhandled VoiceManager propid: 0x%08lx\n", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    return E_PROP_ID_UNSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Buffer::Prop::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    DEBUG(PREFIX "QuerySupport (%p)->(%s, 0x%lx, %p)\n", voidp{this},
        PropidPrinter{guidPropSet}.c_str(), dwPropID, voidp{pTypeSupport});

    if(!pTypeSupport)
        return E_POINTER;
    *pTypeSupport = 0;

    auto self = impl_from_base();
    if(self->mParent.haveExtension(EXT_EAX))
    {
        auto query = [&guidPropSet,dwPropID]() -> std::optional<DWORD>
        {
            if(guidPropSet == EAXPROPERTYID_EAX40_Source)
                return EAX4Source_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX30_BufferProperties)
                return EAX3Buffer_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX20_BufferProperties)
                return EAX2Buffer_Query(dwPropID);
            if(guidPropSet == EAXPROPERTYID_EAX40_FXSlot0
                || guidPropSet == EAXPROPERTYID_EAX40_FXSlot1
                || guidPropSet == EAXPROPERTYID_EAX40_FXSlot2
                || guidPropSet == EAXPROPERTYID_EAX40_FXSlot3)
                return EAX4Slot_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX30_ListenerProperties)
                return EAX3_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX20_ListenerProperties)
                return EAX2_Query(dwPropID);
            if(guidPropSet == EAXPROPERTYID_EAX40_Context)
                return EAX4Context_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX10_ListenerProperties)
                return EAX1_Query(dwPropID);
            if(guidPropSet == DSPROPSETID_EAX10_BufferProperties)
                return EAX1Buffer_Query(dwPropID);
            return std::nullopt;
        };
        auto res = query();
        if(res)
        {
            if(!*res)
                return E_PROP_ID_UNSUPPORTED;

            *pTypeSupport = *res;
            return DS_OK;
        }
    }
    if(guidPropSet == DSPROPSETID_VoiceManager)
    {
        switch(dwPropID)
        {
        case DSPROPERTY_VMANAGER_MODE:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
            return DS_OK;

        case DSPROPERTY_VMANAGER_PRIORITY:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
            return DS_OK;

        case DSPROPERTY_VMANAGER_STATE:
            *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            return DS_OK;
        }

        FIXME(PREFIX "QuerySupport Unhandled VoiceManager propid: 0x%08lx\n", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    FIXME(PREFIX "QuerySupport Unhandled propset: %s (propid: %lu)\n",
        PropidPrinter{guidPropSet}.c_str(), dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX


/*** IDirectSoundNotify interface wrapper. ***/
#define PREFIX "BufferNotify::"
HRESULT STDMETHODCALLTYPE Buffer::Notify::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Notify::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mNotRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG(PREFIX "AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Notify::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mNotRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG(PREFIX "Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}


HRESULT STDMETHODCALLTYPE Buffer::Notify::SetNotificationPositions(DWORD numNotifies,
    const DSBPOSITIONNOTIFY *notifies) noexcept
{
    DEBUG(PREFIX "SetNotificationPositions (%p)->(%lu, %p)\n", voidp{this}, numNotifies,
        cvoidp{notifies});

    if(numNotifies > 0 && !notifies)
    {
        WARN(PREFIX "SetNotificationPositions Null pointer with non-0 count\n");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    std::lock_guard lock{self->mMutex};
    if(self->mSource != 0)
    {
        ALSection alsection{self->mContext};
        ALint state{};
        alGetSourcei(self->mSource, AL_SOURCE_STATE, &state);
        if(state == AL_PLAYING)
        {
            WARN(PREFIX "SetNotificationPositions Source playing\n");
            return DSERR_INVALIDCALL;
        }
        /* If the source isn't playing and still has a notification check
         * pending, it *just* stopped on its own. Trigger notifications and
         * remove it so we can replace the notifications.
         */
        if(self->mParent.isPendingNotify(self)) UNLIKELY
        {
            self->mParent.triggerNotifies();
            self->mParent.removeNotifyBuffer(self);
        }
    }

    auto notifyspan = std::span{notifies, numNotifies};
    std::vector<DSBPOSITIONNOTIFY> newNots{};
    if(!notifyspan.empty())
    {
        auto invalidNotify = std::find_if_not(notifyspan.begin(), notifyspan.end(),
            [self](const DSBPOSITIONNOTIFY &notify) noexcept -> bool
            {
                return notify.dwOffset < self->mBuffer->mData.size() ||
                    notify.dwOffset == static_cast<DWORD>(DSBPN_OFFSETSTOP);
            });
        if(invalidNotify != notifyspan.end())
        {
            WARN(PREFIX "SetNotificationPositions Out of range (%" PRIdPTR ": %lu >= %zu)\n",
                std::distance(notifyspan.begin(), invalidNotify), invalidNotify->dwOffset,
                self->mBuffer->mData.size());
            return DSERR_INVALIDPARAM;
        }
        newNots.assign(notifyspan.begin(), notifyspan.end());
    }
    newNots.swap(self->mNotifies);

    return DS_OK;
}
#undef PREFIX


/*** IUnknown interface wrapper. ***/
HRESULT STDMETHODCALLTYPE Buffer::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

ULONG STDMETHODCALLTYPE Buffer::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("Buffer::Unknown::AddRef (%p) ref %lu\n", voidp{this}, ret);
    return ret;
}

ULONG STDMETHODCALLTYPE Buffer::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("Buffer::Unknown::Release (%p) ref %lu\n", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) UNLIKELY
        self->mParent.dispose(self);
    return ret;
}
