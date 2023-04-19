#include "eax.h"

#include <dsound.h>
#include <vfwmsgs.h>

#include "logging.h"

namespace {

enum DSPROPERTY_EAX10_REVERBPROPERTY : unsigned int {
    DSPROPERTY_EAX10REVERB_ALL,
    DSPROPERTY_EAX10REVERB_ENVIRONMENT,
    DSPROPERTY_EAX10REVERB_VOLUME,
    DSPROPERTY_EAX10REVERB_DECAYTIME,
    DSPROPERTY_EAX10REVERB_DAMPING,
}; // DSPROPERTY_EAX10_REVERBPROPERTY

enum DSPROPERTY_EAX10_BUFFERPROPERTY : unsigned int {
    DSPROPERTY_EAX10BUFFER_ALL,
    DSPROPERTY_EAX10BUFFER_REVERBMIX,
}; // DSPROPERTY_EAX10_BUFFERPROPERTY

enum DSPROPERTY_EAX20_LISTENERPROPERTY : unsigned int {
    DSPROPERTY_EAX20LISTENER_NONE,
    DSPROPERTY_EAX20LISTENER_ALLPARAMETERS,
    DSPROPERTY_EAX20LISTENER_ROOM,
    DSPROPERTY_EAX20LISTENER_ROOMHF,
    DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX20LISTENER_DECAYTIME,
    DSPROPERTY_EAX20LISTENER_DECAYHFRATIO,
    DSPROPERTY_EAX20LISTENER_REFLECTIONS,
    DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY,
    DSPROPERTY_EAX20LISTENER_REVERB,
    DSPROPERTY_EAX20LISTENER_REVERBDELAY,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENT,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION,
    DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF,
    DSPROPERTY_EAX20LISTENER_FLAGS
}; // DSPROPERTY_EAX20_LISTENERPROPERTY

enum DSPROPERTY_EAX20_BUFFERPROPERTY : unsigned int {
    DSPROPERTY_EAX20BUFFER_NONE,
    DSPROPERTY_EAX20BUFFER_ALLPARAMETERS,
    DSPROPERTY_EAX20BUFFER_DIRECT,
    DSPROPERTY_EAX20BUFFER_DIRECTHF,
    DSPROPERTY_EAX20BUFFER_ROOM,
    DSPROPERTY_EAX20BUFFER_ROOMHF,
    DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX20BUFFER_OBSTRUCTION,
    DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO,
    DSPROPERTY_EAX20BUFFER_OCCLUSION,
    DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO,
    DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO,
    DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF,
    DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR,
    DSPROPERTY_EAX20BUFFER_FLAGS
}; // DSPROPERTY_EAX20_BUFFERPROPERTY

enum EAXCONTEXT_PROPERTY : unsigned int {
    EAXCONTEXT_NONE = 0,
    EAXCONTEXT_ALLPARAMETERS,
    EAXCONTEXT_PRIMARYFXSLOTID,
    EAXCONTEXT_DISTANCEFACTOR,
    EAXCONTEXT_AIRABSORPTIONHF,
    EAXCONTEXT_HFREFERENCE,
    EAXCONTEXT_LASTERROR,

    // EAX50
    EAXCONTEXT_SPEAKERCONFIG,
    EAXCONTEXT_EAXSESSION,
    EAXCONTEXT_MACROFXFACTOR,
}; // EAXCONTEXT_PROPERTY

enum EAXFXSLOT_PROPERTY : unsigned int {
    EAXFXSLOT_PARAMETER = 0,

    EAXFXSLOT_NONE = 0x10000,
    EAXFXSLOT_ALLPARAMETERS,
    EAXFXSLOT_LOADEFFECT,
    EAXFXSLOT_VOLUME,
    EAXFXSLOT_LOCK,
    EAXFXSLOT_FLAGS,

