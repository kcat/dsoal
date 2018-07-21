/* DirectSound EAX interface
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define CONST_VTABLE
#include <stdarg.h>
#include <string.h>

#include "windows.h"
#include "dsound.h"

#include "dsound_private.h"
#include "eax-presets.h"


/*******************
 * EAX 3 stuff
 ******************/

static EAXOBSTRUCTIONPROPERTIES EAXSourceObstruction(const EAXSOURCEPROPERTIES *props)
{
    EAXOBSTRUCTIONPROPERTIES ret;
    ret.lObstruction = props->lObstruction;
    ret.flObstructionLFRatio = props->flObstructionLFRatio;
    return ret;
}

static EAXOCCLUSIONPROPERTIES EAXSourceOcclusion(const EAXSOURCEPROPERTIES *props)
{
    EAXOCCLUSIONPROPERTIES ret;
    ret.lOcclusion = props->lOcclusion;
    ret.flOcclusionLFRatio = props->flOcclusionLFRatio;
    ret.flOcclusionRoomRatio = props->flOcclusionRoomRatio;
    ret.flOcclusionDirectRatio = props->flOcclusionDirectRatio;
    return ret;
}

static EAXEXCLUSIONPROPERTIES EAXSourceExclusion(const EAXSOURCEPROPERTIES *props)
{
    EAXEXCLUSIONPROPERTIES ret;
    ret.lExclusion = props->lExclusion;
    ret.flExclusionLFRatio = props->flExclusionLFRatio;
    return ret;
}

HRESULT EAX3_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    switch((propid&~DSPROPERTY_EAX30LISTENER_DEFERRED))
    {
    case DSPROPERTY_EAX30LISTENER_NONE:
    case DSPROPERTY_EAX30LISTENER_ALLPARAMETERS:
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENT:
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTSIZE:
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTDIFFUSION:
    case DSPROPERTY_EAX30LISTENER_ROOM:
    case DSPROPERTY_EAX30LISTENER_ROOMHF:
    case DSPROPERTY_EAX30LISTENER_ROOMLF:
    case DSPROPERTY_EAX30LISTENER_DECAYTIME:
    case DSPROPERTY_EAX30LISTENER_DECAYHFRATIO:
    case DSPROPERTY_EAX30LISTENER_DECAYLFRATIO:
    case DSPROPERTY_EAX30LISTENER_REFLECTIONS:
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSDELAY:
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSPAN:
    case DSPROPERTY_EAX30LISTENER_REVERB:
    case DSPROPERTY_EAX30LISTENER_REVERBDELAY:
    case DSPROPERTY_EAX30LISTENER_REVERBPAN:
    case DSPROPERTY_EAX30LISTENER_ECHOTIME:
    case DSPROPERTY_EAX30LISTENER_ECHODEPTH:
    case DSPROPERTY_EAX30LISTENER_MODULATIONTIME:
    case DSPROPERTY_EAX30LISTENER_MODULATIONDEPTH:
    case DSPROPERTY_EAX30LISTENER_AIRABSORPTIONHF:
    case DSPROPERTY_EAX30LISTENER_HFREFERENCE:
    case DSPROPERTY_EAX30LISTENER_LFREFERENCE:
    case DSPROPERTY_EAX30LISTENER_ROOMROLLOFFFACTOR:
    case DSPROPERTY_EAX30LISTENER_FLAGS:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX3_Set(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    /* Should this be using slot 0 or the primary slot? */
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to set reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX30LISTENER_NONE: /* not setting any property, just applying */
        return DS_OK;

    case DSPROPERTY_EAX30LISTENER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX30LISTENERPROPERTIES))
        {
            /* Ensure compatible types (should generate a warning if not). */
            union { void *v; EAX30LISTENERPROPERTIES *props; } data = { pPropData };
            EAXREVERBPROPERTIES *revprops = data.props;
            return EAXReverb_Set(prim, 0, EAXREVERB_ALLPARAMETERS, revprops, cbPropData);
        }
        return DSERR_INVALIDPARAM;

