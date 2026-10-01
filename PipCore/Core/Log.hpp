#pragma once

#include <cstddef>
#include <cstdarg>

#include <Log.hpp>

namespace pipcore::log::detail
{
    inline constexpr size_t kLineMax = 512;

    using OutputFn = void (*)(Level level, const char *fmt, std::va_list args) noexcept;

    void setOutput(OutputFn output) noexcept;

    [[nodiscard]] bool sinkActive() noexcept;

    void emitSink(Level level, const char *line) noexcept;
}
