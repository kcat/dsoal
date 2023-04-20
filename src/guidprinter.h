#ifndef GUIDPRINTER_H
#define GUIDPRINTER_H

#include <cstdio>
#include <cstring>
#include <iterator>

#include <dsound.h>
#include <ks.h>
#include <objbase.h>

#include "eax.h"
#include "vmanager.h"


struct IidTag { };
struct ClsidTag { };
struct PropidTag { };
struct DevidTag { };
struct Ds3dalgTag { };

class GuidPrinter {
    char mMsg[48];
    const char *mIdStr{};

    void store(const GUID &guid)
    {
        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

    void store_iid(const GUID &guid)
    {
        if(guid == GUID_NULL) mIdStr = "GUID_NULL";
#define CHECKID(x) else if(guid == x) mIdStr = #x;
        CHECKID(IID_IDirectSound)
        CHECKID(IID_IDirectSound8)
        CHECKID(IID_IDirectSoundBuffer)
        CHECKID(IID_IDirectSoundBuffer8)
        CHECKID(IID_IDirectSound3DBuffer)
        CHECKID(IID_IDirectSound3DListener)
        CHECKID(IID_IDirectSoundNotify)
        CHECKID(IID_IDirectSoundCapture)
        CHECKID(IID_IDirectSoundCaptureBuffer)
        CHECKID(IID_IDirectSoundCaptureBuffer8)
        CHECKID(IID_IDirectSoundFullDuplex)
        CHECKID(IID_IKsPropertySet)
        CHECKID(IID_IClassFactory)
        CHECKID(IID_IUnknown)
        if(mIdStr) return;

        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

    void store_propid(const GUID &guid)
    {
        if(false) { }
        CHECKID(DSPROPSETID_EAX10_ListenerProperties)
        CHECKID(DSPROPSETID_EAX10_BufferProperties)
        CHECKID(DSPROPSETID_EAX20_ListenerProperties)
        CHECKID(DSPROPSETID_EAX20_BufferProperties)
        CHECKID(DSPROPSETID_EAX30_ListenerProperties)
        CHECKID(DSPROPSETID_EAX30_BufferProperties)
        CHECKID(EAXPROPERTYID_EAX40_Context)
        CHECKID(EAXPROPERTYID_EAX40_FXSlot0)
        CHECKID(EAXPROPERTYID_EAX40_FXSlot1)
        CHECKID(EAXPROPERTYID_EAX40_FXSlot2)
        CHECKID(EAXPROPERTYID_EAX40_FXSlot3)
        CHECKID(EAXPROPERTYID_EAX40_Source)
        CHECKID(DSPROPSETID_VoiceManager)
        if(mIdStr) return;

        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

    void store_ds3dalg(const GUID &guid)
    {
        if(false) { }
        CHECKID(DS3DALG_DEFAULT)
        CHECKID(DS3DALG_NO_VIRTUALIZATION)
        CHECKID(DS3DALG_HRTF_FULL)
        CHECKID(DS3DALG_HRTF_LIGHT)
        if(mIdStr) return;

        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

    void store_devid(const GUID &guid)
    {
        if(guid == GUID_NULL) mIdStr = "GUID_NULL";
        CHECKID(DSDEVID_DefaultPlayback)
        CHECKID(DSDEVID_DefaultCapture)
        CHECKID(DSDEVID_DefaultVoicePlayback)
        CHECKID(DSDEVID_DefaultVoiceCapture)
        if(mIdStr) return;

        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

    void store_clsid(const GUID &guid)
    {
        if(false) { }
        CHECKID(CLSID_DirectSound)
        CHECKID(CLSID_DirectSound8)
        CHECKID(CLSID_DirectSoundCapture)
        CHECKID(CLSID_DirectSoundCapture8)
        CHECKID(CLSID_DirectSoundFullDuplex)
        if(mIdStr) return;

        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg;
    }

public:
    GuidPrinter(const GUID &guid) { store(guid); }
    GuidPrinter(IidTag, const GUID &guid) { store_iid(guid); }
    GuidPrinter(ClsidTag, const GUID &guid) { store_clsid(guid); }
    GuidPrinter(DevidTag, const GUID &guid) { store_devid(guid); }
    GuidPrinter(PropidTag, const GUID &guid) { store_propid(guid); }
    GuidPrinter(Ds3dalgTag, const GUID &guid) { store_ds3dalg(guid); }

    GuidPrinter(const GUID *guid) { if(!guid) mIdStr = "{null}"; else store(*guid); }
    GuidPrinter(ClsidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_clsid(*guid); }
    GuidPrinter(DevidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_devid(*guid); }
    GuidPrinter(PropidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_propid(*guid); }
    GuidPrinter(Ds3dalgTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_ds3dalg(*guid); }

    const char *c_str() const { return mIdStr; }
};

class IidPrinter : public GuidPrinter {
public:
    template<typename T>
    IidPrinter(T&& guid) : GuidPrinter{IidTag{}, std::forward<T>(guid)} { }
};

class ClsidPrinter : public GuidPrinter {
public:
    template<typename T>
    ClsidPrinter(T&& guid) : GuidPrinter{ClsidTag{}, std::forward<T>(guid)} { }
};

class PropidPrinter : public GuidPrinter {
public:
    template<typename T>
    PropidPrinter(T&& guid) : GuidPrinter{PropidTag{}, std::forward<T>(guid)} { }
};

class DevidPrinter : public GuidPrinter {
public:
    template<typename T>
    DevidPrinter(T&& guid) : GuidPrinter{DevidTag{}, std::forward<T>(guid)} { }
};

class Ds3dalgPrinter : public GuidPrinter {
public:
    template<typename T>
    Ds3dalgPrinter(T&& guid) : GuidPrinter{Ds3dalgTag{}, std::forward<T>(guid)} { }
};

#endif // GUIDPRINTER_H
