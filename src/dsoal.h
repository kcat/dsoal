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

template<typename T> requires(std::is_integral_v<T>)
constexpr auto as_unsigned(T value) noexcept
{
    using UT = std::make_unsigned_t<T>;
    return static_cast<UT>(value);
}

auto wstr_to_utf8(std::wstring_view wstr) -> std::string;
auto utf8_to_wstr(std::string_view str) -> std::wstring;

inline constexpr size_t MaxSources{1024};
inline constexpr size_t MaxHwSources{128};

HRESULT WINAPI GetDeviceID(const GUID &guidSrc, GUID &guidDst) noexcept;

[[nodiscard]]
inline float mB_to_gain(LONG millibels)
{
    if(millibels <= DSBVOLUME_MIN)
        return 0.0f;
    return std::pow(10.0f, static_cast<float>(millibels) / 2'000.0f);
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
