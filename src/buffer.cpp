#include "buffer.h"

#include <algorithm>
#include <bit>
#include <functional>
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


#ifndef AL_SOFT_source_panning
#define AL_SOFT_source_panning
#define AL_PANNING_ENABLED_SOFT                  0x19EC
#define AL_PAN_SOFT                              0x19ED
#endif

namespace {

using voidp = void*;
using cvoidp = const void*;

#define PREFIX "ConvertFormat "
ALenum ConvertFormat(WAVEFORMATEXTENSIBLE &dst, const WAVEFORMATEX &src,
    const std::bitset<ExtensionCount> exts) noexcept
{
    TRACE("Requested buffer format:\n"
        "    FormatTag      = {:#06x}\n"
        "    Channels       = {}\n"
        "    SamplesPerSec  = {}\n"
        "    AvgBytesPerSec = {}\n"
        "    BlockAlign     = {}\n"
        "    BitsPerSample  = {}\n"
        "    Size           = {}",
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
            FIXME("{}-bit integer samples not supported", dst.Format.wBitsPerSample);
            return AL_NONE;
        }
        break;
    case WAVE_FORMAT_IEEE_FLOAT:
        if(dst.Format.wBitsPerSample == 32 && exts.test(EXT_FLOAT32))
            sampleType = Float32;
        else
        {
            FIXME("{}-bit floating point samples not supported", dst.Format.wBitsPerSample);
            return AL_NONE;
        }
        break;
    default:
        FIXME("Format {:#06x} samples not supported", dst.Format.wFormatTag);
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

    FIXME("Could not get OpenAL format (0x{:04x}, {}-bit, {} channels)", dst.Format.wFormatTag,
        dst.Format.wBitsPerSample, dst.Format.nChannels);
    return AL_NONE;
}

ALenum ConvertFormat(WAVEFORMATEXTENSIBLE &dst, const WAVEFORMATEXTENSIBLE &src,
    const std::bitset<ExtensionCount> exts) noexcept
{
    /* NOLINTBEGIN(cppcoreguidelines-pro-type-union-access) */
    TRACE("Requested buffer format:\n"
        "    FormatTag          = {:#06x}\n"
        "    Channels           = {}\n"
        "    SamplesPerSec      = {}\n"
        "    AvgBytesPerSec     = {}\n"
        "    BlockAlign         = {}\n"
        "    BitsPerSample      = {}\n"
        "    Size               = {}\n"
        "    ValidBitsPerSample = {}\n"
        "    ChannelMask        = {:#010x}\n"
        "    SubFormat          = {}",
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
        WARN("Padded sample formats not supported ({}-bit total, {}-bit valid)",
            dst.Format.wBitsPerSample, dst.Samples.wValidBitsPerSample);
        return AL_NONE;
    }
    /* NOLINTEND(cppcoreguidelines-pro-type-union-access) */

    auto unsupported_format = [&dst]
    {
        FIXME("Unsupported channel configuration ({} channels, {:#010x})", dst.Format.nChannels,
            dst.dwChannelMask);
        return AL_NONE;
    };
    enum { Mono, Stereo, Quad, X51, X71 } channelConfig{};
    switch(dst.dwChannelMask)
    {
    case KSAUDIO_SPEAKER_MONO:
        switch(dst.Format.nChannels)
        {
        case 1: channelConfig = Mono; break;
        default: return unsupported_format();
        }
        break;
    case KSAUDIO_SPEAKER_STEREO:
        switch(dst.Format.nChannels)
        {
        case 2: channelConfig = Stereo; break;
        default: return unsupported_format();
        }
        break;
    case KSAUDIO_SPEAKER_QUAD:
        switch(dst.Format.nChannels)
        {
        case 4: channelConfig = Quad; break;
        default: return unsupported_format();
        }
        break;
    case KSAUDIO_SPEAKER_5POINT1_BACK:
    case KSAUDIO_SPEAKER_5POINT1_SURROUND:
        switch(dst.Format.nChannels)
        {
        case 6: channelConfig = X51; break;
        default: return unsupported_format();
        }
        break;
    case KSAUDIO_SPEAKER_7POINT1_SURROUND:
        switch(dst.Format.nChannels)
        {
        case 8: channelConfig = X71; break;
        default: return unsupported_format();
        }
        break;

    case 0:
        switch(dst.Format.nChannels)
        {
        case 1: channelConfig = Mono; break;
        case 2: channelConfig = Stereo; break;
        case 4: channelConfig = Quad; break;
        case 6: channelConfig = X51; break;
        case 8: channelConfig = X71; break;
        default: return unsupported_format();
        }
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
            FIXME("{}-bit integer samples not supported", dst.Format.wBitsPerSample);
            return AL_NONE;
        }
    }
    else if(dst.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
    {
        if(dst.Format.wBitsPerSample == 32)
            sampleType = Float32;
        else
        {
            FIXME("{}-bit floating point samples not supported", dst.Format.wBitsPerSample);
            return AL_NONE;
        }
    }
    else
    {
        FIXME("Unsupported sample subformat {}", FmtidPrinter{dst.SubFormat}.c_str());
        return AL_NONE;
    }

    switch(sampleType)
    {
    case UInt8:
        switch(channelConfig)
        {
        case Mono: return AL_FORMAT_MONO8;
        case Stereo: return AL_FORMAT_STEREO8;
        case Quad:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_QUAD8;
            break;
        case X51:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_51CHN8;
            break;
        case X71:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_71CHN8;
            break;
        }
        break;
    case Int16:
        switch(channelConfig)
        {
        case Mono: return AL_FORMAT_MONO16;
        case Stereo: return AL_FORMAT_STEREO16;
        case Quad:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_QUAD16;
            break;
        case X51:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_51CHN16;
            break;
        case X71:
            if(exts.test(EXT_MCFORMATS))
                return AL_FORMAT_71CHN16;
            break;
        }
        break;
    case Float32:
        if(exts.test(EXT_FLOAT32))
        {
            switch(channelConfig)
            {
            case Mono: return AL_FORMAT_MONO_FLOAT32;
            case Stereo: return AL_FORMAT_STEREO_FLOAT32;
            case Quad:
                if(exts.test(EXT_MCFORMATS))
                    return AL_FORMAT_QUAD32;
                break;
            case X51:
                if(exts.test(EXT_MCFORMATS))
                    return AL_FORMAT_51CHN32;
                break;
            case X71:
                if(exts.test(EXT_MCFORMATS))
                    return AL_FORMAT_71CHN32;
                break;
            }
        }
        break;
    }

    FIXME("Could not get OpenAL format ({}-bit, {} channels, {})", dst.Format.wBitsPerSample,
        dst.Format.nChannels, FmtidPrinter{dst.SubFormat}.c_str());
    return AL_NONE;
}
#undef PREFIX

} // namespace

#define CLASS_PREFIX "SharedBuffer::"
SharedBuffer::~SharedBuffer()
{
    if(mAlBuffer != 0)
        alDeleteBuffersDirect(mContext, 1, &mAlBuffer);
}

