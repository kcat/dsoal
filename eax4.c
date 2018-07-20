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
            ALuint prim_slot;
            TRACE("Parameters:\n\tPrimary FXSlot: %s\n\tDistance Factor: %f\n\t"
                "Air Absorption: %f\n\tHF Reference: %f\n",
                debugstr_guid(&data.props->guidPrimaryFXSlotID), data.props->flDistanceFactor,
                data.props->flAirAbsorptionHF, data.props->flHFReference
            );

            prim_slot = 0;
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
            ALuint prim_slot;
            TRACE("Primary FXSlot: %s\n", debugstr_guid(data.guid));

            prim_slot = 0;
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
            TRACE("Distance Factor: %f\n", *data.fl);

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
            TRACE("Air Absorption: %f\n", *data.fl);

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
            TRACE("HF Reference: %f\n", *data.fl);

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

    case EAXCONTEXT_LASTERROR:
        GET_PROP(InterlockedExchange(&prim->eax_error, EAX_OK), long);
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
            hr = EAXChorus_Set(prim, idx, propid, pPropData, cbPropData);
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
            DWORD effect_type;
            TRACE("Parameters:\n\tLoad Effect: %s\n\tVolume: %ld\n\tLock: %ld\n\tFlags: 0x%lx\n",
                debugstr_guid(&data.props->guidLoadEffect), data.props->lVolume, data.props->lLock,
                data.props->dwFlags
            );

            effect_type = FXSLOT_EFFECT_NULL;
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
            DWORD effect_type;
            TRACE("Load Effect: %s\n", debugstr_guid(data.guid));

            effect_type = FXSLOT_EFFECT_NULL;
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
            TRACE("Volume: %ld\n", *data.l);

            prim->deferred.fxslot[idx].props.lVolume = *data.l;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_VOL_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_LOCK:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Lock: %ld\n", *data.l);

            prim->deferred.fxslot[idx].props.lLock = *data.l;

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_LOCK_BIT);
            return DS_OK;
        }
        break;

    case EAXFXSLOT_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Flags: 0x%lx\n", *data.dw);

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
            hr = EAXChorus_Get(prim, idx, propid, pPropData, cbPropData, pcbReturned);
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


