#ifndef DSOAL_H
#define DSOAL_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

#include <windows.h>
#include <dsound.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"


namespace gsl {
template<typename T> using owner = T;
} /* namespace gsl */

constexpr int64_t operator "" _i64(unsigned long long int n) noexcept { return static_cast<int64_t>(n); }
constexpr uint64_t operator "" _u64(unsigned long long int n) noexcept { return static_cast<uint64_t>(n); }

inline constexpr size_t MaxSources{1024};
inline constexpr size_t MaxHwSources{128};

namespace ds {

/* Define popcount (population count/count 1 bits) and countr_zero (count
 * trailing zero bits, starting from the lsb) methods, for various integer
 * types.
 */
#ifdef __GNUC__

namespace detail_ {
    inline int popcount(unsigned long long val) noexcept { return __builtin_popcountll(val); }
    inline int popcount(unsigned long val) noexcept { return __builtin_popcountl(val); }
    inline int popcount(unsigned int val) noexcept { return __builtin_popcount(val); }

    inline int countr_zero(unsigned long long val) noexcept { return __builtin_ctzll(val); }
    inline int countr_zero(unsigned long val) noexcept { return __builtin_ctzl(val); }
    inline int countr_zero(unsigned int val) noexcept { return __builtin_ctz(val); }
} // namespace detail_

template<typename T>
inline std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value,
int> popcount(T v) noexcept { return detail_::popcount(v); }

template<typename T>
inline std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value,
int> countr_zero(T val) noexcept
{ return val ? detail_::countr_zero(val) : std::numeric_limits<T>::digits; }

#else

/* There be black magics here. The popcount method is derived from
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
 * while the ctz-utilizing-popcount algorithm is shown here
 * http://www.hackersdelight.org/hdcodetxt/ntz.c.txt
 * as the ntz2 variant. These likely aren't the most efficient methods, but
 * they're good enough if the GCC built-ins aren't available.
 */
namespace detail_ {
    template<typename T, size_t = std::numeric_limits<T>::digits>
    struct fast_utype { };
    template<typename T>
    struct fast_utype<T,8> { using type = std::uint_fast8_t; };
    template<typename T>
    struct fast_utype<T,16> { using type = std::uint_fast16_t; };
    template<typename T>
    struct fast_utype<T,32> { using type = std::uint_fast32_t; };
    template<typename T>
    struct fast_utype<T,64> { using type = std::uint_fast64_t; };

    template<typename T>
    constexpr T repbits(unsigned char bits) noexcept
    {
        T ret{bits};
        for(size_t i{1};i < sizeof(T);++i)
            ret = (ret<<8) | bits;
        return ret;
    }
} // namespace detail_

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value,
int> popcount(T val) noexcept
{
    using fast_type = typename detail_::fast_utype<T>::type;
    constexpr fast_type b01010101{detail_::repbits<fast_type>(0x55)};
    constexpr fast_type b00110011{detail_::repbits<fast_type>(0x33)};
    constexpr fast_type b00001111{detail_::repbits<fast_type>(0x0f)};
    constexpr fast_type b00000001{detail_::repbits<fast_type>(0x01)};

    fast_type v{fast_type{val} - ((fast_type{val} >> 1) & b01010101)};
    v = (v & b00110011) + ((v >> 2) & b00110011);
    v = (v + (v >> 4)) & b00001111;
    return static_cast<int>(((v * b00000001) >> ((sizeof(T)-1)*8)) & 0xff);
}

#ifdef _WIN32

template<typename T>
inline std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value
    && std::numeric_limits<T>::digits <= 32,
int> countr_zero(T v)
{
    unsigned long idx{std::numeric_limits<T>::digits};
    _BitScanForward(&idx, static_cast<uint32_t>(v));
    return static_cast<int>(idx);
}

template<typename T>
inline std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value
    && 32 < std::numeric_limits<T>::digits && std::numeric_limits<T>::digits <= 64,
int> countr_zero(T v)
{
    unsigned long idx{std::numeric_limits<T>::digits};
#ifdef _WIN64
    _BitScanForward64(&idx, v);
#else
    if(!_BitScanForward(&idx, static_cast<uint32_t>(v)))
    {
        if(_BitScanForward(&idx, static_cast<uint32_t>(v>>32)))
            idx += 32;
    }
#endif /* _WIN64 */
    return static_cast<int>(idx);
}

#else

template<typename T>
constexpr std::enable_if_t<std::is_integral<T>::value && std::is_unsigned<T>::value,
int> countr_zero(T value)
{ return popcount(static_cast<T>(~value & (value - 1))); }

#endif
#endif


template<typename T, typename Traits>
[[nodiscard]] constexpr
auto sizei(const std::basic_string_view<T,Traits> str) noexcept -> int
{ return static_cast<int>(std::min<std::size_t>(str.size(), std::numeric_limits<int>::max())); }

} // namespace ds