#define PREFIX CLASS_PREFIX "Create "
auto SharedBuffer::Create(ALCcontext *context, const DSBUFFERDESC &bufferDesc,
    const std::bitset<ExtensionCount> exts) noexcept -> ds::expected<ComPtr<SharedBuffer>,HRESULT>
{
    const WAVEFORMATEX *format{bufferDesc.lpwfxFormat};

    if(format->nChannels <= 0)
    {
        WARN("Invalid Channels {}", format->nChannels);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > DSBFREQUENCY_MAX)
    {
        WARN("Invalid SamplesPerSec {}", format->nSamplesPerSec);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign <= 0)
    {
        WARN("Invalid BlockAlign {}", format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->wBitsPerSample == 0 || (format->wBitsPerSample%8) != 0)
    {
        WARN("Invalid BitsPerSample {}", format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nBlockAlign != format->nChannels*format->wBitsPerSample/8)
    {
        WARN("Invalid BlockAlign {} (expected {} = {}*{}/8)", format->nBlockAlign,
            format->nChannels*format->wBitsPerSample/8, format->nChannels, format->wBitsPerSample);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    /* HACK: Some games provide an incorrect value here and expect to work.
     * This is clearly not supposed to succeed with just anything, but until
     * the amount of leeway allowed is discovered, be very lenient.
     */
    if(format->nAvgBytesPerSec == 0)
    {
        WARN("Invalid AvgBytesPerSec {} (expected {} = {}*{})", format->nAvgBytesPerSec,
            format->nSamplesPerSec*format->nBlockAlign, format->nSamplesPerSec,
            format->nBlockAlign);
        return ds::unexpected(DSERR_INVALIDPARAM);
    }
    if(format->nAvgBytesPerSec != format->nBlockAlign*format->nSamplesPerSec)
        WARN("Unexpected AvgBytesPerSec {} (expected {} = {}*{})", format->nAvgBytesPerSec,
            format->nSamplesPerSec*format->nBlockAlign, format->nSamplesPerSec,
            format->nBlockAlign);

    static constexpr DWORD LocFlags{DSBCAPS_LOCSOFTWARE | DSBCAPS_LOCHARDWARE};
    if((bufferDesc.dwFlags&LocFlags) == LocFlags)
    {
        WARN("Hardware and software location requested");
        return ds::unexpected(DSERR_INVALIDPARAM);
    }

    /* Round the buffer size up to the next black alignment. */
    DWORD bufSize{bufferDesc.dwBufferBytes + format->nBlockAlign - 1};
    bufSize -= bufSize%format->nBlockAlign;
    if(bufSize < DSBSIZE_MIN) return ds::unexpected(DSERR_BUFFERTOOSMALL);
    if(bufSize > DSBSIZE_MAX) return ds::unexpected(DSERR_INVALIDPARAM);

    /* Over-allocate the shared buffer, combining it with the sample storage. */
    auto shared = std::invoke([bufSize] {
        try { return ComPtr<SharedBuffer>{new(ExtraBytes(bufSize)) SharedBuffer{}}; }
        catch(...) { return ComPtr<SharedBuffer>{}; }
    });
    if(!shared)
        return ds::unexpected(DSERR_OUTOFMEMORY);
    /* NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast) */
    shared->mData = {reinterpret_cast<char*>(shared.get() + 1), bufSize};
    shared->mFlags = bufferDesc.dwFlags;

    if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        static constexpr WORD ExtExtraSize{sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)};
        if(format->cbSize < ExtExtraSize)
        {
            WARN("EXTENSIBLE size too small ({}, expected {})", format->cbSize, ExtExtraSize);
            return ds::unexpected(DSERR_INVALIDPARAM);
        }
        auto wfe{CONTAINING_RECORD(format, const WAVEFORMATEXTENSIBLE, Format)};
        shared->mAlFormat = ConvertFormat(shared->mWfxFormat, *wfe, exts);
    }
    else
        shared->mAlFormat = ConvertFormat(shared->mWfxFormat, *format, exts);
    if(!shared->mAlFormat) return ds::unexpected(DSERR_INVALIDPARAM);

    shared->mContext = context;
    alGenBuffersDirect(context, 1, &shared->mAlBuffer);
    alBufferDataStaticDirect(context, shared->mAlBuffer, shared->mAlFormat, shared->mData.data(),
        static_cast<ALsizei>(shared->mData.size()),
        static_cast<ALsizei>(shared->mWfxFormat.Format.nSamplesPerSec));
    alGetErrorDirect(context);

    return shared;
}
#undef PREFIX
#undef CLASS_PREFIX

#define CLASS_PREFIX "Buffer::"
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
    if(const auto srcid = std::exchange(mSource, 0))
    {
        alDeleteSourcesDirect(mContext, 1, &srcid);
        alGetErrorDirect(mContext);
    }
    if(mLocStatus == LocStatus::Hardware)
        mParent.getShared().decHwSources();
    else if(mLocStatus == LocStatus::Software)
        mParent.getShared().decSwSources();
    mLocStatus = LocStatus::None;

    if(mBuffer)
    {
        if((mBuffer->mFlags&DSBCAPS_CTRL3D))
            mParent.remove3dBuffer(this);
        mBuffer = nullptr;
    }
}


bool Buffer::updateNotify() noexcept
{
    auto state = ALint{};
    auto ioffset = ALint{};
    alGetSourceiDirect(mContext, mSource, AL_BYTE_OFFSET, &ioffset);
    alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);

    /* If the source is AL_STOPPED, it reached the end naturally, so all
     * notifies since the last position have been hit, along with
     * DSBPN_OFFSETSTOP (-1 or max unsigned value).
     */
    const auto offset = (state == AL_STOPPED) ? static_cast<DWORD>(mBuffer->mData.size())
        : static_cast<DWORD>(ioffset);

    if(offset > mLastPos)
    {
        for(auto &notify : mNotifies)
        {
            if(notify.dwOffset >= mLastPos && notify.dwOffset < offset)
                SetEvent(notify.hEventNotify);
        }
    }
    else if(offset < mLastPos)
    {
        for(auto &notify : mNotifies)
        {
            if((notify.dwOffset >= mLastPos || notify.dwOffset < offset)
                && notify.dwOffset != static_cast<DWORD>(DSBPN_OFFSETSTOP))
                SetEvent(notify.hEventNotify);
        }
    }
    mLastPos = offset;

    if(state != AL_PLAYING)
    {
        for(auto &notify : mNotifies)
        {
            if(notify.dwOffset == static_cast<DWORD>(DSBPN_OFFSETSTOP))
                SetEvent(notify.hEventNotify);
        }
        return false;
    }

    return true;
}