#define HANDLE_PROP(P) case DSPROPERTY_EAX30LISTENER_##P: \
    return EAXReverb_Set(prim, 0, EAXREVERB_##P, pPropData, cbPropData);
    HANDLE_PROP(ENVIRONMENT)
    HANDLE_PROP(ENVIRONMENTSIZE)
    HANDLE_PROP(ENVIRONMENTDIFFUSION)
    HANDLE_PROP(ROOM)
    HANDLE_PROP(ROOMHF)
    HANDLE_PROP(ROOMLF)
    HANDLE_PROP(DECAYTIME)
    HANDLE_PROP(DECAYHFRATIO)
    HANDLE_PROP(DECAYLFRATIO)
    HANDLE_PROP(REFLECTIONS)
    HANDLE_PROP(REFLECTIONSDELAY)
    HANDLE_PROP(REFLECTIONSPAN)
    HANDLE_PROP(REVERB)
    HANDLE_PROP(REVERBDELAY)
    HANDLE_PROP(REVERBPAN)
    HANDLE_PROP(ECHOTIME)
    HANDLE_PROP(ECHODEPTH)
    HANDLE_PROP(MODULATIONTIME)
    HANDLE_PROP(MODULATIONDEPTH)
    HANDLE_PROP(AIRABSORPTIONHF)
    HANDLE_PROP(HFREFERENCE)
    HANDLE_PROP(LFREFERENCE)
    HANDLE_PROP(ROOMROLLOFFFACTOR)
    HANDLE_PROP(FLAGS)
#undef HANDLE_PROP
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return DSERR_INVALIDPARAM;
}

#define GET_PROP(src, T) do {                              \
    if(cbPropData >= sizeof(T))                            \
    {                                                      \
        union { void *v; T *props; } data = { pPropData }; \
        *data.props = src;                                 \
        *pcbReturned = sizeof(T);                          \
        return DS_OK;                                      \
    }                                                      \
    return DSERR_INVALIDPARAM;                             \
} while(0)

HRESULT EAX3_Get(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to get reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX30LISTENER_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case DSPROPERTY_EAX30LISTENER_ALLPARAMETERS:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb, EAX30LISTENERPROPERTIES);
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENT:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.dwEnvironment, DWORD);
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTSIZE:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEnvironmentSize, float);
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEnvironmentDiffusion, float);
    case DSPROPERTY_EAX30LISTENER_ROOM:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lRoom, long);
    case DSPROPERTY_EAX30LISTENER_ROOMHF:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lRoomHF, long);
    case DSPROPERTY_EAX30LISTENER_ROOMLF:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lRoomLF, long);
    case DSPROPERTY_EAX30LISTENER_DECAYTIME:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flDecayTime, float);
    case DSPROPERTY_EAX30LISTENER_DECAYHFRATIO:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flDecayHFRatio, float);
    case DSPROPERTY_EAX30LISTENER_DECAYLFRATIO:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flDecayLFRatio, float);
    case DSPROPERTY_EAX30LISTENER_REFLECTIONS:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lReflections, long);
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSDELAY:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flReflectionsDelay, float);
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSPAN:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.vReflectionsPan, EAXVECTOR);
    case DSPROPERTY_EAX30LISTENER_REVERB:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lReverb, long);
    case DSPROPERTY_EAX30LISTENER_REVERBDELAY:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flReverbDelay, float);
    case DSPROPERTY_EAX30LISTENER_REVERBPAN:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.vReverbPan, EAXVECTOR);
    case DSPROPERTY_EAX30LISTENER_ECHOTIME:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEchoTime, float);
    case DSPROPERTY_EAX30LISTENER_ECHODEPTH:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEchoDepth, float);
    case DSPROPERTY_EAX30LISTENER_MODULATIONTIME:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flModulationTime, float);
    case DSPROPERTY_EAX30LISTENER_MODULATIONDEPTH:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flModulationDepth, float);
    case DSPROPERTY_EAX30LISTENER_AIRABSORPTIONHF:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flAirAbsorptionHF, float);
    case DSPROPERTY_EAX30LISTENER_HFREFERENCE:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flHFReference, float);
    case DSPROPERTY_EAX30LISTENER_LFREFERENCE:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flLFReference, float);
    case DSPROPERTY_EAX30LISTENER_ROOMROLLOFFFACTOR:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flRoomRolloffFactor, float);
    case DSPROPERTY_EAX30LISTENER_FLAGS:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.dwFlags, DWORD);
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return DSERR_INVALIDPARAM;
}


