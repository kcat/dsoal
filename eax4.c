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


void ApplyReverbParams(ALuint effect, const EAXREVERBPROPERTIES *props)
{
    /* FIXME: Need to validate property values... Ignore? Clamp? Error? */
    alEffectf(effect, AL_EAXREVERB_DENSITY,
        clampF(powf(props->flEnvironmentSize, 3.0f) / 16.0f, 0.0f, 1.0f)
    );
    alEffectf(effect, AL_EAXREVERB_DIFFUSION, props->flEnvironmentDiffusion);

    alEffectf(effect, AL_EAXREVERB_GAIN, mB_to_gain(props->lRoom));
    alEffectf(effect, AL_EAXREVERB_GAINHF, mB_to_gain(props->lRoomHF));
    alEffectf(effect, AL_EAXREVERB_GAINLF, mB_to_gain(props->lRoomLF));

    alEffectf(effect, AL_EAXREVERB_DECAY_TIME, props->flDecayTime);
    alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, props->flDecayHFRatio);
    alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, props->flDecayLFRatio);

    /* NOTE: Imprecision can cause some converted volume levels to land outside
     * EFX's gain limits (e.g. EAX's +1000mB volume limit gets converted to
     * 3.162something, while EFX defines the limit as 3.16; close enough for
     * practical uses, but still technically an error).
     */
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN,
        clampF(mB_to_gain(props->lReflections), AL_EAXREVERB_MIN_REFLECTIONS_GAIN,
               AL_EAXREVERB_MAX_REFLECTIONS_GAIN)
    );
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, props->flReflectionsDelay);
    alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, &props->vReflectionsPan.x);

    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN,
        clampF(mB_to_gain(props->lReverb), AL_EAXREVERB_MIN_LATE_REVERB_GAIN,
               AL_EAXREVERB_MAX_LATE_REVERB_GAIN)
    );
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, props->flReverbDelay);
    alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, &props->vReverbPan.x);

    alEffectf(effect, AL_EAXREVERB_ECHO_TIME, props->flEchoTime);
    alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, props->flEchoDepth);

    alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, props->flModulationTime);
    alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, props->flModulationDepth);

    alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF,
        clampF(mB_to_gain(props->flAirAbsorptionHF), AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF,
               AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF)
    );

    alEffectf(effect, AL_EAXREVERB_HFREFERENCE, props->flHFReference);
    alEffectf(effect, AL_EAXREVERB_LFREFERENCE, props->flLFReference);

    alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, props->flRoomRolloffFactor);

    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT,
              (props->dwFlags&EAX30LISTENERFLAGS_DECAYHFLIMIT) ?
              AL_TRUE : AL_FALSE);

    checkALError();
}

void RescaleEnvSize(EAXREVERBPROPERTIES *props, float newsize)
{
    float scale = newsize / props->flEnvironmentSize;

    props->flEnvironmentSize = newsize;

    if((props->dwFlags&EAX30LISTENERFLAGS_DECAYTIMESCALE))
    {
        props->flDecayTime *= scale;
        props->flDecayTime = clampF(props->flDecayTime, 0.1f, 20.0f);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_REFLECTIONSSCALE))
    {
        props->lReflections -= gain_to_mB(scale);
        props->lReflections = clampI(props->lReflections, -10000, 1000);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_REFLECTIONSDELAYSCALE))
    {
        props->flReflectionsDelay *= scale;
        props->flReflectionsDelay = clampF(props->flReflectionsDelay, 0.0f, 0.3f);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_REVERBSCALE))
    {
        LONG diff = gain_to_mB(scale);
        /* This is scaled by an extra 1/3rd if decay time isn't also scaled, to
         * account for the (lack of) change on the send's initial decay.
         */
        if(!(props->dwFlags&EAX30LISTENERFLAGS_DECAYTIMESCALE))
            diff = diff * 3 / 2;
        props->lReverb -= diff;
        props->lReverb = clampI(props->lReverb, -10000, 2000);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_REVERBDELAYSCALE))
    {
        props->flReverbDelay *= scale;
        props->flReverbDelay = clampF(props->flReverbDelay, 0.0f, 0.1f);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_ECHOTIMESCALE))
    {
        props->flEchoTime *= scale;
        props->flEchoTime = clampF(props->flEchoTime, 0.075f, 0.25f);
    }
    if((props->dwFlags&EAX30LISTENERFLAGS_MODTIMESCALE))
    {
        props->flModulationTime *= scale;
        props->flModulationTime = clampF(props->flModulationTime, 0.04f, 4.0f);
    }
}


