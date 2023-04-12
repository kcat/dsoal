#include "dsoal.h"

#include <cstdlib>

#include "logging.h"


extern "C" {

DSOAL_EXPORT BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, void *reserved)
{
    DEBUG("DllMain (%p, %lu, %p)\n", hInstDLL, reason, reserved);

    switch(reason)
    {
    case DLL_PROCESS_ATTACH:
        if(const WCHAR *wstr{_wgetenv(L"DSOAL_LOGFILE")}; wstr && wstr[0] != 0)
        {
            FILE *f = _wfopen(wstr, L"wt");
            if(!f) ERR("Failed to open log file %ls\n", wstr);
            else gLogFile = f;
        }

        if(const char *str{std::getenv("DSOAL_LOGLEVEL")}; str && str[0] != 0)
        {
            char *endptr{};
            const auto level = std::strtol(str, &endptr, 0);
            if(!endptr || *endptr != 0)
                ERR("Invalid log level specified: \"%s\"\n", str);
            else
            {
                if(level < static_cast<long>(LogLevel::Disable))
                    gLogLevel = LogLevel::Disable;
                else if(level > static_cast<long>(LogLevel::Debug))
                    gLogLevel = LogLevel::Debug;
                else
                    gLogLevel = static_cast<LogLevel>(level);
            }
        }

        /* Increase refcount on dsound by 1 */
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)hInstDLL, &hInstDLL);
        break;

    case DLL_PROCESS_DETACH:
        if(gLogFile != stderr)
            fclose(gLogFile);
        gLogFile = stderr;
    }

    return TRUE;
}

HRESULT WINAPI DSOAL_DirectSoundCreate(const GUID *deviceId, IDirectSound **ds, IUnknown *outer)
{
    TRACE("DirectSoundCreate (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCreate8(const GUID *deviceId, IDirectSound8 **ds, IUnknown *outer)
{
    TRACE("DirectSoundCreate8 (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate(const GUID *deviceId, IDirectSoundCapture **ds,
    IUnknown *outer)
{
    TRACE("DirectSoundCaptureCreate (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureCreate8(const GUID *deviceId, IDirectSoundCapture8 **ds,
    IUnknown *outer)
{
    TRACE("DirectSoundCaptureCreate8 (%p, %p, %p)\n", deviceId, ds, outer);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundFullDuplexCreate(const GUID *captureDevice,
    const GUID *renderDevice, const DSCBUFFERDESC *captureBufferDesc,
    const DSBUFFERDESC *renderBufferDesc, HWND hWnd, DWORD coopLevel,
    IDirectSoundFullDuplex **fullDuplex, IDirectSoundCaptureBuffer8 **captureBuffer8,
    IDirectSoundBuffer8 **renderBuffer8, IUnknown *outer)
{
    TRACE("DirectSoundFullDuplexCreate\n");
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr)
{
    TRACE("DirectSoundEnumerateA (%p, %p)\n", callback, userPtr);
    return E_NOTIMPL;
}
HRESULT WINAPI DSOAL_DirectSoundEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr)
{
    TRACE("DirectSoundEnumerateW (%p, %p)\n", callback, userPtr);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA callback, void *userPtr)
{
    TRACE("DirectSoundCaptureEnumerateA (%p, %p)\n", callback, userPtr);
    return E_NOTIMPL;
}
HRESULT WINAPI DSOAL_DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW callback, void *userPtr)
{
    TRACE("DirectSoundCaptureEnumerateW (%p, %p)\n", callback, userPtr);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_DllCanUnloadNow()
{
    TRACE("DllCanUnloadNow\n");
    return S_OK;
}

HRESULT WINAPI DSOAL_DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    TRACE("DllGetClassObject (%p, %p, %p)\n", &rclsid, &riid, ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI DSOAL_GetDeviceID(const GUID *guidSrc, GUID *guidDst)
{
    TRACE("GetDeviceID (%p, %p)\n", guidSrc, guidDst);
    return E_NOTIMPL;
}

} // extern "C"