HRESULT EAX3Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport)
{
    (void)buf;

    switch((propid&~DSPROPERTY_EAX30BUFFER_DEFERRED))
    {
    case DSPROPERTY_EAX30BUFFER_NONE:
    case DSPROPERTY_EAX30BUFFER_ALLPARAMETERS:
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONPARAMETERS:
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONPARAMETERS:
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONPARAMETERS:
    case DSPROPERTY_EAX30BUFFER_DIRECT:
    case DSPROPERTY_EAX30BUFFER_DIRECTHF:
    case DSPROPERTY_EAX30BUFFER_ROOM:
    case DSPROPERTY_EAX30BUFFER_ROOMHF:
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTION:
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONLFRATIO:
    case DSPROPERTY_EAX30BUFFER_OCCLUSION:
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONLFRATIO:
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONROOMRATIO:
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONDIRECTRATIO:
    case DSPROPERTY_EAX30BUFFER_EXCLUSION:
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONLFRATIO:
    case DSPROPERTY_EAX30BUFFER_OUTSIDEVOLUMEHF:
    case DSPROPERTY_EAX30BUFFER_DOPPLERFACTOR:
    case DSPROPERTY_EAX30BUFFER_ROLLOFFFACTOR:
    case DSPROPERTY_EAX30BUFFER_ROOMROLLOFFFACTOR:
    case DSPROPERTY_EAX30BUFFER_AIRABSORPTIONFACTOR:
    case DSPROPERTY_EAX30BUFFER_FLAGS:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}


HRESULT EAX3Buffer_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    switch(propid)
    {
    case DSPROPERTY_EAX30BUFFER_NONE: /* not setting any property, just applying */
        return DS_OK;

    case DSPROPERTY_EAX30BUFFER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX30BUFFERPROPERTIES))
        {
            /* Ensure compatible types (should generate a warning if not). */
            union { void *v; EAX30BUFFERPROPERTIES *props; } data = { pPropData };
            EAXSOURCEPROPERTIES *srcprops = data.props;
            return EAX4Source_Set(buf, EAXSOURCE_ALLPARAMETERS, srcprops, cbPropData);
        }
        return DSERR_INVALIDPARAM;

#define HANDLE_PROP(P) case DSPROPERTY_EAX30BUFFER_##P: \
    return EAX4Source_Set(buf, EAXSOURCE_##P, pPropData, cbPropData);
    HANDLE_PROP(OBSTRUCTIONPARAMETERS)
    HANDLE_PROP(OCCLUSIONPARAMETERS)
    HANDLE_PROP(EXCLUSIONPARAMETERS)
    HANDLE_PROP(DIRECT)
    HANDLE_PROP(DIRECTHF)
    HANDLE_PROP(ROOM)
    HANDLE_PROP(ROOMHF)
    HANDLE_PROP(OBSTRUCTION)
    HANDLE_PROP(OBSTRUCTIONLFRATIO)
    HANDLE_PROP(OCCLUSION)
    HANDLE_PROP(OCCLUSIONLFRATIO)
    HANDLE_PROP(OCCLUSIONROOMRATIO)
    HANDLE_PROP(OCCLUSIONDIRECTRATIO)
    HANDLE_PROP(EXCLUSION)
    HANDLE_PROP(EXCLUSIONLFRATIO)
    HANDLE_PROP(OUTSIDEVOLUMEHF)
    HANDLE_PROP(DOPPLERFACTOR)
    HANDLE_PROP(ROLLOFFFACTOR)
    HANDLE_PROP(ROOMROLLOFFFACTOR)
    HANDLE_PROP(AIRABSORPTIONFACTOR)
    HANDLE_PROP(FLAGS)
#undef HANDLE_PROP
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX3Buffer_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    switch(propid)
    {
    case DSPROPERTY_EAX30BUFFER_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case DSPROPERTY_EAX30BUFFER_ALLPARAMETERS:
        GET_PROP(buf->current.eax, EAX30BUFFERPROPERTIES);
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONPARAMETERS:
        GET_PROP(EAXSourceObstruction(&buf->current.eax), EAXOBSTRUCTIONPROPERTIES);
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONPARAMETERS:
        GET_PROP(EAXSourceOcclusion(&buf->current.eax), EAXOCCLUSIONPROPERTIES);
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONPARAMETERS:
        GET_PROP(EAXSourceExclusion(&buf->current.eax), EAXEXCLUSIONPROPERTIES);
    case DSPROPERTY_EAX30BUFFER_DIRECT:
        GET_PROP(buf->current.eax.lDirect, long);
    case DSPROPERTY_EAX30BUFFER_DIRECTHF:
        GET_PROP(buf->current.eax.lDirectHF, long);
    case DSPROPERTY_EAX30BUFFER_ROOM:
        GET_PROP(buf->current.eax.lRoom, long);
    case DSPROPERTY_EAX30BUFFER_ROOMHF:
        GET_PROP(buf->current.eax.lRoomHF, long);
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTION:
        GET_PROP(buf->current.eax.lObstruction, long);
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->current.eax.flObstructionLFRatio, float);
    case DSPROPERTY_EAX30BUFFER_OCCLUSION:
        GET_PROP(buf->current.eax.lOcclusion, long);
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONLFRATIO:
        GET_PROP(buf->current.eax.flOcclusionLFRatio, float);
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONROOMRATIO:
        GET_PROP(buf->current.eax.flOcclusionRoomRatio, float);
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONDIRECTRATIO:
        GET_PROP(buf->current.eax.flOcclusionDirectRatio, float);
    case DSPROPERTY_EAX30BUFFER_EXCLUSION:
        GET_PROP(buf->current.eax.lExclusion, long);
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONLFRATIO:
        GET_PROP(buf->current.eax.flExclusionLFRatio, float);
    case DSPROPERTY_EAX30BUFFER_OUTSIDEVOLUMEHF:
        GET_PROP(buf->current.eax.lOutsideVolumeHF, long);
    case DSPROPERTY_EAX30BUFFER_DOPPLERFACTOR:
        GET_PROP(buf->current.eax.flDopplerFactor, float);
    case DSPROPERTY_EAX30BUFFER_ROLLOFFFACTOR:
        GET_PROP(buf->current.eax.flRolloffFactor, float);
    case DSPROPERTY_EAX30BUFFER_ROOMROLLOFFFACTOR:
        GET_PROP(buf->current.eax.flRoomRolloffFactor, float);
    case DSPROPERTY_EAX30BUFFER_AIRABSORPTIONFACTOR:
        GET_PROP(buf->current.eax.flAirAbsorptionFactor, float);
    case DSPROPERTY_EAX30BUFFER_FLAGS:
        GET_PROP(buf->current.eax.dwFlags, DWORD);
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return DSERR_INVALIDPARAM;
}