#define PREFIX CLASS_PREFIX "setLocation "
HRESULT Buffer::setLocation(LocStatus locStatus) noexcept
{
    if(locStatus != LocStatus::Any && mLocStatus == locStatus)
        return DS_OK;
    if(locStatus == LocStatus::Any && mLocStatus != LocStatus::None)
        return DS_OK;

    /* If we have a source, we're changing location, so return the source we
     * have to get a new one.
     *
     * HACK: If we have a source and don't have a location marked, we got an
     * unmarked one and just need to set the location and initialize it.
     */
    if(mLocStatus != LocStatus::None)
    {
        if(const auto srcid = std::exchange(mSource, 0))
        {
            alDeleteSourcesDirect(mContext, 1, &srcid);
            alGetErrorDirect(mContext);
        }

        if(mLocStatus == LocStatus::Hardware)
            mParent.getShared().decHwSources();
        else if(mLocStatus == LocStatus::Software)
            mParent.getShared().decSwSources();
        mLocStatus = LocStatus::None;
    }

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
        ERR("Out of {} sources",
            (locStatus == LocStatus::Hardware) ? "hardware" :
            (locStatus == LocStatus::Software) ? "software" : "any"
        );
        return DSERR_ALLOCATED;
    }

    if(mSource == 0)
        alGenSourcesDirect(mContext, 1, &mSource);
    alSourcefDirect(mContext, mSource, AL_GAIN, mB_to_gain(mVolume));
    alSourcefDirect(mContext, mSource, AL_PITCH, (mFrequency == 0) ? 1.0f :
        static_cast<float>(mFrequency)/static_cast<float>(mBuffer->mWfxFormat.Format.nSamplesPerSec));

    if((mBuffer->mFlags&DSBCAPS_CTRL3D))
    {
        if(mImmediate.dwMode == DS3DMODE_DISABLE)
        {
            alSource3fDirect(mContext, mSource, AL_POSITION, 0.0f, 0.0f, -1.0f);
            alSource3fDirect(mContext, mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            alSource3fDirect(mContext, mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
            alSourcefDirect(mContext, mSource, AL_ROLLOFF_FACTOR, 0.0f);
            if(mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                alSourceiDirect(mContext, mSource, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
        }
        else
        {
            alSource3fDirect(mContext, mSource, AL_POSITION, mImmediate.vPosition.x,
                mImmediate.vPosition.y, -mImmediate.vPosition.z);
            alSource3fDirect(mContext, mSource, AL_VELOCITY, mImmediate.vVelocity.x,
                mImmediate.vVelocity.y, -mImmediate.vVelocity.z);
            alSource3fDirect(mContext, mSource, AL_DIRECTION, mImmediate.vConeOrientation.x,
                mImmediate.vConeOrientation.y, -mImmediate.vConeOrientation.z);
            alSourcefDirect(mContext, mSource, AL_ROLLOFF_FACTOR,
                mParent.getPrimary().getCurrentRolloffFactor());
            if(mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                alSourceiDirect(mContext, mSource, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
        }
        alSourceiDirect(mContext, mSource, AL_SOURCE_RELATIVE,
            (mImmediate.dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
        alSourcefDirect(mContext, mSource, AL_CONE_INNER_ANGLE,
            static_cast<float>(mImmediate.dwInsideConeAngle));
        alSourcefDirect(mContext, mSource, AL_CONE_OUTER_ANGLE,
            static_cast<float>(mImmediate.dwOutsideConeAngle));
        alSourcefDirect(mContext, mSource, AL_CONE_OUTER_GAIN,
            mB_to_gain(mImmediate.lConeOutsideVolume));
        alSourcefDirect(mContext, mSource, AL_REFERENCE_DISTANCE, mImmediate.flMinDistance);
        alSourcefDirect(mContext, mSource, AL_MAX_DISTANCE, mImmediate.flMaxDistance);
    }
    else
    {
        alSourcefDirect(mContext, mSource, AL_ROLLOFF_FACTOR, 0.0f);
        alSourceiDirect(mContext, mSource, AL_SOURCE_RELATIVE, AL_TRUE);
        if(mParent.haveExtension(SOFT_SOURCE_PANNING))
        {
            /* Always enable panning for non-3D buffers. This won't have any
             * effect if mPan is never changed, except for mono buffers that
             * get split into stereo (which is similar behavior to native).
             */
            alSourceiDirect(mContext, mSource, AL_PANNING_ENABLED_SOFT, AL_TRUE);

            const auto panf = (mPan <= 0) ? (mB_to_gain(mPan)-1.0f) : (1.0f-mB_to_gain(-mPan));
            alSourcefDirect(mContext, mSource, AL_PAN_SOFT, panf);
        }
        if(mParent.haveExtension(EXT_EAX))
        {
            static std::array<GUID,EAX40_MAX_ACTIVE_FXSLOTS> NullSlots{};

            const auto slots = std::as_writable_bytes(std::span{NullSlots});
            EAXSetDirect(mContext, &EAXPROPERTYID_EAX40_Source, EAXSOURCE_ACTIVEFXSLOTID, mSource,
                slots.data(), static_cast<ALuint>(slots.size()));
        }
    }
    alGetErrorDirect(mContext);

    mLocStatus = locStatus;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QueryInterface "
HRESULT STDMETHODCALLTYPE Buffer::QueryInterface(REFIID riid, void** ppvObject) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, IidPrinter{riid}.c_str(), voidp{ppvObject});

    if(!ppvObject)
        return E_POINTER;
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
            WARN("Requesting IDirectSoundBuffer8 iface for non-DS8 object");
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
            WARN("Requesting IDirectSound3DBuffer iface without DSBCAPS_CTRL3D");
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
            WARN("Requesting IDirectSoundNotify iface without DSBCAPS_CTRLPOSITIONNOTIFY");
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

    FIXME("Unhandled GUID: {}", IidPrinter{riid}.c_str());
    return E_NOINTERFACE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Buffer::AddRef() noexcept
{
    mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = mDsRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Buffer::Release() noexcept
{
    const auto ret = mDsRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1)
        mParent.dispose(this);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCaps "
HRESULT STDMETHODCALLTYPE Buffer::GetCaps(DSBCAPS *bufferCaps) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{bufferCaps});

    if(!bufferCaps || bufferCaps->dwSize < sizeof(*bufferCaps))
    {
        WARN("Invalid DSBCAPS ({}, {})", voidp{bufferCaps}, bufferCaps ? bufferCaps->dwSize : 0);
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
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetCurrentPosition "
HRESULT STDMETHODCALLTYPE Buffer::GetCurrentPosition(DWORD *playCursor, DWORD *writeCursor) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{playCursor}, voidp{writeCursor});

    ALint status{AL_INITIAL};
    ALint ofs{0};

    if(mSource != 0)
    {
        alGetSourceiDirect(mContext, mSource, AL_BYTE_OFFSET, &ofs);
        alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &status);
        alGetErrorDirect(mContext);
    }

    auto &format = mBuffer->mWfxFormat.Format;
    auto pos = DWORD{};
    auto writecursor = DWORD{};
    if(status == AL_PLAYING)
    {
        pos = static_cast<ALuint>(ofs);
        writecursor = format.nSamplesPerSec / mParent.getRefresh() * format.nBlockAlign;
        writecursor += pos;
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
        writecursor = pos;
    }

    /* FIXME: AFAIK, a non-looping buffer that stops on its own should have the
     * play cursor at the buffer size, but the write cursor should probably not
     * be outside the [0,size) range. However, a stopped source should also
     * have play cursor == write cursor, which makes one of those incorrect.
     * Some testing should be done to see what happens. Wine always wraps the
     * play cursor, so just do that for now.
     */
    pos %= mBuffer->mData.size();
    writecursor %= mBuffer->mData.size();

    DEBUG(" pos = {}, write pos = {}", pos, writecursor);

    if(playCursor) *playCursor = pos;
    if(writeCursor)  *writeCursor = writecursor;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFormat "
HRESULT STDMETHODCALLTYPE Buffer::GetFormat(WAVEFORMATEX *wfx, DWORD sizeAllocated, DWORD *sizeWritten) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, voidp{wfx}, sizeAllocated, voidp{sizeWritten});

    if(!wfx && !sizeWritten)
    {
        WARN("Cannot report format or format size");
        return DSERR_INVALIDPARAM;
    }

    auto const size = DWORD{sizeof(mBuffer->mWfxFormat.Format)}+mBuffer->mWfxFormat.Format.cbSize;
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
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetVolume "
HRESULT STDMETHODCALLTYPE Buffer::GetVolume(LONG *volume) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{volume});

    if(!volume)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    *volume = mVolume;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetPan "
HRESULT STDMETHODCALLTYPE Buffer::GetPan(LONG *pan) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{pan});

    if(!pan)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    *pan = mPan;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetFrequency "
HRESULT STDMETHODCALLTYPE Buffer::GetFrequency(DWORD *frequency) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{frequency});

    if(!frequency)
        return DSERR_INVALIDPARAM;

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    *frequency = mFrequency;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetStatus "
HRESULT STDMETHODCALLTYPE Buffer::GetStatus(DWORD *status) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{status});

    if(!status)
        return DSERR_INVALIDPARAM;
    *status = 0;

    auto res = DWORD{};
    if(mBufferLost) [[unlikely]]
        res = DSBSTATUS_BUFFERLOST;
    else
    {
        auto state = ALint{AL_INITIAL};
        auto looping = ALint{AL_FALSE};
        if(mSource != 0)
        {
            alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);
            alGetSourceiDirect(mContext, mSource, AL_LOOPING, &looping);
            alGetErrorDirect(mContext);
        }

        if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
            res |= ds::to_underlying(mLocStatus);
        if(state == AL_PLAYING)
            res |= DSBSTATUS_PLAYING | (looping ? DSBSTATUS_LOOPING : 0);
    }

    *status = res;
    DEBUG(" status = {:#010x}", res);
    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Initialize "
