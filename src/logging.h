#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#ifdef __has_cpp_attribute
#define HAS_ATTRIBUTE __has_cpp_attribute
#else
#define HAS_ATTRIBUTE(...)
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
    Warning,
    Trace,
    Debug
};
extern LogLevel gLogLevel;

extern FILE *gLogFile;

#if HAS_ATTRIBUTE(gnu::format)
#ifdef __USE_MINGW_ANSI_STDIO
[[gnu::format(gnu_printf,3,4)]]
#else
[[gnu::format(printf,3,4)]]
#endif
#endif
void dsoal_print(LogLevel level, FILE *logfile, const char *fmt, ...);

#define DEBUG(...) do {                                                       \
    if(gLogLevel >= LogLevel::Debug) UNLIKELY                                 \
        dsoal_print(LogLevel::Trace, gLogFile, __VA_ARGS__);                  \
} while(0)

#define TRACE(...) do {                                                       \
    if(gLogLevel >= LogLevel::Trace) UNLIKELY                                 \
        dsoal_print(LogLevel::Trace, gLogFile, __VA_ARGS__);                  \
} while(0)

#define WARN(...) do {                                                        \
    if(gLogLevel >= LogLevel::Warning) UNLIKELY                               \
        dsoal_print(LogLevel::Warning, gLogFile, __VA_ARGS__);                \
} while(0)

#define ERR(...) do {                                                         \
    if(gLogLevel >= LogLevel::Error) UNLIKELY                                 \
        dsoal_print(LogLevel::Error, gLogFile, __VA_ARGS__);                  \
} while(0)

#endif // LOGGING_H