/*******************
 * EAX 2 stuff
 ******************/

#define EAX2LISTENERFLAGS_MASK (EAX20LISTENERFLAGS_DECAYTIMESCALE        | \
                                EAX20LISTENERFLAGS_REFLECTIONSSCALE      | \
                                EAX20LISTENERFLAGS_REFLECTIONSDELAYSCALE | \
                                EAX20LISTENERFLAGS_REVERBSCALE           | \
                                EAX20LISTENERFLAGS_REVERBDELAYSCALE      | \
                                EAX20LISTENERFLAGS_DECAYHFLIMIT)

static EAX20LISTENERPROPERTIES EAXRevTo2(const EAXREVERBPROPERTIES *props)
{
    EAX20LISTENERPROPERTIES ret;
    ret.lRoom = props->lRoom;
    ret.lRoomHF = props->lRoomHF;
    ret.flRoomRolloffFactor = props->flRoomRolloffFactor;
    ret.flDecayTime = props->flDecayTime;
    ret.flDecayHFRatio = props->flDecayHFRatio;
    ret.lReflections = props->lReflections;
    ret.flReflectionsDelay = props->flReflectionsDelay;
    ret.lReverb = props->lReverb;
    ret.flReverbDelay = props->flReverbDelay;
    ret.dwEnvironment = props->dwEnvironment;
    ret.flEnvironmentSize = props->flEnvironmentSize;
    ret.flEnvironmentDiffusion = props->flEnvironmentDiffusion;
    ret.flAirAbsorptionHF = props->flAirAbsorptionHF;
    ret.dwFlags = props->dwFlags & EAX2LISTENERFLAGS_MASK;
    return ret;
}

static EAX20BUFFERPROPERTIES EAXSourceTo2(const EAXSOURCEPROPERTIES *props)
{
    EAX20BUFFERPROPERTIES ret;
    ret.lDirect = props->lDirect;
    ret.lDirectHF = props->lDirectHF;
    ret.lRoom = props->lRoom;
    ret.lRoomHF = props->lRoomHF;
    ret.flRoomRolloffFactor = props->flRoomRolloffFactor;
    ret.lObstruction = props->lObstruction;
    ret.flObstructionLFRatio = props->flObstructionLFRatio;
    ret.lOcclusion = props->lOcclusion;
    ret.flOcclusionLFRatio = props->flOcclusionLFRatio;
    ret.flOcclusionRoomRatio = props->flOcclusionRoomRatio;
    ret.lOutsideVolumeHF = props->lOutsideVolumeHF;
    ret.flAirAbsorptionFactor = props->flAirAbsorptionFactor;
    ret.dwFlags = props->dwFlags;
    return ret;
}


