#include "logging.h"

#include <mutex>
#include <string_view>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "fmt/core.h"

namespace {

using namespace std::string_view_literals;

std::mutex sLogMutex;

} // namespace

void dsoal_print_impl(LogLevel level, const fmt::string_view fmt, fmt::format_args args)
{
    const auto msg = fmt::vformat(fmt, std::move(args));

    auto prefix = "debug"sv;
    switch(level)
    {
    case LogLevel::Disable: break;
    case LogLevel::Debug: break;
    case LogLevel::Error: prefix = "err"sv; break;
    case LogLevel::Warning: prefix = "warn"sv; break;
    case LogLevel::Fixme: prefix = "fixme"sv; break;
    case LogLevel::Trace: prefix = "trace"sv; break;
    }

    auto _ = std::lock_guard{sLogMutex};
    auto *logfile = gLogFile ? gLogFile : stderr;
    fmt::println(logfile, "{:04X}:{}:dsound: {}", GetCurrentThreadId(), prefix, msg);
    fflush(logfile);
}
