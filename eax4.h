/*  			DirectSound
 *
 * Copyright 2018 Chris Robinson
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

#ifndef EAX4_H
#define EAX4_H

#ifndef EAXVECTOR_DEFINED
#define EAXVECTOR_DEFINED
typedef struct _EAXVECTOR {
    float x, y, z;
} EAXVECTOR;
#endif

/* EAX 4.0 seems to support 4 FXSlots (auxiliary effect slots), while each
 * buffer/source supports 2 Active FXSlots (auxiliary sends). 
 */
#define EAX_MAX_FXSLOTS 4
#define EAX_MAX_ACTIVE_FXSLOTS 2

/* Used by EAXFXSLOT_LOADEFFECT, EAXCONTEXT_PRIMARYFXSLOTID, and EAXSOURCE_ACTIVEFXSLOTID */
DEFINE_GUID(EAX_NULL_GUID, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);


DEFINE_GUID(EAXPROPERTYID_EAX40_Context, 0x1d4870ad, 0xdef, 0x43c0, 0xa4, 0xc, 0x52, 0x36, 0x32, 0x29, 0x63, 0x42);
typedef enum {
    EAXCONTEXT_NONE = 0,
    EAXCONTEXT_ALLPARAMETERS,
    EAXCONTEXT_PRIMARYFXSLOTID,
    EAXCONTEXT_DISTANCEFACTOR,
    EAXCONTEXT_AIRABSORPTIONHF,
    EAXCONTEXT_HFREFERENCE,
    EAXCONTEXT_LASTERROR /* query only, auto-reset? */
} EAXCONTEXT_PROPERTY;

#define EAXCONTEXT_PARAMETER_IMMEDIATE 0x00000000
#define EAXCONTEXT_PARAMETER_DEFERRED  0x80000000
#define EAXCONTEXT_PARAMETER_COMMITDEFERREDSETTINGS (EAXCONTEXT_NONE | EAXCONTEXT_PARAMETER_IMMEDIATE)

/* EAXCONTEXT_ALLPARAMETERS */
typedef struct _EAXCONTEXTPROPERTIES {
    GUID  guidPrimaryFXSlotID;
    /* TODO: Does this affect EAX properties only (e.g. initial reverb decay,
     * air absorption)? Or is it an alias for DS3D's setting?
     */
    float flDistanceFactor;
    /* NOTE: Not directly supported by OpenAL, but can be applied to each
     * source's air absorption factor as:
     * air_absorption_factor *= flAirAbsorptionHF / -5.0f
     */
    float flAirAbsorptionHF;
    /* NOTE: Not supported by OpenAL. */
    float flHFReference;
} EAXCONTEXTPROPERTIES, *LPEAXCONTEXTPROPERTIES; 

/* EAXCONTEXT_LASTERROR */
#define EAX_OK                   0
#define EAXERR_INVALID_OPERATION (-1)
#define EAXERR_INVALID_VALUE     (-2)
#define EAXERR_NO_EFFECT_LOADED  (-3)
#define EAXERR_UNKNOWN_EFFECT    (-4)


/* Also used by EAXSOURCE_ACTIVEFXSLOTID and EAXCONTEXT_PRIMARYFXSLOTID */
DEFINE_GUID(EAXPROPERTYID_EAX40_FXSlot0, 0xc4d79f1e, 0xf1ac, 0x436b, 0xa8, 0x1d, 0xa7, 0x38, 0xe7, 0x04, 0x54, 0x69);
DEFINE_GUID(EAXPROPERTYID_EAX40_FXSlot1, 0x08c00e96, 0x74be, 0x4491, 0x93, 0xaa, 0xe8, 0xad, 0x35, 0xa4, 0x91, 0x17);
DEFINE_GUID(EAXPROPERTYID_EAX40_FXSlot2, 0x1d433b88, 0xf0f6, 0x4637, 0x91, 0x9f, 0x60, 0xe7, 0xe0, 0x6b, 0x5e, 0xdd);
DEFINE_GUID(EAXPROPERTYID_EAX40_FXSlot3, 0xefff08ea, 0xc7d8, 0x44ab, 0x93, 0xad, 0x6d, 0xbd, 0x5f, 0x91, 0x00, 0x64);
typedef enum {
    EAXFXSLOT_PARAMETER = 0, /* 0x00...0x40 reserved for effect parameters */
    EAXFXSLOT_NONE = 0x10000,
    EAXFXSLOT_ALLPARAMETERS,
    EAXFXSLOT_LOADEFFECT,
    EAXFXSLOT_VOLUME,
    EAXFXSLOT_LOCK,
    EAXFXSLOT_FLAGS
} EAXFXSLOT_PROPERTY;