HRESULT EAX2_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

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
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX2_Set(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to set reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX20LISTENER_NONE: /* not setting any property, just applying */
        return DS_OK;

    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX20LISTENERPROPERTIES))
        {
            union {
                const void *v;
                const EAX20LISTENERPROPERTIES *props;
            } data = { pPropData };
            EAXREVERBPROPERTIES props = REVERB_PRESET_GENERIC;
            TRACE("Parameters:\n\tEnvironment: %lu\n\tEnvSize: %f\n\tEnvDiffusion: %f\n\t"
                "Room: %ld\n\tRoom HF: %ld\n\tDecay Time: %f\n\tDecay HF Ratio: %f\n\t"
                "Reflections: %ld\n\tReflections Delay: %f\n\tReverb: %ld\n\tReverb Delay: %f\n\t"
                "Air Absorption: %f\n\tRoom Rolloff: %f\n\tFlags: 0x%02lx\n",
                data.props->dwEnvironment, data.props->flEnvironmentSize,
                data.props->flEnvironmentDiffusion, data.props->lRoom, data.props->lRoomHF,
                data.props->flDecayTime, data.props->flDecayHFRatio, data.props->lReflections,
                data.props->flReflectionsDelay, data.props->lReverb, data.props->flReverbDelay,
                data.props->flAirAbsorptionHF, data.props->flRoomRolloffFactor, data.props->dwFlags
            );

            if(data.props->dwEnvironment < EAX_ENVIRONMENT_UNDEFINED)
            {
                props = EnvironmentDefaults[data.props->dwEnvironment];
                props.dwEnvironment = data.props->dwEnvironment;
            }
            props.flEnvironmentSize = data.props->flEnvironmentSize;
            props.flEnvironmentDiffusion = data.props->flEnvironmentDiffusion;
            props.lRoom = data.props->lRoom;
            props.lRoomHF = data.props->lRoomHF;
            props.flDecayTime = data.props->flDecayTime;
            props.flDecayHFRatio = data.props->flDecayHFRatio;
            props.lReflections = data.props->lReflections;
            props.flReflectionsDelay = data.props->flReflectionsDelay;
            props.lReverb = data.props->lReverb;
            props.flReverbDelay = data.props->flReverbDelay;
            props.flAirAbsorptionHF = data.props->flAirAbsorptionHF;
            props.flRoomRolloffFactor = data.props->flRoomRolloffFactor;
            props.dwFlags = data.props->dwFlags;
            return EAXReverb_Set(prim, 0, EAXREVERB_ALLPARAMETERS, &props, sizeof(props));
        }
        return DSERR_INVALIDPARAM;

