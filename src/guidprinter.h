#ifndef GUIDPRINTER_H
#define GUIDPRINTER_H

#include <cstdio>
#include <cstring>
#include <iterator>
#include <objbase.h>


class GuidPrinter {
    char mMsg[64];

    void store(const GUID &guid)
    {
        std::snprintf(mMsg, std::size(mMsg), "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            DWORD{guid.Data1}, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
            guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }

public:
    GuidPrinter(const GUID &guid) { store(guid); }
    GuidPrinter(const GUID *guid)
    {
        if(!guid)
            std::strcpy(mMsg, "<null>");
        else
            store(*guid);
    }

    const char *c_str() const { return mMsg; }
};


#endif // GUIDPRINTER_H
