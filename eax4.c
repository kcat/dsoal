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
#undef GET_PROP

    return hr;
}
