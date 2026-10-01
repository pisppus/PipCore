#include "Core/Memory.hpp"

namespace pipcore::debug
{
    constinit MemoryHandler g_memoryHandler = nullptr;

    void setMemoryHandler(MemoryHandler handler) noexcept
    {
        g_memoryHandler = handler;
    }
}
