/*  			DirectSound
 *
 * Copyright 2010 Chris Robinson
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

DEFINE_GUID(DSPROPSETID_EAX20_ListenerProperties, 0x0306a6a8, 0xb224, 0x11d2, 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22);
typedef enum {
    DSPROPERTY_EAXLISTENER_NONE,
    DSPROPERTY_EAXLISTENER_ALLPARAMETERS,
    DSPROPERTY_EAXLISTENER_ROOM,
    DSPROPERTY_EAXLISTENER_ROOMHF,
    DSPROPERTY_EAXLISTENER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAXLISTENER_DECAYTIME,
    DSPROPERTY_EAXLISTENER_DECAYHFRATIO,
    DSPROPERTY_EAXLISTENER_REFLECTIONS,
    DSPROPERTY_EAXLISTENER_REFLECTIONSDELAY,
    DSPROPERTY_EAXLISTENER_REVERB,
    DSPROPERTY_EAXLISTENER_REVERBDELAY,
    DSPROPERTY_EAXLISTENER_ENVIRONMENT,
    DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE,
    DSPROPERTY_EAXLISTENER_ENVIRONMENTDIFFUSION,
    DSPROPERTY_EAXLISTENER_AIRABSORPTIONHF,
    DSPROPERTY_EAXLISTENER_FLAGS
} DSPROPERTY_EAX_LISTENERPROPERTY;

/* Stores the value being set, but does not apply it */
#define DSPROPERTY_EAXLISTENER_DEFERRED               0x80000000
/* The lack of the deferred flag forces a call to CommitDeferredSettings(),
 * applying *all* deferred settings, including the EAX property being set */
#define DSPROPERTY_EAXLISTENER_IMMEDIATE              0x00000000
/* Same as IMMEDIATE; causes a commit of deferred properties but implies no
 * extra property being set */
#define DSPROPERTY_EAXLISTENER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX20LISTENERPROPERTIES {
    LONG lRoom;
    LONG lRoomHF;
    FLOAT flRoomRolloffFactor;
    FLOAT flDecayTime;
    FLOAT flDecayHFRatio;
    LONG lReflections;
    FLOAT flReflectionsDelay;
    LONG lReverb;
    FLOAT flReverbDelay;
    DWORD dwEnvironment;
    FLOAT flEnvironmentSize;
    FLOAT flEnvironmentDiffusion;
    FLOAT flAirAbsorptionHF;
    DWORD dwFlags;
} EAX20LISTENERPROPERTIES, *LPEAX20LISTENERPROPERTIES;

/* These flags determine what properties are modified when the environment size
   is changed */
#define EAXLISTENERFLAGS_DECAYTIMESCALE        0x00000001
#define EAXLISTENERFLAGS_REFLECTIONSSCALE      0x00000002
#define EAXLISTENERFLAGS_REFLECTIONSDELAYSCALE 0x00000004
#define EAXLISTENERFLAGS_REVERBSCALE           0x00000008
#define EAXLISTENERFLAGS_REVERBDELAYSCALE      0x00000010
/* This flag limits the high frequency decay according to air absorption */
#define EAXLISTENERFLAGS_DECAYHFLIMIT          0x00000020

#define EAXLISTENERPROPERTIES EAX20LISTENERPROPERTIES
#define LPEAXLISTENERPROPERTIES LPEAX20LISTENERPROPERTIES