#define EAXFXSLOT_PARAMETER_IMMEDIATE 0x00000000
#define EAXFXSLOT_PARAMETER_DEFERRED  0x80000000
#define EAXFXSLOT_PARAMETER_COMMITDEFERREDSETTINGS (EAXFXSLOT_NONE | EAXFXSLOT_PARAMETER_IMMEDIATE)

/* EAXFXSLOT_ALLPARAMETERS */
typedef struct _EAXFXSLOTPROPERTIES {
    GUID  guidLoadEffect;
    long  lVolume;
    long  lLock;
    DWORD dwFlags;
} EAXFXSLOTPROPERTIES, *LPEAXFXSLOTPROPERTIES;

/* EAXFXSLOT_LOADEFFECT */
DEFINE_GUID(EAX_REVERB_EFFECT,           0x0cf95c8f, 0xa3cc, 0x4849, 0xb0, 0xb6, 0x83, 0x2e, 0xcc, 0x18, 0x22, 0xdf);
DEFINE_GUID(EAX_AGCCOMPRESSOR_EFFECT,    0xbfb7a01e, 0x7825, 0x4039, 0x92, 0x7f, 0x03, 0xaa, 0xbd, 0xa0, 0xc5, 0x60);
DEFINE_GUID(EAX_AUTOWAH_EFFECT,          0xec3130c0, 0xac7a, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_CHORUS_EFFECT,           0xde6d6fe0, 0xac79, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_DISTORTION_EFFECT,       0x975a4ce0, 0xac7e, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_ECHO_EFFECT,             0x0e9f1bc0, 0xac82, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_EQUALIZER_EFFECT,        0x65f94ce0, 0x9793, 0x11d3, 0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0);
DEFINE_GUID(EAX_FLANGER_EFFECT,          0xa70007c0, 0x07d2, 0x11d3, 0x9b, 0x1e, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_FREQUENCYSHIFTER_EFFECT, 0xdc3e1880, 0x9212, 0x11d3, 0x93, 0x9d, 0x00, 0xc0, 0xf0, 0x2d, 0xd6, 0xf0);
DEFINE_GUID(EAX_VOCALMORPHER_EFFECT,     0xe41cf10c, 0x3383, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_PITCHSHIFTER_EFFECT,     0xe7905100, 0xafb2, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);
DEFINE_GUID(EAX_RINGMODULATOR_EFFECT,    0x0b89fe60, 0xafb5, 0x11d2, 0x88, 0xdd, 0x00, 0xa0, 0x24, 0xd1, 0x3c, 0xe1);

/* EAXFXSLOT_LOCK */
enum {
    EAXFXSLOT_UNLOCKED = 0,
    EAXFXSLOT_LOCKED   = 1
};

/* EAXFXSLOT_FLAGS */
#define EAXFXSLOTFLAGS_ENVIRONMENT   0x00000001


DEFINE_GUID(EAXPROPERTYID_EAX40_Source, 0x1b86b823, 0x22df, 0x4eae, 0x8b, 0x3c, 0x12, 0x78, 0xce, 0x54, 0x42, 0x27);
typedef enum {
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
    EAXSOURCE_SENDPARAMETERS,
    EAXSOURCE_ALLSENDPARAMETERS,
    EAXSOURCE_OCCLUSIONSENDPARAMETERS,
    EAXSOURCE_EXCLUSIONSENDPARAMETERS,
    EAXSOURCE_ACTIVEFXSLOTID,
} EAXSOURCE_PROPERTY;

#define EAXSOURCE_PARAMETER_IMMEDIATE 0x00000000
#define EAXSOURCE_PARAMETER_DEFERRED  0x80000000
#define EAXSOURCE_PARAMETER_COMMITDEFERREDSETTINGS (EAXSOURCE_NONE | EAXSOURCE_PARAMETER_IMMEDIATE)