HRESULT STDMETHODCALLTYPE Buffer::Initialize(IDirectSound *directSound, const DSBUFFERDESC *dsBufferDesc) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{directSound}, cvoidp{dsBufferDesc});

    auto const lock = std::lock_guard{mMutex};
    if(mIsInitialized) return DSERR_ALREADYINITIALIZED;

    if(!mBuffer)
    {
        if(!dsBufferDesc)
        {
            WARN("Missing buffer description");
            return DSERR_INVALIDPARAM;
        }
        if(!dsBufferDesc->lpwfxFormat)
        {
            WARN("Missing buffer format");
            return DSERR_INVALIDPARAM;
        }
        if((dsBufferDesc->dwFlags&DSBCAPS_CTRL3D) && dsBufferDesc->lpwfxFormat->nChannels != 1)
        {
            if(mIs8)
            {
                /* DirectSoundBuffer8 objects aren't allowed non-mono 3D
                 * buffers.
                 */
                WARN("Can't create multi-channel 3D buffers");
                return DSERR_INVALIDPARAM;
            }

            if(static bool once{false}; !once)
            {
                once = true;
                ERR("Multi-channel 3D sounds may not play correctly");
            }
        }

        auto shared = SharedBuffer::Create(mContext, *dsBufferDesc, mParent.getExtensions());
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
#undef PREFIX

#define PREFIX CLASS_PREFIX "Lock "
HRESULT STDMETHODCALLTYPE Buffer::Lock(DWORD offset, DWORD bytes, void **audioPtr1, DWORD *audioBytes1, void **audioPtr2, DWORD *audioBytes2, DWORD flags) noexcept
{
    DEBUG("({})->({}, {}, {}, {}, {}, {}, {:#x})", voidp{this}, offset, bytes, voidp{audioPtr1},
        voidp{audioBytes1}, voidp{audioPtr2}, voidp{audioBytes2}, flags);

    if(!audioPtr1 || !audioBytes1)
    {
        WARN("Invalid pointer/len {} {}", voidp{audioPtr1}, voidp{audioBytes1});
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
        WARN("Invalid offset {}", offset);
        return DSERR_INVALIDPARAM;
    }
    if((flags&DSBLOCK_ENTIREBUFFER))
        bytes = static_cast<DWORD>(mBuffer->mData.size());
    else if(bytes > mBuffer->mData.size())
    {
        WARN("Invalid size {}", bytes);
        return DSERR_INVALIDPARAM;
    }

    if(mLocked.exchange(true, std::memory_order_relaxed))
    {
        WARN("Already locked");
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
#undef PREFIX

#define PREFIX CLASS_PREFIX "Play "
HRESULT STDMETHODCALLTYPE Buffer::Play(DWORD reserved1, DWORD priority, DWORD flags) noexcept
{
    DEBUG("({})->({}, {}, {:#x})", voidp{this}, reserved1, priority, flags);

    auto const lock = std::lock_guard{mMutex};
    if(mBufferLost) [[unlikely]]
    {
        WARN("Buffer lost");
        return DSERR_BUFFERLOST;
    }

    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        static constexpr DWORD LocFlags{DSBPLAY_LOCSOFTWARE | DSBPLAY_LOCHARDWARE};
        if((flags&LocFlags) == LocFlags)
        {
            WARN("Both hardware and software specified");
            return DSERR_INVALIDPARAM;
        }

        auto loc = LocStatus::Any;
        if((flags&DSBPLAY_LOCHARDWARE)) loc = LocStatus::Hardware;
        else if((flags&DSBPLAY_LOCSOFTWARE)) loc = LocStatus::Software;

        if(loc != LocStatus::Any && mLocStatus != LocStatus::None && loc != mLocStatus)
        {
            ALint state{};
            alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);
            alGetErrorDirect(mContext);

            if(state == AL_PLAYING)
            {
                FIXME("Attemping to change location on playing buffer");
                return DSERR_INVALIDPARAM;
            }
        }

        if(const auto hr = setLocation(loc); FAILED(hr))
            return hr;
    }
    else if(priority != 0)
    {
        ERR("Invalid priority for non-deferred buffer, {}.", priority);
        return DSERR_INVALIDPARAM;
    }

    ALint state{};
    alSourceiDirect(mContext, mSource, AL_LOOPING, (flags&DSBPLAY_LOOPING) ? AL_TRUE : AL_FALSE);
    alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);
    alGetErrorDirect(mContext);

    if(state == AL_PLAYING)
        return DS_OK;

    mLastPos %= mBuffer->mData.size();
    if(state == AL_INITIAL)
    {
        alSourceiDirect(mContext, mSource, AL_BUFFER, static_cast<ALint>(mBuffer->mAlBuffer));
        alSourceiDirect(mContext, mSource, AL_BYTE_OFFSET, static_cast<ALint>(mLastPos));
    }
    alSourcePlayDirect(mContext, mSource);
    if(alGetErrorDirect(mContext) != AL_NO_ERROR)
    {
        ERR("Couldn't start source");
        return DSERR_GENERIC;
    }

    if((mBuffer->mFlags&DSBCAPS_CTRLPOSITIONNOTIFY) && !mNotifies.empty())
        mParent.addNotifyBuffer(this);

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetCurrentPosition "
HRESULT STDMETHODCALLTYPE Buffer::SetCurrentPosition(DWORD newPosition) noexcept
{
    DEBUG("({})->({})", voidp{this}, newPosition);

    if(newPosition >= mBuffer->mData.size())
        return DSERR_INVALIDPARAM;
    newPosition -= newPosition % mBuffer->mWfxFormat.Format.nBlockAlign;

    auto const lock = std::lock_guard{mMutex};
    if(mSource != 0)
    {
        alSourceiDirect(mContext, mSource, AL_BYTE_OFFSET, static_cast<ALint>(newPosition));
        alGetErrorDirect(mContext);
    }
    mLastPos = newPosition;

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetFormat "
HRESULT STDMETHODCALLTYPE Buffer::SetFormat(const WAVEFORMATEX *wfx) noexcept
{
    DEBUG("({})->({})", voidp{this}, cvoidp{wfx});
    return DSERR_INVALIDCALL;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetVolume "
HRESULT STDMETHODCALLTYPE Buffer::SetVolume(LONG volume) noexcept
{
    DEBUG("({})->({})", voidp{this}, volume);

    if(volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
    {
        WARN("Invalid volume ({})", volume);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;

    auto const lock = std::lock_guard{mMutex};
    mVolume = volume;
    if(mSource != 0) [[likely]]
        alSourcefDirect(mContext, mSource, AL_GAIN, mB_to_gain(volume));

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetPan "
HRESULT STDMETHODCALLTYPE Buffer::SetPan(LONG pan) noexcept
{
    DEBUG("({})->({})", voidp{this}, pan);

    if(pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT)
    {
        WARN("Invalid parameter: {}", pan);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;

    auto const lock = std::lock_guard{mMutex};
    mPan = pan;
    if(!(mBuffer->mFlags&DSBCAPS_CTRL3D) && mSource != 0) [[likely]]
    {
        if(mParent.haveExtension(SOFT_SOURCE_PANNING))
        {
            const auto panf = (pan <= 0) ? (mB_to_gain(pan)-1.0f) : (1.0f-mB_to_gain(-pan));
            alSourcefDirect(mContext, mSource, AL_PAN_SOFT, panf);
        }
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetFrequency "
HRESULT STDMETHODCALLTYPE Buffer::SetFrequency(DWORD frequency) noexcept
{
    DEBUG("({})->({})", voidp{this}, frequency);

    if(frequency != 0 && (frequency < DSBFREQUENCY_MIN || frequency > DSBFREQUENCY_MAX))
    {
        WARN("Invalid parameter: {}", frequency);
        return DSERR_INVALIDPARAM;
    }

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;

    auto const lock = std::lock_guard{mMutex};
    mFrequency = frequency ? frequency : mBuffer->mWfxFormat.Format.nSamplesPerSec;
    if(mSource != 0)
    {
        const float pitch{static_cast<float>(mFrequency) /
            static_cast<float>(mBuffer->mWfxFormat.Format.nSamplesPerSec)};
        alSourcefDirect(mContext, mSource, AL_PITCH, pitch);
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Stop "
HRESULT STDMETHODCALLTYPE Buffer::Stop() noexcept
{
    DEBUG("({})->()", voidp{this});

    auto const lock = std::lock_guard{mMutex};
    if(mSource == 0) [[unlikely]]
        return DS_OK;

    auto ofs = ALint{};
    auto state = ALint{};
    alSourcePauseDirect(mContext, mSource);
    alGetSourceiDirect(mContext, mSource, AL_BYTE_OFFSET, &ofs);
    alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);
    alGetErrorDirect(mContext);

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
#undef PREFIX

#define PREFIX CLASS_PREFIX "Unlock "
HRESULT STDMETHODCALLTYPE Buffer::Unlock(void *audioPtr1, DWORD audioBytes1, void *audioPtr2, DWORD audioBytes2) noexcept
{
    DEBUG("({})->({}, {}, {}, {})", voidp{this}, audioPtr1, audioBytes1, audioPtr2, audioBytes2);

    if(!mLocked.exchange(false, std::memory_order_relaxed))
    {
        WARN("Not locked");
        return DSERR_INVALIDPARAM;
    }

    /* Make sure offset is between boundary and boundary + bufsize */
    auto ofs1 = static_cast<uintptr_t>(static_cast<char*>(audioPtr1) - mBuffer->mData.data());
    auto ofs2 = static_cast<uintptr_t>(audioPtr2 ?
        static_cast<char*>(audioPtr2) - mBuffer->mData.data() : 0);
    if(ofs1 >= mBuffer->mData.size() || mBuffer->mData.size()-ofs1 < audioBytes1 || ofs2 != 0
        || audioBytes2 > ofs1)
    {
        WARN("Invalid parameters ({},{}) ({},{},{},{})", voidp{mBuffer->mData.data()},
            mBuffer->mData.size(), audioPtr1, audioBytes1, audioPtr2, audioBytes2);
        return DSERR_INVALIDPARAM;
    }

    /* NOTE: The data was written directly to the buffer, so there's nothing to
     * transfer.
     */

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Restore "
HRESULT STDMETHODCALLTYPE Buffer::Restore() noexcept
{
    DEBUG("({})->()", voidp{this});

    auto const lock = std::lock_guard{mMutex};
    if(mParent.getPriorityLevel() == DSSCL_WRITEPRIMARY
        && this != mParent.getPrimary().getWriteEmu())
        return DSERR_BUFFERLOST;

    mBufferLost = false;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetFX "
HRESULT STDMETHODCALLTYPE Buffer::SetFX(DWORD effectsCount, DSEFFECTDESC *dsFXDesc, DWORD *resultCodes) noexcept
{
    TRACE("({})->({}, {}, {})", voidp{this}, effectsCount, voidp{dsFXDesc}, voidp{resultCodes});

    if(!(mBuffer->mFlags&DSBCAPS_CTRLFX))
        return DSERR_CONTROLUNAVAIL;

    if(effectsCount == 0)
    {
        /* No effects, we can do that. */
        if(dsFXDesc || resultCodes)
        {
            WARN("Non-null pointers for no effects ({}, {})", voidp{dsFXDesc}, voidp{resultCodes});
            return E_INVALIDARG;
        }
        return DS_OK;
    }

    if(!dsFXDesc)
    {
        WARN("Missing FX descriptions");
        return E_INVALIDARG;
    }
    const auto fxdescs = std::span{dsFXDesc, effectsCount};
    const auto rescodes = std::span{resultCodes, resultCodes ? effectsCount : 0ul};

    /* We don't handle DS8 FX. Still not sure how exactly this is supposed to
     * work, surely it doesn't instantiate a unique effect processor for each
     * buffer? But you may sometimes want multiple instances of the same effect
     * type...
     *
     * Not that many apps used this API, so it's not likely a big loss.
     */
    std::ranges::fill(rescodes, DSFXR_FAILED);

    std::ranges::for_each(fxdescs, [](const DSEFFECTDESC &desc)
    {
        DEBUG("Unsupported effect: {:#x}, {}", desc.dwFlags,
            DsfxPrinter{desc.guidDSFXClass}.c_str());
    });

    return DSERR_FXUNAVAILABLE;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "AcquireResources "
HRESULT STDMETHODCALLTYPE Buffer::AcquireResources(DWORD flags, DWORD effectsCount, DWORD *resultCodes) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, flags, effectsCount, voidp{resultCodes});

    auto const lock = std::lock_guard{mMutex};
    if(mBufferLost) [[unlikely]]
    {
        WARN("Buffer lost");
        return DSERR_BUFFERLOST;
    }

    /* effects aren't supported at the moment.. */
    if(effectsCount != 0 || resultCodes)
    {
        WARN("Non-zero effect count and/or result pointer specified with no effects.");
        return DSERR_INVALIDPARAM;
    }

    if((mBuffer->mFlags&DSBCAPS_LOCDEFER))
    {
        static constexpr DWORD LocFlags{DSBPLAY_LOCSOFTWARE | DSBPLAY_LOCHARDWARE};
        if((flags&LocFlags) == LocFlags)
        {
            WARN("Both hardware and software specified");
            return DSERR_INVALIDPARAM;
        }

        LocStatus loc{LocStatus::Any};
        if((flags&DSBPLAY_LOCHARDWARE)) loc = LocStatus::Hardware;
        else if((flags&DSBPLAY_LOCSOFTWARE)) loc = LocStatus::Software;

        if(loc != LocStatus::Any && mLocStatus != LocStatus::None && loc != mLocStatus)
        {
            ALint state{};
            alGetSourceiDirect(mContext, mSource, AL_SOURCE_STATE, &state);
            alGetErrorDirect(mContext);

            if(state == AL_PLAYING)
            {
                FIXME("Attemping to change location on playing buffer");
                return DSERR_INVALIDPARAM;
            }
        }

        if(auto const hr = setLocation(loc); FAILED(hr))
            return hr;
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetObjectInPath "
HRESULT STDMETHODCALLTYPE Buffer::GetObjectInPath(REFGUID objectId, DWORD index, REFGUID interfaceId, void **ppObject) noexcept
{
    FIXME("({})->({}, {}, {}, {})", voidp{this}, GuidPrinter{objectId}.c_str(), index,
        GuidPrinter{interfaceId}.c_str(), voidp{ppObject});
    return E_NOTIMPL;
}
#undef PREFIX


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
    if(!mSource) [[unlikely]] return;

    if(mImmediate.dwMode != DS3DMODE_DISABLE)
    {
        if(flags.test(Position))
            alSource3fDirect(mContext, mSource, AL_POSITION, params.vPosition.x,
                params.vPosition.y, -params.vPosition.z);
        if(flags.test(Velocity))
            alSource3fDirect(mContext, mSource, AL_VELOCITY, params.vVelocity.x,
                params.vVelocity.y, -params.vVelocity.z);
        if(flags.test(ConeOrientation))
            alSource3fDirect(mContext, mSource, AL_DIRECTION, params.vConeOrientation.x,
                params.vConeOrientation.y, -params.vConeOrientation.z);
    }
    if(flags.test(ConeAngles))
    {
        alSourceiDirect(mContext, mSource, AL_CONE_INNER_ANGLE,
            static_cast<ALint>(params.dwInsideConeAngle));
        alSourceiDirect(mContext, mSource, AL_CONE_OUTER_ANGLE,
            static_cast<ALint>(params.dwOutsideConeAngle));
    }
    if(flags.test(ConeVolume))
        alSourcefDirect(mContext, mSource, AL_CONE_OUTER_GAIN,
            mB_to_gain(params.lConeOutsideVolume));
    if(flags.test(MinDistance))
        alSourcefDirect(mContext, mSource, AL_REFERENCE_DISTANCE, params.flMinDistance);
    if(flags.test(MaxDistance))
        alSourcefDirect(mContext, mSource, AL_MAX_DISTANCE, params.flMaxDistance);
    if(flags.test(Mode))
    {
        if(params.dwMode == DS3DMODE_DISABLE)
        {
            alSource3fDirect(mContext, mSource, AL_POSITION, 0.0f, 0.0f, -1.0f);
            alSource3fDirect(mContext, mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            alSource3fDirect(mContext, mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
            alSourcefDirect(mContext, mSource, AL_ROLLOFF_FACTOR, 0.0f);
            if(mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                alSourceiDirect(mContext, mSource, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
        }
        else
        {
            alSource3fDirect(mContext, mSource, AL_POSITION, mImmediate.vPosition.x,
                mImmediate.vPosition.y, -mImmediate.vPosition.z);
            alSource3fDirect(mContext, mSource, AL_VELOCITY, mImmediate.vVelocity.x,
                mImmediate.vVelocity.y, -mImmediate.vVelocity.z);
            alSource3fDirect(mContext, mSource, AL_DIRECTION, mImmediate.vConeOrientation.x,
                mImmediate.vConeOrientation.y, -mImmediate.vConeOrientation.z);
            alSourcefDirect(mContext, mSource, AL_ROLLOFF_FACTOR,
                mParent.getPrimary().getCurrentRolloffFactor());
            if(mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                alSourceiDirect(mContext, mSource, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
        }
        alSourceiDirect(mContext, mSource, AL_SOURCE_RELATIVE,
            (params.dwMode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
    }
}
#undef CLASS_PREFIX


/*** IDirectSound3DBuffer interface. ***/
#define CLASS_PREFIX "Buffer3D::"
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Buffer::Buffer3D::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mDs3dRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Buffer::Buffer3D::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mDs3dRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) [[unlikely]]
        self->mParent.dispose(self);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetAllParameters "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetAllParameters(DS3DBUFFER *ds3dBuffer) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{ds3dBuffer});

    if(!ds3dBuffer || ds3dBuffer->dwSize < sizeof(*ds3dBuffer))
    {
        WARN("Invalid parameters {} {}", voidp{ds3dBuffer}, ds3dBuffer ? ds3dBuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
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
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetConeAngles "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeAngles(DWORD *insideConeAngle, DWORD *outsideConeAngle) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, voidp{insideConeAngle}, voidp{outsideConeAngle});

    if(!insideConeAngle || !outsideConeAngle)
    {
        WARN("Invalid pointers ({}, {})", voidp{insideConeAngle}, voidp{outsideConeAngle});
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *insideConeAngle = self->mImmediate.dwInsideConeAngle;
    *outsideConeAngle = self->mImmediate.dwOutsideConeAngle;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetConeOrientation "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOrientation(D3DVECTOR *orientation) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{orientation});

    if(!orientation)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *orientation = self->mImmediate.vConeOrientation;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetConeOutsideVolume "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetConeOutsideVolume(LONG *coneOutsideVolume) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{coneOutsideVolume});

    if(!coneOutsideVolume)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *coneOutsideVolume = self->mImmediate.lConeOutsideVolume;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetMaxDistance "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMaxDistance(D3DVALUE *maxDistance) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{maxDistance});

    if(!maxDistance)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *maxDistance = self->mImmediate.flMaxDistance;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetMinDistance "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMinDistance(D3DVALUE *minDistance) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{minDistance});

    if(!minDistance)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *minDistance = self->mImmediate.flMinDistance;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetMode "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetMode(DWORD *mode) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{mode});

    if(!mode)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *mode = self->mImmediate.dwMode;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetPosition "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetPosition(D3DVECTOR *position) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{position});

    if(!position)
    {
        WARN("Invalid pointer");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *position = self->mImmediate.vPosition;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "GetVelocity "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::GetVelocity(D3DVECTOR *velocity) noexcept
{
    DEBUG("({})->({})", voidp{this}, voidp{velocity});

    if(!velocity)
    {
        WARN("Invalid pointer)");
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    *velocity = self->mImmediate.vVelocity;
    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetAllParameters "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetAllParameters(const DS3DBUFFER *ds3dBuffer, DWORD apply) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, cvoidp{ds3dBuffer}, apply);

    if(!ds3dBuffer || ds3dBuffer->dwSize < sizeof(*ds3dBuffer))
    {
        WARN("Invalid DS3DBUFFER ({}, {})", cvoidp{ds3dBuffer},
            ds3dBuffer ? ds3dBuffer->dwSize : 0);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->dwInsideConeAngle > DS3D_MAXCONEANGLE
        || ds3dBuffer->dwOutsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN("Invalid cone angles ({}, {})", ds3dBuffer->dwInsideConeAngle,
            ds3dBuffer->dwOutsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->lConeOutsideVolume > DSBVOLUME_MAX
        || ds3dBuffer->lConeOutsideVolume < DSBVOLUME_MIN)
    {
        WARN("Invalid cone outside volume ({})", ds3dBuffer->lConeOutsideVolume);
        return DSERR_INVALIDPARAM;
    }

    if(!(ds3dBuffer->flMinDistance >= 0.0f))
    {
        WARN("Invalid min distance ({:f})", ds3dBuffer->flMinDistance);
        return DSERR_INVALIDPARAM;
    }

    if(!(ds3dBuffer->flMaxDistance >= 0.0f))
    {
        WARN("Invalid max distance ({:f})", ds3dBuffer->flMaxDistance);
        return DSERR_INVALIDPARAM;
    }

    if(ds3dBuffer->dwMode != DS3DMODE_NORMAL && ds3dBuffer->dwMode != DS3DMODE_HEADRELATIVE
        && ds3dBuffer->dwMode != DS3DMODE_DISABLE)
    {
        WARN("Invalid mode ({})", ds3dBuffer->dwMode);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred = *ds3dBuffer;
        self->mDeferred.dwSize = sizeof(self->mDeferred);
        self->mDirty.set();
    }
    else
    {
        self->setParams(*ds3dBuffer, ~0ull);
        alGetErrorDirect(self->mContext);
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetConeAngles "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeAngles(DWORD insideConeAngle, DWORD outsideConeAngle, DWORD apply) noexcept
{
    DEBUG("({})->({}, {}, {})", voidp{this}, insideConeAngle, outsideConeAngle, apply);

    if(insideConeAngle > DS3D_MAXCONEANGLE || outsideConeAngle > DS3D_MAXCONEANGLE)
    {
        WARN("Invalid cone angles ({}, {})", insideConeAngle, outsideConeAngle);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.dwInsideConeAngle = insideConeAngle;
        self->mDeferred.dwOutsideConeAngle = outsideConeAngle;
        self->mDirty.set(ConeAngles);
    }
    else
    {
        self->mImmediate.dwInsideConeAngle = insideConeAngle;
        self->mImmediate.dwOutsideConeAngle = outsideConeAngle;

        if(self->mSource != 0)
        {
            alSourceiDirect(self->mContext, self->mSource, AL_CONE_INNER_ANGLE,
                static_cast<ALint>(insideConeAngle));
            alSourceiDirect(self->mContext, self->mSource, AL_CONE_OUTER_ANGLE,
                static_cast<ALint>(outsideConeAngle));
        }
    }

    return S_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetConeOrientation "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOrientation(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {})", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vConeOrientation.x = x;
        self->mDeferred.vConeOrientation.y = y;
        self->mDeferred.vConeOrientation.z = z;
        self->mDirty.set(ConeOrientation);
    }
    else
    {
        self->mImmediate.vConeOrientation.x = x;
        self->mImmediate.vConeOrientation.y = y;
        self->mImmediate.vConeOrientation.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3fDirect(self->mContext, self->mSource, AL_DIRECTION, x, y, -z);
            alGetErrorDirect(self->mContext);
        }
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetConeOutsideVolume "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetConeOutsideVolume(LONG coneOutsideVolume, DWORD apply) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, coneOutsideVolume, apply);

    if(coneOutsideVolume > DSBVOLUME_MAX || coneOutsideVolume < DSBVOLUME_MIN)
    {
        WARN("Invalid cone outside volume ({})", coneOutsideVolume);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.lConeOutsideVolume = coneOutsideVolume;
        self->mDirty.set(ConeVolume);
    }
    else
    {
        self->mImmediate.lConeOutsideVolume = coneOutsideVolume;

        if(self->mSource != 0)
            alSourcefDirect(self->mContext, self->mSource, AL_CONE_OUTER_GAIN,
                mB_to_gain(coneOutsideVolume));
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetMaxDistance "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMaxDistance(D3DVALUE maxDistance, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {})", voidp{this}, maxDistance, apply);

    if(!(maxDistance >= 0.0f))
    {
        WARN("Invalid max distance ({:f})", maxDistance);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flMaxDistance = maxDistance;
        self->mDirty.set(MaxDistance);
    }
    else
    {
        self->mImmediate.flMaxDistance = maxDistance;

        if(self->mSource != 0)
            alSourcefDirect(self->mContext, self->mSource, AL_MAX_DISTANCE, maxDistance);
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetMinDistance "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMinDistance(D3DVALUE minDistance, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {})", voidp{this}, minDistance, apply);

    if(!(minDistance >= 0.0f))
    {
        WARN("Invalid min distance ({:f})", minDistance);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.flMinDistance = minDistance;
        self->mDirty.set(MinDistance);
    }
    else
    {
        self->mImmediate.flMinDistance = minDistance;

        if(self->mSource != 0)
            alSourcefDirect(self->mContext, self->mSource, AL_REFERENCE_DISTANCE, minDistance);
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetMode "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetMode(DWORD mode, DWORD apply) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, mode, apply);

    if(mode != DS3DMODE_NORMAL && mode != DS3DMODE_HEADRELATIVE && mode != DS3DMODE_DISABLE)
    {
        WARN("Invalid mode ({})", mode);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.dwMode = mode;
        self->mDirty.set(Mode);
    }
    else
    {
        self->mImmediate.dwMode = mode;

        if(self->mSource != 0)
        {
            if(mode == DS3DMODE_DISABLE)
            {
                alSource3fDirect(self->mContext, self->mSource, AL_POSITION, 0.0f, 0.0f, -1.0f);
                alSource3fDirect(self->mContext, self->mSource, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
                alSource3fDirect(self->mContext, self->mSource, AL_DIRECTION, 0.0f, 0.0f, 0.0f);
                alSourcefDirect(self->mContext, self->mSource, AL_ROLLOFF_FACTOR, 0.0f);
                if(self->mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                    alSourceiDirect(self->mContext, self->mSource, AL_SOURCE_SPATIALIZE_SOFT,
                        AL_FALSE);
            }
            else
            {
                alSource3fDirect(self->mContext, self->mSource, AL_POSITION,
                    self->mImmediate.vPosition.x, self->mImmediate.vPosition.y,
                    -self->mImmediate.vPosition.z);
                alSource3fDirect(self->mContext, self->mSource, AL_VELOCITY,
                    self->mImmediate.vVelocity.x, self->mImmediate.vVelocity.y,
                    -self->mImmediate.vVelocity.z);
                alSource3fDirect(self->mContext, self->mSource, AL_DIRECTION,
                    self->mImmediate.vConeOrientation.x, self->mImmediate.vConeOrientation.y,
                    -self->mImmediate.vConeOrientation.z);
                alSourcefDirect(self->mContext, self->mSource, AL_ROLLOFF_FACTOR,
                    self->mParent.getPrimary().getCurrentRolloffFactor());
                if(self->mParent.haveExtension(SOFT_SOURCE_SPATIALIZE))
                    alSourceiDirect(self->mContext, self->mSource, AL_SOURCE_SPATIALIZE_SOFT,
                        AL_TRUE);
            }
            alSourceiDirect(self->mContext, self->mSource, AL_SOURCE_RELATIVE,
                (mode!=DS3DMODE_NORMAL) ? AL_TRUE : AL_FALSE);
            alGetErrorDirect(self->mContext);
        }
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetPosition "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetPosition(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {})", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vPosition.x = x;
        self->mDeferred.vPosition.y = y;
        self->mDeferred.vPosition.z = z;
        self->mDirty.set(Position);
    }
    else
    {
        self->mImmediate.vPosition.x = x;
        self->mImmediate.vPosition.y = y;
        self->mImmediate.vPosition.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3fDirect(self->mContext, self->mSource, AL_POSITION, x, y, -z);
            alGetErrorDirect(self->mContext);
        }
    }

    return DS_OK;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetVelocity "
HRESULT STDMETHODCALLTYPE Buffer::Buffer3D::SetVelocity(D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD apply) noexcept
{
    DEBUG("({})->({:f}, {:f}, {:f}, {})", voidp{this}, x, y, z, apply);

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(apply == DS3D_DEFERRED)
    {
        self->mDeferred.vVelocity.x = x;
        self->mDeferred.vVelocity.y = y;
        self->mDeferred.vVelocity.z = z;
        self->mDirty.set(Velocity);
    }
    else
    {
        self->mImmediate.vVelocity.x = x;
        self->mImmediate.vVelocity.y = y;
        self->mImmediate.vVelocity.z = z;

        if(self->mImmediate.dwMode != DS3DMODE_DISABLE && self->mSource != 0)
        {
            alSource3fDirect(self->mContext, self->mSource, AL_VELOCITY, x, y, -z);
            alGetErrorDirect(self->mContext);
        }
    }

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX


/*** IKsPropertySet interface. ***/
#define CLASS_PREFIX "BufferProp::"
HRESULT STDMETHODCALLTYPE Buffer::Prop::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Buffer::Prop::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mPropRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Buffer::Prop::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mPropRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) [[unlikely]]
        self->mParent.dispose(self);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Get "
HRESULT STDMETHODCALLTYPE Buffer::Prop::Get(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData,
    ULONG *pcbReturned) noexcept
{
    DEBUG("({})->({}, {:#x}, {}, {}, {}, {}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData, voidp{pcbReturned});

    if(!pcbReturned)
        return E_POINTER;
    *pcbReturned = 0;

    if(cbPropData > 0 && !pPropData)
    {
        WARN("pPropData is null with cbPropData={}", cbPropData);
        return E_POINTER;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
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
            const ALenum err{EAXGetDirect(self->mContext, &guidPropSet, dwPropID, self->mSource,
                pPropData, cbPropData)};
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
                    alGetSourceiDirect(self->mContext, self->mSource, AL_SOURCE_STATE, &state);

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

        FIXME("Unhandled VoiceManager propid: {:#010x}", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Set "
HRESULT STDMETHODCALLTYPE Buffer::Prop::Set(REFGUID guidPropSet, ULONG dwPropID,
    void *pInstanceData, ULONG cbInstanceData, void *pPropData, ULONG cbPropData) noexcept
{
    DEBUG("({})->({}, {:#x}, {}, {}, {}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(),
        dwPropID, pInstanceData, cbInstanceData, pPropData, cbPropData);

    if(cbPropData > 0 && !pPropData)
    {
        WARN("pPropData is null with cbPropData={}", cbPropData);
        return E_POINTER;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(guidPropSet == EAXPROPERTYID_EAX40_Source
        || guidPropSet == DSPROPSETID_EAX30_BufferProperties
        || guidPropSet == DSPROPSETID_EAX20_BufferProperties
        || guidPropSet == DSPROPSETID_EAX10_BufferProperties)
    {
        if(self->mParent.haveExtension(EXT_EAX))
        {
            const bool immediate{!(dwPropID&0x80000000u)};

            /* HACK: Get a source ID if we don't have one. This is necessary
             * since an app can apparently set EAX buffer/source properties on
             * a LOCDEFER buffer prior to calling AcquireResources or Play.
             * Rather than duplicating the storage and logic to manage and
             * track EAX properties, let OpenAL do it.
             */
            if(self->mSource == 0) [[unlikely]]
            {
                alGenSourcesDirect(self->mContext, 1, &self->mSource);
                alGetErrorDirect(self->mContext);
            }

            if(immediate)
                alcSuspendContext(self->mContext);

            const auto err = EAXSetDirect(self->mContext, &guidPropSet, dwPropID, self->mSource,
                pPropData, cbPropData);
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
    else if(guidPropSet == EAXPROPERTYID_EAX40_FXSlot0
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot1
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot2
        || guidPropSet == EAXPROPERTYID_EAX40_FXSlot3
        || guidPropSet == DSPROPSETID_EAX30_ListenerProperties
        || guidPropSet == DSPROPSETID_EAX20_ListenerProperties
        || guidPropSet == EAXPROPERTYID_EAX40_Context
        || guidPropSet == DSPROPSETID_EAX10_ListenerProperties)
    {
        if(self->mParent.haveExtension(EXT_EAX))
        {
            const bool immediate{!(dwPropID&0x80000000u)};

            if(immediate)
                alcSuspendContext(self->mContext);

            const auto err = EAXSetDirect(self->mContext, &guidPropSet, dwPropID, self->mSource,
                pPropData, cbPropData);
            if(immediate)
            {
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
                    TRACE("DSPROPERTY_VMANAGER_MODE: {}", mode);
                    self->mBuffer->mVoiceMode = static_cast<VmMode>(mode);
                    return DS_OK;
                }
            }
            return DSERR_INVALIDPARAM;

        case DSPROPERTY_VMANAGER_PRIORITY:
            if(cbPropData >= sizeof(DWORD))
            {
                const DWORD prio{*static_cast<DWORD*>(pPropData)};
                TRACE("DSPROPERTY_VMANAGER_PRIORITY: {}", prio);
                self->mVmPriority = prio;
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
        }

        FIXME("Unhandled VoiceManager propid: {:#010x}", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "QuerySupport "
HRESULT STDMETHODCALLTYPE Buffer::Prop::QuerySupport(REFGUID guidPropSet, ULONG dwPropID,
    ULONG *pTypeSupport) noexcept
{
    TRACE("({})->({}, {:#x}, {})", voidp{this}, PropidPrinter{guidPropSet}.c_str(), dwPropID,
        voidp{pTypeSupport});

    if(!pTypeSupport)
        return E_POINTER;
    *pTypeSupport = 0;

    auto self = impl_from_base();
    if(self->mParent.haveExtension(EXT_EAX))
    {
        auto res = std::invoke([&guidPropSet,dwPropID]() -> std::optional<DWORD>
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
        });
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

        FIXME("Unhandled VoiceManager propid: 0x{:08x}", dwPropID);
        return E_PROP_ID_UNSUPPORTED;
    }

    FIXME("Unhandled propset: {} (propid: {})", PropidPrinter{guidPropSet}.c_str(), dwPropID);
    return E_PROP_ID_UNSUPPORTED;
}
#undef PREFIX
#undef CLASS_PREFIX


/*** IDirectSoundNotify interface wrapper. ***/
#define CLASS_PREFIX "BufferNotify::"
HRESULT STDMETHODCALLTYPE Buffer::Notify::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Buffer::Notify::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mNotRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Buffer::Notify::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mNotRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) [[unlikely]]
        self->mParent.dispose(self);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "SetNotificationPositions "
HRESULT STDMETHODCALLTYPE Buffer::Notify::SetNotificationPositions(DWORD numNotifies,
    const DSBPOSITIONNOTIFY *notifies) noexcept
{
    DEBUG("({})->({}, {})", voidp{this}, numNotifies, cvoidp{notifies});

    if(numNotifies > 0 && !notifies)
    {
        WARN("Null pointer with {} count", numNotifies);
        return DSERR_INVALIDPARAM;
    }

    auto self = impl_from_base();
    auto const lock = std::lock_guard{self->mMutex};
    if(self->mSource != 0)
    {
        ALint state{};
        alGetSourceiDirect(self->mContext, self->mSource, AL_SOURCE_STATE, &state);
        if(state == AL_PLAYING)
        {
            WARN("Source playing");
            return DSERR_INVALIDCALL;
        }
        /* If the source isn't playing and still has a notification check
         * pending, it *just* stopped on its own. Trigger notifications and
         * remove it so we can replace the notifications.
         */
        if(self->mParent.isPendingNotify(self)) [[unlikely]]
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
            DEBUG(" offset = {}, event = {}", notify.dwOffset, voidp{notify.hEventNotify});
            return notify.dwOffset < self->mBuffer->mData.size()
                || notify.dwOffset == static_cast<DWORD>(DSBPN_OFFSETSTOP);
        });
        if(invalidNotify != notifyspan.end())
        {
            WARN("Out of range ({}: {} >= {})", std::distance(notifyspan.begin(), invalidNotify),
                invalidNotify->dwOffset, self->mBuffer->mData.size());
            return DSERR_INVALIDPARAM;
        }
        newNots.assign(notifyspan.begin(), notifyspan.end());

        std::ranges::stable_sort(newNots, std::less{}, &DSBPOSITIONNOTIFY::dwOffset);
    }
    newNots.swap(self->mNotifies);

    return DS_OK;
}
#undef PREFIX
#undef CLASS_PREFIX


/*** IUnknown interface wrapper. ***/
#define CLASS_PREFIX "Buffer::Unknown::"
HRESULT STDMETHODCALLTYPE Buffer::Unknown::QueryInterface(REFIID riid, void **ppvObject) noexcept
{ return impl_from_base()->QueryInterface(riid, ppvObject); }

#define PREFIX CLASS_PREFIX "AddRef "
ULONG STDMETHODCALLTYPE Buffer::Unknown::AddRef() noexcept
{
    auto self = impl_from_base();
    self->mTotalRef.fetch_add(1u, std::memory_order_relaxed);
    const auto ret = self->mUnkRef.fetch_add(1u, std::memory_order_relaxed) + 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    return ret;
}
#undef PREFIX

#define PREFIX CLASS_PREFIX "Release "
ULONG STDMETHODCALLTYPE Buffer::Unknown::Release() noexcept
{
    auto self = impl_from_base();
    const auto ret = self->mUnkRef.fetch_sub(1u, std::memory_order_relaxed) - 1;
    DEBUG("({}) ref {}", voidp{this}, ret);
    if(self->mTotalRef.fetch_sub(1u, std::memory_order_relaxed) == 1u) [[unlikely]]
        self->mParent.dispose(self);
    return ret;
}
#undef PREFIX
#undef CLASS_PREFIX
