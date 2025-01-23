#ifndef LOGGING_H
#define LOGGING_H

#include <type_traits>

#include "dsoal.h"
#include "fmt/core.h"

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

namespace ds {

template<typename T>
constexpr auto to_underlying(T e) noexcept -> std::underlying_type_t<T>
{ return static_cast<std::underlying_type_t<T>>(e); }

} // namespace ds

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

void dsoal_print_impl(LogLevel level, const fmt::string_view fmt, fmt::format_args args);

template<typename ...Args>
void dsoal_print(LogLevel level, fmt::format_string<Args...> fmt, Args&& ...args) noexcept
try {
    dsoal_print_impl(level, fmt, fmt::make_format_args(args...));
} catch(...) { }


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