HRESULT EAX4Context_Query(DSPrimary *prim, DWORD propid, ULONG *pTypeSupport)
{
    if(!HAS_EXTENSION(prim->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

    switch((propid&~EAXCONTEXT_PARAMETER_DEFER))
    {
    case EAXCONTEXT_NONE:
    case EAXCONTEXT_ALLPARAMETERS:
    case EAXCONTEXT_PRIMARYFXSLOTID:
    case EAXCONTEXT_DISTANCEFACTOR:
    case EAXCONTEXT_AIRABSORPTIONHF:
    case EAXCONTEXT_HFREFERENCE:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;
    case EAXCONTEXT_LASTERROR:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET;
        return DS_OK;

    default:
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX4Context_Set(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(!HAS_EXTENSION(prim->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXCONTEXT_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case EAXCONTEXT_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAXCONTEXTPROPERTIES))
        {
            union { void *v; const EAXCONTEXTPROPERTIES *props; } data = { pPropData };
            ALuint prim_slot = 0;

            if(IsEqualGUID(&data.props->guidPrimaryFXSlotID, &EAXPROPERTYID_EAX40_FXSlot0))
                prim_slot = prim->auxslot[0];
            else if(IsEqualGUID(&data.props->guidPrimaryFXSlotID, &EAXPROPERTYID_EAX40_FXSlot1))
                prim_slot = prim->auxslot[1];
            else if(IsEqualGUID(&data.props->guidPrimaryFXSlotID, &EAXPROPERTYID_EAX40_FXSlot2))
                prim_slot = prim->auxslot[2];
            else if(IsEqualGUID(&data.props->guidPrimaryFXSlotID, &EAXPROPERTYID_EAX40_FXSlot3))
                prim_slot = prim->auxslot[3];
            if(prim_slot == 0 && !IsEqualGUID(&data.props->guidPrimaryFXSlotID, &EAX_NULL_GUID))
            {
                ERR("Unexpected primary FXSlot: %s\n",
                    debugstr_guid(&data.props->guidPrimaryFXSlotID));
                return DSERR_INVALIDPARAM;
            }
            if(!(data.props->flDistanceFactor >= DS3D_MINDISTANCEFACTOR &&
                 data.props->flDistanceFactor <= DS3D_MAXDISTANCEFACTOR))
            {
                ERR("Unexpected distance factor: %f\n", data.props->flDistanceFactor);
                return DSERR_INVALIDPARAM;
            }
            if(!(data.props->flAirAbsorptionHF <= 0.0f && data.props->flAirAbsorptionHF >= -100.0f))
            {
                ERR("Unexpected air absorption: %f\n", data.props->flAirAbsorptionHF);
                return DSERR_INVALIDPARAM;
            }
            if(!(data.props->flHFReference >= 1000.0f && data.props->flHFReference <= 20000.0f))
            {
                ERR("Unexpected HF reference: %f\n", data.props->flAirAbsorptionHF);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.ctx = *data.props;
            prim->primary_slot = prim_slot;

            prim->dirty.bit.prim_slotid = 1;
            prim->dirty.bit.distancefactor2 = 1;
            prim->dirty.bit.air_absorbhf = 1;
            prim->dirty.bit.hfreference = 1;
            return DS_OK;
        }
        break;

    case EAXCONTEXT_PRIMARYFXSLOTID:
        if(cbPropData >= sizeof(GUID))
        {
            union { void *v; const GUID *guid; } data = { pPropData };
            ALuint prim_slot = 0;

            if(IsEqualGUID(data.guid, &EAXPROPERTYID_EAX40_FXSlot0))
                prim_slot = prim->auxslot[0];
            else if(IsEqualGUID(data.guid, &EAXPROPERTYID_EAX40_FXSlot1))
                prim_slot = prim->auxslot[1];
            else if(IsEqualGUID(data.guid, &EAXPROPERTYID_EAX40_FXSlot2))
                prim_slot = prim->auxslot[2];
            else if(IsEqualGUID(data.guid, &EAXPROPERTYID_EAX40_FXSlot3))
                prim_slot = prim->auxslot[3];
            if(prim_slot == 0 && !IsEqualGUID(data.guid, &EAX_NULL_GUID))
            {
                ERR("Unexpected primary FXSlot: %s\n", debugstr_guid(data.guid));
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.ctx.guidPrimaryFXSlotID = *data.guid;
            prim->primary_slot = prim_slot;

            prim->dirty.bit.prim_slotid = 1;
            return DS_OK;
        }
        break;

    case EAXCONTEXT_DISTANCEFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; const float *fl; } data = { pPropData };

            if(!(*data.fl >= DS3D_MINDISTANCEFACTOR && *data.fl <= DS3D_MAXDISTANCEFACTOR))
            {
                ERR("Unexpected distance factor: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.ctx.flDistanceFactor = *data.fl;

            prim->dirty.bit.distancefactor2 = 1;
            return DS_OK;
        }
        break;

    case EAXCONTEXT_AIRABSORPTIONHF:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; const float *fl; } data = { pPropData };

            if(!(*data.fl <= 0.0f && *data.fl >= -100.0f))
            {
                ERR("Unexpected air absorption: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.ctx.flAirAbsorptionHF = *data.fl;

            prim->dirty.bit.air_absorbhf = 1;
            return DS_OK;
        }
        break;

    case EAXCONTEXT_HFREFERENCE:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; const float *fl; } data = { pPropData };

            if(!(*data.fl >= 1000.0f && *data.fl <= 20000.0f))
            {
                ERR("Unexpected HF reference: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.ctx.flHFReference = *data.fl;

            prim->dirty.bit.hfreference = 1;
            return DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX4Context_Get(DSPrimary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(!HAS_EXTENSION(prim->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

#define GET_PROP(src, T) do {                              \
    if(cbPropData >= sizeof(T))                            \
    {                                                      \
        union { void *v; T *props; } data = { pPropData }; \
        *data.props = src;                                 \
        *pcbReturned = sizeof(T);                          \
        hr = DS_OK;                                        \
    }                                                      \
} while(0)
    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXCONTEXT_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case EAXCONTEXT_ALLPARAMETERS:
        GET_PROP(prim->current.ctx, EAXCONTEXTPROPERTIES);
        break;
    case EAXCONTEXT_PRIMARYFXSLOTID:
        GET_PROP(prim->current.ctx.guidPrimaryFXSlotID, GUID);
        break;
    case EAXCONTEXT_DISTANCEFACTOR:
        GET_PROP(prim->current.ctx.flDistanceFactor, float);
        break;
    case EAXCONTEXT_AIRABSORPTIONHF:
        GET_PROP(prim->current.ctx.flAirAbsorptionHF, float);
        break;
    case EAXCONTEXT_HFREFERENCE:
        GET_PROP(prim->current.ctx.flHFReference, float);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}


HRESULT EAX4Slot_Query(DSPrimary *prim, LONG idx, DWORD propid, ULONG *pTypeSupport)
{
    if(prim->auxslot[idx] == 0)
        return E_PROP_ID_UNSUPPORTED;

    if((propid&~EAXFXSLOT_PARAMETER_DEFERRED) < EAXFXSLOT_NONE)
    {
        if(prim->current.fxslot[idx].effect_type == FXSLOT_EFFECT_REVERB)
        {
            if((propid&~EAXFXSLOT_PARAMETER_DEFERRED) <= EAXREVERB_FLAGS)
            {
                *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
                return DS_OK;
            }

            FIXME("Unhandled reverb propid: 0x%08lx\n", propid);
        }
        else if(prim->current.fxslot[idx].effect_type == FXSLOT_EFFECT_CHORUS)
        {
            if((propid&~EAXFXSLOT_PARAMETER_DEFERRED) <= EAXCHORUS_DELAY)
            {
                *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
                return DS_OK;
            }

            FIXME("Unhandled chorus propid: 0x%08lx\n", propid);
        }
        else /*if(prim->current.fxslot[idx].effect_type == FXSLOT_EFFECT_NULL)*/
        {
            FIXME("Unhandled null effect propid: 0x%08lx\n", propid);
        }
    }
    else switch(propid&~EAXFXSLOT_PARAMETER_DEFERRED)
    {
    case EAXFXSLOT_NONE:
    case EAXFXSLOT_ALLPARAMETERS:
    case EAXFXSLOT_LOADEFFECT:
    case EAXFXSLOT_VOLUME:
    case EAXFXSLOT_LOCK:
    case EAXFXSLOT_FLAGS:
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;

    default:
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }
    return E_PROP_ID_UNSUPPORTED;
}

static HRESULT EAXReverb_Set(DSPrimary *prim, LONG idx, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXREVERB_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    /* TODO: Validate slot effect type. */
    case EAXREVERB_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAXREVERBPROPERTIES))
        {
            union {
                const void *v;
                const EAXREVERBPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tEnvironment: %lu\n\tEnvSize: %f\n\tEnvDiffusion: %f\n\t"
                "Room: %ld\n\tRoom HF: %ld\n\tRoom LF: %ld\n\tDecay Time: %f\n\t"
                "Decay HF Ratio: %f\n\tDecay LF Ratio: %f\n\tReflections: %ld\n\t"
                "Reflections Delay: %f\n\tReflections Pan: { %f, %f, %f }\n\tReverb: %ld\n\t"
                "Reverb Delay: %f\n\tReverb Pan: { %f, %f, %f }\n\tEcho Time: %f\n\t"
                "Echo Depth: %f\n\tMod Time: %f\n\tMod Depth: %f\n\tAir Absorption: %f\n\t"
                "HF Reference: %f\n\tLF Reference: %f\n\tRoom Rolloff: %f\n\tFlags: 0x%02lx\n",
                data.props->dwEnvironment, data.props->flEnvironmentSize,
                data.props->flEnvironmentDiffusion, data.props->lRoom, data.props->lRoomHF,
                data.props->lRoomLF, data.props->flDecayTime, data.props->flDecayHFRatio,
                data.props->flDecayLFRatio, data.props->lReflections,
                data.props->flReflectionsDelay, data.props->vReflectionsPan.x,
                data.props->vReflectionsPan.y, data.props->vReflectionsPan.z, data.props->lReverb,
                data.props->flReverbDelay, data.props->vReverbPan.x, data.props->vReverbPan.y,
                data.props->vReverbPan.z, data.props->flEchoTime, data.props->flEchoDepth,
                data.props->flModulationTime, data.props->flModulationDepth,
                data.props->flAirAbsorptionHF, data.props->flHFReference,
                data.props->flLFReference, data.props->flRoomRolloffFactor, data.props->dwFlags
            );

            ApplyReverbParams(prim->effect[idx], data.props);
            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Environment: %lu\n", *data.dw);
            if(*data.dw < EAX_ENVIRONMENT_UNDEFINED)
            {
                prim->deferred.fxslot[idx].fx.reverb = EnvironmentDefaults[*data.dw];
                ApplyReverbParams(prim->effect[idx], &EnvironmentDefaults[*data.dw]);
                FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
                hr = DS_OK;
            }
        }
        break;

    case EAXREVERB_ENVIRONMENTSIZE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Environment Size: %f\n", *data.fl);

            RescaleEnvSize(&prim->deferred.fxslot[idx].fx.reverb, clampF(*data.fl, 1.0f, 100.0f));
            ApplyReverbParams(prim->effect[idx], &prim->deferred.fxslot[idx].fx.reverb);

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_ENVIRONMENTDIFFUSION:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Environment Diffusion: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flEnvironmentDiffusion = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_DIFFUSION,
                      prim->deferred.fxslot[idx].fx.reverb.flEnvironmentDiffusion);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_ROOM:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room: %ld\n", *data.l);

            prim->deferred.fxslot[idx].fx.reverb.lRoom = *data.l;
            alEffectf(prim->effect[idx], AL_EAXREVERB_GAIN,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.lRoom));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room HF: %ld\n", *data.l);

            prim->deferred.fxslot[idx].fx.reverb.lRoomHF = *data.l;
            alEffectf(prim->effect[idx], AL_EAXREVERB_GAINHF,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.lRoomHF));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_ROOMLF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room LF: %ld\n", *data.l);

            prim->deferred.fxslot[idx].fx.reverb.lRoomLF = *data.l;
            alEffectf(prim->effect[idx], AL_EAXREVERB_GAINLF,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.lRoomLF));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_DECAYTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Decay Time: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flDecayTime = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_DECAY_TIME,
                      prim->deferred.fxslot[idx].fx.reverb.flDecayTime);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_DECAYHFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Decay HF Ratio: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flDecayHFRatio = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_DECAY_HFRATIO,
                      prim->deferred.fxslot[idx].fx.reverb.flDecayHFRatio);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_DECAYLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Decay LF Ratio: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flDecayLFRatio = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_DECAY_LFRATIO,
                      prim->deferred.fxslot[idx].fx.reverb.flDecayLFRatio);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_REFLECTIONS:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Reflections: %ld\n", *data.l);

            prim->deferred.fxslot[idx].fx.reverb.lReflections = *data.l;
            alEffectf(prim->effect[idx], AL_EAXREVERB_REFLECTIONS_GAIN,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.lReflections));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_REFLECTIONSDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Reflections Delay: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flReflectionsDelay = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_REFLECTIONS_DELAY,
                      prim->deferred.fxslot[idx].fx.reverb.flReflectionsDelay);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_REFLECTIONSPAN:
        if(cbPropData >= sizeof(EAXVECTOR))
        {
            union { const void *v; const EAXVECTOR *vec; } data = { pPropData };
            TRACE("Reflections Pan: { %f, %f, %f }\n", data.vec->x, data.vec->y, data.vec->z);

            prim->deferred.fxslot[idx].fx.reverb.vReflectionsPan = *data.vec;
            alEffectfv(prim->effect[idx], AL_EAXREVERB_REFLECTIONS_PAN,
                       &prim->deferred.fxslot[idx].fx.reverb.vReflectionsPan.x);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_REVERB:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Reverb: %ld\n", *data.l);

            prim->deferred.fxslot[idx].fx.reverb.lReverb = *data.l;
            alEffectf(prim->effect[idx], AL_EAXREVERB_LATE_REVERB_GAIN,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.lReverb));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_REVERBDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Reverb Delay: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flReverbDelay = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_LATE_REVERB_DELAY,
                      prim->deferred.fxslot[idx].fx.reverb.flReverbDelay);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_REVERBPAN:
        if(cbPropData >= sizeof(EAXVECTOR))
        {
            union { const void *v; const EAXVECTOR *vec; } data = { pPropData };
            TRACE("Reverb Pan: { %f, %f, %f }\n", data.vec->x, data.vec->y, data.vec->z);

            prim->deferred.fxslot[idx].fx.reverb.vReverbPan = *data.vec;
            alEffectfv(prim->effect[idx], AL_EAXREVERB_LATE_REVERB_PAN,
                       &prim->deferred.fxslot[idx].fx.reverb.vReverbPan.x);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_ECHOTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Echo Time: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flEchoTime = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_ECHO_TIME,
                      prim->deferred.fxslot[idx].fx.reverb.flEchoTime);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_ECHODEPTH:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Echo Depth: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flEchoDepth = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_ECHO_DEPTH,
                      prim->deferred.fxslot[idx].fx.reverb.flEchoDepth);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_MODULATIONTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Modulation Time: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flModulationTime = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_MODULATION_TIME,
                      prim->deferred.fxslot[idx].fx.reverb.flModulationTime);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_MODULATIONDEPTH:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Modulation Depth: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flModulationDepth = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_MODULATION_DEPTH,
                      prim->deferred.fxslot[idx].fx.reverb.flModulationDepth);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_AIRABSORPTIONHF:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Air Absorption HF: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flAirAbsorptionHF = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_AIR_ABSORPTION_GAINHF,
                      mB_to_gain(prim->deferred.fxslot[idx].fx.reverb.flAirAbsorptionHF));
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_HFREFERENCE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("HF Reference: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flHFReference = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_HFREFERENCE,
                      prim->deferred.fxslot[idx].fx.reverb.flHFReference);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;
    case EAXREVERB_LFREFERENCE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("LF Reference: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flLFReference = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_LFREFERENCE,
                      prim->deferred.fxslot[idx].fx.reverb.flLFReference);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Room Rolloff Factor: %f\n", *data.fl);

            prim->deferred.fxslot[idx].fx.reverb.flRoomRolloffFactor = *data.fl;
            alEffectf(prim->effect[idx], AL_EAXREVERB_ROOM_ROLLOFF_FACTOR,
                      prim->deferred.fxslot[idx].fx.reverb.flRoomRolloffFactor);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    case EAXREVERB_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Flags: %lu\n", *data.dw);

            prim->deferred.fxslot[idx].fx.reverb.dwFlags = *data.dw;
            alEffecti(prim->effect[idx], AL_EAXREVERB_DECAY_HFLIMIT,
                (prim->deferred.fxslot[idx].fx.reverb.dwFlags&EAX30LISTENERFLAGS_DECAYHFLIMIT) ?
                AL_TRUE : AL_FALSE
            );
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

