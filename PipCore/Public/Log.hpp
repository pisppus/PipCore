#pragma once

#include <cstdarg>
#include <cstdint>

#include "Config.hpp"

namespace pipcore::log
{
    enum class Level : uint8_t
    {
        Verbose = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Off = 5
    };

    using Sink = void (*)(void *user, Level level, const char *line) noexcept;

    void setLevel(Level level) noexcept;
    [[nodiscard]] Level level() noexcept;
    [[nodiscard]] bool enabled(Level level) noexcept;

    void setSink(Sink sink, void *user) noexcept;

    void vprint(Level level, const char *fmt, std::va_list args) noexcept;
    void print(Level level, const char *fmt, ...) noexcept;

    inline void verbose(const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(Level::Verbose, fmt, args);
        va_end(args);
    }

    inline void debug(const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(Level::Debug, fmt, args);
        va_end(args);
    }

    inline void info(const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(Level::Info, fmt, args);
        va_end(args);
    }

    inline void warning(const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(Level::Warning, fmt, args);
        va_end(args);
    }

    inline void error(const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(Level::Error, fmt, args);
        va_end(args);
    }
}
