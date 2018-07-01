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

typedef struct _EAXVECTOR {
    float x, y, z;
} EAXVECTOR;


/* EAX 3.0 stuff. */
DEFINE_GUID(DSPROPSETID_EAX30_ListenerProperties, 0xa8fa6882, 0xb476, 0x11d3, 0xbd, 0xb9, 0x00, 0xc0, 0xf0, 0x2d, 0xdf, 0x87);
typedef enum {
    DSPROPERTY_EAX30LISTENER_NONE,
    DSPROPERTY_EAX30LISTENER_ALLPARAMETERS,
    DSPROPERTY_EAX30LISTENER_ENVIRONMENT,
    DSPROPERTY_EAX30LISTENER_ENVIRONMENTSIZE,
    DSPROPERTY_EAX30LISTENER_ENVIRONMENTDIFFUSION,
    DSPROPERTY_EAX30LISTENER_ROOM,
    DSPROPERTY_EAX30LISTENER_ROOMHF,
    DSPROPERTY_EAX30LISTENER_ROOMLF,
    DSPROPERTY_EAX30LISTENER_DECAYTIME,
    DSPROPERTY_EAX30LISTENER_DECAYHFRATIO,
    DSPROPERTY_EAX30LISTENER_DECAYLFRATIO,
    DSPROPERTY_EAX30LISTENER_REFLECTIONS,
    DSPROPERTY_EAX30LISTENER_REFLECTIONSDELAY,
    DSPROPERTY_EAX30LISTENER_REFLECTIONSPAN,
    DSPROPERTY_EAX30LISTENER_REVERB,
    DSPROPERTY_EAX30LISTENER_REVERBDELAY,
    DSPROPERTY_EAX30LISTENER_REVERBPAN,
    DSPROPERTY_EAX30LISTENER_ECHOTIME,
    DSPROPERTY_EAX30LISTENER_ECHODEPTH,
    DSPROPERTY_EAX30LISTENER_MODULATIONTIME,
    DSPROPERTY_EAX30LISTENER_MODULATIONDEPTH,
    DSPROPERTY_EAX30LISTENER_AIRABSORPTIONHF,
    DSPROPERTY_EAX30LISTENER_HFREFERENCE,
    DSPROPERTY_EAX30LISTENER_LFREFERENCE,
    DSPROPERTY_EAX30LISTENER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX30LISTENER_FLAGS
} DSPROPERTY_EAX30_LISTENERPROPERTY;

/* Stores the value being set, but does not apply it */
#define DSPROPERTY_EAX30LISTENER_DEFERRED               0x80000000
/* The lack of the deferred flag forces a call to CommitDeferredSettings(),
 * applying *all* deferred settings, including the EAX property being set */
#define DSPROPERTY_EAX30LISTENER_IMMEDIATE              0x00000000
/* Same as IMMEDIATE; causes a commit of deferred properties but implies no
 * extra property being set */
#define DSPROPERTY_EAX30LISTENER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX30LISTENERPROPERTIES {
    DWORD dwEnvironment;
    float flEnvironmentSize;
    float flEnvironmentDiffusion;
    long lRoom;
    long lRoomHF;
    long lRoomLF;
    float flDecayTime;
    float flDecayHFRatio;
    float flDecayLFRatio;
    long lReflections;
    float flReflectionsDelay;
    EAXVECTOR vReflectionsPan;
    long lReverb;
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
} EAX30LISTENERPROPERTIES, *LPEAX30LISTENERPROPERTIES;

/* These flags determine what properties are modified when the environment size
   is changed */
#define EAX30LISTENERFLAGS_DECAYTIMESCALE        0x00000001
#define EAX30LISTENERFLAGS_REFLECTIONSSCALE      0x00000002
#define EAX30LISTENERFLAGS_REFLECTIONSDELAYSCALE 0x00000004
#define EAX30LISTENERFLAGS_REVERBSCALE           0x00000008
#define EAX30LISTENERFLAGS_REVERBDELAYSCALE      0x00000010
/* This flag limits the high frequency decay according to air absorption */
#define EAX30LISTENERFLAGS_DECAYHFLIMIT          0x00000020
#define EAX30LISTENERFLAGS_ECHOTIMESCALE         0x00000040
#define EAX30LISTENERFLAGS_MODTIMESCALE          0x00000080