#define HANDLE_PROP(P) case DSPROPERTY_EAX20LISTENER_##P: \
    return EAXReverb_Set(prim, 0, EAXREVERB_##P, pPropData, cbPropData);
    HANDLE_PROP(ROOM)
    HANDLE_PROP(ROOMHF)
    HANDLE_PROP(DECAYTIME)
    HANDLE_PROP(DECAYHFRATIO)
    HANDLE_PROP(REFLECTIONS)
    HANDLE_PROP(REFLECTIONSDELAY)
    HANDLE_PROP(REVERB)
    HANDLE_PROP(REVERBDELAY)
    HANDLE_PROP(ENVIRONMENT)
    HANDLE_PROP(ENVIRONMENTSIZE)
    HANDLE_PROP(ENVIRONMENTDIFFUSION)
    HANDLE_PROP(AIRABSORPTIONHF)
    HANDLE_PROP(ROOMROLLOFFFACTOR)
    HANDLE_PROP(FLAGS)
#undef HANDLE_PROP
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX2_Get(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to get reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX20LISTENER_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
        GET_PROP(EAXRevTo2(&prim->deferred.fxslot[0].fx.reverb), EAX20LISTENERPROPERTIES);
    case DSPROPERTY_EAX20LISTENER_ROOM:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lRoom, long);
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lRoomHF, long);
    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flRoomRolloffFactor, float);
    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flDecayTime, float);
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flDecayHFRatio, float);
    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lReflections, long);
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flReflectionsDelay, float);
    case DSPROPERTY_EAX20LISTENER_REVERB:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.lReverb, long);
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flReverbDelay, float);
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.dwEnvironment, DWORD);
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEnvironmentSize, float);
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flEnvironmentDiffusion, float);
    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.flAirAbsorptionHF, float);
    case DSPROPERTY_EAX20LISTENER_FLAGS:
        GET_PROP(prim->deferred.fxslot[0].fx.reverb.dwFlags&EAX2LISTENERFLAGS_MASK, DWORD);
    }
    FIXME("Unhandled listener propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}


HRESULT EAX2Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport)
{
    (void)buf;

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
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX2Buffer_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    switch(propid)
    {
    case DSPROPERTY_EAX20BUFFER_NONE: /* not setting any property, just applying */
        return DS_OK;

    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX20BUFFERPROPERTIES))
        {
            union {
                const void *v;
                const EAX20BUFFERPROPERTIES *props;
            } data = { pPropData };
            EAXSOURCEPROPERTIES props;
            TRACE("Parameters:\n\tDirect: %ld\n\tDirect HF: %ld\n\tRoom: %ld\n\tRoom HF: %ld\n\t"
                "Room Rolloff Factor: %f\n\tObstruction: %ld\n\tObstruction LF Ratio: %f\n\t"
                "Occlusion: %ld\n\tOcclusion LF Ratio: %f\n\tOcclusion Room Ratio: %f\n\t"
                "Outside Volume HF: %ld\n\tAir Absorb Factor: %f\n\tFlags: 0x%02lx\n",
                data.props->lDirect, data.props->lDirectHF, data.props->lRoom, data.props->lRoomHF,
                data.props->flRoomRolloffFactor, data.props->lObstruction,
                data.props->flObstructionLFRatio, data.props->lOcclusion,
                data.props->flOcclusionLFRatio, data.props->flOcclusionRoomRatio,
                data.props->lOutsideVolumeHF, data.props->flAirAbsorptionFactor,
                data.props->dwFlags
            );

            props = buf->deferred.eax;
            props.lDirect = data.props->lDirect;
            props.lDirectHF = data.props->lDirectHF;
            props.lRoom = data.props->lRoom;
            props.lRoomHF = data.props->lRoomHF;
            props.flRoomRolloffFactor = data.props->flRoomRolloffFactor;
            props.lObstruction = data.props->lObstruction;
            props.flObstructionLFRatio = data.props->flObstructionLFRatio;
            props.lOcclusion = data.props->lOcclusion;
            props.flOcclusionLFRatio = data.props->flOcclusionLFRatio;
            props.flOcclusionRoomRatio = data.props->flOcclusionRoomRatio;
            props.lOutsideVolumeHF = data.props->lOutsideVolumeHF;
            props.flAirAbsorptionFactor = data.props->flAirAbsorptionFactor;
            props.dwFlags = data.props->dwFlags;
            return EAX4Source_Set(buf, EAXSOURCE_ALLPARAMETERS, &props, sizeof(props));
        }
        return DSERR_INVALIDPARAM;

#define HANDLE_PROP(P) case DSPROPERTY_EAX20BUFFER_##P: \
    return EAX4Source_Set(buf, EAXSOURCE_##P, pPropData, cbPropData);
    HANDLE_PROP(DIRECT)
    HANDLE_PROP(DIRECTHF)
    HANDLE_PROP(ROOM)
    HANDLE_PROP(ROOMHF)
    HANDLE_PROP(OBSTRUCTION)
    HANDLE_PROP(OBSTRUCTIONLFRATIO)
    HANDLE_PROP(OCCLUSION)
    HANDLE_PROP(OCCLUSIONLFRATIO)
    HANDLE_PROP(OCCLUSIONROOMRATIO)
    HANDLE_PROP(OUTSIDEVOLUMEHF)
    HANDLE_PROP(ROOMROLLOFFFACTOR)
    HANDLE_PROP(AIRABSORPTIONFACTOR)
    HANDLE_PROP(FLAGS)
#undef HANDLE_PROP
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX2Buffer_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    switch(propid)
    {
    case DSPROPERTY_EAX20BUFFER_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
        GET_PROP(EAXSourceTo2(&buf->current.eax), EAX20BUFFERPROPERTIES);
    case DSPROPERTY_EAX20BUFFER_DIRECT:
        GET_PROP(buf->current.eax.lDirect, long);
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
        GET_PROP(buf->current.eax.lDirectHF, long);
    case DSPROPERTY_EAX20BUFFER_ROOM:
        GET_PROP(buf->current.eax.lRoom, long);
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
        GET_PROP(buf->current.eax.lRoomHF, long);
    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
        GET_PROP(buf->current.eax.flRoomRolloffFactor, float);
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
        GET_PROP(buf->current.eax.lObstruction, long);
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->current.eax.flObstructionLFRatio, float);
    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
        GET_PROP(buf->current.eax.lOcclusion, long);
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
        GET_PROP(buf->current.eax.flOcclusionLFRatio, float);
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
        GET_PROP(buf->current.eax.flOcclusionRoomRatio, float);
    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
        GET_PROP(buf->current.eax.lOutsideVolumeHF, long);
    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
        GET_PROP(buf->current.eax.flAirAbsorptionFactor, float);
    case DSPROPERTY_EAX20BUFFER_FLAGS:
        GET_PROP(buf->current.eax.dwFlags, DWORD);
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}