    // EAX50
    EAXFXSLOT_OCCLUSION,
    EAXFXSLOT_OCCLUSIONLFRATIO,
}; // EAXFXSLOT_PROPERTY

enum EAXSOURCE_PROPERTY : unsigned int {
    // EAX30
    EAXSOURCE_NONE,
    EAXSOURCE_ALLPARAMETERS,
    EAXSOURCE_OBSTRUCTIONPARAMETERS,
    EAXSOURCE_OCCLUSIONPARAMETERS,
    EAXSOURCE_EXCLUSIONPARAMETERS,
    EAXSOURCE_DIRECT,
    EAXSOURCE_DIRECTHF,
    EAXSOURCE_ROOM,
    EAXSOURCE_ROOMHF,
    EAXSOURCE_OBSTRUCTION,
    EAXSOURCE_OBSTRUCTIONLFRATIO,
    EAXSOURCE_OCCLUSION,
    EAXSOURCE_OCCLUSIONLFRATIO,
    EAXSOURCE_OCCLUSIONROOMRATIO,
    EAXSOURCE_OCCLUSIONDIRECTRATIO,
    EAXSOURCE_EXCLUSION,
    EAXSOURCE_EXCLUSIONLFRATIO,
    EAXSOURCE_OUTSIDEVOLUMEHF,
    EAXSOURCE_DOPPLERFACTOR,
    EAXSOURCE_ROLLOFFFACTOR,
    EAXSOURCE_ROOMROLLOFFFACTOR,
    EAXSOURCE_AIRABSORPTIONFACTOR,
    EAXSOURCE_FLAGS,

    // EAX40
    EAXSOURCE_SENDPARAMETERS,
    EAXSOURCE_ALLSENDPARAMETERS,
    EAXSOURCE_OCCLUSIONSENDPARAMETERS,
    EAXSOURCE_EXCLUSIONSENDPARAMETERS,
    EAXSOURCE_ACTIVEFXSLOTID,

    // EAX50
    EAXSOURCE_MACROFXFACTOR,
    EAXSOURCE_SPEAKERLEVELS,
    EAXSOURCE_ALL2DPARAMETERS,
}; // EAXSOURCE_PROPERTY

// Reverb effect properties
enum EAXREVERB_PROPERTY : unsigned int {
    EAXREVERB_NONE,
    EAXREVERB_ALLPARAMETERS,
    EAXREVERB_ENVIRONMENT,
    EAXREVERB_ENVIRONMENTSIZE,
    EAXREVERB_ENVIRONMENTDIFFUSION,
    EAXREVERB_ROOM,
    EAXREVERB_ROOMHF,
    EAXREVERB_ROOMLF,
    EAXREVERB_DECAYTIME,
    EAXREVERB_DECAYHFRATIO,
    EAXREVERB_DECAYLFRATIO,
    EAXREVERB_REFLECTIONS,
    EAXREVERB_REFLECTIONSDELAY,
    EAXREVERB_REFLECTIONSPAN,
    EAXREVERB_REVERB,
    EAXREVERB_REVERBDELAY,
    EAXREVERB_REVERBPAN,
    EAXREVERB_ECHOTIME,
    EAXREVERB_ECHODEPTH,
    EAXREVERB_MODULATIONTIME,
    EAXREVERB_MODULATIONDEPTH,
    EAXREVERB_AIRABSORPTIONHF,
    EAXREVERB_HFREFERENCE,
    EAXREVERB_LFREFERENCE,
    EAXREVERB_ROOMROLLOFFFACTOR,
    EAXREVERB_FLAGS,
}; // EAXREVERB_PROPERTY


constexpr DWORD EAXCONTEXT_PARAMETER_DEFERRED{0x80000000u};
constexpr DWORD EAXFXSLOT_PARAMETER_DEFERRED{0x80000000u};
constexpr DWORD EAXSOURCE_PARAMETER_DEFERRED{0x80000000u};
constexpr DWORD EAX30LISTENER_DEFERRED{0x80000000u};
constexpr DWORD EAX30BUFFER_DEFERRED{0x80000000u};
constexpr DWORD DSPROPERTY_EAX20LISTENER_DEFERRED{0x80000000u};
constexpr DWORD DSPROPERTY_EAX20BUFFER_DEFERRED{0x80000000u};

} // namespace