/* EAX environment presets */
/*    Room   RoomHF   RRlOff  DecTm   DcHF   Refl    RefDel  Revb   RevDel  Env Size    Diffuse  AirAbs  Flags */
#define REVERB_PRESET_GENERIC \
    { -1000, -100,    0.0f,   1.49f,  0.83f, -2602,  0.007f, 200,   0.011f, 0,  7.5f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_PADDEDCELL \
    { -1000, -6000,   0.00f,  0.17f,  0.10f, -1204,  0.001f, 207,   0.002f, 1,  1.4f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_ROOM \
    { -1000, -454,    0.00f,  0.40f,  0.83f, -1646,  0.002f, 53,    0.003f, 2,  1.9f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_BATHROOM \
    { -1000, -1200,   0.00f,  1.49f,  0.54f, -370,   0.007f, 1030,  0.011f, 3,  1.4f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_LIVINGROOM \
    { -1000, -6000,   0.00f,  0.50f,  0.10f, -1376,  0.003f, -1104, 0.004f, 4,  2.5f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_STONEROOM \
    { -1000, -300,    0.00f,  2.31f,  0.64f, -711,   0.012f, 83,    0.017f, 5,  11.6f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_AUDITORIUM \
    { -1000, -476,    0.00f,  4.32f,  0.59f, -789,   0.020f, -289,  0.030f, 6,  21.6f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_CONCERTHALL \
    { -1000, -500,    0.00f,  3.92f,  0.70f, -1230,  0.020f, -2,    0.029f, 7,  19.6f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_CAVE \
    { -1000, 0,       0.00f,  2.91f,  1.30f, -602,   0.015f, -302,  0.022f, 8,  14.6f,  1.000f,  -5.0f,  0x1f }
#define REVERB_PRESET_ARENA \
    { -1000, -698,    0.00f,  7.24f,  0.33f, -1166,  0.020f, 16,    0.030f, 9,  36.2f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_HANGAR \
    { -1000, -1000,   0.00f,  10.05f, 0.23f, -602,   0.020f, 198,   0.030f, 10, 50.3f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_CARPETEDHALLWAY \
    { -1000, -4000,   0.00f,  0.30f,  0.10f, -1831,  0.002f, -1630, 0.030f, 11, 1.9f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_HALLWAY \
    { -1000, -300,    0.00f,  1.49f,  0.59f, -1219,  0.007f, 441,   0.011f, 12, 1.8f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_STONECORRIDOR \
    { -1000, -237,    0.00f,  2.70f,  0.79f, -1214,  0.013f, 395,   0.020f, 13, 13.5f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_ALLEY \
    { -1000, -270,    0.00f,  1.49f,  0.86f, -1204,  0.007f, -4,    0.011f, 14, 7.5f,   0.300f,  -5.0f,  0x3f }
#define REVERB_PRESET_FOREST \
    { -1000, -3300,   0.00f,  1.49f,  0.54f, -2560,  0.162f, -229,  0.088f, 15, 38.0f,  0.300f,  -5.0f,  0x3f }
#define REVERB_PRESET_CITY \
    { -1000, -800,    0.00f,  1.49f,  0.67f, -2273,  0.007f, -1691, 0.011f, 16, 7.5f,   0.500f,  -5.0f,  0x3f }
#define REVERB_PRESET_MOUNTAINS \
    { -1000, -2500,   0.00f,  1.49f,  0.21f, -2780,  0.300f, -1434, 0.100f, 17, 100.0f, 0.270f,  -5.0f,  0x1f }
#define REVERB_PRESET_QUARRY \
    { -1000, -1000,   0.00f,  1.49f,  0.83f, -10000, 0.061f, 500,   0.025f, 18, 17.5f,  1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_PLAIN \
    { -1000, -2000,   0.00f,  1.49f,  0.50f, -2466,  0.179f, -1926, 0.100f, 19, 42.5f,  0.210f,  -5.0f,  0x3f }
#define REVERB_PRESET_PARKINGLOT \
    { -1000, 0,       0.00f,  1.65f,  1.50f, -1363,  0.008f, -1153, 0.012f, 20, 8.3f,   1.000f,  -5.0f,  0x1f }
#define REVERB_PRESET_SEWERPIPE \
    { -1000, -1000,   0.00f,  2.81f,  0.14f, 429,    0.014f, 1023,  0.021f, 21, 1.7f,   0.800f,  -5.0f,  0x3f }
#define REVERB_PRESET_UNDERWATER \
    { -1000, -4000,   0.00f,  1.49f,  0.10f, -449,   0.007f, 1700,  0.011f, 22, 1.8f,   1.000f,  -5.0f,  0x3f }
#define REVERB_PRESET_DRUGGED \
    { -1000, 0,       0.00f,  8.39f,  1.39f, -115,   0.002f, 985,   0.030f, 23, 1.9f,   0.500f,  -5.0f,  0x1f }
#define REVERB_PRESET_DIZZY \
    { -1000, -400,    0.00f, 17.23f,  0.56f, -1713,  0.020f, -613,  0.030f, 24, 1.8f,   0.600f,  -5.0f,  0x1f }
#define REVERB_PRESET_PSYCHOTIC \
    { -1000, -151,    0.00f, 7.56f,   0.91f, -626,   0.020f, 774,   0.030f, 25, 1.0f,   0.500f,  -5.0f,  0x1f }

enum {
    EAX_ENVIRONMENT_GENERIC,
    EAX_ENVIRONMENT_PADDEDCELL,
    EAX_ENVIRONMENT_ROOM,
    EAX_ENVIRONMENT_BATHROOM,
    EAX_ENVIRONMENT_LIVINGROOM,
    EAX_ENVIRONMENT_STONEROOM,
    EAX_ENVIRONMENT_AUDITORIUM,
    EAX_ENVIRONMENT_CONCERTHALL,
    EAX_ENVIRONMENT_CAVE,
    EAX_ENVIRONMENT_ARENA,
    EAX_ENVIRONMENT_HANGAR,
    EAX_ENVIRONMENT_CARPETEDHALLWAY,
    EAX_ENVIRONMENT_HALLWAY,
    EAX_ENVIRONMENT_STONECORRIDOR,
    EAX_ENVIRONMENT_ALLEY,
    EAX_ENVIRONMENT_FOREST,
    EAX_ENVIRONMENT_CITY,
    EAX_ENVIRONMENT_MOUNTAINS,
    EAX_ENVIRONMENT_QUARRY,
    EAX_ENVIRONMENT_PLAIN,
    EAX_ENVIRONMENT_PARKINGLOT,
    EAX_ENVIRONMENT_SEWERPIPE,
    EAX_ENVIRONMENT_UNDERWATER,
    EAX_ENVIRONMENT_DRUGGED,
    EAX_ENVIRONMENT_DIZZY,
    EAX_ENVIRONMENT_PSYCHOTIC,

    EAX_ENVIRONMENT_COUNT,
    EAX_MAX_ENVIRONMENT = EAX_ENVIRONMENT_COUNT-1
};

extern const EAXLISTENERPROPERTIES EnvironmentDefaults[EAX_ENVIRONMENT_COUNT];


DEFINE_GUID(DSPROPSETID_EAX20_BufferProperties, 0x0306a6a7, 0xb224, 0x11d2, 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22);
typedef enum {
    DSPROPERTY_EAXBUFFER_NONE,
    DSPROPERTY_EAXBUFFER_ALLPARAMETERS,
    DSPROPERTY_EAXBUFFER_DIRECT,
    DSPROPERTY_EAXBUFFER_DIRECTHF,
    DSPROPERTY_EAXBUFFER_ROOM,
    DSPROPERTY_EAXBUFFER_ROOMHF,
    DSPROPERTY_EAXBUFFER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAXBUFFER_OBSTRUCTION,
    DSPROPERTY_EAXBUFFER_OBSTRUCTIONLFRATIO,
    DSPROPERTY_EAXBUFFER_OCCLUSION,
    DSPROPERTY_EAXBUFFER_OCCLUSIONLFRATIO,
    DSPROPERTY_EAXBUFFER_OCCLUSIONROOMRATIO,
    DSPROPERTY_EAXBUFFER_OUTSIDEVOLUMEHF,
    DSPROPERTY_EAXBUFFER_AIRABSORPTIONFACTOR,
    DSPROPERTY_EAXBUFFER_FLAGS
} DSPROPERTY_EAX_BUFFERPROPERTY;

/* Stores the value being set, but does not apply it */
#define DSPROPERTY_EAXBUFFER_DEFERRED               0x80000000
/* The lack of the deferred flag forces a call to CommitDeferredSettings(),
 * applying *all* deferred settings, including the EAX property being set */
#define DSPROPERTY_EAXBUFFER_IMMEDIATE              0x00000000
/* Same as IMMEDIATE; causes a commit of deferred properties but implies no
 * extra property being set */
#define DSPROPERTY_EAXBUFFER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX20BUFFERPROPERTIES {
    LONG lDirect;
    LONG lDirectHF;
    LONG lRoom;
    LONG lRoomHF;
    FLOAT flRoomRolloffFactor;
    LONG lObstruction;
    FLOAT flObstructionLFRatio;
    LONG lOcclusion;
    FLOAT flOcclusionLFRatio;
    FLOAT flOcclusionRoomRatio;
    LONG lOutsideVolumeHF;
    FLOAT flAirAbsorptionFactor;
    DWORD dwFlags;
} EAX20BUFFERPROPERTIES, *LPEAX20BUFFERPROPERTIES;

/* Flags that affect lDirectHF, lRoom, and lRoomHF */
#define EAXBUFFERFLAGS_DIRECTHFAUTO 0x00000001
#define EAXBUFFERFLAGS_ROOMAUTO     0x00000002
#define EAXBUFFERFLAGS_ROOMHFAUTO   0x00000004

/* EAX 1.0 stuff. */
DEFINE_GUID(DSPROPSETID_EAX_ReverbProperties, 0x4a4e6fc1, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
typedef enum {
    DSPROPERTY_EAX1_ALL,
    DSPROPERTY_EAX1_ENVIRONMENT,
    DSPROPERTY_EAX1_VOLUME,
    DSPROPERTY_EAX1_DECAYTIME,
    DSPROPERTY_EAX1_DAMPING
} DSPROPERTY_EAX1_REVERBPROPERTY;

typedef struct {
    DWORD dwEnvironment;
    float fVolume;
    float fDecayTime;
    float fDamping;
} EAX1_REVERBPROPERTIES;


DEFINE_GUID(DSPROPSETID_EAXBUFFER_ReverbProperties, 0x4a4e6fc0, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
typedef enum {
    DSPROPERTY_EAX1BUFFER_ALL,
    DSPROPERTY_EAX1BUFFER_REVERBMIX
} DSPROPERTY_EAX1BUFFER_REVERBPROPERTY;

typedef struct {
    float fMix;
} EAX1BUFFER_REVERBPROPERTIES;