HRESULT WINAPI GetDeviceID(const GUID &guidSrc, GUID &guidDst) noexcept;

void SetALContext(ALCcontext *context);
inline void UnsetALContext() { }

struct ALSection {
    ALSection(ALCcontext *context) { SetALContext(context); }
    ~ALSection() { UnsetALContext(); }
};


[[nodiscard]]
inline float mB_to_gain(float millibels)
{
    if(millibels <= DSBVOLUME_MIN)
        return 0.0f;
    return std::pow(10.0f, static_cast<float>(millibels) / 2'000.0f);
}


#ifndef AL_SOFT_map_buffer
#define AL_SOFT_map_buffer 1
typedef unsigned int ALbitfieldSOFT;
#define AL_MAP_READ_BIT_SOFT                     0x00000001
#define AL_MAP_WRITE_BIT_SOFT                    0x00000002
#define AL_MAP_PERSISTENT_BIT_SOFT               0x00000004
#define AL_PRESERVE_DATA_BIT_SOFT                0x00000008
typedef void (AL_APIENTRY*LPALBUFFERSTORAGESOFT)(ALuint buffer, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq, ALbitfieldSOFT flags);
typedef void* (AL_APIENTRY*LPALMAPBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length, ALbitfieldSOFT access);
typedef void (AL_APIENTRY*LPALUNMAPBUFFERSOFT)(ALuint buffer);
typedef void (AL_APIENTRY*LPALFLUSHMAPPEDBUFFERSOFT)(ALuint buffer, ALsizei offset, ALsizei length);
#endif

typedef ALenum(AL_APIENTRY*LPEAXSET)(const GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid *property_buffer, ALuint property_size);
typedef ALenum(AL_APIENTRY*LPEAXGET)(const GUID *property_set_id, ALuint property_id, ALuint property_source_id, ALvoid* property_buffer, ALuint property_size);

inline LPALCCREATECONTEXT palcCreateContext{};
inline LPALCMAKECONTEXTCURRENT palcMakeContextCurrent{};
inline LPALCPROCESSCONTEXT palcProcessContext{};
inline LPALCSUSPENDCONTEXT palcSuspendContext{};
inline LPALCDESTROYCONTEXT palcDestroyContext{};
inline LPALCGETCURRENTCONTEXT palcGetCurrentContext{};
inline LPALCGETCONTEXTSDEVICE palcGetContextsDevice{};
inline LPALCOPENDEVICE palcOpenDevice{};
inline LPALCCLOSEDEVICE palcCloseDevice{};
inline LPALCGETERROR palcGetError{};
inline LPALCISEXTENSIONPRESENT palcIsExtensionPresent{};
inline LPALCGETPROCADDRESS palcGetProcAddress{};
inline LPALCGETENUMVALUE palcGetEnumValue{};
inline LPALCGETSTRING palcGetString{};
inline LPALCGETINTEGERV palcGetIntegerv{};
inline LPALCCAPTUREOPENDEVICE palcCaptureOpenDevice{};
inline LPALCCAPTURECLOSEDEVICE palcCaptureCloseDevice{};
inline LPALCCAPTURESTART palcCaptureStart{};
inline LPALCCAPTURESTOP palcCaptureStop{};
inline LPALCCAPTURESAMPLES palcCaptureSamples{};
inline LPALENABLE palEnable{};
inline LPALDISABLE palDisable{};
inline LPALISENABLED palIsEnabled{};
inline LPALGETSTRING palGetString{};
inline LPALGETBOOLEANV palGetBooleanv{};
inline LPALGETINTEGERV palGetIntegerv{};
inline LPALGETFLOATV palGetFloatv{};
inline LPALGETDOUBLEV palGetDoublev{};
inline LPALGETBOOLEAN palGetBoolean{};
inline LPALGETINTEGER palGetInteger{};
inline LPALGETFLOAT palGetFloat{};
inline LPALGETDOUBLE palGetDouble{};
inline LPALGETERROR palGetError{};
inline LPALISEXTENSIONPRESENT palIsExtensionPresent{};
inline LPALGETPROCADDRESS palGetProcAddress{};
inline LPALGETENUMVALUE palGetEnumValue{};
inline LPALLISTENERF palListenerf{};
inline LPALLISTENER3F palListener3f{};
inline LPALLISTENERFV palListenerfv{};
inline LPALLISTENERI palListeneri{};
inline LPALLISTENER3I palListener3i{};
inline LPALLISTENERIV palListeneriv{};
inline LPALGETLISTENERF palGetListenerf{};
inline LPALGETLISTENER3F palGetListener3f{};
inline LPALGETLISTENERFV palGetListenerfv{};
inline LPALGETLISTENERI palGetListeneri{};
inline LPALGETLISTENER3I palGetListener3i{};
inline LPALGETLISTENERIV palGetListeneriv{};
inline LPALGENSOURCES palGenSources{};
inline LPALDELETESOURCES palDeleteSources{};
inline LPALISSOURCE palIsSource{};
inline LPALSOURCEF palSourcef{};
inline LPALSOURCE3F palSource3f{};
inline LPALSOURCEFV palSourcefv{};
inline LPALSOURCEI palSourcei{};
inline LPALSOURCE3I palSource3i{};
inline LPALSOURCEIV palSourceiv{};
inline LPALGETSOURCEF palGetSourcef{};
inline LPALGETSOURCE3F palGetSource3f{};
inline LPALGETSOURCEFV palGetSourcefv{};
inline LPALGETSOURCEI palGetSourcei{};
inline LPALGETSOURCE3I palGetSource3i{};
inline LPALGETSOURCEIV palGetSourceiv{};
inline LPALSOURCEPLAYV palSourcePlayv{};
inline LPALSOURCESTOPV palSourceStopv{};
inline LPALSOURCEREWINDV palSourceRewindv{};
inline LPALSOURCEPAUSEV palSourcePausev{};
inline LPALSOURCEPLAY palSourcePlay{};
inline LPALSOURCESTOP palSourceStop{};
inline LPALSOURCEREWIND palSourceRewind{};
inline LPALSOURCEPAUSE palSourcePause{};
inline LPALSOURCEQUEUEBUFFERS palSourceQueueBuffers{};
inline LPALSOURCEUNQUEUEBUFFERS palSourceUnqueueBuffers{};
inline LPALGENBUFFERS palGenBuffers{};
inline LPALDELETEBUFFERS palDeleteBuffers{};
inline LPALISBUFFER palIsBuffer{};
inline LPALBUFFERF palBufferf{};
inline LPALBUFFER3F palBuffer3f{};
inline LPALBUFFERFV palBufferfv{};
inline LPALBUFFERI palBufferi{};
inline LPALBUFFER3I palBuffer3i{};
inline LPALBUFFERIV palBufferiv{};
inline LPALGETBUFFERF palGetBufferf{};
inline LPALGETBUFFER3F palGetBuffer3f{};
inline LPALGETBUFFERFV palGetBufferfv{};
inline LPALGETBUFFERI palGetBufferi{};
inline LPALGETBUFFER3I palGetBuffer3i{};
inline LPALGETBUFFERIV palGetBufferiv{};
inline LPALBUFFERDATA palBufferData{};
inline LPALDOPPLERFACTOR palDopplerFactor{};
inline LPALDOPPLERVELOCITY palDopplerVelocity{};
inline LPALDISTANCEMODEL palDistanceModel{};
inline LPALSPEEDOFSOUND palSpeedOfSound{};

/* Extension functions. Technically device- or driver-specific, but as long as
 * they're pulled from the NULL device it should be routed correctly.
 */
inline PFNALCSETTHREADCONTEXTPROC palcSetThreadContext{};
inline PFNALCGETTHREADCONTEXTPROC palcGetThreadContext{};
inline LPEAXSET pEAXSet{};
inline LPEAXGET pEAXGet{};
inline PFNALBUFFERDATASTATICPROC palBufferDataStatic{};
inline LPALBUFFERSTORAGESOFT palBufferStorageSOFT{};
inline LPALMAPBUFFERSOFT palMapBufferSOFT{};
inline LPALUNMAPBUFFERSOFT palUnmapBufferSOFT{};
inline LPALFLUSHMAPPEDBUFFERSOFT palFlushMappedBufferSOFT{};

#ifndef IN_IDE_PARSER
#define alcCreateContext palcCreateContext
#define alcMakeContextCurrent palcMakeContextCurrent
#define alcProcessContext palcProcessContext
#define alcSuspendContext palcSuspendContext
#define alcDestroyContext palcDestroyContext
#define alcGetCurrentContext palcGetCurrentContext
#define alcGetContextsDevice palcGetContextsDevice
#define alcOpenDevice palcOpenDevice
#define alcCloseDevice palcCloseDevice
#define alcGetError palcGetError
#define alcIsExtensionPresent palcIsExtensionPresent
#define alcGetProcAddress palcGetProcAddress
#define alcGetEnumValue palcGetEnumValue
#define alcGetString palcGetString
#define alcGetIntegerv palcGetIntegerv
#define alcCaptureOpenDevice palcCaptureOpenDevice
#define alcCaptureCloseDevice palcCaptureCloseDevice
#define alcCaptureStart palcCaptureStart
#define alcCaptureStop palcCaptureStop
#define alcCaptureSamples palcCaptureSamples
#define alEnable palEnable
#define alDisable palDisable
#define alIsEnabled palIsEnabled
#define alGetString palGetString
#define alGetBooleanv palGetBooleanv
#define alGetIntegerv palGetIntegerv
#define alGetFloatv palGetFloatv
#define alGetDoublev palGetDoublev
#define alGetBoolean palGetBoolean
#define alGetInteger palGetInteger
#define alGetFloat palGetFloat
#define alGetDouble palGetDouble
#define alGetError palGetError
#define alIsExtensionPresent palIsExtensionPresent
#define alGetProcAddress palGetProcAddress
#define alGetEnumValue palGetEnumValue
#define alListenerf palListenerf
#define alListener3f palListener3f
#define alListenerfv palListenerfv
#define alListeneri palListeneri
#define alListener3i palListener3i
#define alListeneriv palListeneriv
#define alGetListenerf palGetListenerf
#define alGetListener3f palGetListener3f
#define alGetListenerfv palGetListenerfv
#define alGetListeneri palGetListeneri
#define alGetListener3i palGetListener3i
#define alGetListeneriv palGetListeneriv
#define alGenSources palGenSources
#define alDeleteSources palDeleteSources
#define alIsSource palIsSource
#define alSourcef palSourcef
#define alSource3f palSource3f
#define alSourcefv palSourcefv
#define alSourcei palSourcei
#define alSource3i palSource3i
#define alSourceiv palSourceiv
#define alGetSourcef palGetSourcef
#define alGetSource3f palGetSource3f
#define alGetSourcefv palGetSourcefv
#define alGetSourcei palGetSourcei
#define alGetSource3i palGetSource3i
#define alGetSourceiv palGetSourceiv
#define alSourcePlayv palSourcePlayv
#define alSourceStopv palSourceStopv
#define alSourceRewindv palSourceRewindv
#define alSourcePausev palSourcePausev
#define alSourcePlay palSourcePlay
#define alSourceStop palSourceStop
#define alSourceRewind palSourceRewind
#define alSourcePause palSourcePause
#define alSourceQueueBuffers palSourceQueueBuffers
#define alSourceUnqueueBuffers palSourceUnqueueBuffers
#define alGenBuffers palGenBuffers
#define alDeleteBuffers palDeleteBuffers
#define alIsBuffer palIsBuffer
#define alBufferf palBufferf
#define alBuffer3f palBuffer3f
#define alBufferfv palBufferfv
#define alBufferi palBufferi
#define alBuffer3i palBuffer3i
#define alBufferiv palBufferiv
#define alGetBufferf palGetBufferf
#define alGetBuffer3f palGetBuffer3f
#define alGetBufferfv palGetBufferfv
#define alGetBufferi palGetBufferi
#define alGetBuffer3i palGetBuffer3i
#define alGetBufferiv palGetBufferiv
#define alBufferData palBufferData
#define alDopplerFactor palDopplerFactor
#define alDopplerVelocity palDopplerVelocity
#define alDistanceModel palDistanceModel
#define alSpeedOfSound palSpeedOfSound
#endif

#define alcSetThreadContext palcSetThreadContext
#define alcGetThreadContext palcGetThreadContext
#define EAXSet pEAXSet
#define EAXGet pEAXGet
#define alBufferDataStatic palBufferDataStatic
#define alBufferStorageSOFT palBufferStorageSOFT
#define alMapBufferSOFT palMapBufferSOFT
#define alUnmapBufferSOFT palUnmapBufferSOFT
#define alFlushMappedBufferSOFT palFlushMappedBufferSOFT

#endif // DSOAL_H
