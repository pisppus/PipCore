#pragma once

#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP

#include <Input/Touch.hpp>

namespace pipcore::desktop
{
    class Touch final : public pipcore::Touch
    {
    public:
        [[nodiscard]] bool configure(const pipcore::TouchConfig &cfg) noexcept override
        {
            _point = {};
            _down = false;
            _lastDown = false;
            return true;
        }

        [[nodiscard]] bool begin() noexcept override { return true; }
        void end() noexcept override {}

        void update() noexcept override
        {
            if (_down)
                _point.state = _lastDown ? pipcore::TouchState::Held : pipcore::TouchState::Pressed;
            else
                _point.state = pipcore::TouchState::Released;
            _lastDown = _down;
        }

        [[nodiscard]] bool ready() const noexcept override { return true; }

        [[nodiscard]] uint8_t count() const noexcept override { return (_down || _lastDown) ? 1 : 0; }

        [[nodiscard]] pipcore::TouchPoint point(uint8_t index) const noexcept override
        {
            if (index == 0 && (_down || _lastDown))
            {
                return _point;
            }
            return {};
        }

        void injectPointer(bool down, uint16_t x, uint16_t y) noexcept
        {
            _down = down;
            if (down)
            {
                _point.x = x;
                _point.y = y;
                _point.id = 0;
            }
        }

    private:
        pipcore::TouchPoint _point = {};
        bool _down = false;
        bool _lastDown = false;
    };
}

#endif