#define APPLY_WET0_PARAMS 1
#define APPLY_WET1_PARAMS 2
#define APPLY_DRY_PARAMS 4
#define APPLY_ALL_PARAMS (APPLY_WET0_PARAMS | APPLY_WET1_PARAMS | APPLY_DRY_PARAMS)
#define APPLY_ALLWET_PARAMS (APPLY_WET0_PARAMS | APPLY_WET1_PARAMS)
static void ApplyFilterParams(DSBuffer *buf, const EAXSOURCEPROPERTIES *props, int apply)
{
    /* The LFRatio properties determine how much the given level applies to low
     * frequencies as well as high frequencies. Technically, given that the
     * obstruction/occlusion/exclusion levels are the absolute level applied to
     * high frequencies (relative to full-scale, according to the EAX 2.0 spec)
     * while the HF filter gains are relative to the low, the HF gains should
     * increase as LFRatio increases.
     *
     * However it seems Creative was either wrong when writing out the spec,
     * or implemented it incorrectly, as the HF filter still applies in full
     * regardless of the LFRatio. So to replicate the hardware behavior, we do
     * the same here.
     */
    /* The interaction of ratios is pretty wierd. The typical combination of
     * the two act as a minimal baseline, while the sum minus one is used when
     * larger. This creates a more linear change with the individual ratios as
     * DirectRatio goes beyond 1, but eases down as the two ratios go toward 0.
     */
    float room_mb = props->lRoom + props->lExclusion*props->flExclusionLFRatio +
        maxF(props->flOcclusionLFRatio+props->flOcclusionRoomRatio-1.0f,
             props->flOcclusionLFRatio*props->flOcclusionRoomRatio) * props->lOcclusion;
    float room_mbhf = props->lRoomHF + props->lExclusion +
                      props->lOcclusion*props->flOcclusionRoomRatio;
    float direct_mb = 0;
    float direct_mbhf = 0;
    int i;

    for(i = 0;i < EAX_MAX_ACTIVE_FXSLOTS;i++)
    {
        const struct Send *send = &buf->deferred.send[i];
        if((apply&(1<<i)) && buf->filter[1+i])
        {
            float mb = room_mb + send->lExclusion*send->flExclusionLFRatio +
                       maxF(send->flOcclusionLFRatio+send->flOcclusionRoomRatio-1.0f,
                            send->flOcclusionLFRatio*send->flOcclusionRoomRatio)*send->lOcclusion;
            float mbhf = room_mbhf + send->lExclusion +
                         send->lOcclusion*send->flOcclusionRoomRatio;

            alFilterf(buf->filter[1+i], AL_LOWPASS_GAIN, mB_to_gain(minF(mb, buf->filter_mBLimit)));
            alFilterf(buf->filter[1+i], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
        }
        /* FIXME: This should either be added like this, or take the minimum
         * volume level from the separate occlusion properties.
         */
        direct_mb += maxF(send->flOcclusionLFRatio+send->flOcclusionDirectRatio-1.0f,
                          send->flOcclusionLFRatio*send->flOcclusionDirectRatio) *
                     send->lOcclusion;
        direct_mbhf += send->lOcclusion*send->flOcclusionDirectRatio;
    }
    if((apply&APPLY_DRY_PARAMS) && buf->filter[0])
    {
        float mb = direct_mb + props->lDirect + props->lObstruction*props->flObstructionLFRatio +
                   maxF(props->flOcclusionLFRatio+props->flOcclusionDirectRatio-1.0f,
                        props->flOcclusionLFRatio*props->flOcclusionDirectRatio)*props->lOcclusion;
        float mbhf = direct_mbhf + props->lDirectHF + props->lObstruction +
                     props->lOcclusion*props->flOcclusionDirectRatio;

        alFilterf(buf->filter[0], AL_LOWPASS_GAIN, mB_to_gain(minF(mb, buf->filter_mBLimit)));
        alFilterf(buf->filter[0], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
    }
    checkALError();
}

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

static struct Send *FindCurrentSend(DSBuffer *buf, const GUID *guid)
{
    int i;
    for(i = 0;i < EAX_MAX_ACTIVE_FXSLOTS;i++)
    {
        DWORD target = buf->current.fxslot_targets[i];
        if((target == FXSLOT_TARGET_0 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot0)) ||
           (target == FXSLOT_TARGET_1 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot1)) ||
           (target == FXSLOT_TARGET_2 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot2)) ||
           (target == FXSLOT_TARGET_3 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot3)) ||
           (target == FXSLOT_TARGET_PRIMARY && IsEqualGUID(guid, &EAX_PrimaryFXSlotID)) ||
           (target == FXSLOT_TARGET_NULL && IsEqualGUID(guid, &EAX_NULL_GUID)))
            return &buf->current.send[i];
    }
    return NULL;
}

static struct Send *FindDeferredSend(DSBuffer *buf, const GUID *guid)
{
    int i;
    for(i = 0;i < EAX_MAX_ACTIVE_FXSLOTS;i++)
    {
        DWORD target = buf->deferred.fxslot_targets[i];
        if((target == FXSLOT_TARGET_0 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot0)) ||
           (target == FXSLOT_TARGET_1 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot1)) ||
           (target == FXSLOT_TARGET_2 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot2)) ||
           (target == FXSLOT_TARGET_3 && IsEqualGUID(guid, &EAXPROPERTYID_EAX40_FXSlot3)) ||
           (target == FXSLOT_TARGET_PRIMARY && IsEqualGUID(guid, &EAX_PrimaryFXSlotID)) ||
           (target == FXSLOT_TARGET_NULL && IsEqualGUID(guid, &EAX_NULL_GUID)))
            return &buf->deferred.send[i];
    }
    return NULL;
}