/*******************
 * EAX 4 stuff
 ******************/

DWORD EAX4Context_Query(DWORD propid)
{
    switch((propid&~EAXCONTEXT_PARAMETER_DEFERRED))
    {
    case EAXCONTEXT_NONE:
    case EAXCONTEXT_ALLPARAMETERS:
    case EAXCONTEXT_PRIMARYFXSLOTID:
    case EAXCONTEXT_DISTANCEFACTOR:
    case EAXCONTEXT_AIRABSORPTIONHF:
    case EAXCONTEXT_HFREFERENCE:
    case EAXCONTEXT_LASTERROR:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX4Context_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}

DWORD EAX4Slot_Query(DWORD propid)
{
    switch((propid&~EAXFXSLOT_PARAMETER_DEFERRED))
    {
    case EAXFXSLOT_NONE:
    case EAXFXSLOT_ALLPARAMETERS:
    case EAXFXSLOT_LOADEFFECT:
    case EAXFXSLOT_VOLUME:
    case EAXFXSLOT_LOCK:
    case EAXFXSLOT_FLAGS:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    /* FIXME: This should probably only succeed for the available parameters of
     * the current effect type.
     */
    if((propid&~EAXFXSLOT_PARAMETER_DEFERRED) <= EAXREVERB_FLAGS)
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    FIXME("EAX4Slot_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}

DWORD EAX4Source_Query(DWORD propid)
{
    switch((propid&~EAXSOURCE_PARAMETER_DEFERRED))
    {
    case EAXSOURCE_NONE:
    case EAXSOURCE_ALLPARAMETERS:
    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
    case EAXSOURCE_OCCLUSIONPARAMETERS:
    case EAXSOURCE_EXCLUSIONPARAMETERS:
    case EAXSOURCE_DIRECT:
    case EAXSOURCE_DIRECTHF:
    case EAXSOURCE_ROOM:
    case EAXSOURCE_ROOMHF:
    case EAXSOURCE_OBSTRUCTION:
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
    case EAXSOURCE_OCCLUSION:
    case EAXSOURCE_OCCLUSIONLFRATIO:
    case EAXSOURCE_OCCLUSIONROOMRATIO:
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
    case EAXSOURCE_EXCLUSION:
    case EAXSOURCE_EXCLUSIONLFRATIO:
    case EAXSOURCE_OUTSIDEVOLUMEHF:
    case EAXSOURCE_DOPPLERFACTOR:
    case EAXSOURCE_ROLLOFFFACTOR:
    case EAXSOURCE_ROOMROLLOFFFACTOR:
    case EAXSOURCE_AIRABSORPTIONFACTOR:
    case EAXSOURCE_FLAGS:
    case EAXSOURCE_SENDPARAMETERS:
    case EAXSOURCE_ALLSENDPARAMETERS:
    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
    case EAXSOURCE_ACTIVEFXSLOTID:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX4Source_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}


/*******************
 * EAX 3 stuff
 ******************/

DWORD EAX3_Query(DWORD propid)
{
    switch((propid&~EAX30LISTENER_DEFERRED))
    {
    case EAXREVERB_NONE:
    case EAXREVERB_ALLPARAMETERS:
    case EAXREVERB_ENVIRONMENT:
    case EAXREVERB_ENVIRONMENTSIZE:
    case EAXREVERB_ENVIRONMENTDIFFUSION:
    case EAXREVERB_ROOM:
    case EAXREVERB_ROOMHF:
    case EAXREVERB_ROOMLF:
    case EAXREVERB_DECAYTIME:
    case EAXREVERB_DECAYHFRATIO:
    case EAXREVERB_DECAYLFRATIO:
    case EAXREVERB_REFLECTIONS:
    case EAXREVERB_REFLECTIONSDELAY:
    case EAXREVERB_REFLECTIONSPAN:
    case EAXREVERB_REVERB:
    case EAXREVERB_REVERBDELAY:
    case EAXREVERB_REVERBPAN:
    case EAXREVERB_ECHOTIME:
    case EAXREVERB_ECHODEPTH:
    case EAXREVERB_MODULATIONTIME:
    case EAXREVERB_MODULATIONDEPTH:
    case EAXREVERB_AIRABSORPTIONHF:
    case EAXREVERB_HFREFERENCE:
    case EAXREVERB_LFREFERENCE:
    case EAXREVERB_ROOMROLLOFFFACTOR:
    case EAXREVERB_FLAGS:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX3_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}

DWORD EAX3Buffer_Query(DWORD propid)
{
    switch((propid&~EAX30BUFFER_DEFERRED))
    {
    case EAXSOURCE_NONE:
    case EAXSOURCE_ALLPARAMETERS:
    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
    case EAXSOURCE_OCCLUSIONPARAMETERS:
    case EAXSOURCE_EXCLUSIONPARAMETERS:
    case EAXSOURCE_DIRECT:
    case EAXSOURCE_DIRECTHF:
    case EAXSOURCE_ROOM:
    case EAXSOURCE_ROOMHF:
    case EAXSOURCE_OBSTRUCTION:
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
    case EAXSOURCE_OCCLUSION:
    case EAXSOURCE_OCCLUSIONLFRATIO:
    case EAXSOURCE_OCCLUSIONROOMRATIO:
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
    case EAXSOURCE_EXCLUSION:
    case EAXSOURCE_EXCLUSIONLFRATIO:
    case EAXSOURCE_OUTSIDEVOLUMEHF:
    case EAXSOURCE_DOPPLERFACTOR:
    case EAXSOURCE_ROLLOFFFACTOR:
    case EAXSOURCE_ROOMROLLOFFFACTOR:
    case EAXSOURCE_AIRABSORPTIONFACTOR:
    case EAXSOURCE_FLAGS:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX3Buffer_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}


/*******************
 * EAX 2 stuff
 ******************/

DWORD EAX2_Query(DWORD propid)
{
    switch((propid&~DSPROPERTY_EAX20LISTENER_DEFERRED))
    {
    case DSPROPERTY_EAX20LISTENER_NONE:
    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
    case DSPROPERTY_EAX20LISTENER_ROOM:
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
    case DSPROPERTY_EAX20LISTENER_REVERB:
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
    case DSPROPERTY_EAX20LISTENER_FLAGS:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX2_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}

DWORD EAX2Buffer_Query(DWORD propid)
{
    switch((propid&~DSPROPERTY_EAX20BUFFER_DEFERRED))
    {
    case DSPROPERTY_EAX20BUFFER_NONE:
    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
    case DSPROPERTY_EAX20BUFFER_DIRECT:
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
    case DSPROPERTY_EAX20BUFFER_ROOM:
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
    case DSPROPERTY_EAX20BUFFER_FLAGS:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX2Buffer_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}


/*******************
 * EAX 1 stuff
 ******************/

DWORD EAX1_Query(DWORD propid)
{
    switch(propid)
    {
    case DSPROPERTY_EAX10REVERB_ALL:
    case DSPROPERTY_EAX10REVERB_ENVIRONMENT:
    case DSPROPERTY_EAX10REVERB_VOLUME:
    case DSPROPERTY_EAX10REVERB_DECAYTIME:
    case DSPROPERTY_EAX10REVERB_DAMPING:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX1_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}

DWORD EAX1Buffer_Query(DWORD propid)
{
    switch(propid)
    {
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        return KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
    }
    FIXME("EAX1Buffer_Query Unhandled propid: 0x%08lx\n", propid);
    return 0;
}