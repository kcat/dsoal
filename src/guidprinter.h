#ifndef GUIDPRINTER_H
#define GUIDPRINTER_H

#include <array>
#include <cstdio>
#include <cstring>
#include <iterator>

#include <dsound.h>
#include <dsconf.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmreg.h>
#include <objbase.h>

#include "eax.h"
#include "vmanager.h"


struct IidTag { };
struct ClsidTag { };
struct PropidTag { };
struct DevidTag { };
struct Ds3dalgTag { };
struct FmtidTag { };
struct DsfxTag { };

class GuidPrinter {
    std::array<char,48> mMsg{};
    const char *mIdStr{};

    void store(const GUID &guid)
    {
        std::snprintf(mMsg.data(), mMsg.size(),
            "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        mIdStr = mMsg.data();
    }

    void store_iid(const GUID &guid)
    {
        if(false) { }
#define CHECKID(x) else if(guid == x) mIdStr = #x;
        CHECKID(GUID_NULL)
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

        store(guid);
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
        CHECKID(DSPROPSETID_DirectSoundDevice)
        if(mIdStr) return;

        store(guid);
    }

    void store_dsfxid(const GUID &guid)
    {
        if(false) { }
        CHECKID(GUID_DSFX_STANDARD_CHORUS)
        CHECKID(GUID_DSFX_STANDARD_COMPRESSOR)
        CHECKID(GUID_DSFX_STANDARD_DISTORTION)
        CHECKID(GUID_DSFX_STANDARD_ECHO)
        CHECKID(GUID_DSFX_STANDARD_FLANGER)
        CHECKID(GUID_DSFX_STANDARD_GARGLE)
        CHECKID(GUID_DSFX_STANDARD_I3DL2REVERB)
        CHECKID(GUID_DSFX_STANDARD_PARAMEQ)
        CHECKID(GUID_DSFX_WAVES_REVERB)
        if(mIdStr) return;

        store(guid);
    }

    void store_ds3dalg(const GUID &guid)
    {
        if(false) { }
        CHECKID(DS3DALG_DEFAULT)
        CHECKID(DS3DALG_NO_VIRTUALIZATION)
        CHECKID(DS3DALG_HRTF_FULL)
        CHECKID(DS3DALG_HRTF_LIGHT)
        if(mIdStr) return;

        store(guid);
    }

    void store_devid(const GUID &guid)
    {
        if(false) { }
        CHECKID(GUID_NULL)
        CHECKID(DSDEVID_DefaultPlayback)
        CHECKID(DSDEVID_DefaultCapture)
        CHECKID(DSDEVID_DefaultVoicePlayback)
        CHECKID(DSDEVID_DefaultVoiceCapture)
        if(mIdStr) return;

        store(guid);
    }

    void store_clsid(const GUID &guid)
    {
        if(false) { }
        CHECKID(CLSID_DirectSound)
        CHECKID(CLSID_DirectSound8)
        CHECKID(CLSID_DirectSoundCapture)
        CHECKID(CLSID_DirectSoundCapture8)
        CHECKID(CLSID_DirectSoundFullDuplex)
        CHECKID(CLSID_DirectSoundPrivate)
        if(mIdStr) return;

        store(guid);
    }

    void store_fmtid(const GUID &guid)
    {
        if(false) { }
        CHECKID(KSDATAFORMAT_SUBTYPE_PCM)
        CHECKID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
#undef CHECKID
        if(mIdStr) return;

        store(guid);
    }

public:
    GuidPrinter(const GUID &guid) { store(guid); }
    GuidPrinter(IidTag, const GUID &guid) { store_iid(guid); }
    GuidPrinter(ClsidTag, const GUID &guid) { store_clsid(guid); }
    GuidPrinter(DevidTag, const GUID &guid) { store_devid(guid); }
    GuidPrinter(PropidTag, const GUID &guid) { store_propid(guid); }
    GuidPrinter(Ds3dalgTag, const GUID &guid) { store_ds3dalg(guid); }
    GuidPrinter(FmtidTag, const GUID &guid) { store_fmtid(guid); }
    GuidPrinter(DsfxTag, const GUID &guid) { store_dsfxid(guid); }

    GuidPrinter(const GUID *guid) { if(!guid) mIdStr = "{null}"; else store(*guid); }
    GuidPrinter(ClsidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_clsid(*guid); }
    GuidPrinter(DevidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_devid(*guid); }
    GuidPrinter(PropidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_propid(*guid); }
    GuidPrinter(Ds3dalgTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_ds3dalg(*guid); }
    GuidPrinter(FmtidTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_fmtid(*guid); }
    GuidPrinter(DsfxTag, const GUID *guid) { if(!guid) mIdStr = "{null}"; else store_dsfxid(*guid); }

    [[nodiscard]]
    const char *c_str() const { return mIdStr; }
};

class IidPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,IidPrinter>,bool> = true>
    IidPrinter(T&& guid) : GuidPrinter{IidTag{}, std::forward<T>(guid)} { }
};

class ClsidPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,ClsidPrinter>,bool> = true>
    ClsidPrinter(T&& guid) : GuidPrinter{ClsidTag{}, std::forward<T>(guid)} { }
};

class PropidPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,PropidPrinter>,bool> = true>
    PropidPrinter(T&& guid) : GuidPrinter{PropidTag{}, std::forward<T>(guid)} { }
};

class DevidPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,DevidPrinter>,bool> = true>
    DevidPrinter(T&& guid) : GuidPrinter{DevidTag{}, std::forward<T>(guid)} { }
};

class Ds3dalgPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,Ds3dalgPrinter>,bool> = true>
    Ds3dalgPrinter(T&& guid) : GuidPrinter{Ds3dalgTag{}, std::forward<T>(guid)} { }
};

class FmtidPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,FmtidPrinter>,bool> = true>
    FmtidPrinter(T&& guid) : GuidPrinter{FmtidTag{}, std::forward<T>(guid)} { }
};

class DsfxPrinter : public GuidPrinter {
public:
    template<typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>,DsfxPrinter>,bool> = true>
    DsfxPrinter(T&& guid) : GuidPrinter{DsfxTag{}, std::forward<T>(guid)} { }
};

#endif // GUIDPRINTER_H