/*******************
 * EAX 1 stuff
 ******************/

HRESULT EAX1_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    switch(propid)
    {
    case DSPROPERTY_EAX10LISTENER_ALL:
    case DSPROPERTY_EAX10LISTENER_ENVIRONMENT:
    case DSPROPERTY_EAX10LISTENER_VOLUME:
    case DSPROPERTY_EAX10LISTENER_DECAYTIME:
    case DSPROPERTY_EAX10LISTENER_DAMPING:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX1_Set(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    static const float eax1_env_volume[EAX_ENVIRONMENT_UNDEFINED] = {
        0.5f, 0.25f, 0.417f, 0.653f, 0.208f, 0.5f, 0.403f, 0.5f, 0.5f,
        0.361f, 0.5f, 0.153f, 0.361f, 0.444f, 0.25f, 0.111f, 0.111f,
        0.194f, 1.0f, 0.097f, 0.208f, 0.652f, 1.0f, 0.875f, 0.139f, 0.486f
    };
    static const float eax1_env_dampening[EAX_ENVIRONMENT_UNDEFINED] = {
        0.5f, 0.0f, 0.666f, 0.166f, 0.0f, 0.888f, 0.5f, 0.5f, 1.304f,
        0.332f, 0.3f, 2.0f, 0.0f, 0.638f, 0.776f, 0.472f, 0.224f, 0.472f,
        0.5f, 0.224f, 1.5f, 0.25f, 0.0f, 1.388f, 0.666f, 0.806f
    };
    HRESULT hr;

    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to set reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX10LISTENER_ALL:
        if(cbPropData >= sizeof(EAX10LISTENERPROPERTIES))
        {
            union {
                const void *v;
                const EAX10LISTENERPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tEnvironment: %lu\n\tVolume: %f\n\tDecay Time: %f\n\t"
                "Damping: %f\n", data.props->dwEnvironment, data.props->fVolume,
                data.props->fDecayTime, data.props->fDamping
            );

            if(data.props->dwEnvironment < EAX_ENVIRONMENT_UNDEFINED)
            {
                /* NOTE: I'm not quite sure how to handle the volume. It's
                 * important to deal with since it can have a notable impact on
                 * the output levels, but given the default EAX1 environment
                 * volumes, they don't align with the gain/room volume for
                 * EAX2+ environments. Presuming the default volumes are
                 * correct, it's possible the reverb implementation was
                 * different and relied on different gains to get the intended
                 * output levels.
                 *
                 * Rather than just blindly applying the volume, we take the
                 * difference from the EAX1 environment's default volume and
                 * apply that as an offset to the EAX2 environment's volume.
                 */
                EAXREVERBPROPERTIES env = EnvironmentDefaults[data.props->dwEnvironment];
                long db_vol = clampI(
                    gain_to_mB(data.props->fVolume / eax1_env_volume[data.props->dwEnvironment]),
                    -10000, 10000
                );
                env.lRoom = clampI(env.lRoom + db_vol, -10000, 0);
                env.flDecayTime = data.props->fDecayTime;

                hr = EAXReverb_Set(prim, 0, EAXREVERB_ALLPARAMETERS, &env, sizeof(env));
                if(SUCCEEDED(hr))
                {
                    prim->deferred.eax1_volume = data.props->fVolume;
                    prim->deferred.eax1_dampening = data.props->fDamping;
                }
                return hr;
            }
        }
        return DSERR_INVALIDPARAM;

    case DSPROPERTY_EAX10LISTENER_ENVIRONMENT:
        hr = EAXReverb_Set(prim, 0, EAXREVERB_ENVIRONMENT, pPropData, cbPropData);
        if(SUCCEEDED(hr))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            prim->deferred.eax1_volume = eax1_env_volume[*data.dw];
            prim->deferred.eax1_dampening = eax1_env_dampening[*data.dw];
        }
        return hr;

    case DSPROPERTY_EAX10LISTENER_VOLUME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            long db_vol = clampI(
                gain_to_mB(*data.fl / eax1_env_volume[prim->deferred.fxslot[0].fx.reverb.dwEnvironment]),
                -10000, 10000
            );
            long room_vol = clampI(
                EnvironmentDefaults[prim->deferred.fxslot[0].fx.reverb.dwEnvironment].lRoom + db_vol,
                -10000, 0
            );
            TRACE("Volume: %f\n", *data.fl);

            hr = EAXReverb_Set(prim, 0, EAXREVERB_ROOM, &room_vol, sizeof(room_vol));
            if(SUCCEEDED(hr)) prim->deferred.eax1_volume = *data.fl;
            return hr;
        }
        return DSERR_INVALIDPARAM;
    case DSPROPERTY_EAX10LISTENER_DECAYTIME:
        return EAXReverb_Set(prim, 0, EAXREVERB_DECAYTIME, pPropData, cbPropData);
    case DSPROPERTY_EAX10LISTENER_DAMPING:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Damping: %f\n", *data.fl);

            prim->deferred.eax1_dampening = *data.fl;
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX1_Get(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    if(prim->effect[0] == 0)
        return E_PROP_ID_UNSUPPORTED;
    if(prim->deferred.fxslot[0].effect_type != FXSLOT_EFFECT_REVERB)
    {
        ERR("Trying to get reverb parameters on non-reverb slot\n");
        return DSERR_INVALIDCALL;
    }

    switch(propid)
    {
    case DSPROPERTY_EAX10LISTENER_ALL:
        if(cbPropData >= sizeof(EAX10LISTENERPROPERTIES))
        {
            union {
                void *v;
                EAX10LISTENERPROPERTIES *props;
            } data = { pPropData };

            data.props->dwEnvironment = prim->deferred.fxslot[0].fx.reverb.dwEnvironment;
            data.props->fVolume = prim->deferred.eax1_volume;
            data.props->fDecayTime = prim->deferred.fxslot[0].fx.reverb.flDecayTime;
            data.props->fDamping = prim->deferred.eax1_dampening;

            *pcbReturned = sizeof(EAX10LISTENERPROPERTIES);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case DSPROPERTY_EAX10LISTENER_ENVIRONMENT:
        return EAXReverb_Get(prim, 0, EAXREVERB_ENVIRONMENT, pPropData, cbPropData, pcbReturned);

    case DSPROPERTY_EAX10LISTENER_VOLUME:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };
            *data.fl = prim->deferred.eax1_volume;
            *pcbReturned = sizeof(float);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case DSPROPERTY_EAX10LISTENER_DECAYTIME:
        return EAXReverb_Get(prim, 0, EAXREVERB_DECAYTIME, pPropData, cbPropData, pcbReturned);

    case DSPROPERTY_EAX10LISTENER_DAMPING:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };
            *data.fl = prim->deferred.eax1_dampening;
            *pcbReturned = sizeof(float);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}


HRESULT EAX1Buffer_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport)
{
    (void)buf;

    switch(propid)
    {
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX1Buffer_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    switch(propid)
    {
    /* NOTE: DSPROPERTY_EAX10BUFFER_ALL is for EAX10BUFFERPROPERTIES, however
     * that struct just contains the single ReverbMix float property.
     */
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            LONG mbvol;
            HRESULT hr;

            TRACE("Reverb Mix: %f\n", *data.fl);
            mbvol = gain_to_mB(*data.fl);

            hr = EAX4Source_Set(buf, EAXSOURCE_ROOM, &mbvol, sizeof(mbvol));
            if(SUCCEEDED(hr)) buf->deferred.eax1_reverbmix = *data.fl;
            return hr;
        }
        return DSERR_INVALIDPARAM;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX1Buffer_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    switch(propid)
    {
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };

            *data.fl = buf->current.eax1_reverbmix;
            *pcbReturned = sizeof(float);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;
    }
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}