static HRESULT EAXReverb_Get(DSPrimary *prim, DWORD idx, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXREVERB_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case EAXREVERB_ALLPARAMETERS:
        GET_PROP(prim->current.fxslot[idx].fx.reverb, EAXREVERBPROPERTIES);
        break;

    case EAXREVERB_ENVIRONMENT:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.dwEnvironment, DWORD);
        break;

    case EAXREVERB_ENVIRONMENTSIZE:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flEnvironmentSize, float);
        break;
    case EAXREVERB_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flEnvironmentDiffusion, float);
        break;

    case EAXREVERB_ROOM:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.lRoom, long);
        break;
    case EAXREVERB_ROOMHF:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.lRoomHF, long);
        break;
    case EAXREVERB_ROOMLF:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.lRoomLF, long);
        break;

    case EAXREVERB_DECAYTIME:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flDecayTime, float);
        break;
    case EAXREVERB_DECAYHFRATIO:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flDecayHFRatio, float);
        break;
    case EAXREVERB_DECAYLFRATIO:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flDecayLFRatio, float);
        break;

    case EAXREVERB_REFLECTIONS:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.lReflections, long);
        break;
    case EAXREVERB_REFLECTIONSDELAY:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flReflectionsDelay, float);
        break;
    case EAXREVERB_REFLECTIONSPAN:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.vReflectionsPan, EAXVECTOR);
        break;

    case EAXREVERB_REVERB:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.lReverb, long);
        break;
    case EAXREVERB_REVERBDELAY:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flReverbDelay, float);
        break;
    case EAXREVERB_REVERBPAN:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.vReverbPan, EAXVECTOR);
        break;

    case EAXREVERB_ECHOTIME:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flEchoTime, float);
        break;
    case EAXREVERB_ECHODEPTH:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flEchoDepth, float);
        break;

    case EAXREVERB_MODULATIONTIME:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flModulationTime, float);
        break;
    case EAXREVERB_MODULATIONDEPTH:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flModulationDepth, float);
        break;

    case EAXREVERB_AIRABSORPTIONHF:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flAirAbsorptionHF, float);
        break;

    case EAXREVERB_HFREFERENCE:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flHFReference, float);
        break;
    case EAXREVERB_LFREFERENCE:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flLFReference, float);
        break;

    case EAXREVERB_ROOMROLLOFFFACTOR:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.flRoomRolloffFactor, float);
        break;

    case EAXREVERB_FLAGS:
        GET_PROP(prim->current.fxslot[idx].fx.reverb.dwFlags, DWORD);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}


