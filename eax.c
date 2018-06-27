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


#define EAX2LISTENERFLAGS_MASK (EAX20LISTENERFLAGS_DECAYTIMESCALE        | \
                                EAX20LISTENERFLAGS_REFLECTIONSSCALE      | \
                                EAX20LISTENERFLAGS_REFLECTIONSDELAYSCALE | \
                                EAX20LISTENERFLAGS_REVERBSCALE           | \
                                EAX20LISTENERFLAGS_REVERBDELAYSCALE      | \
                                EAX20LISTENERFLAGS_DECAYHFLIMIT)

static EAX20LISTENERPROPERTIES EAX3To2(const EAX30LISTENERPROPERTIES *props)
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

static void ApplyReverbParams(DS8Primary *prim, const EAX30LISTENERPROPERTIES *props)
{
    /* FIXME: Need to validate property values... Ignore? Clamp? Error? */
    prim->deferred.eax = *props;
    alEffectf(prim->effect, AL_REVERB_DENSITY,
        clampF(powf(props->flEnvironmentSize, 3.0f) / 16.0f, 0.0f, 1.0f)
    );
    alEffectf(prim->effect, AL_REVERB_DIFFUSION, props->flEnvironmentDiffusion);

    alEffectf(prim->effect, AL_REVERB_GAIN, mB_to_gain(props->lRoom));
    alEffectf(prim->effect, AL_REVERB_GAINHF, mB_to_gain(props->lRoomHF));

    alEffectf(prim->effect, AL_REVERB_DECAY_TIME, props->flDecayTime);
    alEffectf(prim->effect, AL_REVERB_DECAY_HFRATIO, props->flDecayHFRatio);

    alEffectf(prim->effect, AL_REVERB_REFLECTIONS_GAIN, mB_to_gain(props->lReflections));
    alEffectf(prim->effect, AL_REVERB_REFLECTIONS_DELAY, props->flReflectionsDelay);

    alEffectf(prim->effect, AL_REVERB_LATE_REVERB_GAIN, mB_to_gain(props->lReverb));
    alEffectf(prim->effect, AL_REVERB_LATE_REVERB_DELAY, props->flReverbDelay);

    alEffectf(prim->effect, AL_REVERB_AIR_ABSORPTION_GAINHF,
              mB_to_gain(props->flAirAbsorptionHF));

    alEffectf(prim->effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, props->flRoomRolloffFactor);

    alEffecti(prim->effect, AL_REVERB_DECAY_HFLIMIT,
              (props->dwFlags&EAX30LISTENERFLAGS_DECAYHFLIMIT) ?
              AL_TRUE : AL_FALSE);

    checkALError();

    prim->dirty.bit.effect = 1;
}

