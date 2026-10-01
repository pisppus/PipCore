#pragma once

#include "Display/ST/Display.hpp"
#include "Display/ST/Driver.hpp"

namespace pipcore::st7789
{
    using Driver = pipcore::detail::StDriver<pipcore::detail::StDisplayType::ST7789>;
    using Transport = pipcore::display::Transport;

    class Display final : public pipcore::detail::StDisplay<Driver>
    {
    public:
        Display() = default;

        [[nodiscard]] bool configure(pipcore::Platform *platform, Transport *transport, uint16_t width, uint16_t height,
                                     uint8_t order = 0, bool invert = true, bool swap = false, int16_t xOffset = 0,
                                     int16_t yOffset = 0)
        {
            return StDisplay<Driver>::configureBase(platform, transport, width, height, order, invert, swap, xOffset,
                                                    yOffset);
        }
    };
}