/* EAXSOURCE_ALLPARAMETERS */
typedef struct _EAX30SOURCEPROPERTIES {
    long  lDirect;
    long  lDirectHF;
    /* TODO: Do these act as offets for individual sends? Or overwrite them all
     * when set? Or only apply to sends using the context's primary FXSlot?
     */
    long  lRoom;
    long  lRoomHF;
    long  lObstruction;
    float flObstructionLFRatio;
    long  lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    float flOcclusionDirectRatio;
    long  lExclusion;
    float flExclusionLFRatio;
    long  lOutsideVolumeHF;
    float flDopplerFactor;
    /* NOTE: Added, not multiplied, with the listener's value. */
    float flRolloffFactor;
    float flRoomRolloffFactor;
    float flAirAbsorptionFactor;
    DWORD dwFlags;
} EAX30SOURCEPROPERTIES, *LPEAX30SOURCEPROPERTIES;

/* EAXSOURCE_OBSTRUCTIONPARAMETERS */
#ifndef EAX_OBSTRUCTIONPROPERTIES_DEFINED
#define EAX_OBSTRUCTIONPROPERTIES_DEFINED
typedef struct _EAXOBSTRUCTIONPROPERTIES {
    long  lObstruction;
    float flObstructionLFRatio;
} EAXOBSTRUCTIONPROPERTIES, *LPEAXOBSTRUCTIONPROPERTIES;
#endif

/* EAXSOURCE_OCCLUSIONPARAMETERS */
#ifndef EAX_OCCLUSIONPROPERTIES_DEFINED
#define EAX_OCCLUSIONPROPERTIES_DEFINED
typedef struct _EAXOCCLUSIONPROPERTIES {
    long  lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    float flOcclusionDirectRatio;
} EAXOCCLUSIONPROPERTIES, *LPEAXOCCLUSIONPROPERTIES;
#endif

/* EAXSOURCE_EXCLUSIONPARAMETERS */
#ifndef EAX_EXCLUSIONPROPERTIES_DEFINED
#define EAX_EXCLUSIONPROPERTIES_DEFINED
typedef struct _EAXEXCLUSIONPROPERTIES {
    long  lExclusion;
    float flExclusionLFRatio;
} EAXEXCLUSIONPROPERTIES, *LPEAXEXCLUSIONPROPERTIES;
#endif

/* EAXSOURCE_FLAGS */
#define EAXSOURCEFLAGS_DIRECTHFAUTO 0x00000001
#define EAXSOURCEFLAGS_ROOMAUTO     0x00000002
#define EAXSOURCEFLAGS_ROOMHFAUTO   0x00000004

/* EAXSOURCE_SENDPARAMETERS */
typedef struct _EAXSOURCESENDPROPERTIES {
    /* TODO: Check if sources maintain send parameters for each potential
     * effect slot, or if this needs to match a currently active effect slot to
     * match a send (and gets reset when the active effect slot GUID changes).
     * Also need to check how EAX_PrimaryFXSlotID is treated (as itself, or if
     * it looks at the context's primary FXSlot).
     */
    GUID guidReceivingFXSlotID;
    long lSend;
    long lSendHF;
} EAXSOURCESENDPROPERTIES, *LPEAXSOURCESENDPROPERTIES;

/* EAXSOURCE_ALLSENDPARAMETERS */
typedef struct _EAXSOURCEALLSENDPROPERTIES {
    GUID  guidReceivingFXSlotID;
    long  lSend;
    long  lSendHF;
    long  lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    float flOcclusionDirectRatio;
    long  lExclusion;
    float flExclusionLFRatio;
} EAXSOURCEALLSENDPROPERTIES, *LPEAXSOURCEALLSENDPROPERTIES;

/* EAXSOURCE_OCCLUSIONSENDPARAMETERS */
typedef struct _EAXSOURCEOCCLUSIONSENDPROPERTIES {
    GUID  guidReceivingFXSlotID;
    long  lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    float flOcclusionDirectRatio;
} EAXSOURCEOCCLUSIONSENDPROPERTIES, *LPEAXSOURCEOCCLUSIONSENDPROPERTIES;