/* EAX environment presets */
//  Env   Size   Diffus    Room    RoomHF  RoomLF  DecTm   DcHF    DcLF    Refl    RefDel  Ref Pan             Revb    RevDel   Rev Pan            EchTm   EchDp   ModTm   ModDp   AirAbs  HFRef     LFRef   RRlOff  FLAGS
#define REVERB_PRESET_GENERIC \
    {0,   7.5f,   1.000f,  -1000,  -100,   0,      1.49f,  0.83f,  1.00f,  -2602,  0.007f,  {0.0f,0.0f,0.0f},  200,    0.011f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_PADDEDCELL \
    {1,   1.4f,   1.000f,  -1000,  -6000,  0,      0.17f,  0.10f,  1.00f,  -1204,  0.001f,  {0.0f,0.0f,0.0f},  207,    0.002f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_ROOM \
    {2,   1.9f,   1.000f,  -1000,  -454,   0,      0.40f,  0.83f,  1.00f,  -1646,  0.002f,  {0.0f,0.0f,0.0f},  53,     0.003f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_BATHROOM \
    {3,   1.4f,   1.000f,  -1000,  -1200,  0,      1.49f,  0.54f,  1.00f,  -370,   0.007f,  {0.0f,0.0f,0.0f},  1030,   0.011f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_LIVINGROOM \
    {4,   2.5f,   1.000f,  -1000,  -6000,  0,      0.50f,  0.10f,  1.00f,  -1376,  0.003f,  {0.0f,0.0f,0.0f},  -1104,  0.004f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_STONEROOM \
    {5,   11.6f,  1.000f,  -1000,  -300,   0,      2.31f,  0.64f,  1.00f,  -711,   0.012f,  {0.0f,0.0f,0.0f},  83,     0.017f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_AUDITORIUM \
    {6,   21.6f,  1.000f,  -1000,  -476,   0,      4.32f,  0.59f,  1.00f,  -789,   0.020f,  {0.0f,0.0f,0.0f},  -289,   0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_CONCERTHALL \
    {7,   19.6f,  1.000f,  -1000,  -500,   0,      3.92f,  0.70f,  1.00f,  -1230,  0.020f,  {0.0f,0.0f,0.0f},  -02,    0.029f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_CAVE \
    {8,   14.6f,  1.000f,  -1000,  0,      0,      2.91f,  1.30f,  1.00f,  -602,   0.015f,  {0.0f,0.0f,0.0f},  -302,   0.022f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }
#define REVERB_PRESET_ARENA \
    {9,   36.2f,  1.000f,  -1000,  -698,   0,      7.24f,  0.33f,  1.00f,  -1166,  0.020f,  {0.0f,0.0f,0.0f},  16,     0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_HANGAR \
    {10,  50.3f,  1.000f,  -1000,  -1000,  0,      10.05f, 0.23f,  1.00f,  -602,   0.020f,  {0.0f,0.0f,0.0f},  198,    0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_CARPETEDHALLWAY \
    {11,  1.9f,   1.000f,  -1000,  -4000,  0,      0.30f,  0.10f,  1.00f,  -1831,  0.002f,  {0.0f,0.0f,0.0f},  -1630,  0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_HALLWAY \
    {12,  1.8f,   1.000f,  -1000,  -300,   0,      1.49f,  0.59f,  1.00f,  -1219,  0.007f,  {0.0f,0.0f,0.0f},  441,    0.011f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_STONECORRIDOR \
    {13,  13.5f,  1.000f,  -1000,  -237,   0,      2.70f,  0.79f,  1.00f,  -1214,  0.013f,  {0.0f,0.0f,0.0f},  395,    0.020f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_ALLEY \
    {14,  7.5f,   0.300f,  -1000,  -270,   0,      1.49f,  0.86f,  1.00f,  -1204,  0.007f,  {0.0f,0.0f,0.0f},  -4,     0.011f,  {0.0f,0.0f,0.0f},  0.125f, 0.950f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_FOREST \
    {15,  38.0f,  0.300f,  -1000,  -3300,  0,      1.49f,  0.54f,  1.00f,  -2560,  0.162f,  {0.0f,0.0f,0.0f},  -229,   0.088f,  {0.0f,0.0f,0.0f},  0.125f, 1.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_CITY \
    {16,  7.5f,   0.500f,  -1000,  -800,   0,      1.49f,  0.67f,  1.00f,  -2273,  0.007f,  {0.0f,0.0f,0.0f},  -1691,  0.011f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_MOUNTAINS \
    {17,  100.0f, 0.270f,  -1000,  -2500,  0,      1.49f,  0.21f,  1.00f,  -2780,  0.300f,  {0.0f,0.0f,0.0f},  -1434,  0.100f,  {0.0f,0.0f,0.0f},  0.250f, 1.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }
#define REVERB_PRESET_QUARRY \
    {18,  17.5f,  1.000f,  -1000,  -1000,  0,      1.49f,  0.83f,  1.00f,  -10000, 0.061f,  {0.0f,0.0f,0.0f},  500,    0.025f,  {0.0f,0.0f,0.0f},  0.125f, 0.700f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_PLAIN \
    {19,  42.5f,  0.210f,  -1000,  -2000,  0,      1.49f,  0.50f,  1.00f,  -2466,  0.179f,  {0.0f,0.0f,0.0f},  -1926,  0.100f,  {0.0f,0.0f,0.0f},  0.250f, 1.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_PARKINGLOT \
    {20,  8.3f,   1.000f,  -1000,  0,      0,      1.65f,  1.50f,  1.00f,  -1363,  0.008f,  {0.0f,0.0f,0.0f},  -1153,  0.012f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }
#define REVERB_PRESET_SEWERPIPE \
    {21,  1.7f,   0.800f,  -1000,  -1000,  0,      2.81f,  0.14f,  1.00f,  429,    0.014f,  {0.0f,0.0f,0.0f},  1023,   0.021f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 0.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_UNDERWATER \
    {22,  1.8f,   1.000f,  -1000,  -4000,  0,      1.49f,  0.10f,  1.00f,  -449,   0.007f,  {0.0f,0.0f,0.0f},  1700,   0.011f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 1.180f, 0.348f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x3f }
#define REVERB_PRESET_DRUGGED \
    {23,  1.9f,   0.500f,  -1000,  0,      0,      8.39f,  1.39f,  1.00f,  -115,   0.002f,  {0.0f,0.0f,0.0f},  985,    0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 0.250f, 1.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }
#define REVERB_PRESET_DIZZY \
    {24,  1.8f,   0.600f,  -1000,  -400,   0,      17.23f, 0.56f,  1.00f,  -1713,  0.020f,  {0.0f,0.0f,0.0f},  -613,   0.030f,  {0.0f,0.0f,0.0f},  0.250f, 1.000f, 0.810f, 0.310f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }
#define REVERB_PRESET_PSYCHOTIC \
    {25,  1.0f,   0.500f,  -1000,  -151,   0,      7.56f,  0.91f,  1.00f,  -626,   0.020f,  {0.0f,0.0f,0.0f},  774,    0.030f,  {0.0f,0.0f,0.0f},  0.250f, 0.000f, 4.000f, 1.000f, -5.0f,  5000.0f,  250.0f, 0.00f,  0x1f }

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
};

extern const EAX30LISTENERPROPERTIES EnvironmentDefaults[EAX_ENVIRONMENT_COUNT];

DEFINE_GUID(DSPROPSETID_EAX30_BufferProperties, 0xa8fa6881, 0xb476, 0x11d3, 0xbd, 0xb9, 0x00, 0xc0, 0xf0, 0x2d, 0xdf, 0x87);
typedef enum {
    DSPROPERTY_EAX30BUFFER_NONE,
    DSPROPERTY_EAX30BUFFER_ALLPARAMETERS,
    DSPROPERTY_EAX30BUFFER_DIRECT,
    DSPROPERTY_EAX30BUFFER_DIRECTHF,
    /* NOTE: Not 100% sure these DirectLF and RoomLF properties are correct,
     * however it does line everything else up and it fits with OpenAL's band-
     * pass filter, so it's probably right.
     */
    DSPROPERTY_EAX30BUFFER_DIRECTLF,
    DSPROPERTY_EAX30BUFFER_ROOM,
    DSPROPERTY_EAX30BUFFER_ROOMHF,
    DSPROPERTY_EAX30BUFFER_ROOMLF,
    DSPROPERTY_EAX30BUFFER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX30BUFFER_OBSTRUCTION,
    DSPROPERTY_EAX30BUFFER_OBSTRUCTIONLFRATIO,
    DSPROPERTY_EAX30BUFFER_OCCLUSION,
    DSPROPERTY_EAX30BUFFER_OCCLUSIONLFRATIO,
    /* TODO: Check if the order of the room and direct ratio properties are
     * correct.
     */
    DSPROPERTY_EAX30BUFFER_OCCLUSIONROOMRATIO,
    DSPROPERTY_EAX30BUFFER_OCCLUSIONDIRECTRATIO,
    DSPROPERTY_EAX30BUFFER_EXCLUSION,
    DSPROPERTY_EAX30BUFFER_EXCLUSIONLFRATIO,
    DSPROPERTY_EAX30BUFFER_OUTSIDEVOLUMEHF,
    DSPROPERTY_EAX30BUFFER_AIRABSORPTIONFACTOR,
    DSPROPERTY_EAX30BUFFER_FLAGS
} DSPROPERTY_EAX30_BUFFERPROPERTY;

#define DSPROPERTY_EAX30BUFFER_DEFERRED               0x80000000
/* NOTE: This applies all deferred changes, not just the buffer's. */
#define DSPROPERTY_EAX30BUFFER_IMMEDIATE              0x00000000
#define DSPROPERTY_EAX30BUFFER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX30BUFFERPROPERTIES {
    long lDirect;
    long lDirectHF;
    long lDirectLF;
    long lRoom;
    long lRoomHF;
    long lRoomLF;
    float flRoomRolloffFactor;
    long lObstruction;
    float flObstructionLFRatio;
    long lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    float flOcclusionDirectRatio;
    long lExclusion;
    float flExclusionLFRatio;
    long lOutsideVolumeHF;
    float flAirAbsorptionFactor;
    DWORD dwFlags;
} EAX30BUFFERPROPERTIES, *LPEAX30BUFFERPROPERTIES;

/* Flags that affect lDirectHF, lRoom, and lRoomHF */
#define EAX30BUFFERFLAGS_DIRECTHFAUTO 0x00000001
#define EAX30BUFFERFLAGS_ROOMAUTO     0x00000002
#define EAX30BUFFERFLAGS_ROOMHFAUTO   0x00000004


/* EAX 2.0 stuff. */
DEFINE_GUID(DSPROPSETID_EAX20_ListenerProperties, 0x0306a6a8, 0xb224, 0x11d2, 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22);
typedef enum {
    DSPROPERTY_EAX20LISTENER_NONE,
    DSPROPERTY_EAX20LISTENER_ALLPARAMETERS,
    DSPROPERTY_EAX20LISTENER_ROOM,
    DSPROPERTY_EAX20LISTENER_ROOMHF,
    DSPROPERTY_EAX20LISTENER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX20LISTENER_DECAYTIME,
    DSPROPERTY_EAX20LISTENER_DECAYHFRATIO,
    DSPROPERTY_EAX20LISTENER_REFLECTIONS,
    DSPROPERTY_EAX20LISTENER_REFLECTIONSDELAY,
    DSPROPERTY_EAX20LISTENER_REVERB,
    DSPROPERTY_EAX20LISTENER_REVERBDELAY,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENT,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENTSIZE,
    DSPROPERTY_EAX20LISTENER_ENVIRONMENTDIFFUSION,
    DSPROPERTY_EAX20LISTENER_AIRABSORPTIONHF,
    DSPROPERTY_EAX20LISTENER_FLAGS
} DSPROPERTY_EAX20_LISTENERPROPERTY;

#define DSPROPERTY_EAX20LISTENER_DEFERRED               0x80000000
#define DSPROPERTY_EAX20LISTENER_IMMEDIATE              0x00000000
#define DSPROPERTY_EAX20LISTENER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX20LISTENERPROPERTIES {
    long lRoom;
    long lRoomHF;
    float flRoomRolloffFactor;
    float flDecayTime;
    float flDecayHFRatio;
    long lReflections;
    float flReflectionsDelay;
    long lReverb;
    float flReverbDelay;
    DWORD dwEnvironment;
    float flEnvironmentSize;
    float flEnvironmentDiffusion;
    float flAirAbsorptionHF;
    DWORD dwFlags;
} EAX20LISTENERPROPERTIES, *LPEAX20LISTENERPROPERTIES;

#define EAX20LISTENERFLAGS_DECAYTIMESCALE        0x00000001
#define EAX20LISTENERFLAGS_REFLECTIONSSCALE      0x00000002
#define EAX20LISTENERFLAGS_REFLECTIONSDELAYSCALE 0x00000004
#define EAX20LISTENERFLAGS_REVERBSCALE           0x00000008
#define EAX20LISTENERFLAGS_REVERBDELAYSCALE      0x00000010
#define EAX20LISTENERFLAGS_DECAYHFLIMIT          0x00000020


DEFINE_GUID(DSPROPSETID_EAX20_BufferProperties, 0x0306a6a7, 0xb224, 0x11d2, 0x99, 0xe5, 0x00, 0x00, 0xe8, 0xd8, 0xc7, 0x22);
typedef enum {
    DSPROPERTY_EAX20BUFFER_NONE,
    DSPROPERTY_EAX20BUFFER_ALLPARAMETERS,
    DSPROPERTY_EAX20BUFFER_DIRECT,
    DSPROPERTY_EAX20BUFFER_DIRECTHF,
    DSPROPERTY_EAX20BUFFER_ROOM,
    DSPROPERTY_EAX20BUFFER_ROOMHF,
    DSPROPERTY_EAX20BUFFER_ROOMROLLOFFFACTOR,
    DSPROPERTY_EAX20BUFFER_OBSTRUCTION,
    DSPROPERTY_EAX20BUFFER_OBSTRUCTIONLFRATIO,
    DSPROPERTY_EAX20BUFFER_OCCLUSION,
    DSPROPERTY_EAX20BUFFER_OCCLUSIONLFRATIO,
    DSPROPERTY_EAX20BUFFER_OCCLUSIONROOMRATIO,
    DSPROPERTY_EAX20BUFFER_OUTSIDEVOLUMEHF,
    DSPROPERTY_EAX20BUFFER_AIRABSORPTIONFACTOR,
    DSPROPERTY_EAX20BUFFER_FLAGS
} DSPROPERTY_EAX20_BUFFERPROPERTY;

#define DSPROPERTY_EAX20BUFFER_DEFERRED               0x80000000
#define DSPROPERTY_EAX20BUFFER_IMMEDIATE              0x00000000
#define DSPROPERTY_EAX20BUFFER_COMMITDEFERREDSETTINGS 0x00000000

typedef struct _EAX20BUFFERPROPERTIES {
    long lDirect;
    long lDirectHF;
    long lRoom;
    long lRoomHF;
    float flRoomRolloffFactor;
    long lObstruction;
    float flObstructionLFRatio;
    long lOcclusion;
    float flOcclusionLFRatio;
    float flOcclusionRoomRatio;
    long lOutsideVolumeHF;
    float flAirAbsorptionFactor;
    DWORD dwFlags;
} EAX20BUFFERPROPERTIES, *LPEAX20BUFFERPROPERTIES;

#define EAX20BUFFERFLAGS_DIRECTHFAUTO 0x00000001
#define EAX20BUFFERFLAGS_ROOMAUTO     0x00000002
#define EAX20BUFFERFLAGS_ROOMHFAUTO   0x00000004


/* EAX 1.0 stuff. */
DEFINE_GUID(DSPROPSETID_EAX10_ListenerProperties, 0x4a4e6fc1, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
typedef enum {
    DSPROPERTY_EAX10LISTENER_ALL,
    DSPROPERTY_EAX10LISTENER_ENVIRONMENT,
    DSPROPERTY_EAX10LISTENER_VOLUME,
    DSPROPERTY_EAX10LISTENER_DECAYTIME,
    DSPROPERTY_EAX10LISTENER_DAMPING
} DSPROPERTY_EAX10_REVERBPROPERTY;

typedef struct {
    DWORD dwEnvironment;
    float fVolume;
    float fDecayTime;
    float fDamping;
} EAX10LISTENERPROPERTIES;


DEFINE_GUID(DSPROPSETID_EAX10_BufferProperties, 0x4a4e6fc0, 0xc341, 0x11d1, 0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
typedef enum {
    DSPROPERTY_EAX10BUFFER_ALL,
    DSPROPERTY_EAX10BUFFER_REVERBMIX
} DSPROPERTY_EAX10_BUFFERPROPERTY;

typedef struct {
    float fMix;
} EAX10BUFFERPROPERTIES;