HRESULT EAX4Source_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport)
{
    if(!HAS_EXTENSION(buf->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

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
        *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
        return DS_OK;

    default:
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT EAX4Source_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(!HAS_EXTENSION(buf->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXSOURCE_NONE:
        return DS_OK;

    case EAXSOURCE_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEPROPERTIES))
        {
            union {
                const void *v;
                const EAXSOURCEPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tDirect: %ld\n\tDirect HF: %ld\n\tRoom: %ld\n\tRoom HF: %ld\n\t"
                "Obstruction: %ld\n\tObstruction LF Ratio: %f\n\tOcclusion: %ld\n\t"
                "Occlusion LF Ratio: %f\n\tOcclusion Room Ratio: %f\n\t"
                "Occlusion Direct Ratio: %f\n\tExclusion: %ld\n\tExclusion LF Ratio: %f\n\t"
                "Outside Volume HF: %ld\n\tDoppler Factor: %f\n\tRolloff Factor: %f\n\t"
                "Room Rolloff Factor: %f\n\tAir Absorb Factor: %f\n\tFlags: 0x%02lx\n",
                data.props->lDirect, data.props->lDirectHF, data.props->lRoom, data.props->lRoomHF,
                data.props->lObstruction, data.props->flObstructionLFRatio, data.props->lOcclusion,
                data.props->flOcclusionLFRatio, data.props->flOcclusionRoomRatio,
                data.props->flOcclusionDirectRatio, data.props->lExclusion,
                data.props->flExclusionLFRatio, data.props->lOutsideVolumeHF,
                data.props->flDopplerFactor, data.props->flRolloffFactor,
                data.props->flRoomRolloffFactor, data.props->flAirAbsorptionFactor,
                data.props->dwFlags
            );

            buf->deferred.eax = *data.props;
            ApplyFilterParams(buf, data.props, APPLY_ALL_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            buf->dirty.bit.doppler = 1;
            buf->dirty.bit.rolloff = 1;
            buf->dirty.bit.room_rolloff = 1;
            buf->dirty.bit.cone_outsidevolumehf = 1;
            buf->dirty.bit.air_absorb = 1;
            buf->dirty.bit.flags = 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
        if(cbPropData >= sizeof(EAXOBSTRUCTIONPROPERTIES))
        {
            union {
                const void *v;
                const EAXOBSTRUCTIONPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tObstruction: %ld\n\tObstruction LF Ratio: %f\n",
                  data.props->lObstruction, data.props->flObstructionLFRatio);

            buf->deferred.eax.lObstruction = data.props->lObstruction;
            buf->deferred.eax.flObstructionLFRatio = data.props->flObstructionLFRatio;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONPARAMETERS:
        if(cbPropData >= sizeof(EAXOCCLUSIONPROPERTIES))
        {
            union {
                const void *v;
                const EAXOCCLUSIONPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tOcclusion: %ld\n\tOcclusion LF Ratio: %f\n\t"
                "Occlusion Room Ratio: %f\n\tOcclusion Direct Ratio: %f\n",
                data.props->lOcclusion, data.props->flOcclusionLFRatio,
                data.props->flOcclusionRoomRatio, data.props->flOcclusionDirectRatio
            );

            buf->deferred.eax.lOcclusion = data.props->lOcclusion;
            buf->deferred.eax.flOcclusionLFRatio = data.props->flOcclusionLFRatio;
            buf->deferred.eax.flOcclusionRoomRatio = data.props->flOcclusionRoomRatio;
            buf->deferred.eax.flOcclusionDirectRatio = data.props->flOcclusionDirectRatio;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALL_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_EXCLUSIONPARAMETERS:
        if(cbPropData >= sizeof(EAXEXCLUSIONPROPERTIES))
        {
            union {
                const void *v;
                const EAXEXCLUSIONPROPERTIES *props;
            } data = { pPropData };
            TRACE("Parameters:\n\tExclusion: %ld\n\tExclusion LF Ratio: %f\n",
                  data.props->lExclusion, data.props->flExclusionLFRatio);

            buf->deferred.eax.lExclusion = data.props->lExclusion;
            buf->deferred.eax.flExclusionLFRatio = data.props->flExclusionLFRatio;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_DIRECT:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Direct: %ld\n", *data.l);

            buf->deferred.eax.lDirect = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_DIRECTHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Direct HF: %ld\n", *data.l);

            buf->deferred.eax.lDirectHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_ROOM:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room: %ld\n", *data.l);

            buf->deferred.eax.lRoom = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room HF: %ld\n", *data.l);

            buf->deferred.eax.lRoomHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_OBSTRUCTION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Obstruction: %ld\n", *data.l);

            buf->deferred.eax.lObstruction = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Obstruction LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flObstructionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_OCCLUSION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Occlusion: %ld\n", *data.l);

            buf->deferred.eax.lOcclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALL_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALL_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONROOMRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion Room Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionRoomRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion Direct Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionDirectRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_EXCLUSION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Exclusion: %ld\n", *data.l);

            buf->deferred.eax.lExclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;
    case EAXSOURCE_EXCLUSIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Exclusion LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flExclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_ALLWET_PARAMS);

            buf->dirty.bit.send_filter = (1<<buf->share->num_sends) - 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_OUTSIDEVOLUMEHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Outisde Volume HF: %ld\n", *data.l);

            buf->deferred.eax.lOutsideVolumeHF = *data.l;

            buf->dirty.bit.cone_outsidevolumehf = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_DOPPLERFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Doppler Factor: %f\n", *data.fl);

            buf->deferred.eax.flDopplerFactor = *data.fl;

            buf->dirty.bit.doppler = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_ROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Rolloff Factor: %f\n", *data.fl);

            buf->deferred.eax.flRolloffFactor = *data.fl;

            buf->dirty.bit.rolloff = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Room Rolloff Factor: %f\n", *data.fl);

            buf->deferred.eax.flRoomRolloffFactor = *data.fl;

            buf->dirty.bit.room_rolloff = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_AIRABSORPTIONFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Air Absorb Factor: %f\n", *data.fl);

            buf->deferred.eax.flAirAbsorptionFactor = *data.fl;

            buf->dirty.bit.air_absorb = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Flags: 0x%lx\n", *data.dw);

            buf->deferred.eax.dwFlags = *data.dw;

            buf->dirty.bit.flags = 1;
            hr = DS_OK;
        }
        break;

    case EAXSOURCE_SENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCESENDPROPERTIES))
        {
            union { const void *v; const EAXSOURCESENDPROPERTIES *send; } data = { pPropData };
            struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = cbPropData / sizeof(EAXSOURCESENDPROPERTIES);
            LONG wetmask=0, i;

            if(count > buf->share->num_sends)
            {
                ERR("Setting %ld sends, only %d supported\n", count, buf->share->num_sends);
                return DSERR_INVALIDPARAM;
            }

            for(i = 0;i < count;++i)
            {
                TRACE("Send parameters:\n\tReceiving: %s\n\tSend: %ld\n\tSend HF: %ld\n",
                    debugstr_guid(&data.send[i].guidReceivingFXSlotID),
                    data.send[i].lSend, data.send[i].lSendHF
                );
                srcsend[i] = FindDeferredSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
                wetmask |= 1 << (srcsend[i]-buf->deferred.send);
            }

            for(i = 0;i < count;++i)
            {
                srcsend[i]->lSend = data.send[i].lSend;
                srcsend[i]->lSendHF = data.send[i].lSendHF;
            }
            ApplyFilterParams(buf, &buf->deferred.eax, wetmask);

            buf->dirty.bit.send_filter |= wetmask;
            return DS_OK;
        }
        break;
    case EAXSOURCE_ALLSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEALLSENDPROPERTIES))
        {
            union { const void *v; const EAXSOURCEALLSENDPROPERTIES *send; } data = { pPropData };
            struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = cbPropData / sizeof(EAXSOURCEALLSENDPROPERTIES);
            LONG wetmask=0, i;

            if(count > buf->share->num_sends)
            {
                ERR("Setting %ld sends, only %d supported\n", count, buf->share->num_sends);
                return DSERR_INVALIDPARAM;
            }

            for(i = 0;i < count;++i)
            {
                TRACE("All send parameters:\n\tReceiving: %s\n\tSend: %ld\n\tSend HF: %ld\n\t"
                    "Occlusion: %ld\n\tOcclusion LF Ratio: %f\n\tOcclusion Room Ratio: %f\n\t"
                    "Occlusion Direct Ratio: %f\n\tExclusion: %ld\n\tExclusion LF Ratio: %f\n",
                    debugstr_guid(&data.send[i].guidReceivingFXSlotID),
                    data.send[i].lSend, data.send[i].lSendHF, data.send[i].lOcclusion,
                    data.send[i].flOcclusionLFRatio, data.send[i].flOcclusionRoomRatio,
                    data.send[i].flOcclusionDirectRatio, data.send[i].lExclusion,
                    data.send[i].flExclusionLFRatio
                );
                srcsend[i] = FindDeferredSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
                wetmask |= 1 << (srcsend[i]-buf->deferred.send);
            }

            for(i = 0;i < count;++i)
            {
                srcsend[i]->lSend = data.send[i].lSend;
                srcsend[i]->lSendHF = data.send[i].lSendHF;
                srcsend[i]->lOcclusion = data.send[i].lOcclusion;
                srcsend[i]->flOcclusionLFRatio = data.send[i].flOcclusionLFRatio;
                srcsend[i]->flOcclusionRoomRatio = data.send[i].flOcclusionRoomRatio;
                srcsend[i]->flOcclusionDirectRatio = data.send[i].flOcclusionDirectRatio;
                srcsend[i]->lExclusion = data.send[i].lExclusion;
                srcsend[i]->flExclusionLFRatio = data.send[i].flExclusionLFRatio;
            }
            ApplyFilterParams(buf, &buf->deferred.eax, wetmask|APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter |= wetmask;
            return DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES))
        {
            union { const void *v; const EAXSOURCEOCCLUSIONSENDPROPERTIES *send; } data = { pPropData };
            struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = cbPropData / sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES);
            LONG wetmask=0, i;

            if(count > buf->share->num_sends)
            {
                ERR("Setting %ld sends, only %d supported\n", count, buf->share->num_sends);
                return DSERR_INVALIDPARAM;
            }

            for(i = 0;i < count;++i)
            {
                TRACE("Occlusion send parameters:\n\tReceiving: %s\n\tOcclusion: %ld\n\t"
                    "Occlusion LF Ratio: %f\n\tOcclusion Room Ratio: %f\n\t"
                    "Occlusion Direct Ratio: %f\n",
                    debugstr_guid(&data.send[i].guidReceivingFXSlotID),  data.send[i].lOcclusion,
                    data.send[i].flOcclusionLFRatio, data.send[i].flOcclusionRoomRatio,
                    data.send[i].flOcclusionDirectRatio
                );
                srcsend[i] = FindDeferredSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
                wetmask |= 1 << (srcsend[i]-buf->deferred.send);
            }

            for(i = 0;i < count;++i)
            {
                srcsend[i]->lOcclusion = data.send[i].lOcclusion;
                srcsend[i]->flOcclusionLFRatio = data.send[i].flOcclusionLFRatio;
                srcsend[i]->flOcclusionRoomRatio = data.send[i].flOcclusionRoomRatio;
                srcsend[i]->flOcclusionDirectRatio = data.send[i].flOcclusionDirectRatio;
            }
            ApplyFilterParams(buf, &buf->deferred.eax, wetmask|APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.send_filter |= wetmask;
            return DS_OK;
        }
        break;
    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES))
        {
            union { const void *v; const EAXSOURCEEXCLUSIONSENDPROPERTIES *send; } data = { pPropData };
            struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = cbPropData / sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES);
            LONG wetmask=0, i;

            if(count > buf->share->num_sends)
            {
                ERR("Setting %ld sends, only %d supported\n", count, buf->share->num_sends);
                return DSERR_INVALIDPARAM;
            }

            for(i = 0;i < count;++i)
            {
                TRACE("Exclusion send parameters:\n\tReceiving: %s\n\tExclusion: %ld\n\t"
                    "Exclusion LF Ratio: %f\n",
                    debugstr_guid(&data.send[i].guidReceivingFXSlotID),
                    data.send[i].lExclusion, data.send[i].flExclusionLFRatio
                );
                srcsend[i] = FindDeferredSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
                wetmask |= 1 << (srcsend[i]-buf->deferred.send);
            }

            for(i = 0;i < count;++i)
            {
                srcsend[i]->lExclusion = data.send[i].lExclusion;
                srcsend[i]->flExclusionLFRatio = data.send[i].flExclusionLFRatio;
            }
            ApplyFilterParams(buf, &buf->deferred.eax, wetmask);

            buf->dirty.bit.send_filter |= wetmask;
            return DS_OK;
        }
        break;

    case EAXSOURCE_ACTIVEFXSLOTID:
        if(cbPropData && (cbPropData%sizeof(GUID)) == 0)
        {
            union { const void *v; const GUID *guid; } data = { pPropData };
            DWORD targets[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = cbPropData / sizeof(GUID);
            LONG i;

            if(count > buf->share->num_sends)
            {
                ERR("Setting %ld sends, only %d supported\n", count, buf->share->num_sends);
                return DSERR_INVALIDPARAM;
            }

            for(i = 0;i < count;i++)
            {
                TRACE("Active FXSlot %ld: %s\n", i, debugstr_guid(&data.guid[i]));

                targets[i] = FXSLOT_TARGET_NULL;
                if(IsEqualGUID(&data.guid[i], &EAXPROPERTYID_EAX40_FXSlot0))
                    targets[i] = FXSLOT_TARGET_0;
                else if(IsEqualGUID(&data.guid[i], &EAXPROPERTYID_EAX40_FXSlot1))
                    targets[i] = FXSLOT_TARGET_1;
                else if(IsEqualGUID(&data.guid[i], &EAXPROPERTYID_EAX40_FXSlot2))
                    targets[i] = FXSLOT_TARGET_2;
                else if(IsEqualGUID(&data.guid[i], &EAXPROPERTYID_EAX40_FXSlot3))
                    targets[i] = FXSLOT_TARGET_3;
                else if(IsEqualGUID(&data.guid[i], &EAX_PrimaryFXSlotID))
                    targets[i] = FXSLOT_TARGET_PRIMARY;
                if(targets[i] == FXSLOT_TARGET_NULL && !IsEqualGUID(&data.guid[i], &EAX_NULL_GUID))
                {
                    ERR("Invalid FXSlot GUID: %s\n", debugstr_guid(&data.guid[i]));
                    return DSERR_INVALIDPARAM;
                }
            }

            for(i = 0;i < count;++i)
                buf->deferred.fxslot_targets[i] = targets[i];
            buf->dirty.bit.send_filter |= (1<<count) - 1;
            return DS_OK;
        }

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX4Source_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(!HAS_EXTENSION(buf->share, EXT_EFX))
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXSOURCE_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case EAXSOURCE_ALLPARAMETERS:
        GET_PROP(buf->current.eax, EAXSOURCEPROPERTIES);
        return DSERR_INVALIDPARAM;

    case EAXSOURCE_OBSTRUCTIONPARAMETERS:
        GET_PROP(EAXSourceObstruction(&buf->current.eax), EAXOBSTRUCTIONPROPERTIES);
        break;
    case EAXSOURCE_OCCLUSIONPARAMETERS:
        GET_PROP(EAXSourceOcclusion(&buf->current.eax), EAXOCCLUSIONPROPERTIES);
        break;
    case EAXSOURCE_EXCLUSIONPARAMETERS:
        GET_PROP(EAXSourceExclusion(&buf->current.eax), EAXEXCLUSIONPROPERTIES);
        break;

    case EAXSOURCE_DIRECT:
        GET_PROP(buf->current.eax.lDirect, long);
        break;
    case EAXSOURCE_DIRECTHF:
        GET_PROP(buf->current.eax.lDirectHF, long);
        break;

    case EAXSOURCE_ROOM:
        GET_PROP(buf->current.eax.lRoom, long);
        break;
    case EAXSOURCE_ROOMHF:
        GET_PROP(buf->current.eax.lRoomHF, long);
        break;

    case EAXSOURCE_OBSTRUCTION:
        GET_PROP(buf->current.eax.lObstruction, long);
        break;
    case EAXSOURCE_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->current.eax.flObstructionLFRatio, float);
        break;

    case EAXSOURCE_OCCLUSION:
        GET_PROP(buf->current.eax.lOcclusion, long);
        break;
    case EAXSOURCE_OCCLUSIONLFRATIO:
        GET_PROP(buf->current.eax.flOcclusionLFRatio, float);
        break;
    case EAXSOURCE_OCCLUSIONROOMRATIO:
        GET_PROP(buf->current.eax.flOcclusionRoomRatio, float);
        break;
    case EAXSOURCE_OCCLUSIONDIRECTRATIO:
        GET_PROP(buf->current.eax.flOcclusionDirectRatio, float);
        break;

    case EAXSOURCE_EXCLUSION:
        GET_PROP(buf->current.eax.lExclusion, long);
        break;
    case EAXSOURCE_EXCLUSIONLFRATIO:
        GET_PROP(buf->current.eax.flExclusionLFRatio, float);
        break;

    case EAXSOURCE_OUTSIDEVOLUMEHF:
        GET_PROP(buf->current.eax.lOutsideVolumeHF, long);
        break;

    case EAXSOURCE_DOPPLERFACTOR:
        GET_PROP(buf->current.eax.flDopplerFactor, float);
        break;

    case EAXSOURCE_ROLLOFFFACTOR:
        GET_PROP(buf->current.eax.flRolloffFactor, float);
        break;
    case EAXSOURCE_ROOMROLLOFFFACTOR:
        GET_PROP(buf->current.eax.flRoomRolloffFactor, float);
        break;

    case EAXSOURCE_AIRABSORPTIONFACTOR:
        GET_PROP(buf->current.eax.flAirAbsorptionFactor, float);
        break;

    case EAXSOURCE_FLAGS:
        GET_PROP(buf->current.eax.dwFlags, DWORD);
        break;

    case EAXSOURCE_SENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCESENDPROPERTIES))
        {
            union { void *v; EAXSOURCESENDPROPERTIES *send; } data = { pPropData };
            const struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = minI(cbPropData / sizeof(EAXSOURCESENDPROPERTIES),
                              EAX_MAX_ACTIVE_FXSLOTS);
            LONG i;

            for(i = 0;i < count;++i)
            {
                srcsend[i] = FindCurrentSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
            }

            for(i = 0;i < count;++i)
            {
                data.send[i].lSend = srcsend[i]->lSend;
                data.send[i].lSendHF = srcsend[i]->lSendHF;
            }
            *pcbReturned = sizeof(EAXSOURCESENDPROPERTIES)*count;
            return DS_OK;
        }
        break;

    case EAXSOURCE_ALLSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEALLSENDPROPERTIES))
        {
            union { void *v; EAXSOURCEALLSENDPROPERTIES *send; } data = { pPropData };
            const struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = minI(cbPropData / sizeof(EAXSOURCEALLSENDPROPERTIES),
                              EAX_MAX_ACTIVE_FXSLOTS);
            LONG i;

            for(i = 0;i < count;++i)
            {
                srcsend[i] = FindCurrentSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
            }

            for(i = 0;i < count;++i)
            {
                data.send[i].lSend = srcsend[i]->lSend;
                data.send[i].lSendHF = srcsend[i]->lSendHF;
                data.send[i].lOcclusion = srcsend[i]->lOcclusion;
                data.send[i].flOcclusionLFRatio = srcsend[i]->flOcclusionLFRatio;
                data.send[i].flOcclusionRoomRatio = srcsend[i]->flOcclusionRoomRatio;
                data.send[i].flOcclusionDirectRatio = srcsend[i]->flOcclusionDirectRatio;
                data.send[i].lExclusion = srcsend[i]->lExclusion;
                data.send[i].flExclusionLFRatio = srcsend[i]->flExclusionLFRatio;
            }
            *pcbReturned = sizeof(EAXSOURCEALLSENDPROPERTIES)*count;
            return DS_OK;
        }
        break;
    case EAXSOURCE_OCCLUSIONSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES))
        {
            union { void *v; EAXSOURCEOCCLUSIONSENDPROPERTIES *send; } data = { pPropData };
            const struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = minI(cbPropData / sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES),
                              EAX_MAX_ACTIVE_FXSLOTS);
            LONG i;

            for(i = 0;i < count;++i)
            {
                srcsend[i] = FindCurrentSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
            }

            for(i = 0;i < count;++i)
            {
                data.send[i].lOcclusion = srcsend[i]->lOcclusion;
                data.send[i].flOcclusionLFRatio = srcsend[i]->flOcclusionLFRatio;
                data.send[i].flOcclusionRoomRatio = srcsend[i]->flOcclusionRoomRatio;
                data.send[i].flOcclusionDirectRatio = srcsend[i]->flOcclusionDirectRatio;
            }
            *pcbReturned = sizeof(EAXSOURCEOCCLUSIONSENDPROPERTIES)*count;
            return DS_OK;
        }
        break;
    case EAXSOURCE_EXCLUSIONSENDPARAMETERS:
        if(cbPropData >= sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES))
        {
            union { void *v; EAXSOURCEEXCLUSIONSENDPROPERTIES *send; } data = { pPropData };
            const struct Send *srcsend[EAX_MAX_ACTIVE_FXSLOTS];
            LONG count = minI(cbPropData / sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES),
                              EAX_MAX_ACTIVE_FXSLOTS);
            LONG i;

            for(i = 0;i < count;++i)
            {
                srcsend[i] = FindCurrentSend(buf, &data.send[i].guidReceivingFXSlotID);
                if(!srcsend[i])
                {
                    ERR("Failed to find FXSlot target: %s\n",
                        debugstr_guid(&data.send[i].guidReceivingFXSlotID));
                    return DSERR_INVALIDPARAM;
                }
            }

            for(i = 0;i < count;++i)
            {
                data.send[i].lExclusion = srcsend[i]->lExclusion;
                data.send[i].flExclusionLFRatio = srcsend[i]->flExclusionLFRatio;
            }
            *pcbReturned = sizeof(EAXSOURCEEXCLUSIONSENDPROPERTIES)*count;
            return DS_OK;
        }
        break;
    case EAXSOURCE_ACTIVEFXSLOTID:
        if(cbPropData >= sizeof(GUID))
        {
            union { void *v; GUID *guid; } data = { pPropData };
            LONG count = minI(cbPropData / sizeof(GUID), EAX_MAX_ACTIVE_FXSLOTS);
            LONG i;

            for(i = 0;i < count;++i)
            {
                if(buf->current.fxslot_targets[i] == FXSLOT_TARGET_0)
                    data.guid[i] = EAXPROPERTYID_EAX40_FXSlot0;
                else if(buf->current.fxslot_targets[i] == FXSLOT_TARGET_1)
                    data.guid[i] = EAXPROPERTYID_EAX40_FXSlot1;
                else if(buf->current.fxslot_targets[i] == FXSLOT_TARGET_2)
                    data.guid[i] = EAXPROPERTYID_EAX40_FXSlot2;
                else if(buf->current.fxslot_targets[i] == FXSLOT_TARGET_3)
                    data.guid[i] = EAXPROPERTYID_EAX40_FXSlot3;
                else if(buf->current.fxslot_targets[i] == FXSLOT_TARGET_PRIMARY)
                    data.guid[i] = EAX_PrimaryFXSlotID;
                else /*if(buf->current.fxslot_targets[i] >= FXSLOT_TARGET_NULL)*/
                    data.guid[i] = EAX_NULL_GUID;
            }

            *pcbReturned = sizeof(GUID)*count;
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
