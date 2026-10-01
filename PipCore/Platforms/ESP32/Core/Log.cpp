#include <cstdarg>
#include <cstdio>

#include <esp_log.h>

#include "Core/Log.hpp"
#include <Log.hpp>

namespace pipcore::log
{
    namespace
    {
        constexpr const char *kTag = "pipcore";

        esp_log_level_t toEspLevel(Level level) noexcept
        {
            static constexpr esp_log_level_t kMap[] = {ESP_LOG_VERBOSE, ESP_LOG_DEBUG, ESP_LOG_INFO, ESP_LOG_WARN,
                                                       ESP_LOG_ERROR, ESP_LOG_NONE};
            const auto idx = static_cast<uint8_t>(level);
            return kMap[idx < 6 ? idx : 5];
        }

        void espOutput(Level level, const char *fmt, std::va_list args) noexcept
        {
            if (detail::sinkActive())
            {

                char line[detail::kLineMax];
                std::vsnprintf(line, sizeof(line), fmt, args);
                detail::emitSink(level, line);
                esp_log_write(toEspLevel(level), kTag, "%s", line);
            }
            else
            {
                esp_log_writev(toEspLevel(level), kTag, fmt, args);
            }
        }

        struct BackendRegistrar
        {
            BackendRegistrar() noexcept { detail::setOutput(&espOutput); }
        };

        const BackendRegistrar s_registrar;
    }
}
