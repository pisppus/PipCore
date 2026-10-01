#include "Core/Log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace pipcore::log
{
    namespace
    {

#ifndef PIPCORE_LOG_DEFAULT_LEVEL
#ifdef CONFIG_PIPCORE_LOG_LEVEL
#define PIPCORE_LOG_DEFAULT_LEVEL CONFIG_PIPCORE_LOG_LEVEL
#else
#define PIPCORE_LOG_DEFAULT_LEVEL 2
#endif
#endif

        std::atomic<uint8_t> sLevel{PIPCORE_LOG_DEFAULT_LEVEL};

        Sink sSink = nullptr;
        void *sSinkUser = nullptr;

        detail::OutputFn sOutput = nullptr;

        void defaultOutput(Level level, const char *fmt, std::va_list args) noexcept
        {
            char line[detail::kLineMax];
            std::vsnprintf(line, sizeof(line), fmt, args);
            std::fputs(line, stdout);
            std::fputc('\n', stdout);
            std::fflush(stdout);
            detail::emitSink(level, line);
        }

    }

    void setLevel(Level level) noexcept
    {
        sLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
    }

    Level level() noexcept
    {
        return static_cast<Level>(sLevel.load(std::memory_order_relaxed));
    }

    bool enabled(Level level) noexcept
    {
        return static_cast<uint8_t>(level) >= sLevel.load(std::memory_order_relaxed);
    }

    void setSink(Sink sink, void *user) noexcept
    {
        sSink = sink;
        sSinkUser = user;
    }

    namespace detail
    {
        void setOutput(OutputFn output) noexcept
        {
            sOutput = output;
        }

        bool sinkActive() noexcept
        {
            return sSink != nullptr;
        }

        void emitSink(Level level, const char *line) noexcept
        {
            if (sSink)
                sSink(sSinkUser, level, line);
        }
    }

    void vprint(Level level, const char *fmt, std::va_list args) noexcept
    {
        if (!enabled(level))
            return;

        if (sOutput)
            sOutput(level, fmt, args);
        else
            defaultOutput(level, fmt, args);
    }

    void print(Level level, const char *fmt, ...) noexcept
    {
        std::va_list args;
        va_start(args, fmt);
        vprint(level, fmt, args);
        va_end(args);
    }
}
