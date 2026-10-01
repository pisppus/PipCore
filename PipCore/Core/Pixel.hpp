#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Config.hpp"

namespace pipcore::util
{
    [[nodiscard]] inline constexpr uint16_t swap16(uint16_t v) noexcept
    {
        return __builtin_bswap16(v);
    }

    inline void PIPCORE_HOT copySwap565(uint16_t *dst, const uint16_t *src, size_t pixels) noexcept
    {
        if (pixels == 0)
            return;

        const bool canUse32 = (((reinterpret_cast<uintptr_t>(src) | reinterpret_cast<uintptr_t>(dst)) & 3U) == 0U);

        if (canUse32) [[likely]]
        {
            auto *dst32 = reinterpret_cast<uint32_t *>(dst);
            auto *src32 = reinterpret_cast<const uint32_t *>(src);
            size_t pairs = pixels >> 1;

            while (pairs--)
            {
                const uint32_t p = __builtin_bswap32(*src32++);
                *dst32++ = (p >> 16) | (p << 16);
            }

            src = reinterpret_cast<const uint16_t *>(src32);
            dst = reinterpret_cast<uint16_t *>(dst32);
            pixels &= 1U;
        }

        while (pixels--)
            *dst++ = __builtin_bswap16(*src++);
    }

    inline void PIPCORE_HOT fillSwap565(uint16_t *dst, size_t pixels, uint16_t color565) noexcept
    {
        if (pixels == 0)
            return;

        const uint16_t v = __builtin_bswap16(color565);

        if ((v >> 8) == (v & 0xFFU)) [[likely]]
        {
            std::memset(dst, v & 0xFFU, pixels * sizeof(uint16_t));
            return;
        }

        if ((reinterpret_cast<uintptr_t>(dst) & 2U) != 0U)
        {
            *dst++ = v;
            --pixels;
        }

        const uint32_t v32 = (static_cast<uint32_t>(v) << 16) | v;
        auto *dst32 = reinterpret_cast<uint32_t *>(dst);
        size_t pairs = pixels >> 1;

        while (pairs >= 4)
        {
            dst32[0] = v32;
            dst32[1] = v32;
            dst32[2] = v32;
            dst32[3] = v32;
            dst32 += 4;
            pairs -= 4;
        }
        while (pairs--)
            *dst32++ = v32;
        if ((pixels & 1U) != 0U)
            *reinterpret_cast<uint16_t *>(dst32) = v;
    }
}