#define APPLY_DRY_PARAMS 1
#define APPLY_WET_PARAMS 2
static void ApplyFilterParams(DS8Buffer *buf, const EAX20BUFFERPROPERTIES *props, int apply)
{
    /* The LFRatio properties determine how much the given level applies to low
     * frequencies as well as high frequencies. Given that the high frequency
     * levels are specified relative to the low, they should increase as the
     * low frequency levels reduce.
     */
    FLOAT occl   = props->lOcclusion *       props->flOcclusionLFRatio;
    FLOAT occlhf = props->lOcclusion * (1.0f-props->flOcclusionLFRatio);

    if((apply&APPLY_DRY_PARAMS))
    {
        FLOAT obstr   = props->lObstruction *       props->flObstructionLFRatio;
        FLOAT obstrhf = props->lObstruction * (1.0f-props->flObstructionLFRatio);
        FLOAT mb   = props->lDirect   + obstr   + occl;
        FLOAT mbhf = props->lDirectHF + obstrhf + occlhf;

        alFilterf(buf->filter[0], AL_LOWPASS_GAIN, mB_to_gain(mb));
        alFilterf(buf->filter[0], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
    }
    if((apply&APPLY_WET_PARAMS))
    {
        FLOAT occlroom = props->flOcclusionRoomRatio;
        FLOAT mb   = props->lRoom   + occlroom*occl;
        FLOAT mbhf = props->lRoomHF + occlroom*occlhf;

        alFilterf(buf->filter[1], AL_LOWPASS_GAIN, mB_to_gain(mb));
        alFilterf(buf->filter[1], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
    }
    checkALError();
}


HRESULT EAX2_Set(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(prim->effect == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX20LISTENER_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX20LISTENERPROPERTIES))
        {
            union {
                const void *v;
                const EAX20LISTENERPROPERTIES *props;
            } data = { pPropData };
            EAX30LISTENERPROPERTIES props3 = REVERB_PRESET_GENERIC;
            if(data.props->dwEnvironment < EAX_ENVIRONMENT_COUNT)
            {
                props3 = EnvironmentDefaults[data.props->dwEnvironment];
                props3.dwEnvironment = data.props->dwEnvironment;
            }
            props3.flEnvironmentSize = data.props->flEnvironmentSize;
            props3.flEnvironmentDiffusion = data.props->flEnvironmentDiffusion;
            props3.lRoom = data.props->lRoom;
            props3.lRoomHF = data.props->lRoomHF;
            props3.flDecayTime = data.props->flDecayTime;
            props3.flDecayHFRatio = data.props->flDecayHFRatio;
            props3.lReflections = data.props->lReflections;
            props3.flReflectionsDelay = data.props->flReflectionsDelay;
            props3.lReverb = data.props->lReverb;
            props3.flReverbDelay = data.props->flReverbDelay;
            props3.flAirAbsorptionHF = data.props->flAirAbsorptionHF;
            props3.flRoomRolloffFactor = data.props->flRoomRolloffFactor;
            props3.dwFlags = data.props->dwFlags;

            ApplyReverbParams(prim, &props3);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ROOM:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            prim->deferred.eax.lRoom = *data.l;
            alEffectf(prim->effect, AL_REVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lRoom));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            prim->deferred.eax.lRoomHF = *data.l;
            alEffectf(prim->effect, AL_REVERB_GAINHF,
                      mB_to_gain(prim->deferred.eax.lRoomHF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flRoomRolloffFactor = *data.fl;
            alEffectf(prim->effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,
                      prim->deferred.eax.flRoomRolloffFactor);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flDecayTime = *data.fl;
            alEffectf(prim->effect, AL_REVERB_DECAY_TIME,
                      prim->deferred.eax.flDecayTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flDecayHFRatio = *data.fl;
            alEffectf(prim->effect, AL_REVERB_DECAY_HFRATIO,
                      prim->deferred.eax.flDecayHFRatio);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            prim->deferred.eax.lReflections = *data.l;
            alEffectf(prim->effect, AL_REVERB_REFLECTIONS_GAIN,
                      mB_to_gain(prim->deferred.eax.lReflections));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flReflectionsDelay = *data.fl;
            alEffectf(prim->effect, AL_REVERB_REFLECTIONS_DELAY,
                      prim->deferred.eax.flReflectionsDelay);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_REVERB:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            prim->deferred.eax.lReverb = *data.l;
            alEffectf(prim->effect, AL_REVERB_LATE_REVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lReverb));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flReverbDelay = *data.fl;
            alEffectf(prim->effect, AL_REVERB_LATE_REVERB_DELAY,
                      prim->deferred.eax.flReverbDelay);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            if(*data.dw < EAX_ENVIRONMENT_COUNT)
            {
                ApplyReverbParams(prim, &EnvironmentDefaults[*data.dw]);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };
            if(*data.fl >= 1.0f && *data.fl <= 100.0f)
            {
                float scale = (*data.fl)/prim->deferred.eax.flEnvironmentSize;

                prim->deferred.eax.flEnvironmentSize = *data.fl;

                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_DECAYTIMESCALE))
                {
                    prim->deferred.eax.flDecayTime *= scale;
                    prim->deferred.eax.flDecayTime = clampF(prim->deferred.eax.flDecayTime, 0.1f, 20.0f);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_REFLECTIONSSCALE))
                {
                    prim->deferred.eax.lReflections -= gain_to_mB(scale);
                    prim->deferred.eax.lReflections = clampI(prim->deferred.eax.lReflections, -10000, 1000);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_REFLECTIONSDELAYSCALE))
                {
                    prim->deferred.eax.flReflectionsDelay *= scale;
                    prim->deferred.eax.flReflectionsDelay = clampF(prim->deferred.eax.flReflectionsDelay, 0.0f, 0.3f);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_REVERBSCALE))
                {
                    prim->deferred.eax.lReverb -= gain_to_mB(scale);
                    prim->deferred.eax.lReverb = clampI(prim->deferred.eax.lReverb, -10000, 2000);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_REVERBDELAYSCALE))
                {
                    prim->deferred.eax.flReverbDelay *= scale;
                    prim->deferred.eax.flReverbDelay = clampF(prim->deferred.eax.flReverbDelay, 0.0f, 0.1f);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_ECHOTIMESCALE))
                {
                    prim->deferred.eax.flEchoTime *= scale;
                    prim->deferred.eax.flEchoTime = clampF(prim->deferred.eax.flEchoTime, 0.075f, 0.25f);
                }
                if((prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_MODTIMESCALE))
                {
                    prim->deferred.eax.flModulationTime *= scale;
                    prim->deferred.eax.flModulationTime = clampF(prim->deferred.eax.flModulationTime, 0.04f, 4.0f);
                }

                ApplyReverbParams(prim, &prim->deferred.eax);
                hr = DS_OK;
            }
        }
        break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flEnvironmentDiffusion = *data.fl;
            alEffectf(prim->effect, AL_REVERB_DIFFUSION,
                      prim->deferred.eax.flEnvironmentDiffusion);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flAirAbsorptionHF = *data.fl;
            alEffectf(prim->effect, AL_REVERB_AIR_ABSORPTION_GAINHF,
                      mB_to_gain(prim->deferred.eax.flAirAbsorptionHF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };

            prim->deferred.eax.dwFlags = *data.dw;
            alEffecti(prim->effect, AL_REVERB_DECAY_HFLIMIT,
                      (prim->deferred.eax.dwFlags&EAX30LISTENERFLAGS_DECAYHFLIMIT) ?
                      AL_TRUE : AL_FALSE);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled listener propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX2_Get(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(prim->effect == 0)
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
    case DSPROPERTY_EAX20LISTENER_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX20LISTENER_ALLPARAMETERS:
        GET_PROP(EAX3To2(&prim->deferred.eax), EAX20LISTENERPROPERTIES);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOM:
        GET_PROP(prim->deferred.eax.lRoom, LONG);
        break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        GET_PROP(prim->deferred.eax.lRoomHF, LONG);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        GET_PROP(prim->deferred.eax.flRoomRolloffFactor, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        GET_PROP(prim->deferred.eax.flDecayTime, FLOAT);
        break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        GET_PROP(prim->deferred.eax.flDecayHFRatio, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        GET_PROP(prim->deferred.eax.lReflections, LONG);
        break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        GET_PROP(prim->deferred.eax.flReflectionsDelay, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_REVERB:
        GET_PROP(prim->deferred.eax.lReverb, LONG);
        break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        GET_PROP(prim->deferred.eax.flReverbDelay, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
        GET_PROP(prim->deferred.eax.dwEnvironment, DWORD);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        GET_PROP(prim->deferred.eax.flEnvironmentSize, FLOAT);
        break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->deferred.eax.flEnvironmentDiffusion, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        GET_PROP(prim->deferred.eax.flAirAbsorptionHF, FLOAT);
        break;

    case DSPROPERTY_EAX20LISTENER_FLAGS:
        GET_PROP(prim->deferred.eax.dwFlags&EAX2LISTENERFLAGS_MASK, DWORD);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled listener propid: 0x%08lx\n", propid);
        break;
    }
#undef GET_PROP

    return hr;
}


HRESULT EAX2Buffer_Set(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(buf->filter[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX20BUFFER_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX20BUFFERPROPERTIES))
        {
            union {
                const void *v;
                const EAX20BUFFERPROPERTIES *props;
            } data = { pPropData };

            buf->deferred.eax = *data.props;
            ApplyFilterParams(buf, data.props, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            buf->dirty.bit.room_rolloff = 1;
            buf->dirty.bit.cone_outsidevolumehf = 1;
            buf->dirty.bit.air_absorb = 1;
            buf->dirty.bit.flags = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_DIRECT:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lDirect = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lDirectHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_ROOM:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lRoom = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lRoomHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.flRoomRolloffFactor = *data.fl;

            buf->dirty.bit.room_rolloff = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lObstruction = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.flObstructionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lOcclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.flOcclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.flOcclusionRoomRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
        if(cbPropData >= sizeof(LONG))
        {
            union { const void *v; const LONG *l; } data = { pPropData };

            buf->deferred.eax.lOutsideVolumeHF = *data.l;

            buf->dirty.bit.cone_outsidevolumehf = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.flAirAbsorptionFactor = *data.fl;

            buf->dirty.bit.air_absorb = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };

            buf->deferred.eax.dwFlags = *data.dw;

            buf->dirty.bit.flags = 1;
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled buffer propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX2Buffer_Get(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(buf->filter[0] == 0)
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
    case DSPROPERTY_EAX20BUFFER_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX20BUFFER_ALLPARAMETERS:
        GET_PROP(buf->deferred.eax, EAX20BUFFERPROPERTIES);
        break;

    case DSPROPERTY_EAX20BUFFER_DIRECT:
        GET_PROP(buf->deferred.eax.lDirect, LONG);
        break;
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
        GET_PROP(buf->deferred.eax.lDirectHF, LONG);
        break;

    case DSPROPERTY_EAX20BUFFER_ROOM:
        GET_PROP(buf->deferred.eax.lRoom, LONG);
        break;
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
        GET_PROP(buf->deferred.eax.lRoomHF, LONG);
        break;

    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
        GET_PROP(buf->deferred.eax.flRoomRolloffFactor, FLOAT);
        break;

    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
        GET_PROP(buf->deferred.eax.lObstruction, LONG);
        break;
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->deferred.eax.flObstructionLFRatio, FLOAT);
        break;

    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
        GET_PROP(buf->deferred.eax.lOcclusion, LONG);
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionLFRatio, FLOAT);
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionRoomRatio, FLOAT);
        break;

    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
        GET_PROP(buf->deferred.eax.lOutsideVolumeHF, LONG);
        break;

    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
        GET_PROP(buf->deferred.eax.flAirAbsorptionFactor, FLOAT);
        break;

    case DSPROPERTY_EAX20BUFFER_FLAGS:
        GET_PROP(buf->deferred.eax.dwFlags, DWORD);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled buffer propid: 0x%08lx\n", propid);
        break;
    }
#undef GET_PROP

    return hr;
}


HRESULT EAX1_Set(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    static const float eax1_env_dampening[EAX_ENVIRONMENT_COUNT] = {
        0.5f, 0.0f, 0.666f, 0.166f, 0.0f, 0.888f, 0.5f, 0.5f, 1.304f,
        0.332f, 0.3f, 2.0f, 0.0f, 0.638f, 0.776f, 0.472f, 0.224f, 0.472f,
        0.5f, 0.224f, 1.5f, 0.25f, 0.0f, 1.388f, 0.666f, 0.806f
    };
    HRESULT hr;

    if(prim->effect == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX1_ALL:
        if(cbPropData >= sizeof(EAX1_REVERBPROPERTIES))
        {
            union {
                const void *v;
                const EAX1_REVERBPROPERTIES *props;
            } data = { pPropData };

            if(data.props->dwEnvironment < EAX_ENVIRONMENT_COUNT)
            {
                EAX30LISTENERPROPERTIES env = EnvironmentDefaults[data.props->dwEnvironment];
                env.lRoom = gain_to_mB(data.props->fVolume);
                env.flDecayTime = data.props->fDecayTime;
                prim->deferred.eax1_dampening = data.props->fDamping;
                ApplyReverbParams(prim, &env);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX1_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };

            if(*data.dw < EAX_ENVIRONMENT_COUNT)
            {
                prim->deferred.eax1_dampening = eax1_env_dampening[*data.dw];
                ApplyReverbParams(prim, &EnvironmentDefaults[*data.dw]);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX1_VOLUME:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.lRoom = gain_to_mB(*data.fl);
            alEffectf(prim->effect, AL_REVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lRoom));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX1_DECAYTIME:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax.flDecayTime = *data.fl;
            alEffectf(prim->effect, AL_REVERB_DECAY_TIME,
                      prim->deferred.eax.flDecayTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX1_DAMPING:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            prim->deferred.eax1_dampening = *data.fl;

            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled listener propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX1_Get(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(prim->effect == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX1_ALL:
        if(cbPropData >= sizeof(EAX1_REVERBPROPERTIES))
        {
            union {
                void *v;
                EAX1_REVERBPROPERTIES *props;
            } data = { pPropData };

            data.props->dwEnvironment = prim->deferred.eax.dwEnvironment;
            data.props->fVolume = mB_to_gain(prim->deferred.eax.lRoom);
            data.props->fDecayTime = prim->deferred.eax.flDecayTime;
            data.props->fDamping = prim->deferred.eax1_dampening;

            *pcbReturned = sizeof(EAX1_REVERBPROPERTIES);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX1_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { void *v; DWORD *dw; } data = { pPropData };

            *data.dw = prim->deferred.eax.dwEnvironment;

            *pcbReturned = sizeof(DWORD);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX1_VOLUME:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { void *v; FLOAT *fl; } data = { pPropData };

            *data.fl = mB_to_gain(prim->deferred.eax.lRoom);

            *pcbReturned = sizeof(FLOAT);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX1_DECAYTIME:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { void *v; FLOAT *fl; } data = { pPropData };

            *data.fl = prim->deferred.eax.flDecayTime;

            *pcbReturned = sizeof(FLOAT);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX1_DAMPING:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { void *v; FLOAT *fl; } data = { pPropData };

            *data.fl = prim->deferred.eax1_dampening;

            *pcbReturned = sizeof(FLOAT);
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled listener propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX1Buffer_Set(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(buf->filter[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    /* NOTE: DSPROPERTY_EAX1BUFFER_ALL is for EAX1BUFFER_REVERBPROPERTIES,
     * however that struct just contains the single ReverbMix float property.
     */
    case DSPROPERTY_EAX1BUFFER_ALL:
    case DSPROPERTY_EAX1BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { const void *v; const FLOAT *fl; } data = { pPropData };

            buf->deferred.eax.lRoom = gain_to_mB(*data.fl);
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled buffer propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}

HRESULT EAX1Buffer_Get(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr;

    if(buf->filter[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX1BUFFER_ALL:
    case DSPROPERTY_EAX1BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(FLOAT))
        {
            union { void *v; FLOAT *fl; } data = { pPropData };

            *data.fl = mB_to_gain(buf->deferred.eax.lRoom);
            *pcbReturned = sizeof(FLOAT);
            hr = DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled buffer propid: 0x%08lx\n", propid);
        break;
    }

    return hr;
}
