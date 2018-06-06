#ifdef __WINESRC__
#error This file should not be compiled with Wine.
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>
#include "dsound.h"

/* allocate some tmp string space */
/* FIXME: this is not 100% thread-safe */
static char *get_temp_buffer( size_t size )
{
    static char *list[32];
    static LONG pos;
    char *ret;
    int idx;

    idx = InterlockedExchangeAdd( &pos, 1 ) % (sizeof(list)/sizeof(list[0]));
    if ((ret = realloc( list[idx], size ))) list[idx] = ret;
    return ret;
}


/* release unused part of the buffer */
static void release_temp_buffer( char *buffer, size_t size )
{
    /* don't bother doing anything */
    (void)buffer;
    (void)size;
}

/* printf with temp buffer allocation */
const char *wine_dbg_sprintf( const char *format, ... )
{
    static const int max_size = 200;
    char *ret;
    int len;
    va_list valist;

    va_start(valist, format);
    ret = get_temp_buffer( max_size );
    len = vsnprintf( ret, max_size, format, valist );
    if (len == -1 || len >= max_size) ret[max_size-1] = 0;
    else release_temp_buffer( ret, len + 1 );
    va_end(valist);
    return ret;
}

/* default implementation of wine_dbgstr_wn */
const char *wine_dbgstr_wn( const WCHAR *str, int n )
{
    char *dst, *res;
    size_t size;

    if (!((ULONG_PTR)str >> 16))
    {
        if (!str) return "(null)";
        res = get_temp_buffer( 6 );
        sprintf( res, "#%04x", LOWORD(str) );
        return res;
    }
    if (n == -1)
    {
        const WCHAR *end = str;
        while (*end) end++;
        n = end - str;
    }
    if (n < 0) n = 0;
    size = 12 + min( 300, n * 5 );
    dst = res = get_temp_buffer( size );
    *dst++ = 'L';
    *dst++ = '"';
    while (n-- > 0 && dst <= res + size - 10)
    {
        WCHAR c = *str++;
        switch (c)
        {
        case '\n': *dst++ = '\\'; *dst++ = 'n'; break;
        case '\r': *dst++ = '\\'; *dst++ = 'r'; break;
        case '\t': *dst++ = '\\'; *dst++ = 't'; break;
        case '"':  *dst++ = '\\'; *dst++ = '"'; break;
        case '\\': *dst++ = '\\'; *dst++ = '\\'; break;
        default:
            if (c >= ' ' && c <= 126)
                *dst++ = (char)c;
            else
            {
                *dst++ = '\\';
                sprintf(dst,"%04x",c);
                dst+=4;
            }
        }
    }
    *dst++ = '"';
    if (n > 0)
    {
        *dst++ = '.';
        *dst++ = '.';
        *dst++ = '.';
    }
    *dst++ = 0;
    release_temp_buffer( res, dst - res );
    return res;
}

const char *debugstr_guid( const GUID *id )
{
    if (!id) return "(null)";
    if (!((ULONG_PTR)id >> 16)) return wine_dbg_sprintf( "<guid-0x%04hx>", (WORD)(ULONG_PTR)id );
    return wine_dbg_sprintf( "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                             id->Data1, id->Data2, id->Data3,
                             id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3],
                             id->Data4[4], id->Data4[5], id->Data4[6], id->Data4[7] );
}
