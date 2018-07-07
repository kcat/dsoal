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


static void ApplyReverbParams(DS8Primary *prim, const EAX30LISTENERPROPERTIES *props)
{
    /* FIXME: Need to validate property values... Ignore? Clamp? Error? */
    prim->deferred.eax = *props;
    alEffectf(prim->effect, AL_EAXREVERB_DENSITY,
        clampF(powf(props->flEnvironmentSize, 3.0f) / 16.0f, 0.0f, 1.0f)
    );
    alEffectf(prim->effect, AL_EAXREVERB_DIFFUSION, props->flEnvironmentDiffusion);

    alEffectf(prim->effect, AL_EAXREVERB_GAIN, mB_to_gain(props->lRoom));
    alEffectf(prim->effect, AL_EAXREVERB_GAINHF, mB_to_gain(props->lRoomHF));
    alEffectf(prim->effect, AL_EAXREVERB_GAINLF, mB_to_gain(props->lRoomLF));

    alEffectf(prim->effect, AL_EAXREVERB_DECAY_TIME, props->flDecayTime);
    alEffectf(prim->effect, AL_EAXREVERB_DECAY_HFRATIO, props->flDecayHFRatio);
    alEffectf(prim->effect, AL_EAXREVERB_DECAY_LFRATIO, props->flDecayLFRatio);

    /* NOTE: Imprecision can cause some converted volume levels to land outside
     * EFX's gain limits (e.g. EAX's +1000mB volume limit gets converted to
     * 3.162something, while EFX defines the limit as 3.16; close enough for
     * practical uses, but still technically an error).
     */
    alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_GAIN,
        clampF(mB_to_gain(props->lReflections), AL_EAXREVERB_MIN_REFLECTIONS_GAIN,
               AL_EAXREVERB_MAX_REFLECTIONS_GAIN)
    );
    alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_DELAY, props->flReflectionsDelay);
    alEffectfv(prim->effect, AL_EAXREVERB_REFLECTIONS_PAN, &props->vReflectionsPan.x);

    alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_GAIN,
        clampF(mB_to_gain(props->lReverb), AL_EAXREVERB_MIN_LATE_REVERB_GAIN,
               AL_EAXREVERB_MAX_LATE_REVERB_GAIN)
    );
    alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_DELAY, props->flReverbDelay);
    alEffectfv(prim->effect, AL_EAXREVERB_LATE_REVERB_PAN, &props->vReverbPan.x);

    alEffectf(prim->effect, AL_EAXREVERB_ECHO_TIME, props->flEchoTime);
    alEffectf(prim->effect, AL_EAXREVERB_ECHO_DEPTH, props->flEchoDepth);

    alEffectf(prim->effect, AL_EAXREVERB_MODULATION_TIME, props->flModulationTime);
    alEffectf(prim->effect, AL_EAXREVERB_MODULATION_DEPTH, props->flModulationDepth);

    alEffectf(prim->effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF,
        clampF(mB_to_gain(props->flAirAbsorptionHF), AL_EAXREVERB_MIN_AIR_ABSORPTION_GAINHF,
               AL_EAXREVERB_MAX_AIR_ABSORPTION_GAINHF)
    );

    alEffectf(prim->effect, AL_EAXREVERB_HFREFERENCE, props->flHFReference);
    alEffectf(prim->effect, AL_EAXREVERB_LFREFERENCE, props->flLFReference);

    alEffectf(prim->effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, props->flRoomRolloffFactor);

    alEffecti(prim->effect, AL_EAXREVERB_DECAY_HFLIMIT,
              (props->dwFlags&EAX30LISTENERFLAGS_DECAYHFLIMIT) ?
              AL_TRUE : AL_FALSE);

    checkALError();

    prim->dirty.bit.effect = 1;
}

