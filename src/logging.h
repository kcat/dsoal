#ifndef LOGGING_H
#define LOGGING_H

#include <cstdio>

#include "dsoal.h"

#ifdef __has_cpp_attribute
#define HAS_ATTRIBUTE __has_cpp_attribute
#else
#define HAS_ATTRIBUTE(...) (0)
#endif

#if HAS_ATTRIBUTE(likely)
#define LIKELY [[likely]]
#define UNLIKELY [[unlikely]]
#else
#define LIKELY
#define UNLIKELY
#endif


enum class LogLevel {
    Disable,
    Error,
    Fixme,
    Warning,
    Trace,
    Debug
};
inline LogLevel gLogLevel{LogLevel::Error};

inline gsl::owner<FILE*> gLogFile{};

#if HAS_ATTRIBUTE(gnu::format)
#if defined(__USE_MINGW_ANSI_STDIO) && !defined(__clang__)
[[gnu::format(gnu_printf,2,3)]]
#else
[[gnu::format(printf,2,3)]]
#endif
#endif
void dsoal_print(LogLevel level, const char *fmt, ...);

#define DEBUG(...) do {                                                       \
    if(gLogLevel >= LogLevel::Debug) UNLIKELY                                 \
        dsoal_print(LogLevel::Debug, __VA_ARGS__);                            \
} while(0)

#define TRACE(...) do {                                                       \
    if(gLogLevel >= LogLevel::Trace) UNLIKELY                                 \
        dsoal_print(LogLevel::Trace, __VA_ARGS__);                            \
} while(0)

#define WARN(...) do {                                                        \
    if(gLogLevel >= LogLevel::Warning) UNLIKELY                               \
        dsoal_print(LogLevel::Warning, __VA_ARGS__);                          \
} while(0)

#define FIXME(...) do {                                                       \
    if(gLogLevel >= LogLevel::Fixme) UNLIKELY                                 \
        dsoal_print(LogLevel::Fixme, __VA_ARGS__);                            \
} while(0)

#define ERR(...) do {                                                         \
    if(gLogLevel >= LogLevel::Error) UNLIKELY                                 \
        dsoal_print(LogLevel::Error, __VA_ARGS__);                            \
} while(0)

#endif // LOGGING_H
