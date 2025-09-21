#ifndef LOGGING_H
#define LOGGING_H

#include <fstream>
#include <type_traits>

#include "dsoal.h"
#include "fmt/core.h"


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
inline auto gLogLevel = LogLevel::Error;

inline auto gLogFile = std::ofstream{};

void dsoal_print_impl(LogLevel level, const fmt::string_view fmt, fmt::format_args args);

template<typename ...Args>
void dsoal_print(LogLevel level, fmt::format_string<Args...> fmt, Args&& ...args) noexcept
try {
    dsoal_print_impl(level, fmt, fmt::make_format_args(args...));
} catch(...) { }


#define DEBUG(...) do {                                                       \
    if(gLogLevel >= LogLevel::Debug) [[unlikely]]                             \
        dsoal_print(LogLevel::Debug, PREFIX __VA_ARGS__);                     \
} while(0)

#define TRACE(...) do {                                                       \
    if(gLogLevel >= LogLevel::Trace) [[unlikely]]                             \
        dsoal_print(LogLevel::Trace, PREFIX __VA_ARGS__);                     \
} while(0)

#define WARN(...) do {                                                        \
    if(gLogLevel >= LogLevel::Warning) [[unlikely]]                           \
        dsoal_print(LogLevel::Warning, PREFIX __VA_ARGS__);                   \
} while(0)

#define FIXME(...) do {                                                       \
    if(gLogLevel >= LogLevel::Fixme) [[unlikely]]                             \
        dsoal_print(LogLevel::Fixme, PREFIX __VA_ARGS__);                     \
} while(0)

#define ERR(...) do {                                                         \
    if(gLogLevel >= LogLevel::Error) [[unlikely]]                             \
        dsoal_print(LogLevel::Error, PREFIX __VA_ARGS__);                     \
} while(0)

#endif // LOGGING_H