#define APPLY_DRY_PARAMS 1
#define APPLY_WET_PARAMS 2
static void ApplyFilterParams(DS8Buffer *buf, const EAX30BUFFERPROPERTIES *props, int apply)
{
    /* The LFRatio properties determine how much the given level applies to low
     * frequencies as well as high frequencies. Given that the high frequency
     * levels are specified relative to the low, they should increase as the
     * low frequency levels reduce.
     */
    float occl   = props->lOcclusion *       props->flOcclusionLFRatio;
    float occlhf = props->lOcclusion * (1.0f-props->flOcclusionLFRatio);

    if((apply&APPLY_DRY_PARAMS))
    {
        float obstr   = props->lObstruction *       props->flObstructionLFRatio;
        float obstrhf = props->lObstruction * (1.0f-props->flObstructionLFRatio);
        float occldirect = props->flOcclusionDirectRatio;
        float mb   = props->lDirect   + obstr   + occldirect*occl;
        float mbhf = props->lDirectHF + obstrhf + occldirect*occlhf;

        alFilterf(buf->filter[0], AL_LOWPASS_GAIN, clampF(mB_to_gain(mb), 0.0f, 1.0f));
        alFilterf(buf->filter[0], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
    }
    if((apply&APPLY_WET_PARAMS))
    {
        float excl   = props->lExclusion *       props->flExclusionLFRatio;
        float exclhf = props->lExclusion * (1.0f-props->flExclusionLFRatio);
        float occlroom = props->flOcclusionRoomRatio;
        float mb   = props->lRoom   + excl   + occlroom*occl;
        float mbhf = props->lRoomHF + exclhf + occlroom*occlhf;

        alFilterf(buf->filter[1], AL_LOWPASS_GAIN, clampF(mB_to_gain(mb), 0.0f, 1.0f));
        alFilterf(buf->filter[1], AL_LOWPASS_GAINHF, mB_to_gain(mbhf));
    }
    checkALError();
}


static void RescaleEnvSize(EAX30LISTENERPROPERTIES *props, float newsize)
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
        props->lReverb -= gain_to_mB(scale);
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


/*******************
 * EAX 3 stuff
 ******************/

static EAXOBSTRUCTIONPROPERTIES EAX3BufferObstruction(const EAX30BUFFERPROPERTIES *props)
{
    EAXOBSTRUCTIONPROPERTIES ret;
    ret.lObstruction = props->lObstruction;
    ret.flObstructionLFRatio = props->flObstructionLFRatio;
    return ret;
}

static EAXOCCLUSIONPROPERTIES EAX3BufferOcclusion(const EAX30BUFFERPROPERTIES *props)
{
    EAXOCCLUSIONPROPERTIES ret;
    ret.lOcclusion = props->lOcclusion;
    ret.flOcclusionLFRatio = props->flOcclusionLFRatio;
    ret.flOcclusionRoomRatio = props->flOcclusionRoomRatio;
    ret.flOcclusionDirectRatio = props->flOcclusionDirectRatio;
    return ret;
}

static EAXEXCLUSIONPROPERTIES EAX3BufferExclusion(const EAX30BUFFERPROPERTIES *props)
{
    EAXEXCLUSIONPROPERTIES ret;
    ret.lExclusion = props->lExclusion;
    ret.flExclusionLFRatio = props->flExclusionLFRatio;
    return ret;
}

HRESULT EAX3_Set(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(prim->effect == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX30LISTENER_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX30LISTENER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX30LISTENERPROPERTIES))
        {
            union {
                const void *v;
                const EAX30LISTENERPROPERTIES *props;
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

            /* FIXME: There's some unknown behavior here. When RightMark3DSound
             * deals with environment panning and morphing, the current preset
             * ID will sometimes be repeated, which seems to expect some
             * parameters to stay unmodified. Other cases see preset ID 26,
             * which is out of range. I'm not sure how these values are
             * supposed to be treated.
             */
            ApplyReverbParams(prim, data.props);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            if(*data.dw < EAX_ENVIRONMENT_UNDEFINED)
            {
                ApplyReverbParams(prim, &EnvironmentDefaults[*data.dw]);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTSIZE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            if(*data.fl >= 1.0f && *data.fl <= 100.0f)
            {
                RescaleEnvSize(&prim->deferred.eax, *data.fl);

                ApplyReverbParams(prim, &prim->deferred.eax);
                hr = DS_OK;
            }
        }
        break;
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTDIFFUSION:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flEnvironmentDiffusion = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DIFFUSION,
                      prim->deferred.eax.flEnvironmentDiffusion);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_ROOM:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lRoom = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lRoom));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lRoomHF = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_GAINHF,
                      mB_to_gain(prim->deferred.eax.lRoomHF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_ROOMLF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lRoomLF = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_GAINLF,
                      mB_to_gain(prim->deferred.eax.lRoomLF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_DECAYTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flDecayTime = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_TIME,
                      prim->deferred.eax.flDecayTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_DECAYHFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flDecayHFRatio = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_HFRATIO,
                      prim->deferred.eax.flDecayHFRatio);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_DECAYLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flDecayLFRatio = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_LFRATIO,
                      prim->deferred.eax.flDecayLFRatio);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_REFLECTIONS:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lReflections = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_GAIN,
                      mB_to_gain(prim->deferred.eax.lReflections));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flReflectionsDelay = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_DELAY,
                      prim->deferred.eax.flReflectionsDelay);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSPAN:
        if(cbPropData >= sizeof(EAXVECTOR))
        {
            union { const void *v; const EAXVECTOR *vec; } data = { pPropData };

            prim->deferred.eax.vReflectionsPan = *data.vec;
            alEffectfv(prim->effect, AL_EAXREVERB_REFLECTIONS_PAN,
                       &prim->deferred.eax.vReflectionsPan.x);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_REVERB:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lReverb = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lReverb));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_REVERBDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flReverbDelay = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_DELAY,
                      prim->deferred.eax.flReverbDelay);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_REVERBPAN:
        if(cbPropData >= sizeof(EAXVECTOR))
        {
            union { const void *v; const EAXVECTOR *vec; } data = { pPropData };

            prim->deferred.eax.vReverbPan = *data.vec;
            alEffectfv(prim->effect, AL_EAXREVERB_LATE_REVERB_PAN,
                       &prim->deferred.eax.vReverbPan.x);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_ECHOTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flEchoTime = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_ECHO_TIME,
                      prim->deferred.eax.flEchoTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_ECHODEPTH:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flEchoDepth = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_ECHO_DEPTH,
                      prim->deferred.eax.flEchoDepth);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_MODULATIONTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flModulationTime = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_MODULATION_TIME,
                      prim->deferred.eax.flModulationTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_MODULATIONDEPTH:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flModulationDepth = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_MODULATION_DEPTH,
                      prim->deferred.eax.flModulationDepth);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_AIRABSORPTIONHF:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flAirAbsorptionHF = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF,
                      mB_to_gain(prim->deferred.eax.flAirAbsorptionHF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_HFREFERENCE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flHFReference = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_HFREFERENCE,
                      prim->deferred.eax.flHFReference);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30LISTENER_LFREFERENCE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flLFReference = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_LFREFERENCE,
                      prim->deferred.eax.flLFReference);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flRoomRolloffFactor = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR,
                      prim->deferred.eax.flRoomRolloffFactor);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30LISTENER_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };

            prim->deferred.eax.dwFlags = *data.dw;
            alEffecti(prim->effect, AL_EAXREVERB_DECAY_HFLIMIT,
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

HRESULT EAX3_Get(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
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
    case DSPROPERTY_EAX30LISTENER_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX30LISTENER_ALLPARAMETERS:
        GET_PROP(prim->deferred.eax, EAX30LISTENERPROPERTIES);
        break;

    case DSPROPERTY_EAX30LISTENER_ENVIRONMENT:
        GET_PROP(prim->deferred.eax.dwEnvironment, DWORD);
        break;

    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTSIZE:
        GET_PROP(prim->deferred.eax.flEnvironmentSize, float);
        break;
    case DSPROPERTY_EAX30LISTENER_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->deferred.eax.flEnvironmentDiffusion, float);
        break;

    case DSPROPERTY_EAX30LISTENER_ROOM:
        GET_PROP(prim->deferred.eax.lRoom, long);
        break;
    case DSPROPERTY_EAX30LISTENER_ROOMHF:
        GET_PROP(prim->deferred.eax.lRoomHF, long);
        break;
    case DSPROPERTY_EAX30LISTENER_ROOMLF:
        GET_PROP(prim->deferred.eax.lRoomLF, long);
        break;

    case DSPROPERTY_EAX30LISTENER_DECAYTIME:
        GET_PROP(prim->deferred.eax.flDecayTime, float);
        break;
    case DSPROPERTY_EAX30LISTENER_DECAYHFRATIO:
        GET_PROP(prim->deferred.eax.flDecayHFRatio, float);
        break;
    case DSPROPERTY_EAX30LISTENER_DECAYLFRATIO:
        GET_PROP(prim->deferred.eax.flDecayLFRatio, float);
        break;

    case DSPROPERTY_EAX30LISTENER_REFLECTIONS:
        GET_PROP(prim->deferred.eax.lReflections, long);
        break;
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSDELAY:
        GET_PROP(prim->deferred.eax.flReflectionsDelay, float);
        break;
    case DSPROPERTY_EAX30LISTENER_REFLECTIONSPAN:
        GET_PROP(prim->deferred.eax.vReflectionsPan, EAXVECTOR);
        break;

    case DSPROPERTY_EAX30LISTENER_REVERB:
        GET_PROP(prim->deferred.eax.lReverb, long);
        break;
    case DSPROPERTY_EAX30LISTENER_REVERBDELAY:
        GET_PROP(prim->deferred.eax.flReverbDelay, float);
        break;
    case DSPROPERTY_EAX30LISTENER_REVERBPAN:
        GET_PROP(prim->deferred.eax.vReverbPan, EAXVECTOR);
        break;

    case DSPROPERTY_EAX30LISTENER_ECHOTIME:
        GET_PROP(prim->deferred.eax.flEchoTime, float);
        break;
    case DSPROPERTY_EAX30LISTENER_ECHODEPTH:
        GET_PROP(prim->deferred.eax.flEchoDepth, float);
        break;

    case DSPROPERTY_EAX30LISTENER_MODULATIONTIME:
        GET_PROP(prim->deferred.eax.flModulationTime, float);
        break;
    case DSPROPERTY_EAX30LISTENER_MODULATIONDEPTH:
        GET_PROP(prim->deferred.eax.flModulationDepth, float);
        break;

    case DSPROPERTY_EAX30LISTENER_AIRABSORPTIONHF:
        GET_PROP(prim->deferred.eax.flAirAbsorptionHF, float);
        break;

    case DSPROPERTY_EAX30LISTENER_HFREFERENCE:
        GET_PROP(prim->deferred.eax.flHFReference, float);
        break;
    case DSPROPERTY_EAX30LISTENER_LFREFERENCE:
        GET_PROP(prim->deferred.eax.flLFReference, float);
        break;

    case DSPROPERTY_EAX30LISTENER_ROOMROLLOFFFACTOR:
        GET_PROP(prim->deferred.eax.flRoomRolloffFactor, float);
        break;

    case DSPROPERTY_EAX30LISTENER_FLAGS:
        GET_PROP(prim->deferred.eax.dwFlags, DWORD);
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled listener propid: 0x%08lx\n", propid);
        break;
    }
