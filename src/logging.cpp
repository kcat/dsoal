#include "logging.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


LogLevel gLogLevel{LogLevel::Error};

FILE *gLogFile{stderr};

namespace {

std::mutex sLogMutex;

} // namespace

void dsoal_print(LogLevel level, FILE *logfile, const char *fmt, ...)
{
    const char *prefix = "debug";
    switch(level)
    {
    case LogLevel::Disable: break;
    case LogLevel::Debug: break;
    case LogLevel::Error: prefix = "err"; break;
    case LogLevel::Fixme: prefix = "fixme"; break;
    case LogLevel::Warning: prefix = "warn"; break;
    case LogLevel::Trace: prefix = "trace"; break;
    }

    std::vector<char> dynmsg;
    std::array<char,256> stcmsg{};
    char *str{stcmsg.data()};

    const auto threadId = GetCurrentThreadId();
    int prefixlen{std::snprintf(str, stcmsg.size(), "%04lx:%s:dsound:", threadId, prefix)};
    if(prefixlen < 0) prefixlen = 0;
    const auto uprefixlen = static_cast<unsigned int>(prefixlen);

    std::va_list args, args2;
    va_start(args, fmt);
    va_copy(args2, args);
    const int msglen{std::vsnprintf(str+uprefixlen, stcmsg.size()-uprefixlen, fmt, args)};
    if(msglen >= 0 && static_cast<size_t>(msglen)+uprefixlen >= stcmsg.size()) UNLIKELY
    {
        dynmsg.resize(static_cast<size_t>(msglen)+uprefixlen + 1u);
        str = dynmsg.data();

        std::copy_n(stcmsg.data(), uprefixlen, str);

        std::vsnprintf(str+uprefixlen, dynmsg.size()-uprefixlen, fmt, args2);
    }
    va_end(args2);
    va_end(args);

    std::lock_guard _{sLogMutex};
    fputs(str, logfile);
    fflush(logfile);
}