HRESULT EAX4Slot_Set(DSPrimary *prim, LONG idx, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(prim->auxslot[idx] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    if(propid < EAXFXSLOT_NONE)
    {
        if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_REVERB)
            hr = EAXReverb_Set(prim, idx, propid, pPropData, cbPropData);
        else if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_CHORUS)
        {
            ERR("Chorus effect not yet handled\n");
            hr = E_PROP_ID_UNSUPPORTED;
        }
        else /*if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_NULL)*/
        {
            ERR("Unexpected null effect propid 0x%08lx\n", propid);
            hr = E_PROP_ID_UNSUPPORTED;
        }
    }
    else switch(propid)
    {
    case EAXFXSLOT_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case EAXFXSLOT_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAXFXSLOTPROPERTIES))
        {
            union { const void *v; const EAXFXSLOTPROPERTIES *props; } data = { pPropData };
            DWORD effect_type = FXSLOT_EFFECT_NULL;

            if(IsEqualGUID(&data.props->guidLoadEffect, &EAX_REVERB_EFFECT))
                effect_type = FXSLOT_EFFECT_REVERB;
            else if(IsEqualGUID(&data.props->guidLoadEffect, &EAX_CHORUS_EFFECT))
                effect_type = FXSLOT_EFFECT_CHORUS;
            else if(!IsEqualGUID(&data.props->guidLoadEffect, &EAX_NULL_GUID))
            {
                ERR("Unhandled effect GUID: %s\n", debugstr_guid(&data.props->guidLoadEffect));
                return DSERR_INVALIDPARAM;
            }

            if(data.props->lLock == EAXFXSLOT_LOCKED &&
               prim->deferred.fxslot[idx].props.lLock == EAXFXSLOT_LOCKED &&
               prim->deferred.fxslot[idx].effect_type != effect_type)
            {
                ERR("Attempting to change effect type for locked FXSlot\n");
                return DSERR_INVALIDCALL;
            }

            if(prim->deferred.fxslot[idx].effect_type != effect_type)
            {
                alGetError();
                alEffecti(prim->effect[idx], AL_EFFECT_TYPE,
                    (effect_type == FXSLOT_EFFECT_REVERB) ? AL_EFFECT_EAXREVERB :
                    (effect_type == FXSLOT_EFFECT_CHORUS) ? AL_EFFECT_CHORUS : AL_EFFECT_NULL
                );
                if(alGetError() != AL_NO_ERROR)
                {
                    ERR("Failed to set effect type %lu\n", effect_type);
                    return DSERR_INVALIDPARAM;
                }

                prim->deferred.fxslot[idx].effect_type = effect_type;
                memset(&prim->deferred.fxslot[idx].fx, 0, sizeof(prim->deferred.fxslot[idx].fx));
                if(effect_type == FXSLOT_EFFECT_REVERB)
                    prim->deferred.fxslot[idx].fx.reverb = EnvironmentDefaults[EAX_ENVIRONMENT_GENERIC];
                else if(effect_type == FXSLOT_EFFECT_CHORUS)
                {
                    prim->deferred.fxslot[idx].fx.chorus.dwWaveform = EAX_CHORUS_TRIANGLE;
                    prim->deferred.fxslot[idx].fx.chorus.lPhase = 90;
                    prim->deferred.fxslot[idx].fx.chorus.flRate = 1.1f;
                    prim->deferred.fxslot[idx].fx.chorus.flDepth = 0.1f;
                    prim->deferred.fxslot[idx].fx.chorus.flFeedback = 0.25f;
                    prim->deferred.fxslot[idx].fx.chorus.flDelay = 0.016f;
                }

                FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            }
            prim->deferred.fxslot[idx].props = *data.props;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_VOL_BIT);
            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_LOCK_BIT);
            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_FLAGS_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_LOADEFFECT:
        if(cbPropData >= sizeof(GUID))
        {
            union { const void *v; const GUID *guid; } data = { pPropData };
            DWORD effect_type = FXSLOT_EFFECT_NULL;

            if(IsEqualGUID(data.guid, &EAX_REVERB_EFFECT))
                effect_type = FXSLOT_EFFECT_REVERB;
            else if(IsEqualGUID(data.guid, &EAX_CHORUS_EFFECT))
                effect_type = FXSLOT_EFFECT_CHORUS;
            else if(!IsEqualGUID(data.guid, &EAX_NULL_GUID))
            {
                ERR("Unhandled effect GUID: %s\n", debugstr_guid(data.guid));
                return DSERR_INVALIDPARAM;
            }

            if(prim->deferred.fxslot[idx].props.lLock == EAXFXSLOT_LOCKED)
            {
                ERR("Attempting to change effect type for locked FXSlot\n");
                return DSERR_INVALIDCALL;
            }

            alGetError();
            alEffecti(prim->effect[idx], AL_EFFECT_TYPE,
                (effect_type == FXSLOT_EFFECT_REVERB) ? AL_EFFECT_EAXREVERB :
                (effect_type == FXSLOT_EFFECT_CHORUS) ? AL_EFFECT_CHORUS : AL_EFFECT_NULL
            );
            if(alGetError() != AL_NO_ERROR)
            {
                ERR("Failed to set effect type %lu\n", effect_type);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].effect_type = effect_type;
            memset(&prim->deferred.fxslot[idx].fx, 0, sizeof(prim->deferred.fxslot[idx].fx));
            if(effect_type == FXSLOT_EFFECT_REVERB)
                prim->deferred.fxslot[idx].fx.reverb = EnvironmentDefaults[EAX_ENVIRONMENT_GENERIC];
            else if(effect_type == FXSLOT_EFFECT_CHORUS)
            {
                prim->deferred.fxslot[idx].fx.chorus.dwWaveform = EAX_CHORUS_TRIANGLE;
                prim->deferred.fxslot[idx].fx.chorus.lPhase = 90;
                prim->deferred.fxslot[idx].fx.chorus.flRate = 1.1f;
                prim->deferred.fxslot[idx].fx.chorus.flDepth = 0.1f;
                prim->deferred.fxslot[idx].fx.chorus.flFeedback = 0.25f;
                prim->deferred.fxslot[idx].fx.chorus.flDelay = 0.016f;
            }
            prim->deferred.fxslot[idx].props.guidLoadEffect = *data.guid;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_VOLUME:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.fxslot[idx].props.lVolume = *data.l;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_VOL_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_LOCK:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.fxslot[idx].props.lLock = *data.l;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_LOCK_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };

            prim->deferred.fxslot[idx].props.dwFlags = *data.dw;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_FLAGS_BIT);
            return DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX4Slot_Get(DSPrimary *prim, LONG idx, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(prim->auxslot[idx] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    if(propid < EAXFXSLOT_NONE)
    {
        if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_REVERB)
            hr = EAXReverb_Get(prim, idx, propid, pPropData, cbPropData, pcbReturned);
        else if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_CHORUS)
        {
            ERR("Chorus effect not yet handled\n");
            hr = E_PROP_ID_UNSUPPORTED;
        }
        else /*if(prim->deferred.fxslot[idx].effect_type == FXSLOT_EFFECT_NULL)*/
        {
            ERR("Unexpected null effect propid 0x%08lx\n", propid);
            hr = E_PROP_ID_UNSUPPORTED;
        }
    }
    else switch(propid)
    {
    case EAXFXSLOT_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case EAXFXSLOT_ALLPARAMETERS:
        GET_PROP(prim->current.fxslot[idx].props, EAXFXSLOTPROPERTIES);
        break;
    case EAXFXSLOT_LOADEFFECT:
        GET_PROP(prim->current.fxslot[idx].props.guidLoadEffect, GUID);
        break;
    case EAXFXSLOT_VOLUME:
        GET_PROP(prim->current.fxslot[idx].props.lVolume, long);
        break;
    case EAXFXSLOT_LOCK:
        GET_PROP(prim->current.fxslot[idx].props.lLock, long);
        break;
    case EAXFXSLOT_FLAGS:
        GET_PROP(prim->current.fxslot[idx].props.dwFlags, DWORD);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}