#undef GET_PROP

    return hr;
}

HRESULT EAX3Buffer_Set(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr;

    if(buf->filter[0] == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case DSPROPERTY_EAX30BUFFER_NONE: /* not setting any property, just applying */
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX30BUFFER_ALLPARAMETERS:
        if(cbPropData >= sizeof(EAX30BUFFERPROPERTIES))
        {
            union {
                const void *v;
                const EAX30BUFFERPROPERTIES *props;
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
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONPARAMETERS:
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
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONPARAMETERS:
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
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONPARAMETERS:
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
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_DIRECT:
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
    case DSPROPERTY_EAX30BUFFER_DIRECTHF:
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

    case DSPROPERTY_EAX30BUFFER_ROOM:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room: %ld\n", *data.l);

            buf->deferred.eax.lRoom = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room HF: %ld\n", *data.l);

            buf->deferred.eax.lRoomHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_OBSTRUCTION:
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
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONLFRATIO:
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

    case DSPROPERTY_EAX30BUFFER_OCCLUSION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Occlusion: %ld\n", *data.l);

            buf->deferred.eax.lOcclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONROOMRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion Room Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionRoomRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONDIRECTRATIO:
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

    case DSPROPERTY_EAX30BUFFER_EXCLUSION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Exclusion: %ld\n", *data.l);

            buf->deferred.eax.lExclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Exclusion LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flExclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_OUTSIDEVOLUMEHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Outisde Volume HF: %ld\n", *data.l);

            buf->deferred.eax.lOutsideVolumeHF = *data.l;

            buf->dirty.bit.cone_outsidevolumehf = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_DOPPLERFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Doppler Factor: %f\n", *data.fl);

            buf->deferred.eax.flDopplerFactor = *data.fl;

            buf->dirty.bit.doppler = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_ROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Rolloff Factor: %f\n", *data.fl);

            buf->deferred.eax.flRolloffFactor = *data.fl;

            buf->dirty.bit.rolloff = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Room Rolloff Factor: %f\n", *data.fl);

            buf->deferred.eax.flRoomRolloffFactor = *data.fl;

            buf->dirty.bit.room_rolloff = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_AIRABSORPTIONFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Air Absorb Factor: %f\n", *data.fl);

            buf->deferred.eax.flAirAbsorptionFactor = *data.fl;

            buf->dirty.bit.air_absorb = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX30BUFFER_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Flags: 0x%lx\n", *data.dw);

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

HRESULT EAX3Buffer_Get(DS8Buffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
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
    case DSPROPERTY_EAX30BUFFER_NONE:
        *pcbReturned = 0;
        hr = DS_OK;
        break;

    case DSPROPERTY_EAX30BUFFER_ALLPARAMETERS:
        GET_PROP(buf->deferred.eax, EAX30BUFFERPROPERTIES);
        break;
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONPARAMETERS:
        GET_PROP(EAX3BufferObstruction(&buf->deferred.eax), EAXOBSTRUCTIONPROPERTIES);
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONPARAMETERS:
        GET_PROP(EAX3BufferOcclusion(&buf->deferred.eax), EAXOCCLUSIONPROPERTIES);
        break;
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONPARAMETERS:
        GET_PROP(EAX3BufferExclusion(&buf->deferred.eax), EAXEXCLUSIONPROPERTIES);
        break;

    case DSPROPERTY_EAX30BUFFER_DIRECT:
        GET_PROP(buf->deferred.eax.lDirect, long);
        break;
    case DSPROPERTY_EAX30BUFFER_DIRECTHF:
        GET_PROP(buf->deferred.eax.lDirectHF, long);
        break;

    case DSPROPERTY_EAX30BUFFER_ROOM:
        GET_PROP(buf->deferred.eax.lRoom, long);
        break;
    case DSPROPERTY_EAX30BUFFER_ROOMHF:
        GET_PROP(buf->deferred.eax.lRoomHF, long);
        break;

    case DSPROPERTY_EAX30BUFFER_OBSTRUCTION:
        GET_PROP(buf->deferred.eax.lObstruction, long);
        break;
    case DSPROPERTY_EAX30BUFFER_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->deferred.eax.flObstructionLFRatio, float);
        break;

    case DSPROPERTY_EAX30BUFFER_OCCLUSION:
        GET_PROP(buf->deferred.eax.lOcclusion, long);
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONLFRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionLFRatio, float);
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONROOMRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionRoomRatio, float);
        break;
    case DSPROPERTY_EAX30BUFFER_OCCLUSIONDIRECTRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionDirectRatio, float);
        break;

    case DSPROPERTY_EAX30BUFFER_EXCLUSION:
        GET_PROP(buf->deferred.eax.lExclusion, long);
        break;
    case DSPROPERTY_EAX30BUFFER_EXCLUSIONLFRATIO:
        GET_PROP(buf->deferred.eax.flExclusionLFRatio, float);
        break;

    case DSPROPERTY_EAX30BUFFER_OUTSIDEVOLUMEHF:
        GET_PROP(buf->deferred.eax.lOutsideVolumeHF, long);
        break;

    case DSPROPERTY_EAX30BUFFER_DOPPLERFACTOR:
        GET_PROP(buf->deferred.eax.flDopplerFactor, float);
        break;

    case DSPROPERTY_EAX30BUFFER_ROLLOFFFACTOR:
        GET_PROP(buf->deferred.eax.flRolloffFactor, float);
        break;
    case DSPROPERTY_EAX30BUFFER_ROOMROLLOFFFACTOR:
        GET_PROP(buf->deferred.eax.flRoomRolloffFactor, float);
        break;

    case DSPROPERTY_EAX30BUFFER_AIRABSORPTIONFACTOR:
        GET_PROP(buf->deferred.eax.flAirAbsorptionFactor, float);
        break;

    case DSPROPERTY_EAX30BUFFER_FLAGS:
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


/*******************
 * EAX 2 stuff
 ******************/

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

static EAX20BUFFERPROPERTIES EAXBuffer3To2(const EAX30BUFFERPROPERTIES *props)
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
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lRoom = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lRoom));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lRoomHF = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_GAINHF,
                      mB_to_gain(prim->deferred.eax.lRoomHF));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flRoomRolloffFactor = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR,
                      prim->deferred.eax.flRoomRolloffFactor);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flDecayTime = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_TIME,
                      prim->deferred.eax.flDecayTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flDecayHFRatio = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_HFRATIO,
                      prim->deferred.eax.flDecayHFRatio);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lReflections = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_GAIN,
                      mB_to_gain(prim->deferred.eax.lReflections));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flReflectionsDelay = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_REFLECTIONS_DELAY,
                      prim->deferred.eax.flReflectionsDelay);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_REVERB:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };

            prim->deferred.eax.lReverb = *data.l;
            alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_GAIN,
                      mB_to_gain(prim->deferred.eax.lReverb));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flReverbDelay = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_LATE_REVERB_DELAY,
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
            if(*data.dw < EAX_ENVIRONMENT_UNDEFINED)
            {
                ApplyReverbParams(prim, &EnvironmentDefaults[*data.dw]);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            if(*data.fl >= 1.0f && *data.fl <= 100.0f)
            {
                RescaleEnvSize(&prim->deferred.eax, *data.fl);

                ApplyReverbParams(prim, &prim->deferred.eax);
                hr = DS_OK;
            }
        }
        break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flEnvironmentDiffusion = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DIFFUSION,
                      prim->deferred.eax.flEnvironmentDiffusion);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };

            prim->deferred.eax.flAirAbsorptionHF = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF,
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
            alEffecti(prim->effect, AL_EAXREVERB_DECAY_HFLIMIT,
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
        GET_PROP(prim->deferred.eax.lRoom, long);
        break;
    case DSPROPERTY_EAX20LISTENER_ROOMHF:
        GET_PROP(prim->deferred.eax.lRoomHF, long);
        break;

    case DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR:
        GET_PROP(prim->deferred.eax.flRoomRolloffFactor, float);
        break;

    case DSPROPERTY_EAX20LISTENER_DECAYTIME:
        GET_PROP(prim->deferred.eax.flDecayTime, float);
        break;
    case DSPROPERTY_EAX20LISTENER_DECAYHFRATIO:
        GET_PROP(prim->deferred.eax.flDecayHFRatio, float);
        break;

    case DSPROPERTY_EAX20LISTENER_REFLECTIONS:
        GET_PROP(prim->deferred.eax.lReflections, long);
        break;
    case DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY:
        GET_PROP(prim->deferred.eax.flReflectionsDelay, float);
        break;

    case DSPROPERTY_EAX20LISTENER_REVERB:
        GET_PROP(prim->deferred.eax.lReverb, long);
        break;
    case DSPROPERTY_EAX20LISTENER_REVERBDELAY:
        GET_PROP(prim->deferred.eax.flReverbDelay, float);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENT:
        GET_PROP(prim->deferred.eax.dwEnvironment, DWORD);
        break;

    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE:
        GET_PROP(prim->deferred.eax.flEnvironmentSize, float);
        break;
    case DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION:
        GET_PROP(prim->deferred.eax.flEnvironmentDiffusion, float);
        break;

    case DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF:
        GET_PROP(prim->deferred.eax.flAirAbsorptionHF, float);
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

            buf->deferred.eax.lDirect = data.props->lDirect;
            buf->deferred.eax.lDirectHF = data.props->lDirectHF;
            buf->deferred.eax.lRoom = data.props->lRoom;
            buf->deferred.eax.lRoomHF = data.props->lRoomHF;
            buf->deferred.eax.flRoomRolloffFactor = data.props->flRoomRolloffFactor;
            buf->deferred.eax.lObstruction = data.props->lObstruction;
            buf->deferred.eax.flObstructionLFRatio = data.props->flObstructionLFRatio;
            buf->deferred.eax.lOcclusion = data.props->lOcclusion;
            buf->deferred.eax.flOcclusionLFRatio = data.props->flOcclusionLFRatio;
            buf->deferred.eax.flOcclusionRoomRatio = data.props->flOcclusionRoomRatio;
            buf->deferred.eax.lOutsideVolumeHF = data.props->lOutsideVolumeHF;
            buf->deferred.eax.flAirAbsorptionFactor = data.props->flAirAbsorptionFactor;
            buf->deferred.eax.dwFlags = data.props->dwFlags;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

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
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
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

    case DSPROPERTY_EAX20BUFFER_ROOM:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room: %ld\n", *data.l);

            buf->deferred.eax.lRoom = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Room HF: %ld\n", *data.l);

            buf->deferred.eax.lRoomHF = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Room Rolloff Factor: %f\n", *data.fl);

            buf->deferred.eax.flRoomRolloffFactor = *data.fl;

            buf->dirty.bit.room_rolloff = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
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
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
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

    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Occlusion: %ld\n", *data.l);

            buf->deferred.eax.lOcclusion = *data.l;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion LF Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionLFRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_DRY_PARAMS|APPLY_WET_PARAMS);

            buf->dirty.bit.dry_filter = 1;
            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Occlusion Room Ratio: %f\n", *data.fl);

            buf->deferred.eax.flOcclusionRoomRatio = *data.fl;
            ApplyFilterParams(buf, &buf->deferred.eax, APPLY_WET_PARAMS);

            buf->dirty.bit.wet_filter = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
        if(cbPropData >= sizeof(long))
        {
            union { const void *v; const long *l; } data = { pPropData };
            TRACE("Outisde Volume HF: %ld\n", *data.l);

            buf->deferred.eax.lOutsideVolumeHF = *data.l;

            buf->dirty.bit.cone_outsidevolumehf = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Air Absorb Factor: %f\n", *data.fl);

            buf->deferred.eax.flAirAbsorptionFactor = *data.fl;

            buf->dirty.bit.air_absorb = 1;
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX20BUFFER_FLAGS:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Flags: 0x%lx\n", *data.dw);

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
        GET_PROP(EAXBuffer3To2(&buf->deferred.eax), EAX20BUFFERPROPERTIES);
        break;

    case DSPROPERTY_EAX20BUFFER_DIRECT:
        GET_PROP(buf->deferred.eax.lDirect, long);
        break;
    case DSPROPERTY_EAX20BUFFER_DIRECTHF:
        GET_PROP(buf->deferred.eax.lDirectHF, long);
        break;

    case DSPROPERTY_EAX20BUFFER_ROOM:
        GET_PROP(buf->deferred.eax.lRoom, long);
        break;
    case DSPROPERTY_EAX20BUFFER_ROOMHF:
        GET_PROP(buf->deferred.eax.lRoomHF, long);
        break;

    case DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR:
        GET_PROP(buf->deferred.eax.flRoomRolloffFactor, float);
        break;

    case DSPROPERTY_EAX20BUFFER_OBSTRUCTION:
        GET_PROP(buf->deferred.eax.lObstruction, long);
        break;
    case DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO:
        GET_PROP(buf->deferred.eax.flObstructionLFRatio, float);
        break;

    case DSPROPERTY_EAX20BUFFER_OCCLUSION:
        GET_PROP(buf->deferred.eax.lOcclusion, long);
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionLFRatio, float);
        break;
    case DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO:
        GET_PROP(buf->deferred.eax.flOcclusionRoomRatio, float);
        break;

    case DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF:
        GET_PROP(buf->deferred.eax.lOutsideVolumeHF, long);
        break;

    case DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR:
        GET_PROP(buf->deferred.eax.flAirAbsorptionFactor, float);
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


