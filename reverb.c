/* EAX reverb interface
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


static void ApplyReverbParams(ALuint effect, const EAXREVERBPROPERTIES *props)
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

static void RescaleEnvSize(EAXREVERBPROPERTIES *props, float newsize)
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


HRESULT EAXReverb_Set(DSPrimary *prim, LONG idx, DWORD propid, void *pPropData, ULONG cbPropData)
{
    HRESULT hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXREVERB_NONE: /* not setting any property, just applying */
        return DS_OK;

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
            return DS_OK;
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
                return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
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
            return DS_OK;
        }
        break;

    default:
        hr = E_PROP_ID_UNSUPPORTED;
        FIXME("Unhandled propid: 0x%08lx\n", propid);
    }

    return hr;
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

HRESULT EAXReverb_Get(DSPrimary *prim, DWORD idx, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned)
{
    HRESULT hr = DSERR_INVALIDPARAM;
    switch(propid)
    {
    case EAXREVERB_NONE:
        *pcbReturned = 0;
        return DS_OK;

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
    }

    return hr;
}
