#ifndef DSOAL_H
#define DSOAL_H

#include <windows.h>
#include <dsound.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"


HRESULT WINAPI GetDeviceID(const GUID &guidSrc, GUID &guidDst) noexcept;

inline constexpr size_t MaxSources{1024};
inline constexpr size_t MaxHwSources{128};


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
inline LPALDEFERUPDATESSOFT palDeferUpdatesSOFT{};
inline LPALPROCESSUPDATESSOFT palProcessUpdatesSOFT{};
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
#define alDeferUpdatesSOFT palDeferUpdatesSOFT
#define alProcessUpdatesSOFT palProcessUpdatesSOFT
#define alBufferStorageSOFT palBufferStorageSOFT
#define alMapBufferSOFT palMapBufferSOFT
#define alUnmapBufferSOFT palUnmapBufferSOFT
#define alFlushMappedBufferSOFT palFlushMappedBufferSOFT

#endif // DSOAL_H
