/* EAX chorus interface
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
#include "eax4.h"


static void ApplyChorusParams(ALuint effect, const EAXCHORUSPROPERTIES *props)
{
    alEffecti(effect, AL_CHORUS_WAVEFORM, props->dwWaveform);
    alEffecti(effect, AL_CHORUS_PHASE, props->lPhase);
    alEffectf(effect, AL_CHORUS_RATE, props->flRate);
    alEffectf(effect, AL_CHORUS_DEPTH, props->flDepth);
    alEffectf(effect, AL_CHORUS_FEEDBACK, props->flFeedback);
    alEffectf(effect, AL_CHORUS_DELAY, props->flDelay);

    checkALError();
}


HRESULT EAXChorus_Set(DSPrimary *prim, LONG idx, DWORD propid, void *pPropData, ULONG cbPropData)
{
    switch(propid)
    {
    case EAXCHORUS_NONE: /* not setting any property, just applying */
        return DS_OK;

    case EAXCHORUS_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAXCHORUSPROPERTIES))
        {
            union { const void *v; const EAXCHORUSPROPERTIES *props; } data = { pPropData };
            TRACE("Parameters:\n\tWaveform: %lu\n\tPhase: %ld\n\tRate: %f\n\t"
                "Depth: %f\n\tFeedback: %f\n\tDelay: %f\n",
                data.props->dwWaveform, data.props->lPhase, data.props->flRate,
                data.props->flDepth, data.props->flFeedback, data.props->flDelay
            );

            if(data.props->dwWaveform != EAX_CHORUS_SINUSOID &&
               data.props->dwWaveform != EAX_CHORUS_TRIANGLE)
            {
                ERR("Unexpected chorus waveform: %lu\n", data.props->dwWaveform);
                return DSERR_INVALIDPARAM;
            }
            if(data.props->lPhase < -180 || data.props->lPhase > 180)
            {
                ERR("Unexpected chorus phase: %ld\n", data.props->lPhase);
                return DSERR_INVALIDPARAM;
            }
            if(data.props->flRate < 0.0f || data.props->flRate > 10.0f)
            {
                ERR("Unexpected chorus rate: %f\n", data.props->flRate);
                return DSERR_INVALIDPARAM;
            }
            if(data.props->flDepth < 0.0f || data.props->flDepth > 1.0f)
            {
                ERR("Unexpected chorus depth: %f\n", data.props->flDepth);
                return DSERR_INVALIDPARAM;
            }
            if(data.props->flFeedback < -1.0f || data.props->flFeedback > 1.0f)
            {
                ERR("Unexpected chorus feedback: %f\n", data.props->flFeedback);
                return DSERR_INVALIDPARAM;
            }
            if(data.props->flDelay < 0.0f || data.props->flDelay > 0.016f)
            {
                ERR("Unexpected chorus delay: %f\n", data.props->flDelay);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus = *data.props;
            ApplyChorusParams(prim->effect[idx], data.props);

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_WAVEFORM:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Waveform: %lu\n", *data.dw);

            if(*data.dw != EAX_CHORUS_SINUSOID && *data.dw != EAX_CHORUS_TRIANGLE)
            {
                ERR("Unexpected chorus waveform: %lu\n", *data.dw);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.dwWaveform = *data.dw;
            alEffecti(prim->effect[idx], AL_CHORUS_WAVEFORM, *data.dw);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_PHASE:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Phase: %ld\n", *data.l);

            if(*data.l < -180 || *data.l > 180)
            {
                ERR("Unexpected chorus phase: %ld\n", *data.l);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.lPhase = *data.l;
            alEffecti(prim->effect[idx], AL_CHORUS_PHASE, *data.l);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_RATE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Rate: %f\n", *data.fl);

            if(*data.fl < 0.0f || *data.fl > 10.0f)
            {
                ERR("Unexpected chorus rate: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.flRate = *data.fl;
            alEffectf(prim->effect[idx], AL_CHORUS_RATE, *data.fl);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_DEPTH:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Depth: %f\n", *data.fl);

            if(*data.fl < 0.0f || *data.fl > 1.0f)
            {
                ERR("Unexpected chorus depth: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.flDepth = *data.fl;
            alEffectf(prim->effect[idx], AL_CHORUS_DEPTH, *data.fl);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_FEEDBACK:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Feedback: %f\n", *data.fl);

            if(*data.fl < -1.0f || *data.fl > 1.0f)
            {
                ERR("Unexpected chorus feedback: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.flFeedback = *data.fl;
            alEffectf(prim->effect[idx], AL_CHORUS_FEEDBACK, *data.fl);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_DELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Delay: %f\n", *data.fl);

            if(*data.fl < 0.0f || *data.fl > 0.016f)
            {
                ERR("Unexpected chorus delay: %f\n", *data.fl);
                return DSERR_INVALIDPARAM;
            }

            prim->deferred.fxslot[idx].fx.chorus.flDelay = *data.fl;
            alEffectf(prim->effect[idx], AL_CHORUS_DELAY, *data.fl);
            checkALError();

            FXSLOT_SET_DIRTY(prim->dirty.bit, idx, FXSLOT_EFFECT_BIT);
            return DS_OK;
        }
        return DSERR_INVALIDPARAM;

    default:
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }
    return E_PROP_ID_UNSUPPORTED;
}

#define GET_PROP(src, T) do {                              \
    if(cbPropData >= sizeof(T))                            \
    {                                                      \
        union { void *v; T *props; } data = { pPropData }; \
        *data.props = src;                                 \
        *pcbReturned = sizeof(T);                          \
        return DS_OK;                                      \
    }                                                      \
} while(0)

HRESULT EAXChorus_Get(DSPrimary *prim, DWORD idx, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    switch(propid)
    {
    case EAXCHORUS_NONE:
        *pcbReturned = 0;
        return DS_OK;

    case EAXCHORUS_ALLPARAMETERS:
        GET_PROP(prim->current.fxslot[idx].fx.chorus, EAXCHORUSPROPERTIES);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_WAVEFORM:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.dwWaveform, DWORD);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_PHASE:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.lPhase, long);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_RATE:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.flRate, float);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_DEPTH:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.flDepth, float);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_FEEDBACK:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.flFeedback, float);
        return DSERR_INVALIDPARAM;

    case EAXCHORUS_DELAY:
        GET_PROP(prim->deferred.fxslot[idx].fx.chorus.flDelay, float);
        return DSERR_INVALIDPARAM;

    default:
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }
    return E_PROP_ID_UNSUPPORTED;
}