/*******************
 * EAX 1 stuff
 ******************/

HRESULT EAX1_Set(DS8Primary *prim, DWORD propid, void *pPropData, ULONG cbPropData)
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

    if(prim->effect == 0)
        return E_PROP_ID_UNSUPPORTED;

    hr = DSERR_INVALIDPARAM;
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
                EAX30LISTENERPROPERTIES env = EnvironmentDefaults[data.props->dwEnvironment];
                long db_vol = clampI(
                    gain_to_mB(data.props->fVolume / eax1_env_volume[data.props->dwEnvironment]),
                    -10000, 10000
                );
                env.lRoom = clampI(env.lRoom + db_vol, -10000, 0);
                env.flDecayTime = data.props->fDecayTime;
                prim->deferred.eax1_volume = data.props->fVolume;
                prim->deferred.eax1_dampening = data.props->fDamping;
                ApplyReverbParams(prim, &env);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX10LISTENER_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { const void *v; const DWORD *dw; } data = { pPropData };
            TRACE("Environment: %lu\n", *data.dw);

            if(*data.dw < EAX_ENVIRONMENT_UNDEFINED)
            {
                prim->deferred.eax1_volume = eax1_env_volume[*data.dw];
                prim->deferred.eax1_dampening = eax1_env_dampening[*data.dw];
                ApplyReverbParams(prim, &EnvironmentDefaults[*data.dw]);
                hr = DS_OK;
            }
        }
        break;

    case DSPROPERTY_EAX10LISTENER_VOLUME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            long db_vol = clampI(
                gain_to_mB(*data.fl / eax1_env_volume[prim->deferred.eax.dwEnvironment]),
                -10000, 10000
            );
            long room_vol = clampI(
                EnvironmentDefaults[prim->deferred.eax.dwEnvironment].lRoom + db_vol,
                -10000, 0
            );
            TRACE("Volume: %f\n", *data.fl);

            prim->deferred.eax.lRoom = room_vol;
            prim->deferred.eax1_volume = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_GAIN, mB_to_gain(room_vol));
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX10LISTENER_DECAYTIME:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Decay Time: %f\n", *data.fl);

            prim->deferred.eax.flDecayTime = *data.fl;
            alEffectf(prim->effect, AL_EAXREVERB_DECAY_TIME,
                      prim->deferred.eax.flDecayTime);
            checkALError();

            prim->dirty.bit.effect = 1;
            hr = DS_OK;
        }
        break;
    case DSPROPERTY_EAX10LISTENER_DAMPING:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Damping: %f\n", *data.fl);

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
    case DSPROPERTY_EAX10LISTENER_ALL:
        if(cbPropData >= sizeof(EAX10LISTENERPROPERTIES))
        {
            union {
                void *v;
                EAX10LISTENERPROPERTIES *props;
            } data = { pPropData };

            data.props->dwEnvironment = prim->deferred.eax.dwEnvironment;
            data.props->fVolume = prim->deferred.eax1_volume;
            data.props->fDecayTime = prim->deferred.eax.flDecayTime;
            data.props->fDamping = prim->deferred.eax1_dampening;

            *pcbReturned = sizeof(EAX10LISTENERPROPERTIES);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX10LISTENER_ENVIRONMENT:
        if(cbPropData >= sizeof(DWORD))
        {
            union { void *v; DWORD *dw; } data = { pPropData };

            *data.dw = prim->deferred.eax.dwEnvironment;

            *pcbReturned = sizeof(DWORD);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX10LISTENER_VOLUME:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };

            *data.fl = prim->deferred.eax1_volume;

            *pcbReturned = sizeof(float);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX10LISTENER_DECAYTIME:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };

            *data.fl = prim->deferred.eax.flDecayTime;

            *pcbReturned = sizeof(float);
            hr = DS_OK;
        }
        break;

    case DSPROPERTY_EAX10LISTENER_DAMPING:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };

            *data.fl = prim->deferred.eax1_dampening;

            *pcbReturned = sizeof(float);
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
    /* NOTE: DSPROPERTY_EAX10BUFFER_ALL is for EAX10BUFFERPROPERTIES, however
     * that struct just contains the single ReverbMix float property.
     */
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(float))
        {
            union { const void *v; const float *fl; } data = { pPropData };
            TRACE("Reverb Mix: %f\n", *data.fl);

            buf->deferred.eax.lRoom = gain_to_mB(*data.fl);
            buf->deferred.eax1_reverbmix = *data.fl;
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
    case DSPROPERTY_EAX10BUFFER_ALL:
    case DSPROPERTY_EAX10BUFFER_REVERBMIX:
        if(cbPropData >= sizeof(float))
        {
            union { void *v; float *fl; } data = { pPropData };

            *data.fl = buf->deferred.eax1_reverbmix;
            *pcbReturned = sizeof(float);
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
