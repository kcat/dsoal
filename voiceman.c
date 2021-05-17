#include "dsound_private.h"

HRESULT VoiceMan_Query(DSBuffer *buf, DWORD propid, ULONG *pTypeSupport) {
    (void)buf;
    
    switch (propid) {
        case DSPROPERTY_VMANAGER_MODE:
            TRACE("DSPROPERTY_VMANAGER_MODE\n");
            *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
            
            return DS_OK;
            
        case DSPROPERTY_VMANAGER_PRIORITY:
            TRACE("DSPROPERTY_VMANAGER_PRIORITY\n");
            *pTypeSupport = KSPROPERTY_SUPPORT_GET | KSPROPERTY_SUPPORT_SET;
            
            return DS_OK;
            
        case DSPROPERTY_VMANAGER_STATE:
            TRACE("DSPROPERTY_VMANAGER_STATE\n");
            *pTypeSupport = KSPROPERTY_SUPPORT_GET;
            
            return DS_OK;
    }
    
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT VoiceMan_Set(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData) {
    switch (propid) {
        case DSPROPERTY_VMANAGER_MODE:
            if (cbPropData >= sizeof(DWORD) && *(DWORD*)pPropData < VMANAGER_MODE_MAX) {
                TRACE("DSPROPERTY_VMANAGER_MODE set: %ld\n", *(DWORD*)pPropData);
                buf->share->vm_managermode = *(DWORD*)pPropData;
                
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
            
        case DSPROPERTY_VMANAGER_PRIORITY:
            if (cbPropData >= sizeof(DWORD)) {
                TRACE("DSPROPERTY_VMANAGER_PRIORITY set: %ld\n", *(DWORD*)pPropData);
                buf->vm_voicepriority = *(DWORD*)pPropData;
                
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
    }
    
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}

HRESULT VoiceMan_Get(DSBuffer *buf, DWORD propid, void *pPropData, ULONG cbPropData, ULONG *pcbReturned) {
    *pcbReturned = 0;
    
    switch (propid) {
        case DSPROPERTY_VMANAGER_MODE:
            if (cbPropData >= sizeof(DWORD)) {
                *pcbReturned = sizeof(DWORD);
                
                *(DWORD*)pPropData = buf->share->vm_managermode;
                TRACE("DSPROPERTY_VMANAGER_MODE get %ld\n", *(DWORD*)pPropData);
                
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
            
        case DSPROPERTY_VMANAGER_PRIORITY:
            if (cbPropData >= sizeof(DWORD)) {
                *pcbReturned = sizeof(DWORD);
                
                *(DWORD*)pPropData = buf->vm_voicepriority;
                TRACE("DSPROPERTY_VMANAGER_PRIORITY get %ld\n", *(DWORD*)pPropData);
                
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
            
        case DSPROPERTY_VMANAGER_STATE:
            if (cbPropData >= sizeof(DWORD)) {
                *pcbReturned = sizeof(DWORD);
                
                /* FIXME: dubious handling */
                if (buf->isplaying) {
                    *(DWORD*)pPropData = DSPROPERTY_VMANAGER_STATE_PLAYING3DHW;
                } else {
                    *(DWORD*)pPropData = DSPROPERTY_VMANAGER_STATE_SILENT;
                }
                TRACE("DSPROPERTY_VMANAGER_STATE get %ld\n", *(DWORD*)pPropData);
                
                return DS_OK;
            }
            return DSERR_INVALIDPARAM;
    }
    
    FIXME("Unhandled propid: 0x%08lx\n", propid);
    return E_PROP_ID_UNSUPPORTED;
}