/* EAXSOURCE_EXCLUSIONSENDPARAMETERS */
typedef struct _EAXSOURCEEXCLUSIONSENDPROPERTIES {
    GUID  guidReceivingFXSlotID;
    long  lExclusion;
    float flExclusionLFRatio;
} EAXSOURCEEXCLUSIONSENDPROPERTIES, *LPEAXSOURCEEXCLUSIONSENDPROPERTIES;

/* EAXSOURCE_ACTIVEFXSLOTID */
typedef struct _EAXACTIVEFXSLOTS {
    GUID guidActiveFXSlots[EAX_MAX_ACTIVE_FXSLOTS];
} EAXACTIVEFXSLOTS, *LPEAXACTIVEFXSLOTS;

DEFINE_GUID(EAX_PrimaryFXSlotID, 0xf317866d, 0x924c, 0x450c, 0x86, 0x1b, 0xe6, 0xda, 0xa2, 0x5e, 0x7c, 0x20);


/* EAX_REVERB_EFFECT properties. */
typedef enum {
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
    EAXREVERB_FLAGS
} EAXREVERB_PROPERTY;

#define EAXREVERB_IMMEDIATE 0x00000000
#define EAXREVERB_DEFERRED  0x80000000
#define EAXREVERB_COMMITDEFERREDSETTINGS (EAXREVERB_NONE | EAXREVERB_IMMEDIATE)

/* EAXREVERB_ALLPARAMETERS */
typedef struct _EAXREVERBPROPERTIES {
    DWORD dwEnvironment;
    float flEnvironmentSize;
    float flEnvironmentDiffusion;
    long  lRoom;
    long  lRoomHF;
    long  lRoomLF;
    float flDecayTime;
    float flDecayHFRatio;
    float flDecayLFRatio;
    long  lReflections;
    float flReflectionsDelay;
    EAXVECTOR vReflectionsPan;
    long  lReverb;
    float flReverbDelay;
    EAXVECTOR vReverbPan;
    float flEchoTime;
    float flEchoDepth;
    float flModulationTime;
    float flModulationDepth;
    float flAirAbsorptionHF;
    float flHFReference;
    float flLFReference;
    float flRoomRolloffFactor;
    DWORD dwFlags;
} EAXREVERBPROPERTIES, *LPEAXREVERBPROPERTIES;

/* EAXREVERB_FLAGS */
/* These flags determine what properties are modified when the environment size
 * is changed.
 */
#define EAXREVERBFLAGS_DECAYTIMESCALE        0x00000001
#define EAXREVERBFLAGS_REFLECTIONSSCALE      0x00000002
#define EAXREVERBFLAGS_REFLECTIONSDELAYSCALE 0x00000004
#define EAXREVERBFLAGS_REVERBSCALE           0x00000008
#define EAXREVERBFLAGS_REVERBDELAYSCALE      0x00000010
#define EAXREVERBFLAGS_ECHOTIMESCALE         0x00000040
#define EAXREVERBFLAGS_MODULATIONTIMESCALE   0x00000080
/* This flag limits the high frequency decay according to air absorption */
#define EAXREVERBFLAGS_DECAYHFLIMIT          0x00000020


/* EAX_CHORUS_EFFECT properties */
typedef enum {
    EAXCHORUS_NONE,
    EAXCHORUS_ALLPARAMETERS,
    EAXCHORUS_WAVEFORM,
    EAXCHORUS_PHASE,
    EAXCHORUS_RATE,
    EAXCHORUS_DEPTH,
    EAXCHORUS_FEEDBACK,
    EAXCHORUS_DELAY
} EAXCHORUS_PROPERTY;

#define EAXCHORUS_IMMEDIATE 0x00000000
#define EAXCHORUS_DEFERRED  0x80000000
#define EAXCHORUS_COMMITDEFERREDSETTINGS (EAXCHORUS_NONE | EAXCHORUS_IMMEDIATE)

/* EAXCHORUS_ALLPARAMETERS */
typedef struct _EAXCHORUSPROPERTIES {
    DWORD dwWaveform;
    long  lPhase;
    float flRate;
    float flDepth;
    float flFeedback;
    float flDelay;
} EAXCHORUSPROPERTIES, *LPEAXCHORUSPROPERTIES;

/* EAXCHORUS_WAVEFORM */
enum {
    EAX_CHORUS_SINUSOID,
    EAX_CHORUS_TRIANGLE
};


/* TODO: Other effects. */


#endif /* EAX4_H */
