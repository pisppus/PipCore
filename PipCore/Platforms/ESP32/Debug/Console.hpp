#pragma once

#include "Config.hpp"

namespace pipcore::debug
{
    class Console
    {
    public:
        static Console &instance() noexcept;
        void begin() noexcept;

    private:
        Console() = default;
        bool _started = false;
    };
}
