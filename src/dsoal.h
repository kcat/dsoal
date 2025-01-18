#ifndef DSOAL_H
#define DSOAL_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
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

template<typename T, std::enable_if_t<std::is_integral_v<T>,bool> = true>
constexpr auto as_unsigned(T value) noexcept
{
    using UT = std::make_unsigned_t<T>;
    return static_cast<UT>(value);
}

inline constexpr size_t MaxSources{1024};
inline constexpr size_t MaxHwSources{128};

auto wstr_to_utf8(std::wstring_view wstr) -> std::string;
auto utf8_to_wstr(std::string_view str) -> std::wstring;

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

[[nodiscard]]
inline float mB_to_gain(float millibels)
{
    if(millibels <= DSBVOLUME_MIN)
        return 0.0f;
    return std::pow(10.0f, millibels / 2'000.0f);
}


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

inline LPALENABLEDIRECT alEnableDirect{};
inline LPALDISABLEDIRECT alDisableDirect{};
inline LPALISENABLEDDIRECT alIsEnabledDirect{};
inline LPALGETSTRINGDIRECT alGetStringDirect{};
inline LPALGETBOOLEANVDIRECT alGetBooleanvDirect{};
inline LPALGETINTEGERVDIRECT alGetIntegervDirect{};
inline LPALGETFLOATVDIRECT alGetFloatvDirect{};
inline LPALGETDOUBLEVDIRECT alGetDoublevDirect{};
inline LPALGETBOOLEANDIRECT alGetBooleanDirect{};
inline LPALGETINTEGERDIRECT alGetIntegerDirect{};
inline LPALGETFLOATDIRECT alGetFloatDirect{};
inline LPALGETDOUBLEDIRECT alGetDoubleDirect{};
inline LPALGETERRORDIRECT alGetErrorDirect{};
inline LPALISEXTENSIONPRESENTDIRECT alIsExtensionPresentDirect{};
inline LPALGETPROCADDRESSDIRECT alGetProcAddressDirect{};
inline LPALGETENUMVALUEDIRECT alGetEnumValueDirect{};
inline LPALLISTENERFDIRECT alListenerfDirect{};
inline LPALLISTENER3FDIRECT alListener3fDirect{};
inline LPALLISTENERFVDIRECT alListenerfvDirect{};
inline LPALLISTENERIDIRECT alListeneriDirect{};
inline LPALLISTENER3IDIRECT alListener3iDirect{};
inline LPALLISTENERIVDIRECT alListenerivDirect{};
inline LPALGETLISTENERFDIRECT alGetListenerfDirect{};
inline LPALGETLISTENER3FDIRECT alGetListener3fDirect{};
inline LPALGETLISTENERFVDIRECT alGetListenerfvDirect{};
inline LPALGETLISTENERIDIRECT alGetListeneriDirect{};
inline LPALGETLISTENER3IDIRECT alGetListener3iDirect{};
inline LPALGETLISTENERIVDIRECT alGetListenerivDirect{};
inline LPALGENSOURCESDIRECT alGenSourcesDirect{};
inline LPALDELETESOURCESDIRECT alDeleteSourcesDirect{};
inline LPALISSOURCEDIRECT alIsSourceDirect{};
inline LPALSOURCEFDIRECT alSourcefDirect{};
inline LPALSOURCE3FDIRECT alSource3fDirect{};
inline LPALSOURCEFVDIRECT alSourcefvDirect{};
inline LPALSOURCEIDIRECT alSourceiDirect{};
inline LPALSOURCE3IDIRECT alSource3iDirect{};
inline LPALSOURCEIVDIRECT alSourceivDirect{};
inline LPALGETSOURCEFDIRECT alGetSourcefDirect{};
inline LPALGETSOURCE3FDIRECT alGetSource3fDirect{};
inline LPALGETSOURCEFVDIRECT alGetSourcefvDirect{};
inline LPALGETSOURCEIDIRECT alGetSourceiDirect{};
inline LPALGETSOURCE3IDIRECT alGetSource3iDirect{};
inline LPALGETSOURCEIVDIRECT alGetSourceivDirect{};
inline LPALSOURCEPLAYVDIRECT alSourcePlayvDirect{};
inline LPALSOURCESTOPVDIRECT alSourceStopvDirect{};
inline LPALSOURCEREWINDVDIRECT alSourceRewindvDirect{};
inline LPALSOURCEPAUSEVDIRECT alSourcePausevDirect{};
inline LPALSOURCEPLAYDIRECT alSourcePlayDirect{};
inline LPALSOURCESTOPDIRECT alSourceStopDirect{};
inline LPALSOURCEREWINDDIRECT alSourceRewindDirect{};
inline LPALSOURCEPAUSEDIRECT alSourcePauseDirect{};
inline LPALSOURCEQUEUEBUFFERSDIRECT alSourceQueueBuffersDirect{};
inline LPALSOURCEUNQUEUEBUFFERSDIRECT alSourceUnqueueBuffersDirect{};
inline LPALGENBUFFERSDIRECT alGenBuffersDirect{};
inline LPALDELETEBUFFERSDIRECT alDeleteBuffersDirect{};
inline LPALISBUFFERDIRECT alIsBufferDirect{};
inline LPALBUFFERFDIRECT alBufferfDirect{};
inline LPALBUFFER3FDIRECT alBuffer3fDirect{};
inline LPALBUFFERFVDIRECT alBufferfvDirect{};
inline LPALBUFFERIDIRECT alBufferiDirect{};
inline LPALBUFFER3IDIRECT alBuffer3iDirect{};
inline LPALBUFFERIVDIRECT alBufferivDirect{};
inline LPALGETBUFFERFDIRECT alGetBufferfDirect{};
inline LPALGETBUFFER3FDIRECT alGetBuffer3fDirect{};
inline LPALGETBUFFERFVDIRECT alGetBufferfvDirect{};
inline LPALGETBUFFERIDIRECT alGetBufferiDirect{};
inline LPALGETBUFFER3IDIRECT alGetBuffer3iDirect{};
inline LPALGETBUFFERIVDIRECT alGetBufferivDirect{};
inline LPALBUFFERDATADIRECT alBufferDataDirect{};
inline LPALDOPPLERFACTORDIRECT alDopplerFactorDirect{};
inline LPALDISTANCEMODELDIRECT alDistanceModelDirect{};
inline LPALSPEEDOFSOUNDDIRECT alSpeedOfSoundDirect{};

/* Extension functions. Technically device- or driver-specific, but as long as
 * they're pulled from the NULL device it should be routed correctly.
 */
inline LPEAXSETDIRECT EAXSetDirect{};
inline LPEAXGETDIRECT EAXGetDirect{};
inline LPALBUFFERDATASTATICDIRECT alBufferDataStaticDirect{};

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
#endif

#endif // DSOAL_H
